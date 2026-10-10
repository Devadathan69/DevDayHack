import test from "node:test";
import assert from "node:assert/strict";
import { rmSync } from "node:fs";
import { randomUUID } from "node:crypto";
import { fileURLToPath } from "node:url";
import { SqliteMemoryStore } from "../src/memory/sqlite-memory-store.js";

function temporaryStore(t) {
  const directory = fileURLToPath(new URL(`./.memory-fixture-${randomUUID()}/`, import.meta.url));
  let store = new SqliteMemoryStore(directory);
  t.after(() => {
    store.close();
    rmSync(directory, { recursive: true, force: true });
  });
  return { directory, get store() { return store; }, reopen() { store.close(); store = new SqliteMemoryStore(directory); return store; } };
}

function context(store, overrides = {}) {
  const key = { worldId: "world-a", playerId: "player-a", npcId: "demo-npc-001", ...overrides };
  return { ...key, turnId: "turn-1", gameSessionId: "session-1", sceneGeneration: 0,
    memoryGeneration: store.currentGeneration(key) };
}

test("SQLite history survives reopening and stays isolated by world/player/NPC", async (t) => {
  const fixture = temporaryStore(t);
  let store = fixture.store;
  const turn = context(store);
  await store.recordAcceptedInput(turn, "എനിക്ക് പഴംപൊരി ഇഷ്ടമാണ്");
  await store.recordGeneratedReply(turn, { replyMl: "ഓർത്ത് വെക്കാം!" });
  await store.markDelivery(turn, { subtitleDisplayed: true, audioPlayed: false });
  store = fixture.reopen();

  assert.deepEqual(await store.loadContext(turn), {
    generation: 0,
    turns: [{ player: "എനിക്ക് പഴംപൊരി ഇഷ്ടമാണ്", npc: "ഓർത്ത് വെക്കാം!" }],
  });
  assert.deepEqual(await store.loadContext({ ...turn, playerId: "player-b" }), { generation: 0, turns: [] });
  assert.deepEqual(await store.loadContext({ ...turn, npcId: "different-npc" }), { generation: 0, turns: [] });
  assert.deepEqual(await store.loadContext({ ...turn, worldId: "world-b" }), { generation: 0, turns: [] });
});

test("reset erases history and rejects a stale generation write", async (t) => {
  const { store } = temporaryStore(t);
  const oldTurn = context(store);
  await store.recordAcceptedInput(oldTurn, "രഹസ്യമല്ലാത്ത ഡെമോ വിവരം");
  const generation = await store.reset(oldTurn);
  assert.equal(generation, 1);
  assert.deepEqual(await store.loadContext(oldTurn), { generation: 1, turns: [] });
  await assert.rejects(store.recordGeneratedReply(oldTurn, { replyMl: "പഴയ മറുപടി" }), /memory that has been reset/i);
});

test("generated replies stay out of context until delivery is acknowledged", async (t) => {
  const { store } = temporaryStore(t);
  const turn = context(store);
  await store.recordAcceptedInput(turn, "എന്റെ പ്രിയപ്പെട്ട നിറം പച്ചയാണ്");
  await store.recordGeneratedReply(turn, { replyMl: "ശരി, ഓർത്ത് വെക്കാം." });
  assert.deepEqual((await store.loadContext(turn)).turns, []);
  await store.markDelivery(turn, { subtitleDisplayed: false, audioPlayed: true });
  assert.equal((await store.loadContext(turn)).turns.length, 1);
});
