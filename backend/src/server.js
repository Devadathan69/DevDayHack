import { createServer } from "node:http";
import { createHash, randomBytes, randomUUID, timingSafeEqual } from "node:crypto";
import { mkdir, readFile, rename, unlink, writeFile } from "node:fs/promises";
import { fileURLToPath } from "node:url";
import { dirname, resolve } from "node:path";
import { loadConfig } from "./config.js";
import { validateProfile } from "./domain.js";
import { NpcRegistry } from "./npc-registry.js";
import { SessionManager } from "./session-manager.js";
import { createLiveProviders } from "./providers/live-providers.js";
import { SqliteMemoryStore } from "./memory/sqlite-memory-store.js";

function normalizeAdminProfile(item) {
  if (!item || typeof item !== "object" || !Array.isArray(item.versions) || item.versions.length > 20) {
    throw new Error("Each NPC profile needs a valid version history of at most 20 entries.");
  }
  const data = validateProfile(item.data);
  const versions = item.versions.map((version) => {
    if (!version || !Number.isSafeInteger(version.number) || version.number < 1 || typeof version.publishedAt !== "string" || !Number.isFinite(Date.parse(version.publishedAt))) {
      throw new Error("A published profile version has invalid metadata.");
    }
    const publishedProfile = validateProfile(version.profile);
    if (publishedProfile.id !== data.id) throw new Error("A published version must use the profile's ID.");
    return { number: version.number, publishedAt: version.publishedAt, profile: publishedProfile };
  });
  const activeVersion = item.activeVersion ?? null;
  if (activeVersion !== null && !versions.some((version) => version.number === activeVersion)) {
    throw new Error("The active published version does not exist in the profile history.");
  }
  return { data, versions, activeVersion };
}

function normalizeAdminState(state, seedProfile) {
  const profiles = (state?.profiles ?? [{ data: seedProfile, versions: [], activeVersion: null }]).map(normalizeAdminProfile);
  if (profiles.length < 1 || profiles.length > 50) throw new Error("The dashboard must contain between 1 and 50 NPC profiles.");
  const ids = new Set(profiles.map((item) => item.data.id));
  if (ids.size !== profiles.length) throw new Error("NPC profile IDs must be unique.");
  if (!ids.has(seedProfile.id)) throw new Error("The GTA bridge profile must remain in the roster.");
  const selectedId = ids.has(state?.selectedId) ? state.selectedId : seedProfile.id;
  return { profiles, selectedId };
}

