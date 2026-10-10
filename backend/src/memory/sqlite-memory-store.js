import { DatabaseSync } from "node:sqlite";
import { mkdirSync } from "node:fs";
import { join } from "node:path";
import { StaleMemoryGenerationError, validateConversationKey, validateTurnContext } from "../domain.js";

export class SqliteMemoryStore {
  #database;
  #statements;

  constructor(dataDirectory) {
    mkdirSync(dataDirectory, { recursive: true });
    this.#database = new DatabaseSync(join(dataDirectory, "memory.sqlite"));
    this.#database.exec(`
      PRAGMA journal_mode = WAL;
      PRAGMA foreign_keys = ON;
      CREATE TABLE IF NOT EXISTS memory_scopes (
        world_id TEXT NOT NULL,
        player_id TEXT NOT NULL,
        npc_id TEXT NOT NULL,
        generation INTEGER NOT NULL DEFAULT 0,
        PRIMARY KEY (world_id, player_id, npc_id)
      );
      CREATE TABLE IF NOT EXISTS turns (
        turn_id TEXT PRIMARY KEY,
        world_id TEXT NOT NULL,
        player_id TEXT NOT NULL,
        npc_id TEXT NOT NULL,
        generation INTEGER NOT NULL,
        input_text TEXT NOT NULL,
        reply_ml TEXT,
        subtitle_displayed INTEGER NOT NULL DEFAULT 0,
        audio_played INTEGER NOT NULL DEFAULT 0,
        created_at INTEGER NOT NULL,
        delivered_at INTEGER,
        FOREIGN KEY (world_id, player_id, npc_id)
          REFERENCES memory_scopes (world_id, player_id, npc_id)
      );
      CREATE INDEX IF NOT EXISTS turns_scope_delivery
        ON turns (world_id, player_id, npc_id, generation, delivered_at, created_at);
    `);
    this.#statements = {
      ensureScope: this.#database.prepare(`INSERT INTO memory_scopes (world_id, player_id, npc_id)
        VALUES (?, ?, ?) ON CONFLICT DO NOTHING`),
      generation: this.#database.prepare(`SELECT generation FROM memory_scopes
        WHERE world_id = ? AND player_id = ? AND npc_id = ?`),
      byTurnId: this.#database.prepare("SELECT * FROM turns WHERE turn_id = ?"),
      insertTurn: this.#database.prepare(`INSERT INTO turns
        (turn_id, world_id, player_id, npc_id, generation, input_text, created_at)
        VALUES (?, ?, ?, ?, ?, ?, ?)`),
      history: this.#database.prepare(`SELECT input_text, reply_ml FROM turns
        WHERE world_id = ? AND player_id = ? AND npc_id = ? AND generation = ? AND delivered_at IS NOT NULL
        ORDER BY created_at DESC LIMIT 12`),
      saveReply: this.#database.prepare(`UPDATE turns SET reply_ml = ?
        WHERE turn_id = ? AND world_id = ? AND player_id = ? AND npc_id = ? AND generation = ? AND reply_ml IS NULL`),
      saveDelivery: this.#database.prepare(`UPDATE turns SET
        subtitle_displayed = MAX(subtitle_displayed, ?),
        audio_played = MAX(audio_played, ?),
        delivered_at = COALESCE(delivered_at, ?)
        WHERE turn_id = ? AND world_id = ? AND player_id = ? AND npc_id = ? AND generation = ? AND reply_ml IS NOT NULL`),
      trimTurns: this.#database.prepare(`DELETE FROM turns
        WHERE world_id = ? AND player_id = ? AND npc_id = ? AND turn_id NOT IN (
          SELECT turn_id FROM turns WHERE world_id = ? AND player_id = ? AND npc_id = ?
          ORDER BY created_at DESC LIMIT 100
        )`),
      deleteTurns: this.#database.prepare(`DELETE FROM turns WHERE world_id = ? AND player_id = ? AND npc_id = ?`),
      incrementGeneration: this.#database.prepare(`UPDATE memory_scopes SET generation = generation + 1
        WHERE world_id = ? AND player_id = ? AND npc_id = ?`),
    };
  }

  #ensureScope(key) {
    const safe = validateConversationKey(key);
    this.#statements.ensureScope.run(safe.worldId, safe.playerId, safe.npcId);
    return safe;
  }

  currentGeneration(key) {
    const safe = this.#ensureScope(key);
    return this.#statements.generation.get(safe.worldId, safe.playerId, safe.npcId).generation;
  }

  async loadContext(key) {
    const safe = this.#ensureScope(key);
    const generation = this.currentGeneration(safe);
    const turns = this.#statements.history.all(safe.worldId, safe.playerId, safe.npcId, generation)
      .reverse().map(({ input_text, reply_ml }) => ({ player: input_text, npc: reply_ml }));
    return { generation, turns };
  }

  async recordAcceptedInput(context, text) {
    const safe = validateTurnContext(context);
    const generation = this.currentGeneration(safe);
    if (generation !== safe.memoryGeneration) throw new StaleMemoryGenerationError();
    const existing = this.#statements.byTurnId.get(safe.turnId);
    if (existing) {
      if (existing.world_id !== safe.worldId || existing.player_id !== safe.playerId || existing.npc_id !== safe.npcId || existing.input_text !== text) {
        throw new Error("Turn ID is already used by a different conversation.");
      }
      return;
    }
    this.#statements.insertTurn.run(safe.turnId, safe.worldId, safe.playerId, safe.npcId,
      safe.memoryGeneration, text, Date.now());
    this.#statements.trimTurns.run(safe.worldId, safe.playerId, safe.npcId,
      safe.worldId, safe.playerId, safe.npcId);
  }

  async recordGeneratedReply(context, reply) {
    const safe = validateTurnContext(context);
    this.#assertGeneration(safe);
    const result = this.#statements.saveReply.run(reply.replyMl, safe.turnId, safe.worldId,
      safe.playerId, safe.npcId, safe.memoryGeneration);
    if (result.changes === 0) {
      const existing = this.#statements.byTurnId.get(safe.turnId);
      if (existing?.reply_ml === reply.replyMl) return;
      throw new Error("The player input for this reply is missing or has already changed.");
    }
  }

  async markDelivery(context, receipt) {
    const safe = validateTurnContext(context);
    this.#assertGeneration(safe);
    if (receipt?.subtitleDisplayed !== true && receipt?.audioPlayed !== true) {
      throw new Error("Delivery requires visible subtitles or confirmed audio playback.");
    }
    const result = this.#statements.saveDelivery.run(receipt.subtitleDisplayed === true ? 1 : 0,
      receipt.audioPlayed === true ? 1 : 0, Date.now(), safe.turnId, safe.worldId,
      safe.playerId, safe.npcId, safe.memoryGeneration);
    if (result.changes === 0) throw new Error("The reply is not ready to mark as delivered.");
  }

  async reset(key) {
    const safe = this.#ensureScope(key);
    this.#database.exec("BEGIN IMMEDIATE");
    try {
      this.#statements.incrementGeneration.run(safe.worldId, safe.playerId, safe.npcId);
      this.#statements.deleteTurns.run(safe.worldId, safe.playerId, safe.npcId);
      const generation = this.currentGeneration(safe);
      this.#database.exec("COMMIT");
      return generation;
    } catch (error) {
      this.#database.exec("ROLLBACK");
      throw error;
    }
  }

  #assertGeneration(context) {
    const generation = this.currentGeneration(context);
    if (context.memoryGeneration !== generation) throw new StaleMemoryGenerationError();
  }

  close() {
    this.#database.close();
  }
}
