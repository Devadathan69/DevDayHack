import test from "node:test";
import assert from "node:assert/strict";
import { request } from "node:http";
import { once } from "node:events";
import { randomUUID } from "node:crypto";
import { rmSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { createNpcServer } from "../src/server.js";
import { SqliteMemoryStore } from "../src/memory/sqlite-memory-store.js";
import { WORLD_CONTEXT_TTL_MS } from "../src/world-context.js";

const profile = { id: "demo-npc-001", displayName: "Manu", personality: "friendly",
  originalBackstory: "A fictional guide from an invented town.", voiceRef: "test-voice", language: "ml", memoryPolicy: "private" };
const selection = { gameSessionId: "gta-world-test", sceneGeneration: 257,
  pedestrian: { modelId: 7, pedType: 4, modelGender: "male" } };
const replyMl = "ഇപ്പോൾ ഗാന്റണിലാണ്.";

async function fixture(t) {
  let now = Date.now();
  const initialTime = now;
  const directory = fileURLToPath(new URL(`./.world-fixture-${randomUUID()}/`, import.meta.url));
  const memory = new SqliteMemoryStore(directory);
  const inputs = [];
  const recordings = [];
  const speech = [];
  const providers = {
    stt: { transcribe: async (input) => {
      recordings.push(input);
      return { text: input.text ?? "Voice location question" };
    } },
    brain: { respond: async (input) => { inputs.push(input); return { replyMl }; } },
    tts: { synthesize: async (input) => { speech.push(input); return { playable: false }; } },
  };
  const config = { host: "127.0.0.1", port: 0, worldId: "test-world", playerId: "test-player",
    turnTimeoutMs: 2000, liveTurnLimit: 0, maxAudioBytes: 65536, maxAudioSeconds: 15 };
  const app = createNpcServer({ config, profile, memory, providers, clock: () => now });
  app.server.listen(0, config.host);
  await once(app.server, "listening");
  t.after(async () => {
    app.manager.cancel("Test completed.");
    app.server.closeAllConnections();
    await new Promise((resolve) => app.server.close(resolve));
    memory.close();
    rmSync(directory, { recursive: true, force: true });
  });
  const origin = `http://127.0.0.1:${app.server.address().port}`;
  function call(path, { method = "GET", body, rawBody, token = app.bridgeToken, headers = {} } = {}) {
    const bytes = rawBody ?? (body === undefined ? null : Buffer.from(JSON.stringify(body)));
    return new Promise((resolve, reject) => {
      const outgoing = request({ host: config.host, port: app.server.address().port, path, method,
        headers: { ...(token ? { "X-Bridge-Token": token } : {}),
          ...(bytes ? { "Content-Type": "application/json", "Content-Length": bytes.length } : {}), ...headers } }, (response) => {
        const chunks = [];
        response.on("data", (chunk) => chunks.push(chunk));
        response.on("end", () => {
          const text = Buffer.concat(chunks).toString("utf8");
          resolve({ status: response.statusCode, headers: response.headers,
            json: response.headers["content-type"]?.includes("application/json") ? JSON.parse(text) : null });
        });
        response.on("error", reject);
      });
      outgoing.on("error", reject);
      outgoing.end(bytes);
    });
  }
  const post = (path, body) => call(path, { method: "POST", body });
  const snapshot = (changes = {}) => ({ location: { name: "Ganton", zoneCode: "GANTON", position: { x: 2490, y: -1670, z: 13, area: 0 } },
    time: { hour: 22, minute: 15 }, weather: { id: 8, targetId: 1, condition: "rainy", rainIntensity: 0.65 }, capturedAt: now, ...changes });
  async function browser() {
    const page = await call("/", { token: null });
    return { token: null, headers: { Cookie: page.headers["set-cookie"][0].split(";")[0], Origin: origin } };
  }
  const deliver = (turn) => post("/api/delivery", { turnId: turn.json.turnId, subtitleDisplayed: true });
  return { ...app, memory, inputs, recordings, speech, providers, call, post, browser, snapshot, deliver,
    initialTime, setTime: (value) => { now = value; } };
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

test("only native bridge authentication accepts strict world snapshots; browser/admin cannot author current game facts", async (t) => {
  const app = await fixture(t);
  const nativeSelection = { ...selection, worldContext: app.snapshot() };
  assert.equal((await app.call("/api/select", { method: "POST", body: nativeSelection, token: "wrong-token" })).status, 403);
  const browser = await app.browser();
  for (const worldContext of [app.snapshot(), null, { source: "gta-native" }]) {
    assert.equal((await app.call("/api/select", { ...browser, method: "POST", body: { worldContext } })).status, 403);
  }
  assert.equal((await app.call("/api/status", browser)).json.worldContext.available, false);
  for (const worldContext of [
    { ...app.snapshot(), source: "gta-native" }, { ...app.snapshot(), token: app.bridgeToken },
    { ...app.snapshot(), location: { name: "x".repeat(81) } },
    { ...app.snapshot(), time: { hour: 24, minute: 1 } },
    { ...app.snapshot(), weather: { id: 23 } },
    { ...app.snapshot(), weather: { targetId: 23 } },
    { ...app.snapshot(), weather: { rainIntensity: 1.1 } },
    { ...app.snapshot(), capturedAt: app.initialTime + 2001 },
    { ...app.snapshot(), location: { position: { x: 1e30, y: 0, z: 0, area: 0 } } },
  ]) {
    const result = await app.post("/api/select", { ...selection, worldContext });
    assert.equal(result.status, 400);
    assert.ok(!JSON.stringify(result.json).includes(app.bridgeToken));
    assert.equal(app.manager.hasSelection, false);
  }
  assert.equal((await app.post("/api/select", nativeSelection)).status, 200);
  const status = (await app.call("/api/status", browser)).json;
  assert.equal(status.worldContext.source, "gta-native");
  assert.equal(status.worldContext.available, true);
  assert.equal(status.worldContext.stale, false);
  assert.equal(status.worldContext.ageMs, 0);
  assert.deepEqual(status.worldContext.scope, { gameSessionId: selection.gameSessionId, sceneGeneration: selection.sceneGeneration });
  assert.deepEqual(status.worldContext, status.dialogueWorldContext);
  assert.equal(app.inputs.length, 0);
});

test("native typed and raw WAV turns feed the current scoped observations to the brain with unchanged delivery and memory", async (t) => {
  const app = await fixture(t);
  const selected = await app.post("/api/select", { ...selection, worldContext: app.snapshot() });
  const conversationKey = app.manager.selectedConversationKey;
  const turn = await app.post("/api/text-turn", { text: "Where are we and is it raining?" });
  assert.equal(turn.status, 200);
  assert.equal(turn.json.inputMode, "typed");
  assert.equal(app.inputs[0].worldContext.location.name, "Ganton");
  assert.deepEqual(app.inputs[0].worldContext.time, { hour: 22, minute: 15 });
  assert.equal(app.inputs[0].worldContext.weather.condition, "rainy");
  assert.deepEqual((await app.memory.loadContext(conversationKey)).turns, []);
  assert.equal((await app.deliver(turn)).status, 200);
  const next = app.snapshot({ location: { name: "Idlewood", zoneCode: "IDLE" }, time: { hour: 7, minute: 3 },
    weather: { id: 1, targetId: 1, condition: "sunny", rainIntensity: 0 } });
  const reselected = await app.post("/api/select", { ...selection, worldContext: next });
  assert.equal(reselected.json.npcId, selected.json.npcId);
  assert.equal(reselected.json.personality, selected.json.personality);
  const wav = Buffer.from("RIFF-world-test-WAVE");
  const voice = await app.call("/api/turn", { method: "POST", rawBody: wav, headers: { "Content-Type": "audio/wav" } });
  assert.equal(voice.status, 200);
  assert.equal(voice.json.inputMode, "voice");
  assert.deepEqual(app.recordings[1].bytes, wav);
  assert.equal(app.recordings[1].mimeType, "audio/wav");
  assert.equal(app.inputs[1].worldContext.location.name, "Idlewood");
  assert.deepEqual(app.inputs[1].worldContext.time, { hour: 7, minute: 3 });
  assert.equal(app.inputs[1].worldContext.weather.condition, "sunny");
  assert.equal(Object.hasOwn(app.inputs[1].worldContext.weather, "targetId"), false);
  assert.equal((await app.call("/api/status")).json.worldContext.weather.targetId, 1);
  assert.equal(app.inputs[1].history.length, 1);
  assert.equal(app.speech[1].voiceRef, app.speech[0].voiceRef);
  assert.equal(app.memory.currentGeneration(conversationKey), 0);
  assert.equal((await app.deliver(voice)).status, 200);
  assert.equal((await app.memory.loadContext(conversationKey)).turns.length, 2);
  assert.equal((await app.call("/api/status")).json.liveTurnsRemaining, null);
});

test("native weather separates observed conditions from UI-only target ids on each typed turn", async (t) => {
  const app = await fixture(t);
  const cases = [
    [{ id: 1, targetId: 8, condition: "transitioning", rainIntensity: 0.05 }, "transitioning"],
    [{ id: 1, targetId: 1, condition: "rainy", rainIntensity: 0.2 }, "rainy"],
    [{ id: 8, targetId: 8, condition: "transitioning", rainIntensity: 0 }, "transitioning"],
    [{ id: 1, targetId: 8, condition: null, rainIntensity: null }, null],
  ];
  for (const [weather, condition] of cases) {
    const selected = await app.post("/api/select", { ...selection, worldContext: app.snapshot({ weather }) });
    assert.equal(selected.status, 200);
    const turn = await app.post("/api/text-turn", { text: "What is the actual weather now?" });
    assert.equal(turn.status, 200);
    const actual = app.inputs.at(-1).worldContext.weather;
    assert.equal(actual.condition, condition);
    assert.equal(actual.id, weather.id);
    assert.equal(actual.rainIntensity, weather.rainIntensity);
    assert.equal(Object.hasOwn(actual, "targetId"), false);
    const displayed = (await app.call("/api/status")).json.worldContext.weather;
    assert.equal(displayed.targetId, weather.targetId);
    assert.equal(displayed.condition, condition);
    await app.deliver(turn);
  }
});

test("same native select refresh during a valid raw-WAV turn does not abort, change voice, or clear delivery state", async (t) => {
  const app = await fixture(t);
  const pendingStt = deferred();
  app.providers.stt.transcribe = () => pendingStt.promise;
  const selected = await app.post("/api/select", { ...selection, worldContext: app.snapshot() });
  const pending = app.call("/api/turn", { method: "POST", rawBody: Buffer.from("WAVE"), headers: { "Content-Type": "audio/wav" } });
  await waitFor(() => app.manager.state === "TRANSCRIBING");
  const turnId = app.manager.activeTurnId;
  const refreshed = await app.post("/api/select", { ...selection, worldContext: app.snapshot({ time: { hour: 9, minute: 0 } }) });
  assert.equal(refreshed.status, 200);
  assert.equal(refreshed.json.npcId, selected.json.npcId);
  assert.equal(app.manager.activeTurnId, turnId);
  assert.equal(app.manager.state, "TRANSCRIBING");
  pendingStt.resolve({ text: "What time is it?" });
  const turn = await pending;
  assert.equal(turn.status, 200);
  assert.equal(turn.json.turnId, turnId);
  assert.deepEqual(app.inputs[0].worldContext.time, { hour: 9, minute: 0 });
  const awaiting = await app.post("/api/select", { ...selection, worldContext: app.snapshot({ location: { name: "Idlewood" } }) });
  assert.equal(awaiting.json.state, "AWAITING_DELIVERY");
  assert.equal(app.manager.activeTurnId, turnId);
  assert.equal((await app.deliver(turn)).status, 200);
});

test("status retains the last verified snapshot after deselect while stale/default/browser and changed scopes supply no invented realtime facts", async (t) => {
  const app = await fixture(t);
  const browser = await app.browser();
  await app.post("/api/select", { ...selection, worldContext: app.snapshot() });
  await app.post("/api/deselect", {});
  const retained = (await app.call("/api/status", browser)).json;
  assert.equal(retained.selected, false);
  assert.equal(retained.worldContext.location.name, "Ganton");
  assert.equal(retained.dialogueWorldContext.available, false);
  await app.call("/api/select", { ...browser, method: "POST", body: {} });
  const browserTurn = await app.call("/api/text-turn", { ...browser, method: "POST", body: { text: "Where am I now?" } });
  assert.equal(browserTurn.status, 200);
  assert.equal(app.inputs[0].worldContext.location, null);
  assert.equal(app.inputs[0].worldContext.time, null);
  assert.equal(app.inputs[0].worldContext.weather, null);
  await app.call("/api/delivery", { ...browser, method: "POST", body: { turnId: browserTurn.json.turnId, subtitleDisplayed: true } });
  await app.post("/api/select", { ...selection, sceneGeneration: selection.sceneGeneration + 1 });
  assert.equal((await app.call("/api/status")).json.dialogueWorldContext.available, false);
  const unknown = await app.post("/api/text-turn", { text: "What is the weather?" });
  assert.equal(app.inputs[1].worldContext.weather, null);
  await app.deliver(unknown);
  app.setTime(app.initialTime + WORLD_CONTEXT_TTL_MS + 1);
  const stale = (await app.call("/api/status", browser)).json.worldContext;
  assert.equal(stale.source, "gta-native");
  assert.equal(stale.capturedAt, app.initialTime);
  assert.equal(stale.ageMs, WORLD_CONTEXT_TTL_MS + 1);
  assert.equal(stale.stale, true);
  assert.equal(stale.available, false);
  assert.equal(stale.location, null);
  assert.equal(stale.time, null);
  assert.equal(stale.weather, null);
  await app.post("/api/select", { ...selection, worldContext: app.snapshot({ capturedAt: app.initialTime }) });
  const staleTurn = await app.post("/api/text-turn", { text: "Is that old time current?" });
  assert.equal(staleTurn.status, 200);
  assert.equal(app.inputs[2].worldContext.stale, true);
  assert.equal(app.inputs[2].worldContext.time, null);
  await app.deliver(staleTurn);
});

test("backend recreation keeps delivered conversation memory but never restores native observations from SQLite", async (t) => {
  const app = await fixture(t);
  const scope = { gameSessionId: selection.gameSessionId, sceneGeneration: selection.sceneGeneration };
  await app.post("/api/select", { ...scope, worldContext: app.snapshot() });
  const conversationKey = app.manager.selectedConversationKey;
  const first = await app.post("/api/text-turn", { text: "Tell me about this place" });
  await app.deliver(first);
  const restarted = createNpcServer({ config: { host: "127.0.0.1", port: 0, worldId: "test-world", playerId: "test-player" },
    profile, memory: app.memory, providers: app.providers, clock: () => app.initialTime });
  t.after(() => restarted.manager.cancel("Test completed."));
  assert.equal(restarted.manager.lastWorldContext.source, null);
  assert.equal(restarted.manager.lastWorldContext.available, false);
  restarted.manager.selectNpc({ ...conversationKey, ...scope });
  await restarted.manager.startTurn({ text: "Where are we now?" });
  assert.equal(app.inputs[1].history.length, 1);
  assert.equal(app.inputs[1].worldContext.source, null);
  assert.equal(app.inputs[1].worldContext.location, null);
  assert.equal(app.inputs[1].worldContext.time, null);
  assert.equal(app.inputs[1].worldContext.weather, null);
});

test("published admin preferences update the same pedestrian while preserving ground truth, authored boundaries, persona, voice and memory", async (t) => {
  const app = await fixture(t);
  const browser = await app.browser();
  const original = await app.post("/api/select", { ...selection, worldContext: app.snapshot() });
  const first = await app.post("/api/text-turn", { text: "Remember this greeting" });
  await app.deliver(first);
  const published = { ...profile, traits: { warmth: 80, curiosity: 20, humor: 30, confidence: 70 },
    dialogue: { replyLength: "brief", language: "ml-en", creativity: 35, focus: 90,
      boundary: "Never claim to know things outside the conversation." },
    behavior: { greeting: "reserved", ambient: "rare", memoryEnabled: true, sceneAware: true, teasing: false },
    voice: { preset: "default", mood: "calm", speed: 90 } };
  async function publish(data, number) {
    const version = { number, publishedAt: new Date(app.initialTime).toISOString(), profile: data };
    const result = await app.call("/api/admin/profiles", { ...browser, method: "PUT",
      body: { selectedId: profile.id, profiles: [{ data, versions: [version], activeVersion: number }] } });
    assert.equal(result.status, 200);
  }
  await publish(published, 1);
  const updated = await app.post("/api/select", { gameSessionId: selection.gameSessionId, sceneGeneration: selection.sceneGeneration,
    worldContext: app.snapshot({ time: { hour: 6, minute: 30 } }) });
  assert.equal(updated.json.npcId, original.json.npcId);
  assert.equal(updated.json.personality, original.json.personality);
  const turn = await app.post("/api/text-turn", { text: "What is our game time?" });
  assert.equal(turn.status, 200);
  assert.deepEqual(app.inputs[1].profile.dialogue, published.dialogue);
  assert.deepEqual(app.inputs[1].profile.personalityControls, published.traits);
  assert.deepEqual(app.inputs[1].profile.behavior, published.behavior);
  assert.deepEqual(app.inputs[1].worldContext.time, { hour: 6, minute: 30 });
  assert.equal(app.inputs[1].history.length, 1);
  assert.equal(app.speech[1].voiceRef, app.speech[0].voiceRef);
  assert.deepEqual(app.speech[1].voice, published.voice);
  await app.deliver(turn);
  const disabled = { ...published, behavior: { ...published.behavior, sceneAware: false } };
  await publish(disabled, 2);
  await app.post("/api/select", { ...selection, worldContext: app.snapshot() });
  const disabledTurn = await app.post("/api/text-turn", { text: "What is the weather?" });
  assert.equal(disabledTurn.status, 200);
  assert.equal(app.inputs[2].worldContext.available, false);
  assert.equal(app.inputs[2].worldContext.weather, null);
  assert.equal((await app.call("/api/status", browser)).json.worldContext.weather.condition, "rainy");
  await app.deliver(disabledTurn);
});