export function createNpcServer({ config, profile, memory, providers = createLiveProviders(config), adminState = null, persistAdminState = async () => {} }) {
  const initialAdminState = normalizeAdminState(adminState, profile);
  const adminProfiles = initialAdminState.profiles;
  const profileForRuntime = (item) => item.activeVersion === null
    ? (item.data.id === profile.id ? profile : null)
    : item.versions.find((version) => version.number === item.activeVersion)?.profile ?? null;
  const runtimeProfiles = adminProfiles.map(profileForRuntime).filter(Boolean);
  const registry = new NpcRegistry([profile], { ...providers, memory });
  for (const runtimeProfile of runtimeProfiles) registry.upsert(runtimeProfile);
  const manager = new SessionManager({ registry, memory });
  const companionDirectory = fileURLToPath(new URL("../../companion/", import.meta.url));
  const sessionToken = randomBytes(32).toString("hex");
  const bridgeToken = randomBytes(32).toString("hex");
  const audioItems = new Map();
  const adminDirectory = fileURLToPath(new URL("../../admin/", import.meta.url));
  let liveTurns = 0;
  let lastBridgeSeenAt = 0;
  let profileRecords = adminProfiles;
  let selectedAdminProfileId = initialAdminState.selectedId;
  let runtimeProfileIds = new Set(runtimeProfiles.map((item) => item.id));

  const staticFiles = new Map([
    ["/", ["index.html", "text/html; charset=utf-8"]],
    ["/app.js", ["app.js", "text/javascript; charset=utf-8"]],
    ["/styles.css", ["styles.css", "text/css; charset=utf-8"]],
    ["/admin", ["index.html", "text/html; charset=utf-8", "admin"]],
    ["/admin/", ["index.html", "text/html; charset=utf-8", "admin"]],
    ["/admin/index.html", ["index.html", "text/html; charset=utf-8", "admin"]],
    ["/admin/app.js", ["app.js", "text/javascript; charset=utf-8", "admin"]],
    ["/admin/styles.css", ["styles.css", "text/css; charset=utf-8", "admin"]],
    ["/admin/extras.css", ["extras.css", "text/css; charset=utf-8", "admin"]],
  ]);

  function sendJson(response, status, value) {
    response.writeHead(status, { "Content-Type": "application/json; charset=utf-8", "Cache-Control": "no-store", "X-Content-Type-Options": "nosniff" });
    response.end(JSON.stringify(value));
  }

  function getCookie(request) {
    const cookie = request.headers.cookie?.split(";").map((part) => part.trim()).find((part) => part.startsWith("malayalam_session="));
    return cookie?.slice("malayalam_session=".length) ?? "";
  }

  function localOrigin() {
    return `http://127.0.0.1:${server.address()?.port ?? config.port}`;
  }

  function isBrowserAuthenticated(request) {
    const supplied = Buffer.from(getCookie(request));
    const expected = Buffer.from(sessionToken);
    if (supplied.length !== expected.length || !timingSafeEqual(supplied, expected)) return false;
    if (request.method !== "GET" && request.headers.origin !== localOrigin()) return false;
    return true;
  }

  function isBridgeRoute(request, pathname) {
    if (request.method === "GET" && pathname === "/api/status") return true;
    if (request.method === "GET" && /^\/api\/audio\/[0-9a-f-]{36}$/iu.test(pathname)) return true;
    return request.method === "POST" && ["/api/select", "/api/text-turn", "/api/delivery", "/api/cancel", "/api/deselect"].includes(pathname);
  }

  function isBridgeAuthenticated(request, pathname) {
    if (!isBridgeRoute(request, pathname)) return false;
    const supplied = request.headers["x-bridge-token"];
    if (typeof supplied !== "string") return false;
    const suppliedBytes = Buffer.from(supplied);
    const expectedBytes = Buffer.from(bridgeToken);
    if (suppliedBytes.length !== expectedBytes.length || !timingSafeEqual(suppliedBytes, expectedBytes)) return false;
    lastBridgeSeenAt = Date.now();
    return true;
  }

  function jsonBody(request, maxBytes = 16 * 1024) {
    return new Promise((resolveBody, reject) => {
      const chunks = [];
      let size = 0;
      let tooLarge = false;
      request.on("data", (chunk) => {
        if (tooLarge) return;
        size += chunk.length;
        if (size > maxBytes) {
          tooLarge = true;
          chunks.length = 0;
          reject(new Error("Request body is too large."));
          return;
        }
        chunks.push(chunk);
      });
      request.on("end", () => {
        if (tooLarge) return;
        try {
          const body = JSON.parse(Buffer.concat(chunks).toString("utf8"));
          if (!body || typeof body !== "object" || Array.isArray(body)) throw new Error();
          resolveBody(body);
        } catch { reject(new Error("Request body must be a valid JSON object.")); }
      });
      request.on("error", reject);
    });
  }

  function audioBody(request) {
    return new Promise((resolveBody, reject) => {
      const chunks = [];
      let size = 0;
      let tooLarge = false;
      request.on("data", (chunk) => {
        if (tooLarge) return;
        size += chunk.length;
        if (size > config.maxAudioBytes) {
          tooLarge = true;
          chunks.length = 0;
          reject(new Error(`Recording is too large. The limit is ${Math.round(config.maxAudioBytes / 1024 / 1024)} MiB.`));
          return;
        }
        chunks.push(chunk);
      });
      request.on("end", () => { if (!tooLarge) resolveBody(Buffer.concat(chunks)); });
      request.on("error", reject);
    });
  }

  function saveAudio(bytes, mimeType, turnId) {
    const id = randomUUID();
    const expiresAt = Date.now() + 5 * 60_000;
    audioItems.set(id, { bytes, mimeType, expiresAt, turnId });
    while (audioItems.size > 8) audioItems.delete(audioItems.keys().next().value);
    setTimeout(() => {
      const item = audioItems.get(id);
      if (item?.expiresAt === expiresAt) audioItems.delete(id);
    }, 5 * 60_000).unref();
    return `/api/audio/${id}`;
  }

  function clearAudioForTurn(turnId = null) {
    for (const [id, item] of audioItems) {
      if (turnId === null || item.turnId === turnId) audioItems.delete(id);
    }
  }

  async function runTurn(request, audioInput, response) {
    if (manager.state !== "SELECTED") throw new Error("Select an NPC first, then retry your message.");
    if (liveTurns >= config.liveTurnLimit) {
      throw new Error(`This run reached its ${config.liveTurnLimit}-turn live API limit. Restart the local server to begin another demo session.`);
    }
    liveTurns += 1;

    const pendingTurn = manager.startTurn(audioInput);
    const turnId = manager.activeTurnId;
    let clientGone = false;
    const onAborted = () => {
      clientGone = true;
      clearAudioForTurn(turnId);
      manager.cancel("The client disconnected during this turn.", turnId);
    };
    const onClosed = () => { if (!response.writableEnded) onAborted(); };
    request.once("aborted", onAborted);
    response.once("close", onClosed);
    const timeout = setTimeout(() => manager.cancel("Turn timed out. Try a shorter message or check the provider connection.", turnId), config.turnTimeoutMs);
    try {
      const result = await pendingTurn;
      if (clientGone || response.destroyed) return;
      let audioUrl = null;
      if (result.audio?.playable && Buffer.isBuffer(result.audio.bytes)) {
        audioUrl = saveAudio(result.audio.bytes, result.audio.mimeType ?? "audio/mpeg", result.turnContext.turnId);
      }
      sendJson(response, 200, {
        turnId: result.turnContext.turnId,
        transcript: result.transcript.text,
        inputMode: result.transcript.inputMode ?? (audioInput.text ? "typed" : "voice"),
        replyMl: result.reply.replyMl,
        audioUrl,
        speechError: result.speechError,
        timings: result.timings,
      });
    } finally {
      clearTimeout(timeout);
      request.removeListener("aborted", onAborted);
      response.removeListener("close", onClosed);
    }
  }

  const server = createServer(async (request, response) => {
    try {
      const host = request.headers.host;
      if (`http://${host}` !== localOrigin()) {
        sendJson(response, 421, { error: "Use the local companion URL printed by the server." });
        return;
      }
      const url = new URL(request.url, `http://${host}`);
      if (request.method === "GET" && staticFiles.has(url.pathname)) {
        const [filename, mime, section] = staticFiles.get(url.pathname);
        const path = resolve(section === "admin" ? adminDirectory : companionDirectory, filename);
        const bytes = await readFile(path);
        response.writeHead(200, {
          "Content-Type": mime,
          "Cache-Control": "no-store",
          "X-Content-Type-Options": "nosniff",
          "Content-Security-Policy": "default-src 'self'; script-src 'self'; style-src 'self' https://fonts.googleapis.com; font-src 'self' https://fonts.gstatic.com; connect-src 'self'; media-src 'self' blob:; img-src 'self' data:; object-src 'none'; base-uri 'none'; frame-ancestors 'none'",
          ...(["/", "/admin", "/admin/", "/admin/index.html"].includes(url.pathname) ? { "Set-Cookie": `malayalam_session=${sessionToken}; HttpOnly; SameSite=Strict; Path=/; Max-Age=28800` } : {}),
        });
        response.end(bytes);
        return;
      }
      if (!url.pathname.startsWith("/api/")) {
        sendJson(response, 404, { error: "Not found." });
        return;
      }
      const bridgeAuthenticated = isBridgeAuthenticated(request, url.pathname);
      if (!bridgeAuthenticated && !isBrowserAuthenticated(request)) {
        sendJson(response, 403, { error: "Open the local companion page first, then retry." });
        return;
      }

      if (request.method === "GET" && url.pathname === "/api/admin/profiles") {
        sendJson(response, 200, {
          profiles: profileRecords,
          selectedId: selectedAdminProfileId,
          companionReady: Boolean(registry.getProfile(selectedAdminProfileId)),
          gameProfileId: profile.id,
        });
        return;
      }
      if (request.method === "PUT" && url.pathname === "/api/admin/profiles") {
        const nextState = normalizeAdminState(await jsonBody(request, 1024 * 1024), profile);
        const nextRuntimeProfiles = nextState.profiles.map(profileForRuntime).filter(Boolean);
        const nextRuntimeIds = new Set(nextRuntimeProfiles.map((item) => item.id));
        const changedRuntimeIds = new Set();
        for (const nextProfile of nextRuntimeProfiles) {
          const existing = registry.getProfile(nextProfile.id);
          if (!existing || JSON.stringify(existing) !== JSON.stringify(nextProfile)) changedRuntimeIds.add(nextProfile.id);
        }
        for (const existingId of runtimeProfileIds) if (!nextRuntimeIds.has(existingId)) changedRuntimeIds.add(existingId);

        await persistAdminState(nextState);
        if (manager.selectedConversationKey && changedRuntimeIds.has(manager.selectedConversationKey.npcId)) {
          clearAudioForTurn(manager.activeTurnId);
          manager.invalidateScene();
        }
        for (const existingId of runtimeProfileIds) if (!nextRuntimeIds.has(existingId)) registry.remove(existingId);
        for (const nextProfile of nextRuntimeProfiles) {
          if (changedRuntimeIds.has(nextProfile.id)) registry.upsert(nextProfile);
        }
        profileRecords = nextState.profiles;
        selectedAdminProfileId = nextState.selectedId;
        runtimeProfileIds = nextRuntimeIds;
        sendJson(response, 200, {
          saved: true,
          companionReady: Boolean(registry.getProfile(selectedAdminProfileId)),
          gameProfileId: profile.id,
        });
        return;
      }

      if (request.method === "GET" && url.pathname === "/api/status") {
        const gameConnected = Date.now() - lastBridgeSeenAt < 15_000;
        const statusNpcId = manager.selectedConversationKey?.npcId ?? (registry.getProfile(selectedAdminProfileId) ? selectedAdminProfileId : profile.id);
        const statusProfile = registry.getProfile(statusNpcId) ?? profile;
        sendJson(response, 200, {
          state: manager.state,
          selected: manager.hasSelection,
          activeTurnId: manager.activeTurnId,
          npc: { id: statusNpcId, name: statusProfile.displayName },
          gameConnected,
          gameMessage: gameConnected
            ? "The local ASI bridge is authenticated and responding."
            : "The local ASI bridge has not connected.",
          liveTurnsRemaining: Math.max(0, config.liveTurnLimit - liveTurns),
          audioLimitSeconds: config.maxAudioSeconds,
        });
        return;
      }
      if (request.method === "GET" && url.pathname === "/api/history") {
        if (!manager.hasSelection) {
          sendJson(response, 200, { turns: [] });
          return;
        }
        sendJson(response, 200, await memory.loadContext(manager.selectedConversationKey));
        return;
      }
      if (request.method === "GET" && url.pathname.startsWith("/api/audio/")) {
        const id = url.pathname.slice("/api/audio/".length);
        const item = audioItems.get(id);
        if (!item || item.expiresAt < Date.now()) {
          audioItems.delete(id);
          sendJson(response, 404, { error: "This temporary reply audio expired. Generate a new reply to hear it." });
          return;
        }
        response.writeHead(200, { "Content-Type": item.mimeType, "Cache-Control": "no-store", "Content-Length": item.bytes.length, "X-Content-Type-Options": "nosniff" });
        response.end(item.bytes);
        return;
      }
      if (request.method === "POST" && url.pathname === "/api/select") {
        const body = bridgeAuthenticated ? await jsonBody(request) : {};
        if (bridgeAuthenticated || !manager.hasSelection) {
          const previousTurnId = manager.activeTurnId;
          // Ped pool generations are meaningful only within their game process/session.
          const worldId = bridgeAuthenticated
            ? `native-${createHash("sha256").update(JSON.stringify([config.worldId, body.gameSessionId, body.sceneGeneration])).digest("hex")}`
            : config.worldId;
          const npcId = bridgeAuthenticated ? profile.id : registry.getProfile(selectedAdminProfileId) ? selectedAdminProfileId : profile.id;
          const selectedProfile = registry.getProfile(npcId);
          manager.selectNpc({ npcId, worldId, playerId: config.playerId,
            gameSessionId: bridgeAuthenticated ? body.gameSessionId : `companion-${randomUUID()}`,
            sceneGeneration: bridgeAuthenticated ? body.sceneGeneration : 0 });
          if (previousTurnId && manager.activeTurnId !== previousTurnId) clearAudioForTurn(previousTurnId);
          sendJson(response, 200, { state: manager.state, npc: selectedProfile.displayName, npcId });
          return;
        }
        sendJson(response, 200, { state: manager.state, npc: manager.selectedConversationKey?.npcId ? registry.getProfile(manager.selectedConversationKey.npcId)?.displayName : profile.displayName, npcId: manager.selectedConversationKey?.npcId ?? profile.id });
        return;
      }
      if (request.method === "POST" && url.pathname === "/api/deselect") {
        clearAudioForTurn(manager.activeTurnId);
        manager.invalidateScene();
        sendJson(response, 200, { state: manager.state });
        return;
      }
      if (request.method === "POST" && url.pathname === "/api/turn") {
        if (!/^audio\/(webm|ogg|wav|mpeg|mp4|mp3)$/iu.test((request.headers["content-type"] ?? "").split(";")[0])) {
          sendJson(response, 415, { error: "Microphone recording must be WebM, Ogg, MP4, WAV, or MP3 audio." });
          return;
        }
        const bytes = await audioBody(request);
        if (!bytes.length) throw new Error("The microphone recording is empty. Hold F7 while speaking, then release it.");
        await runTurn(request, { bytes, mimeType: (request.headers["content-type"] ?? "audio/webm").split(";")[0] }, response);
        return;
      }
      if (request.method === "POST" && url.pathname === "/api/text-turn") {
        const body = await jsonBody(request);
        if (typeof body.text !== "string" || body.text.trim().length === 0 || body.text.length > 1000) {
          throw new Error("Text fallback must contain 1–1,000 characters.");
        }
        await runTurn(request, { text: body.text }, response);
        return;
      }
      if (request.method === "POST" && url.pathname === "/api/delivery") {
        const body = await jsonBody(request);
        if (!manager.activeTurnId || body.turnId !== manager.activeTurnId) throw new Error("This reply is no longer the active turn.");
        await manager.acknowledgeDelivery({ subtitleDisplayed: body.subtitleDisplayed === true, audioPlayed: body.audioPlayed === true });
        clearAudioForTurn(body.turnId);
        sendJson(response, 200, { state: manager.state });
        return;
      }
      if (request.method === "POST" && url.pathname === "/api/cancel") {
        const body = request.headers["transfer-encoding"] || Number(request.headers["content-length"]) > 0
          ? await jsonBody(request) : {};
        if (body.turnId !== undefined && body.turnId !== manager.activeTurnId) throw new Error("This reply is no longer the active turn.");
        clearAudioForTurn(manager.activeTurnId);
        manager.cancel("Cancelled by the client.");
        sendJson(response, 200, { state: manager.state });
        return;
      }
      if (request.method === "POST" && url.pathname === "/api/reset") {
        clearAudioForTurn();
        const generation = await manager.resetSelectedMemory();
        sendJson(response, 200, { generation });
        return;
      }
      sendJson(response, 404, { error: "Not found." });
    } catch (error) {
      if (response.headersSent || response.destroyed) return;
      const message = error instanceof Error ? error.message : "The request failed.";
      sendJson(response, 400, { error: message });
    }
  });

  server.on("clientError", (_error, socket) => socket.end("HTTP/1.1 400 Bad Request\r\nConnection: close\r\n\r\n"));
  server.once("close", () => {
    manager.cancel("Local server shutting down.");
    clearAudioForTurn();
  });
  return { server, manager, bridgeToken };
}

