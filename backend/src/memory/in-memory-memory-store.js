import { StaleMemoryGenerationError, validateConversationKey, validateTurnContext } from "../domain.js";

function scopeId(key) {
  const safeKey = validateConversationKey(key);
  return JSON.stringify([safeKey.worldId, safeKey.playerId, safeKey.npcId]);
}

export class InMemoryMemoryStore {
  #scopes = new Map();

  #getScope(key) {
    const id = scopeId(key);
    let scope = this.#scopes.get(id);
    if (!scope) {
      scope = { generation: 0, turns: new Map() };
      this.#scopes.set(id, scope);
    }
    return scope;
  }

  currentGeneration(key) {
    return this.#getScope(key).generation;
  }

  async loadContext(key) {
    const scope = this.#getScope(key);
    const deliveredTurns = [...scope.turns.values()]
      .filter((turn) => turn.delivery && turn.replyMl)
      .sort((a, b) => a.createdAt - b.createdAt)
      .slice(-12)
      .map(({ inputText, replyMl }) => ({ player: inputText, npc: replyMl }));
    return { generation: scope.generation, turns: deliveredTurns };
  }

  async recordAcceptedInput(context, text) {
    const safeContext = validateTurnContext(context);
    const scope = this.#assertGeneration(safeContext);
    if (scope.turns.has(safeContext.turnId)) return;
    scope.turns.set(safeContext.turnId, {
      turnId: safeContext.turnId,
      inputText: text,
      createdAt: Date.now(),
      replyMl: null,
      delivery: null,
    });
    this.#trim(scope);
  }

  async recordGeneratedReply(context, reply) {
    const safeContext = validateTurnContext(context);
    const scope = this.#assertGeneration(safeContext);
    const turn = scope.turns.get(safeContext.turnId);
    if (!turn) throw new Error("Cannot save a reply before its accepted player input.");
    turn.replyMl = reply.replyMl;
  }

  async markDelivery(context, receipt) {
    const safeContext = validateTurnContext(context);
    const scope = this.#assertGeneration(safeContext);
    const turn = scope.turns.get(safeContext.turnId);
    if (!turn?.replyMl) throw new Error("Cannot acknowledge a reply that was not generated.");
    if (receipt?.dialogueBoxDisplayed !== true && receipt?.audioPlayed !== true) {
      throw new Error("Delivery requires a displayed dialogue box or confirmed audio playback.");
    }
    turn.delivery = {
      dialogueBoxDisplayed: receipt.dialogueBoxDisplayed === true,
      audioPlayed: receipt.audioPlayed === true,
      deliveredAt: Date.now(),
    };
    this.#trim(scope);
  }

  async reset(key) {
    const scope = this.#getScope(key);
    scope.generation += 1;
    scope.turns.clear();
    return scope.generation;
  }

  #assertGeneration(context) {
    const scope = this.#getScope(context);
    if (context.memoryGeneration !== scope.generation) throw new StaleMemoryGenerationError();
    return scope;
  }

  #trim(scope) {
    const maxStoredTurns = 100;
    const oldestFirst = [...scope.turns.values()].sort((a, b) => a.createdAt - b.createdAt);
    while (scope.turns.size > maxStoredTurns) {
      scope.turns.delete(oldestFirst.shift().turnId);
    }
  }
}
