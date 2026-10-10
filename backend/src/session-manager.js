import { randomUUID } from "node:crypto";
import { isAbortError, validateConversationKey } from "./domain.js";

export class SessionManager {
  #selection = null;
  #active = null;

  constructor({ registry, memory, events = () => {} }) {
    this.registry = registry;
    this.memory = memory;
    this.events = events;
    this.state = "IDLE";
  }

  get activeTurnId() {
    return this.#active?.context.turnId ?? null;
  }

  get hasSelection() {
    return this.#selection !== null;
  }

  selectNpc({ npcId, worldId, playerId, gameSessionId, sceneGeneration }) {
    if (this.#active) throw new Error("Finish or cancel the current turn before selecting an NPC.");
    const profile = this.registry.getProfile(npcId);
    if (!profile) throw new Error(`NPC '${npcId}' is not configured.`);
    const key = validateConversationKey({ worldId, playerId, npcId });
    if (typeof gameSessionId !== "string" || gameSessionId.length === 0) throw new Error("A game session ID is required.");
    if (!Number.isSafeInteger(sceneGeneration) || sceneGeneration < 0) throw new Error("Scene generation must be a non-negative integer.");
    this.#selection = { ...key, gameSessionId, sceneGeneration };
    this.#setState("SELECTED");
    return profile;
  }

  async startTurn(audioInput) {
    if (!this.#selection) throw new Error("Select the configured NPC before starting a conversation.");
    if (this.#active) throw new Error("A turn is already active. Wait for its delivery acknowledgement or cancel it.");

    const selection = this.#selection;
    const turnContext = {
      ...selection,
      turnId: randomUUID(),
      memoryGeneration: this.memory.currentGeneration(selection),
    };
    const abortController = new AbortController();
    const active = { context: turnContext, abortController, result: null };
    this.#active = active;

    active.promise = this.registry.get(selection.npcId)
      .handleUtterance(turnContext, audioInput, abortController.signal, ({ state }) => {
        if (this.#active === active) this.#setState(state, turnContext);
      })
      .then((result) => {
        if (this.#active === active) {
          active.result = result;
        }
        return result;
      })
      .catch((error) => {
        if (this.#active === active) {
          this.#active = null;
          this.#setState(isAbortError(error) ? "CANCELLED" : "ERROR", turnContext, error.message);
          if (this.#selection) this.#setState("SELECTED");
        }
        throw error;
      });
    return active.promise;
  }

  async acknowledgeDelivery(receipt) {
    const active = this.#active;
    if (!active?.result) throw new Error("There is no completed reply awaiting delivery acknowledgement.");
    await this.registry.get(active.context.npcId).acknowledgeDelivery(active.context, receipt);
    if (this.#active === active) {
      this.#active = null;
      this.#setState("SELECTED", active.context);
    }
  }

  cancel(reason = "Conversation cancelled.") {
    if (!this.#active) return false;
    const active = this.#active;
    this.#active = null;
    active.abortController.abort(new DOMException(reason, "AbortError"));
    this.#setState("CANCELLED", active.context, reason);
    if (this.#selection) this.#setState("SELECTED");
    return true;
  }

  invalidateScene() {
    this.cancel("Scene changed; the active turn was discarded.");
    this.#selection = null;
    this.#setState("IDLE");
  }

  async resetSelectedMemory() {
    if (!this.#selection) throw new Error("Select an NPC before resetting its memory.");
    if (this.#active && this.#active.context.npcId === this.#selection.npcId) {
      this.cancel("NPC memory reset; the active turn was discarded.");
    }
    return this.memory.reset(this.#selection);
  }

  #setState(state, context, message) {
    this.state = state;
    this.events({ type: "conversation.state", state, npcId: context?.npcId ?? this.#selection?.npcId ?? null,
      turnId: context?.turnId ?? null, message });
  }
}
