import { assertNotAborted } from "../domain.js";

export function createMockProviders({ delayMs = 0, failAt = null, malformedAt = null } = {}) {
  if (!Number.isInteger(delayMs) || delayMs < 0 || delayMs > 30_000) {
    throw new Error("Mock provider delay must be an integer from 0 to 30,000 milliseconds.");
  }
  for (const [name, value] of [["failAt", failAt], ["malformedAt", malformedAt]]) {
    if (value !== null && !["stt", "brain", "tts"].includes(value)) {
      throw new Error(`${name} must be 'stt', 'brain', 'tts', or null.`);
    }
  }

  async function prepare(stage, signal) {
    assertNotAborted(signal);
    if (delayMs > 0) {
      await new Promise((resolve, reject) => {
        const timer = setTimeout(() => {
          signal.removeEventListener("abort", onAbort);
          resolve();
        }, delayMs);
        const onAbort = () => {
          clearTimeout(timer);
          signal.removeEventListener("abort", onAbort);
          reject(signal.reason);
        };
        signal.addEventListener("abort", onAbort, { once: true });
      });
    }
    assertNotAborted(signal);
    if (failAt === stage) throw new Error(`Mock ${stage} failure (requested by configuration).`);
    return malformedAt === stage;
  }

  return {
    stt: {
      async transcribe(input, signal) {
        if (await prepare("stt", signal)) return { text: 42, language: "ml" };
        if (!input || typeof input.text !== "string") {
          throw new Error("Mock speech input needs a 'text' field. Live microphone capture is not connected.");
        }
        return { text: input.text, language: "ml" };
      },
    },
    brain: {
      async respond(input, signal) {
        if (await prepare("brain", signal)) return { replyMl: "Malformed mock response" };
        const asksForRecall = /ഓർക്കുന്ന|ഓർത്തുവെച്ച|മുമ്പ് പറഞ്ഞത്|എന്താണ്.*പറഞ്ഞത്/u.test(input.transcript);
        const remembered = asksForRecall ? input.history.at(-1)?.player : null;
        return {
          replyMl: remembered
            ? `മോക്ക് ഓർമ്മ: നീ മുമ്പ് പറഞ്ഞത് “${remembered.slice(0, 240)}” എന്നാണ്.`
            : "ഇത് മോക്ക് മറുപടിയാണ്; ലൈവ് AI ബന്ധിപ്പിച്ചിട്ടില്ല. കുറച്ച് കൂടി പറയാമോ?",
        };
      },
    },
    tts: {
      async synthesize(input, signal) {
        if (await prepare("tts", signal)) return { kind: "mock", voiceRef: input.voiceRef, playable: "no" };
        return { kind: "mock", voiceRef: input.voiceRef, playable: false };
      },
    },
  };
}
