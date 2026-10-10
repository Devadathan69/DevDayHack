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

  function begin(path, { method = "GET", body, token = app.bridgeToken, headers = {} } = {}) {
    const bytes = body === undefined ? null : Buffer.from(JSON.stringify(body));
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
  for (const path of ["/api/select", "/api/text-turn", "/api/delivery", "/api/cancel", "/api/deselect"]) {
    assert.equal((await app.call(path, { method: "POST", body: {}, token: null })).status, 403);
  }
  assert.equal((await app.call("/api/history")).status, 403);
  assert.equal((await app.post("/api/reset")).status, 403);
  assert.equal((await app.post("/api/turn")).status, 403);
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

test("per-run turn budget is enforced without calling providers for rejected turns", async (t) => {
  const app = await fixture(t, { liveTurnLimit: 1 });
  await app.post("/api/select", selection);
  const turn = (await app.post("/api/text-turn", { text: "Allowed" })).json;
  await app.post("/api/delivery", { turnId: turn.turnId, subtitleDisplayed: true });
  assert.equal((await app.post("/api/text-turn", { text: "Over budget" })).status, 400);
  assert.equal((await app.call("/api/status")).json.liveTurnsRemaining, 0);
  assert.equal(app.calls.length, 2);
});
