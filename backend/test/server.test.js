import test from "node:test";
import assert from "node:assert/strict";
import { request } from "node:http";
import { once } from "node:events";
import { randomUUID } from "node:crypto";
import { rmSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { createNpcServer } from "../src/server.js";
import { createLiveProviders } from "../src/providers/live-providers.js";
import { SqliteMemoryStore } from "../src/memory/sqlite-memory-store.js";

const profile = {
  id: "demo-npc-001", displayName: "Manu", personality: "friendly",
  originalBackstory: "An original local guide.", voiceRef: "test-voice",
  language: "ml", memoryPolicy: "private",
};
const selection = { gameSessionId: "gta-1234", sceneGeneration: 257 };
const key = { worldId: "test-world", playerId: "test-player", npcId: profile.id };
const replyMl = "നമസ്കാരം, സുഖമാണോ?";
const audioBytes = Buffer.from([73, 68, 51, 1, 2, 3]);

async function fixture(t, overrides = {}) {
  const directory = fileURLToPath(new URL(`./.server-fixture-${randomUUID()}/`, import.meta.url));
  const memory = new SqliteMemoryStore(directory);
  const config = {
    host: "127.0.0.1", port: 0, worldId: key.worldId, playerId: key.playerId,
    turnTimeoutMs: 2000, liveTurnLimit: 25, maxAudioBytes: 65536, maxAudioSeconds: 15,
    openAiApiKey: "test-key", openAiModel: "test-model", elevenLabsApiKey: "test-key",
    elevenLabsVoiceId: "test-voice", elevenLabsTtsModel: "eleven_v4", ...overrides,
  };
  const calls = [];
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async (url, options) => {
    calls.push({ url: String(url), options });
    if (String(url) === "https://api.openai.com/v1/responses") {
      return new Response(JSON.stringify({ output_text: replyMl }));
    }
    if (String(url).includes("/text-to-speech/")) {
      return new Response(audioBytes, { headers: { "Content-Type": "audio/mpeg" } });
    }
    if (String(url).endsWith("/speech-to-text")) {
      return new Response(JSON.stringify({ text: "Voice input", language_code: "ml" }));
    }
    throw new Error("Unexpected provider request; network calls are disabled in this test.");
  };
  const providers = createLiveProviders(config);
  const app = createNpcServer({ config, profile, memory, providers });
  app.server.listen(0, config.host);
  await once(app.server, "listening");
  t.after(async () => {
    app.manager.cancel("Test completed.");
    app.server.closeAllConnections();
    await new Promise((resolve) => app.server.close(resolve));
    globalThis.fetch = originalFetch;
    memory.close();
    rmSync(directory, { recursive: true, force: true });
  });

  function begin(path, { method = "GET", body, rawBody, token = app.bridgeToken, headers = {} } = {}) {
    const bytes = rawBody ?? (body === undefined ? null : Buffer.from(JSON.stringify(body)));
    let outgoing;
    const promise = new Promise((resolve, reject) => {
      outgoing = request({
        host: config.host, port: app.server.address().port, path, method,
        headers: {
          ...(token ? { "X-Bridge-Token": token } : {}),
          ...(bytes ? { "Content-Type": "application/json", "Content-Length": bytes.length } : {}),
          ...headers,
        },
      }, (response) => {
        const chunks = [];
        response.on("data", (chunk) => chunks.push(chunk));
        response.on("end", () => {
          const bytes = Buffer.concat(chunks);
          resolve({ status: response.statusCode, headers: response.headers, bytes,
            json: response.headers["content-type"]?.includes("application/json") ? JSON.parse(bytes.toString()) : null });
        });
        response.on("error", reject);
      });
      outgoing.on("error", reject);
      outgoing.end(bytes);
    });
    return { request: outgoing, promise };
  }
  let lastConversationKey = key;
  async function call(path, options) {
    const response = await begin(path, options).promise;
    lastConversationKey = app.manager.selectedConversationKey ?? lastConversationKey;
    return response;
  }
  return { ...app, config, memory, providers, calls, begin, call,
    post: (path, body = {}) => call(path, { method: "POST", body }),
    history: () => memory.loadContext(lastConversationKey),
  };
}

function deferred() {
  let resolve;
  const promise = new Promise((done) => { resolve = done; });
  return { promise, resolve };
}

async function waitFor(check) {
  for (let attempt = 0; attempt < 100; attempt += 1) {
    if (check()) return;
    await new Promise((resolve) => setTimeout(resolve, 5));
  }
  assert.fail("Expected backend state was not reached.");
}

test("native credentials authenticate only bridge routes and selection/input are validated", async (t) => {
  const app = await fixture(t);
  for (const path of ["/api/status", "/api/audio/00000000-0000-0000-0000-000000000000"]) {
    assert.equal((await app.call(path, { token: "wrong-token" })).status, 403);
  }
  for (const path of ["/api/select", "/api/turn", "/api/text-turn", "/api/delivery", "/api/cancel", "/api/deselect"]) {
    assert.equal((await app.call(path, { method: "POST", body: {}, token: null })).status, 403);
  }
  assert.equal((await app.call("/api/history")).status, 403);
  assert.equal((await app.post("/api/reset")).status, 403);
  assert.equal((await app.post("/api/turn")).status, 415);
  assert.equal((await app.call("/api/status", { headers: { Host: "example.invalid" } })).status, 421);
  assert.equal((await app.call("/api/status")).json.gameConnected, true);
  for (const body of [null, [], {}, { ...selection, gameSessionId: " " }, { ...selection, sceneGeneration: -1 }]) {
    assert.equal((await app.post("/api/select", body)).status, 400);
  }
  assert.equal((await app.post("/api/select", selection)).status, 200);
  for (const body of [null, [], {}, { text: " " }, { text: "x".repeat(1001) }]) {
    assert.equal((await app.post("/api/text-turn", body)).status, 400);
  }
  assert.equal(app.calls.length, 0);
});

test("browser selection retains cookie/origin authentication and does not impersonate bridge presence", async (t) => {
  const app = await fixture(t);
  const page = await app.call("/", { token: null });
  assert.equal(page.status, 200);
  const cookie = page.headers["set-cookie"][0].split(";")[0];
  const origin = `http://127.0.0.1:${app.server.address().port}`;
  const browser = { token: null, headers: { Cookie: cookie, Origin: origin } };
  assert.equal((await app.call("/api/status", browser)).json.gameConnected, false);
  assert.equal((await app.call("/api/select", { ...browser, method: "POST", headers: { Cookie: cookie }, body: {} })).status, 403);
  assert.equal((await app.call("/api/select", { ...browser, method: "POST", body: {} })).json.state, "SELECTED");
  const turn = (await app.call("/api/text-turn", { ...browser, method: "POST", body: { text: "Browser input" } })).json;
  assert.equal((await app.call("/api/delivery", { ...browser, method: "POST", body: { turnId: turn.turnId, subtitleDisplayed: true } })).status, 200);
  assert.equal((await app.call("/api/history", browser)).json.turns.length, 1);
  assert.equal((await app.call("/api/status", browser)).json.gameConnected, false);
  assert.equal((await app.call("/api/deselect", { ...browser, method: "POST", body: {} })).json.state, "IDLE");
});

test("native typed reply, MP3 retrieval and delivery acknowledgement form one coherent turn", async (t) => {
  const app = await fixture(t);
  await app.post("/api/select", selection);
  const result = await app.post("/api/text-turn", { text: "  Hello Manu  " });
  assert.equal(result.status, 200);
  const turn = result.json;
  assert.equal(turn.transcript, "Hello Manu");
  assert.equal(turn.inputMode, "typed");
  assert.equal(turn.replyMl, replyMl);
  assert.equal(turn.speechError, null);
  assert.match(turn.audioUrl, /^\/api\/audio\/[0-9a-f-]{36}$/u);
  assert.ok(Object.values(turn.timings).every(Number.isInteger));
  assert.equal(app.calls.length, 2);
  assert.equal(JSON.parse(app.calls[0].options.body).store, false);
  assert.equal((await app.call("/api/status")).json.state, "AWAITING_DELIVERY");
  assert.deepEqual((await app.history()).turns, []);
  assert.equal((await app.post("/api/text-turn", { text: "Overlapping message" })).status, 400);
  const audio = await app.call(turn.audioUrl);
  assert.equal(audio.status, 200);
  assert.equal(audio.headers["content-type"], "audio/mpeg");
  assert.deepEqual(audio.bytes, audioBytes);
  assert.equal((await app.post("/api/delivery", { turnId: "stale", subtitleDisplayed: true })).status, 400);
  assert.equal((await app.post("/api/delivery", { turnId: turn.turnId })).status, 400);
  assert.equal((await app.post("/api/cancel", { turnId: "stale" })).status, 400);
  assert.equal((await app.post("/api/delivery", {
    turnId: turn.turnId, subtitleDisplayed: true, audioPlayed: true,
  })).json.state, "SELECTED");
  assert.deepEqual((await app.history()).turns, [{ player: "Hello Manu", npc: replyMl }]);
  assert.equal((await app.call(turn.audioUrl)).status, 404);
  assert.equal((await app.post("/api/delivery", { turnId: turn.turnId, audioPlayed: true })).status, 400);
  assert.equal((await app.post("/api/deselect")).json.state, "IDLE");
  assert.equal((await app.post("/api/text-turn", { text: "Not selected" })).status, 400);
});

test("speech failure returns usable subtitles, and only actual delivery enters memory", async (t) => {
  const app = await fixture(t);
  await app.post("/api/select", selection);
  app.providers.tts.synthesize = async () => { throw new Error("Test speech unavailable."); };
  const turn = (await app.post("/api/text-turn", { text: "Hello" })).json;
  assert.equal(turn.replyMl, replyMl);
  assert.equal(turn.audioUrl, null);
  assert.equal(turn.speechError, "Test speech unavailable.");
  assert.equal((await app.post("/api/delivery", { turnId: turn.turnId, audioPlayed: true })).status, 400);
  assert.deepEqual((await app.history()).turns, []);
  assert.equal((await app.post("/api/delivery", { turnId: turn.turnId, subtitleDisplayed: true, audioPlayed: false })).status, 200);
  assert.equal((await app.history()).turns.length, 1);
});

test("ElevenLabs 401 preserves a valid text reply and reports a sanitized account restriction", async (t) => {
  const app = await fixture(t);
  const providerFetch = globalThis.fetch;
  globalThis.fetch = async (url, options) => String(url).includes("/text-to-speech/")
    ? new Response(JSON.stringify({ detail: {
      status: "detected_unusual_activity", message: "private-provider-message-with-fake-secret",
    } }), { status: 401 })
    : providerFetch(url, options);
  await app.post("/api/select", selection);
  const response = await app.post("/api/text-turn", { text: "Still return subtitles" });
  assert.equal(response.status, 200);
  const turn = response.json;
  assert.ok(turn.turnId);
  assert.equal(turn.replyMl, replyMl);
  assert.equal(turn.audioUrl, null);
  assert.match(turn.speechError, /HTTP 401, reason detected_unusual_activity/u);
  assert.match(turn.speechError, /contact ElevenLabs support/u);
  assert.doesNotMatch(turn.speechError, /private-provider-message|fake-secret/u);
  assert.equal((await app.post("/api/delivery", { turnId: turn.turnId, subtitleDisplayed: false, audioPlayed: false })).status, 400);
  assert.deepEqual((await app.history()).turns, []);
  assert.equal((await app.post("/api/delivery", { turnId: turn.turnId, subtitleDisplayed: true, audioPlayed: false })).status, 200);
  assert.equal((await app.history()).turns.length, 1);
  assert.equal((await app.post("/api/deselect")).json.state, "IDLE");
});

test("cancel and deselect remove pending audio and discard undelivered conversation history", async (t) => {
  const app = await fixture(t);
  await app.post("/api/select", selection);
  const first = (await app.post("/api/text-turn", { text: "Cancelled reply" })).json;
  assert.equal((await app.post("/api/cancel", { turnId: first.turnId })).json.state, "SELECTED");
  assert.equal((await app.call(first.audioUrl)).status, 404);
  assert.equal((await app.post("/api/delivery", { turnId: first.turnId, subtitleDisplayed: true })).status, 400);
  const second = (await app.post("/api/text-turn", { text: "Scene changes" })).json;
  assert.equal((await app.post("/api/deselect")).json.state, "IDLE");
  assert.equal((await app.call(second.audioUrl)).status, 404);
  assert.deepEqual((await app.history()).turns, []);
});

test("selection retries are idempotent and crossing ped scopes cancels the active turn", async (t) => {
  const app = await fixture(t);
  await app.post("/api/select", selection);
  const pending = deferred();
  app.providers.brain.respond = () => pending.promise;
  const oldRequest = app.post("/api/text-turn", { text: "Old scene" });
  await waitFor(() => app.manager.state === "THINKING");
  assert.equal((await app.post("/api/select", selection)).json.state, "THINKING");
  assert.equal((await app.post("/api/select", { gameSessionId: "gta-9999", sceneGeneration: 513 })).json.state, "SELECTED");
  pending.resolve({ replyMl });
  assert.equal((await oldRequest).status, 400);
  assert.equal(app.manager.state, "SELECTED");
  assert.equal(app.calls.length, 0);
  const contexts = [];
  app.providers.brain.respond = async () => ({ replyMl });
  const record = app.memory.recordAcceptedInput.bind(app.memory);
  app.memory.recordAcceptedInput = async (context, text) => { contexts.push(context); return record(context, text); };
  const result = await app.post("/api/text-turn", { text: "New scene" });
  assert.equal(result.status, 200);
  assert.equal(contexts[0].gameSessionId, "gta-9999");
  assert.equal(contexts[0].sceneGeneration, 513);
});

for (const method of ["recordAcceptedInput", "loadContext", "recordGeneratedReply"]) {
  test(`cancellation during ${method} does not advance to another provider stage`, async (t) => {
    const app = await fixture(t);
    await app.post("/api/select", selection);
    const reached = deferred();
    const resume = deferred();
    const original = app.memory[method].bind(app.memory);
    app.memory[method] = async (...args) => {
      const result = await original(...args);
      reached.resolve();
      await resume.promise;
      return result;
    };
    const pending = app.post("/api/text-turn", { text: "Cancel at memory boundary" });
    await reached.promise;
    assert.equal((await app.post("/api/cancel")).json.state, "SELECTED");
    resume.resolve();
    assert.equal((await pending).status, 400);
    assert.equal(app.calls.length, method === "recordGeneratedReply" ? 1 : 0);
    assert.deepEqual((await app.history()).turns, []);
  });
}

test("a cancelled speech synthesis cannot return late audio to the native client", async (t) => {
  const app = await fixture(t);
  await app.post("/api/select", selection);
  const speech = deferred();
  app.providers.tts.synthesize = () => speech.promise;
  const pending = app.post("/api/text-turn", { text: "Cancel speaking" });
  await waitFor(() => app.manager.state === "SYNTHESIZING");
  await app.post("/api/cancel");
  speech.resolve({ playable: true, bytes: audioBytes, mimeType: "audio/mpeg" });
  const result = await pending;
  assert.equal(result.status, 400);
  assert.equal(result.json.audioUrl, undefined);
  assert.equal(app.manager.state, "SELECTED");
  assert.deepEqual((await app.history()).turns, []);
});

test("disconnecting a completed HTTP upload cancels its pending provider work", async (t) => {
  const app = await fixture(t);
  await app.post("/api/select", selection);
  const pending = deferred();
  app.providers.brain.respond = () => pending.promise;
  const oldRequest = app.begin("/api/text-turn", { method: "POST", body: { text: "Disconnected client" } });
  const disconnected = assert.rejects(oldRequest.promise);
  await waitFor(() => app.manager.state === "THINKING");
  oldRequest.request.destroy();
  await disconnected;
  await waitFor(() => app.manager.activeTurnId === null);
  assert.equal(app.manager.state, "SELECTED");
  pending.resolve({ replyMl });
  assert.deepEqual((await app.history()).turns, []);
});

test("an older request timeout cannot cancel a newer turn", async (t) => {
  const app = await fixture(t, { turnTimeoutMs: 80 });
  await app.post("/api/select", selection);
  const oldProvider = deferred();
  const newProvider = deferred();
  let invoked = 0;
  app.providers.brain.respond = () => (++invoked === 1 ? oldProvider.promise : newProvider.promise);
  const oldRequest = app.post("/api/text-turn", { text: "Old pending turn" });
  await waitFor(() => invoked === 1);
  await app.post("/api/cancel");
  app.config.turnTimeoutMs = 2000;
  const newRequest = app.post("/api/text-turn", { text: "New pending turn" });
  await waitFor(() => invoked === 2);
  const newTurnId = app.manager.activeTurnId;
  await new Promise((resolve) => setTimeout(resolve, 100));
  assert.equal(app.manager.activeTurnId, newTurnId);
  oldProvider.resolve({ replyMl });
  assert.equal((await oldRequest).status, 400);
  assert.equal(app.manager.activeTurnId, newTurnId);
  newProvider.resolve({ replyMl });
  assert.equal((await newRequest).status, 200);
  assert.equal(app.manager.state, "AWAITING_DELIVERY");
  assert.deepEqual((await app.history()).turns, []);
});

test("ped slot/generation and game session isolate memory while the default persona remains Manu", async (t) => {
  const app = await fixture(t);
  await app.post("/api/select", selection);
  const firstKey = app.manager.selectedConversationKey;
  const first = (await app.post("/api/text-turn", { text: "First ped detail" })).json;
  await app.post("/api/delivery", { turnId: first.turnId, subtitleDisplayed: true });
  const undelivered = (await app.post("/api/text-turn", { text: "Undelivered first ped detail" })).json;
  assert.equal((await app.post("/api/select", selection)).json.state, "AWAITING_DELIVERY");
  assert.equal((await app.call(undelivered.audioUrl)).status, 200);

  const secondSelection = { ...selection, sceneGeneration: 513 };
  assert.equal((await app.post("/api/select", secondSelection)).json.npc, "Manu");
  assert.equal((await app.call(undelivered.audioUrl)).status, 404);
  assert.equal((await app.post("/api/delivery", { turnId: undelivered.turnId, subtitleDisplayed: true })).status, 400);
  const secondKey = app.manager.selectedConversationKey;
  assert.notDeepEqual(firstKey, secondKey);
  assert.deepEqual((await app.history()).turns, []);
  const second = (await app.post("/api/text-turn", { text: "Second ped detail" })).json;
  const dialogueCall = app.calls.filter((call) => call.url.includes("openai.com")).at(-1);
  assert.deepEqual(JSON.parse(JSON.parse(dialogueCall.options.body).input).deliveredHistory, []);
  await app.post("/api/delivery", { turnId: second.turnId, subtitleDisplayed: true });

  const page = await app.call("/", { token: null });
  const browser = { token: null, headers: {
    Cookie: page.headers["set-cookie"][0].split(";")[0],
    Origin: `http://127.0.0.1:${app.server.address().port}`,
  } };
  assert.deepEqual((await app.call("/api/history", browser)).json.turns, [{ player: "Second ped detail", npc: replyMl }]);
  assert.equal((await app.call("/api/reset", { ...browser, method: "POST", body: {} })).status, 200);
  assert.deepEqual((await app.memory.loadContext(secondKey)).turns, []);
  assert.deepEqual((await app.memory.loadContext(firstKey)).turns, [{ player: "First ped detail", npc: replyMl }]);
  await app.post("/api/select", selection);
  assert.deepEqual((await app.history()).turns, [{ player: "First ped detail", npc: replyMl }]);
  await app.post("/api/select", { ...selection, sceneGeneration: 258 });
  assert.deepEqual((await app.history()).turns, []);
  await app.post("/api/select", { ...selection, gameSessionId: "gta-9999" });
  assert.deepEqual((await app.history()).turns, []);
});

test("bridge pedestrian metadata and unsupported selection fields reject without provider calls", async (t) => {
  const app = await fixture(t);
  for (const pedestrian of [null, [], "male", 4, { modelId: "7" }, { modelId: 65536 }, { pedType: 32 },
    { pedType: 1.5 }, { gender: true }, { gender: "female".repeat(100) }, { personality: "custom" },
    { pedType: 4, gender: "female" }, { pedType: 5, gender: "male" }]) {
    assert.equal((await app.post("/api/select", { ...selection, pedestrian })).status, 400);
    assert.equal(app.manager.hasSelection, false);
  }
  for (const body of [{ ...selection, worldId: "custom" }, { ...selection, gameSessionId: "x".repeat(129) },
    { ...selection, sceneGeneration: "257" }]) {
    assert.equal((await app.post("/api/select", body)).status, 400);
  }
  assert.equal(app.calls.length, 0);
  assert.equal((await app.post("/api/select", { ...selection, pedestrian: {} })).json.gender, "unknown");
});

test("scoped native profiles return stable names, independent personalities and private delivered memory", async (t) => {
  const app = await fixture(t);
  const firstSelection = { ...selection, pedestrian: { modelId: 7, pedType: 4, gender: "male" } };
  const firstProfile = (await app.post("/api/select", firstSelection)).json;
  assert.equal(firstProfile.npc, firstProfile.displayName);
  assert.equal(firstProfile.gender, "male");
  assert.match(firstProfile.npcId, /^ped-[0-9a-f]{56}$/u);
  assert.equal(app.calls.length, 0);
  assert.equal((await app.call("/api/status")).json.liveTurnsRemaining, app.config.liveTurnLimit);
  assert.deepEqual((await app.post("/api/select", firstSelection)).json, firstProfile);
  assert.equal(app.calls.length, 0);
  assert.deepEqual(Object.keys(firstProfile).sort(), ["state", "npc", "npcId", "displayName", "gender", "personality"].sort());
  const firstKey = app.manager.selectedConversationKey;
  const firstTurn = (await app.post("/api/text-turn", { text: "First resident detail" })).json;
  const firstPayload = JSON.parse(app.calls[0].options.body);
  assert.ok(firstPayload.instructions.includes(firstProfile.displayName));
  assert.ok(firstPayload.instructions.includes(firstProfile.personality));
  assert.match(firstPayload.instructions, /Gender does not determine interests/u);
  await app.post("/api/delivery", { turnId: firstTurn.turnId, subtitleDisplayed: true });

  const secondSelection = { ...firstSelection, sceneGeneration: 513 };
  const secondProfile = (await app.post("/api/select", secondSelection)).json;
  assert.equal(secondProfile.gender, "male");
  assert.notEqual(secondProfile.npcId, firstProfile.npcId);
  assert.notEqual(secondProfile.displayName, firstProfile.displayName);
  assert.notEqual(secondProfile.personality, firstProfile.personality);
  assert.deepEqual((await app.history()).turns, []);
  const secondTurn = (await app.post("/api/text-turn", { text: "Second resident detail" })).json;
  await app.post("/api/delivery", { turnId: secondTurn.turnId, subtitleDisplayed: true });
  const secondKey = app.manager.selectedConversationKey;

  // Optional metadata can disappear on reselect without reverting a known resident to Manu.
  assert.deepEqual((await app.post("/api/select", selection)).json, firstProfile);
  assert.deepEqual(app.manager.selectedConversationKey, firstKey);
  assert.deepEqual((await app.history()).turns, [{ player: "First resident detail", npc: replyMl }]);
  await app.post("/api/deselect");
  assert.deepEqual((await app.post("/api/select", { ...firstSelection, pedestrian: { modelId: 8, pedType: 4 } })).json, firstProfile);
  await app.post("/api/select", { ...firstSelection, gameSessionId: "gta-9999" });
  assert.deepEqual((await app.history()).turns, []);
  assert.notEqual(app.manager.selectedConversationKey.npcId, firstKey.npcId);
  assert.deepEqual((await app.memory.loadContext(secondKey)).turns, [{ player: "Second resident detail", npc: replyMl }]);
});

test("same resident profile reselect keeps active reply while new resident cancels stale work", async (t) => {
  const app = await fixture(t);
  const resident = { ...selection, pedestrian: { modelId: 9, pedType: 5 } };
  const original = (await app.post("/api/select", resident)).json;
  const pending = deferred();
  app.providers.brain.respond = () => pending.promise;
  const oldRequest = app.post("/api/text-turn", { text: "Pending resident turn" });
  await waitFor(() => app.manager.state === "THINKING");
  const turnId = app.manager.activeTurnId;
  assert.equal((await app.post("/api/select", resident)).json.state, "THINKING");
  assert.equal((await app.post("/api/select", selection)).json.npcId, original.npcId);
  assert.equal(app.manager.activeTurnId, turnId);
  assert.equal((await app.post("/api/select", { ...resident, pedestrian: { modelId: "bad" } })).status, 400);
  assert.equal((await app.post("/api/select", { ...resident, pedestrian: { pedType: 5, gender: "male" } })).status, 400);
  assert.equal(app.manager.activeTurnId, turnId);
  const next = (await app.post("/api/select", { ...resident, sceneGeneration: 258 })).json;
  assert.notEqual(next.npcId, original.npcId);
  assert.equal(app.manager.activeTurnId, null);
  pending.resolve({ replyMl });
  assert.equal((await oldRequest).status, 400);
  assert.equal(app.calls.length, 0);
  assert.deepEqual((await app.history()).turns, []);
});

test("browser and metadata-free initial native selection keep the original Manu contract", async (t) => {
  const app = await fixture(t);
  const native = (await app.post("/api/select", selection)).json;
  assert.equal(native.displayName, "Manu");
  assert.equal(native.npcId, profile.id);
  assert.equal(native.gender, "unknown");
  await app.post("/api/deselect");
  const page = await app.call("/", { token: null });
  const browser = { token: null, headers: {
    Cookie: page.headers["set-cookie"][0].split(";")[0],
    Origin: `http://127.0.0.1:${app.server.address().port}`,
  } };
  const selected = (await app.call("/api/select", { ...browser, method: "POST", body: {
    ...selection, pedestrian: { pedType: 5 },
  } })).json;
  assert.equal(selected.npc, "Manu");
  assert.equal(selected.displayName, "Manu");
  assert.equal(selected.npcId, profile.id);
  assert.equal(selected.personality, profile.personality);
  assert.deepEqual((await app.call("/api/status", browser)).json.npc, { id: profile.id, name: "Manu" });
  assert.equal(app.calls.length, 0);
});

test("native voices honor approved gender configuration, neutral uses male fallback, and browser keeps the default", async (t) => {
  const app = await fixture(t, { elevenLabsMaleVoiceId: "test-private-male-id", elevenLabsFemaleVoiceId: "test-private-female-id" });
  for (const [sceneGeneration, pedestrian, expectedVoice] of [
    [257, { modelId: 7, pedType: 4, gender: "male" }, "test-private-male-id"],
    [513, { modelId: 8, pedType: 5, gender: "female" }, "test-private-female-id"],
    [769, { modelId: 9, pedType: 7, gender: "unknown" }, "test-private-male-id"],
  ]) {
    const selected = await app.post("/api/select", { ...selection, sceneGeneration, pedestrian });
    assert.equal(selected.status, 200);
    assert.doesNotMatch(JSON.stringify(selected.json), /test-private|test-key/u);
    const before = app.calls.length;
    const turn = await app.post("/api/text-turn", { text: "Voice routing", voiceRef: "unapproved-caller-id", voiceId: "unapproved-caller-id" });
    assert.equal(turn.status, 200);
    assert.equal(app.calls.length, before + 2);
    const speechCall = app.calls.at(-1);
    assert.match(speechCall.url, new RegExp(`/text-to-speech/${expectedVoice}\\?`, "u"));
    assert.doesNotMatch(speechCall.url, /unapproved-caller-id/u);
    await app.post("/api/delivery", { turnId: turn.json.turnId, subtitleDisplayed: true });
  }
  assert.equal((await app.post("/api/select", { ...selection, pedestrian: { pedType: 4, voiceRef: "unapproved-caller-id" } })).status, 400);
  await app.post("/api/deselect");
  const page = await app.call("/", { token: null });
  const browser = { token: null, headers: { Cookie: page.headers["set-cookie"][0].split(";")[0],
    Origin: `http://127.0.0.1:${app.server.address().port}` } };
  assert.equal((await app.call("/api/select", { ...browser, method: "POST", body: {} })).json.displayName, "Manu");
  const browserTurn = (await app.call("/api/text-turn", { ...browser, method: "POST", body: { text: "Browser default voice" } })).json;
  assert.match(app.calls.at(-1).url, /text-to-speech\/test-voice\?/u);
  await app.call("/api/delivery", { ...browser, method: "POST", body: { turnId: browserTurn.turnId, subtitleDisplayed: true } });
});

const namedPools = {
  elevenLabsMaleVoices: [{ name: "Miyan", voiceId: "test-miyan" }, { name: "Adith", voiceId: "test-adith" },
    { name: "DT", voiceId: "test-dt-voice" }, { name: "Damu", voiceId: "test-damu" }],
  elevenLabsFemaleVoices: [{ name: "Manju", voiceId: "test-manju" }],
};

test("adjacent male NPCs rotate named voices and model-female metadata wins over male live classifications", async (t) => {
  const app = await fixture(t, { ...namedPools, liveTurnLimit: 0 });
  const used = new Map();
  for (const sceneGeneration of Array.from({ length: 16 }, (_, index) => 3 * 2 ** 17 + (index + 1) * 256 + 1)) {
    const input = { ...selection, sceneGeneration, pedestrian: { modelId: 7, modelGender: "male", pedType: 4, gender: "male" } };
    const selected = (await app.post("/api/select", input)).json;
    assert.deepEqual((await app.post("/api/select", input)).json, selected);
    const reply = (await app.post("/api/text-turn", { text: "Male pool rotation" })).json;
    const voice = namedPools.elevenLabsMaleVoices.find(({ voiceId }) => app.calls.at(-1).url.includes(`/text-to-speech/${voiceId}?`));
    assert.ok(voice);
    assert.ok(selected.displayName.startsWith(`${voice.name} (`));
    used.set(voice.voiceId, (used.get(voice.voiceId) ?? 0) + 1);
    await app.post("/api/delivery", { turnId: reply.turnId, subtitleDisplayed: true });
  }
  assert.equal(used.size, 4);
  assert.deepEqual([...used.values()], [4, 4, 4, 4]);
  for (const pedType of [4, 22]) {
    const input = { ...selection, sceneGeneration: 1281 + pedType, pedestrian: { modelId: 8, modelGender: "female", pedType, gender: "female" } };
    const selected = (await app.post("/api/select", input)).json;
    assert.equal(selected.gender, "female");
    assert.match(selected.displayName, /^Manju \(/u);
    assert.doesNotMatch(JSON.stringify(selected), /test-manju|test-key/u);
    const reply = (await app.post("/api/text-turn", { text: "Female model metadata" })).json;
    assert.match(app.calls.at(-1).url, /text-to-speech\/test-manju\?/u);
    await app.post("/api/delivery", { turnId: reply.turnId, subtitleDisplayed: true });
  }
});

test("new model metadata corrects a cached voice without changing NPC identity or delivered memory", async (t) => {
  const app = await fixture(t, namedPools);
  const original = (await app.post("/api/select", { ...selection, pedestrian: { pedType: 4 } })).json;
  const first = (await app.post("/api/text-turn", { text: "Resident memory stays private" })).json;
  await app.post("/api/delivery", { turnId: first.turnId, subtitleDisplayed: true });
  const pending = (await app.post("/api/text-turn", { text: "Discard old voice" })).json;
  const correctedInput = { ...selection, pedestrian: { modelId: 8, modelGender: "female", pedType: 4, gender: "female" } };
  const corrected = (await app.post("/api/select", correctedInput)).json;
  assert.equal(corrected.npcId, original.npcId);
  assert.equal(corrected.personality, original.personality);
  assert.equal(corrected.gender, "female");
  assert.match(corrected.displayName, /^Manju \(/u);
  assert.equal(app.manager.state, "SELECTED");
  assert.equal((await app.call(pending.audioUrl)).status, 404);
  assert.equal((await app.post("/api/delivery", { turnId: pending.turnId, subtitleDisplayed: true })).status, 400);
  assert.deepEqual((await app.history()).turns, [{ player: "Resident memory stays private", npc: replyMl }]);
  assert.deepEqual((await app.post("/api/select", correctedInput)).json, corrected);
  const reply = (await app.post("/api/text-turn", { text: "Correct female voice" })).json;
  assert.match(app.calls.at(-1).url, /text-to-speech\/test-manju\?/u);
  await app.post("/api/delivery", { turnId: reply.turnId, subtitleDisplayed: true });
});

test("model correction cancels a pending old-voice provider reply even within the same ped scope", async (t) => {
  const app = await fixture(t, namedPools);
  await app.post("/api/select", { ...selection, pedestrian: { pedType: 4 } });
  const pending = deferred();
  app.providers.brain.respond = () => pending.promise;
  const oldRequest = app.post("/api/text-turn", { text: "Pending old classification" });
  await waitFor(() => app.manager.state === "THINKING");
  const corrected = await app.post("/api/select", { ...selection, pedestrian: { modelGender: "female", pedType: 4, gender: "female" } });
  assert.equal(corrected.status, 200);
  assert.equal(corrected.json.gender, "female");
  pending.resolve({ replyMl });
  assert.equal((await oldRequest).status, 400);
  assert.equal(app.calls.length, 0);
  assert.deepEqual((await app.history()).turns, []);
});

function wavSample() {
  const dataBytes = 320;
  const bytes = Buffer.alloc(44 + dataBytes);
  bytes.write("RIFF", 0); bytes.writeUInt32LE(bytes.length - 8, 4); bytes.write("WAVEfmt ", 8);
  bytes.writeUInt32LE(16, 16); bytes.writeUInt16LE(1, 20); bytes.writeUInt16LE(1, 22);
  bytes.writeUInt32LE(16000, 24); bytes.writeUInt32LE(32000, 28); bytes.writeUInt16LE(2, 32);
  bytes.writeUInt16LE(16, 34); bytes.write("data", 36); bytes.writeUInt32LE(dataBytes, 40);
  return bytes;
}

test("native bridge authenticates raw WAV voice turns and sends STT multipart file without exposing voice IDs", async (t) => {
  const app = await fixture(t, { ...namedPools, liveTurnLimit: 0 });
  const wav = wavSample();
  const input = { method: "POST", rawBody: wav, headers: { "Content-Type": "audio/wav" } };
  for (const token of [null, "wrong-token"]) assert.equal((await app.call("/api/turn", { ...input, token })).status, 403);
  assert.equal((await app.call("/api/turn", input)).status, 400);
  assert.equal(app.calls.length, 0);
  await app.post("/api/select", { ...selection, pedestrian: { modelId: 8, modelGender: "female", pedType: 4, gender: "female" } });
  for (const [rawBody, contentType, expected] of [[Buffer.alloc(0), "audio/wav", 400], [Buffer.alloc(65537), "audio/wav", 400], [wav, "text/plain", 415]]) {
    assert.equal((await app.call("/api/turn", { ...input, rawBody, headers: { "Content-Type": contentType } })).status, expected);
  }
  assert.equal(app.calls.length, 0);
  const result = await app.call("/api/turn", input);
  assert.equal(result.status, 200);
  assert.equal(result.json.inputMode, "voice");
  assert.equal(result.json.transcript, "Voice input");
  assert.equal(app.calls.length, 3);
  const stt = app.calls[0];
  assert.equal(stt.url, "https://api.elevenlabs.io/v1/speech-to-text");
  assert.equal(stt.options.body.get("model_id"), "scribe_v2");
  const file = stt.options.body.get("file");
  assert.equal(file.name, "utterance.wav");
  assert.equal(file.type, "audio/wav");
  assert.deepEqual(Buffer.from(await file.arrayBuffer()), wav);
  assert.match(app.calls[2].url, /text-to-speech\/test-manju\?/u);
  assert.doesNotMatch(JSON.stringify(result.json), /test-manju|test-key/u);
  assert.deepEqual((await app.history()).turns, []);
  assert.deepEqual((await app.call(result.json.audioUrl)).bytes, audioBytes);
  await app.post("/api/delivery", { turnId: result.json.turnId, subtitleDisplayed: true });
  assert.deepEqual((await app.history()).turns, [{ player: "Voice input", npc: replyMl }]);
});

test("unlimited app setting admits more than 25 turns and reports null remaining while provider failures remain visible", async (t) => {
  const app = await fixture(t, { liveTurnLimit: 0 });
  await app.post("/api/select", selection);
  for (let index = 0; index < 27; index += 1) {
    const turn = await app.post("/api/text-turn", { text: `Allowed turn ${index}` });
    assert.equal(turn.status, 200);
    await app.post("/api/delivery", { turnId: turn.json.turnId, subtitleDisplayed: true });
  }
  const status = (await app.call("/api/status")).json;
  assert.equal(status.liveTurnLimit, 0);
  assert.equal(status.liveTurnsRemaining, null);
  assert.equal(status.audioLimitSeconds, 15);
  assert.equal(app.calls.length, 54);
  app.providers.brain.respond = async () => { throw new Error("Provider quota exhausted (HTTP 429)."); };
  const rejected = await app.post("/api/text-turn", { text: "Provider failure is not hidden" });
  assert.equal(rejected.status, 400);
  assert.match(rejected.json.error, /Provider quota exhausted/u);
  assert.equal((await app.call("/api/status")).json.liveTurnsRemaining, null);
});

test("per-run turn budget is enforced without calling providers for rejected turns", async (t) => {
  const app = await fixture(t, { liveTurnLimit: 1 });
  await app.post("/api/select", selection);
  const turn = (await app.post("/api/text-turn", { text: "Allowed" })).json;
  await app.post("/api/delivery", { turnId: turn.turnId, subtitleDisplayed: true });
  assert.equal((await app.post("/api/text-turn", { text: "Over budget" })).status, 400);
  assert.equal((await app.call("/api/status")).json.liveTurnsRemaining, 0);
  assert.equal(app.calls.length, 2);
});