async function startServer() {
  const config = loadConfig();
  const profilePath = new URL("../../profiles/demo-npc-001.json", import.meta.url);
  const profile = JSON.parse(await readFile(fileURLToPath(profilePath), "utf8"));
  await mkdir(config.dataDirectory, { recursive: true });
  const adminProfilePath = resolve(config.dataDirectory, "npc-profiles.json");
  let adminState = null;
  try { adminState = JSON.parse(await readFile(adminProfilePath, "utf8")); }
  catch (error) { if (error?.code !== "ENOENT") throw new Error(`Could not read the local NPC profile store: ${error.message}`); }
  const persistAdminState = async (state) => {
    const temporaryPath = `${adminProfilePath}.${randomUUID()}.tmp`;
    await writeFile(temporaryPath, `${JSON.stringify(state, null, 2)}\n`, { encoding: "utf8", flag: "wx" });
    try { await rename(temporaryPath, adminProfilePath); }
    catch (error) { await unlink(temporaryPath).catch(() => {}); throw error; }
  };
  const memory = new SqliteMemoryStore(config.dataDirectory);
  const { server, manager, bridgeToken } = createNpcServer({ config, profile, memory, adminState, persistAdminState });
  const bridgeTokenFileContents = `token=${bridgeToken}\nport=${config.port}\n`;
  server.listen(config.port, config.host, async () => {
    try {
      await mkdir(dirname(config.bridgeTokenPath), { recursive: true });
      await writeFile(config.bridgeTokenPath, bridgeTokenFileContents, { encoding: "utf8", mode: 0o600 });
    } catch (error) {
      console.error(`Could not write the local bridge token: ${error instanceof Error ? error.message : "unknown error"}`);
      server.close(() => process.exit(1));
      return;
    }
    console.log(`Malayalam NPC companion: http://${config.host}:${config.port}`);
    console.log(`Providers: OpenAI ${config.openAiModel}, ElevenLabs ${config.elevenLabsTtsModel} (credentials hidden)`);
    console.log(`Memory: ${resolve(config.dataDirectory, "memory.sqlite")}`);
    console.log(`GTA bridge token: written to the current user's local application data.`);
  });

  async function shutdown() {
    manager.cancel("Local server shutting down.");
    server.close(async () => {
      memory.close?.();
      await removeBridgeToken();
      process.exit(0);
    });
    setTimeout(() => process.exit(1), 3_000).unref();
  }

  async function removeBridgeToken() {
    try {
      if (await readFile(config.bridgeTokenPath, "utf8") === bridgeTokenFileContents) {
        await unlink(config.bridgeTokenPath);
      }
    } catch (error) {
      if (error?.code !== "ENOENT") console.error("Could not remove the local bridge token file.");
    }
  }
  process.once("SIGINT", shutdown);
  process.once("SIGTERM", shutdown);
}

if (process.argv[1] && resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  await startServer();
}
