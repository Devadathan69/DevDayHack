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
    const memoryEnabled = this.profile.behavior?.memoryEnabled !== false;

    this.#emit(turn, "TRANSCRIBING", onState);
    const sttStartedAt = performance.now();
    const transcript = validateTranscript(await this.dependencies.stt.transcribe(audioInput, signal));
    const speechRecognitionMs = Math.round(performance.now() - sttStartedAt);
    assertNotAborted(signal);
    if (memoryEnabled) await this.dependencies.memory.recordAcceptedInput(turn, transcript.text);
    assertNotAborted(signal);

    this.#emit(turn, "THINKING", onState);
    const history = memoryEnabled
      ? await this.dependencies.memory.loadContext(turn)
      : { generation: turn.memoryGeneration, turns: [] };
    assertNotAborted(signal);
    if (history.generation !== turn.memoryGeneration) {
      throw new Error("Conversation memory changed during this turn. Please try again.");
    }
    const dialogueStartedAt = performance.now();
    const reply = validateDialogueReply(await this.dependencies.brain.respond({
      profile: this.profile,
      transcript: transcript.text,
      history: history.turns,
    }, signal));
    const dialogueMs = Math.round(performance.now() - dialogueStartedAt);
    assertNotAborted(signal);
    if (memoryEnabled) await this.dependencies.memory.recordGeneratedReply(turn, reply);
    assertNotAborted(signal);

    this.#emit(turn, "SYNTHESIZING", onState);
    let audio = null;
    let speechError = null;
    let speechSynthesisMs = 0;
    const synthesisStartedAt = performance.now();
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
    } finally {
      speechSynthesisMs = Math.round(performance.now() - synthesisStartedAt);
    }

    assertNotAborted(signal);
    this.#emit(turn, "AWAITING_DELIVERY", onState);
    return { turnContext: turn, transcript, reply, audio, speechError,
      timings: { speechRecognitionMs, dialogueMs, speechSynthesisMs } };
  }

  async acknowledgeDelivery(context, receipt) {
    if (this.profile.behavior?.memoryEnabled === false) return;
    return this.dependencies.memory.markDelivery(context, receipt);
  }

  #emit(context, state, onState) {
    const event = { type: "conversation.state", state, npcId: this.profile.id, turnId: context.turnId };
    this.events(event);
    onState(event);
  }
}
