import { assertNotAborted, validateDialogueReply, validateProfile, validateTranscript, validateTurnContext } from "./domain.js";

export class NpcController {
  constructor(profile, dependencies) {
    this.profile = validateProfile(profile);
    for (const name of ["stt", "brain", "tts", "memory"]) {
      if (!dependencies?.[name]) throw new Error(`NpcController requires a '${name}' dependency.`);
    }
    this.dependencies = dependencies;
    this.events = dependencies.events ?? (() => {});
  }

  async handleUtterance(context, audioInput, signal, onState = () => {}) {
    const turn = validateTurnContext(context);
    if (turn.npcId !== this.profile.id) throw new Error("Turn NPC does not match this controller's profile.");
    assertNotAborted(signal);

    this.#emit(turn, "TRANSCRIBING", onState);
    const transcript = validateTranscript(await this.dependencies.stt.transcribe(audioInput, signal));
    assertNotAborted(signal);
    await this.dependencies.memory.recordAcceptedInput(turn, transcript.text);

    this.#emit(turn, "THINKING", onState);
    const history = await this.dependencies.memory.loadContext(turn);
    if (history.generation !== turn.memoryGeneration) {
      throw new Error("Conversation memory changed during this turn. Please try again.");
    }
    const reply = validateDialogueReply(await this.dependencies.brain.respond({
      profile: this.profile,
      transcript: transcript.text,
      history: history.turns,
    }, signal));
    assertNotAborted(signal);
    await this.dependencies.memory.recordGeneratedReply(turn, reply);

    this.#emit(turn, "SYNTHESIZING", onState);
    let audio = null;
    let speechError = null;
    try {
      audio = await this.dependencies.tts.synthesize({
        text: reply.replyMl,
        language: this.profile.language,
        voiceRef: this.profile.voiceRef,
      }, signal);
      if (!audio || typeof audio !== "object" || typeof audio.playable !== "boolean") {
        throw new Error("Speech synthesis returned invalid audio metadata.");
      }
      assertNotAborted(signal);
    } catch (error) {
      if (signal.aborted) throw error;
      speechError = error instanceof Error ? error.message : "Speech synthesis failed.";
    }

    this.#emit(turn, "AWAITING_DELIVERY", onState);
    return { turnContext: turn, transcript, reply, audio, speechError };
  }

  async acknowledgeDelivery(context, receipt) {
    return this.dependencies.memory.markDelivery(context, receipt);
  }

  #emit(context, state, onState) {
    const event = { type: "conversation.state", state, npcId: this.profile.id, turnId: context.turnId };
    this.events(event);
    onState(event);
  }
}
