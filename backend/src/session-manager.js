import { randomUUID } from "node:crypto";
import { assertNotAborted, isAbortError, validateConversationKey, validateGameScope } from "./domain.js";
import { describeWorldContext, validateWorldSnapshot, worldContextForDialogue } from "./world-context.js";

export class SessionManager {
  #selection = null;
  #active = null;
  #worldSnapshot = null;
  #lastWorldSnapshot = null;
  #lastWorldScope = null;

  constructor({ registry, memory, events = () => {}, clock = Date.now }) {
    this.registry = registry;
    this.memory = memory;
    this.events = events;
    this.clock = clock;
    this.state = "IDLE";
  }

  get activeTurnId() {
    return this.#active?.context.turnId ?? null;
  }

  get hasSelection() {
    return this.#selection !== null;
  }

  get selectedProfile() {
    return this.#selection ? this.registry.getProfile(this.#selection.npcId) : null;
  }

  get selectedConversationKey() {
    if (!this.#selection) return null;
    const { worldId, playerId, npcId } = this.#selection;
    return { worldId, playerId, npcId };
  }

  get worldContext() {
    return describeWorldContext(this.#worldSnapshot, this.#selection, this.clock());
  }

  get lastWorldContext() {
    return describeWorldContext(this.#lastWorldSnapshot, this.#lastWorldScope, this.clock());
  }

  selectNpc({ npcId, worldId, playerId, gameSessionId, sceneGeneration, worldContext }) {
    const profile = this.registry.getProfile(npcId);
    if (!profile) throw new Error(`NPC '${npcId}' is not configured.`);
    const key = validateConversationKey({ worldId, playerId, npcId });
    const selection = { ...key, ...validateGameScope({ gameSessionId, sceneGeneration }) };
    const snapshot = validateWorldSnapshot(worldContext, this.clock());
    const sameSelection = this.#selection && Object.keys(selection).every((field) => this.#selection[field] === selection[field]);
    if (!sameSelection) {
      this.cancel("Selected NPC scope changed; the previous turn was discarded.");
      this.#selection = selection;
    }
    this.#worldSnapshot = snapshot;
    if (snapshot) {
      this.#lastWorldSnapshot = snapshot;
      this.#lastWorldScope = selection;
    }
    if (!sameSelection) this.#setState("SELECTED");
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
      }, () => this.#selection === selection ? worldContextForDialogue(this.worldContext, this.clock()) : describeWorldContext(null, selection, this.clock()))
      .then((result) => {
        assertNotAborted(abortController.signal);
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
    if (receipt?.audioPlayed === true && active.result.audio?.playable !== true) {
      throw new Error("This reply has no playable audio to acknowledge.");
    }
    await this.registry.get(active.context.npcId).acknowledgeDelivery(active.context, receipt);
    if (this.#active === active) {
      this.#active = null;
      this.#setState("SELECTED", active.context);
    }
  }

  cancel(reason = "Conversation cancelled.", turnId = this.activeTurnId) {
    if (!this.#active || this.activeTurnId !== turnId) return false;
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
    this.#worldSnapshot = null;
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
