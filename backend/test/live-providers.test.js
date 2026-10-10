import test from "node:test";
import assert from "node:assert/strict";
import { createLiveProviders } from "../src/providers/live-providers.js";

const config = {
  openAiApiKey: "test-openai-key",
  openAiModel: "test-model",
  elevenLabsApiKey: "test-elevenlabs-key",
  elevenLabsVoiceId: "test-voice",
  elevenLabsTtsModel: "eleven_v4",
};

test("live adapters send audio to Scribe and request ElevenLabs audio without logging input", async () => {
  const originalFetch = globalThis.fetch;
  const calls = [];
  globalThis.fetch = async (url, options) => {
    calls.push({ url: String(url), options });
    if (String(url).includes("speech-to-text")) return new Response(JSON.stringify({ text: "നമസ്കാരം", language_code: "ml" }));
    return new Response(new Uint8Array([1, 2, 3]), { headers: { "Content-Type": "audio/mpeg" } });
  };
  try {
    const providers = createLiveProviders(config);
    const transcript = await providers.stt.transcribe({ bytes: Buffer.from([4, 5]), mimeType: "audio/webm" }, new AbortController().signal);
    const audio = await providers.tts.synthesize({ text: "നമസ്കാരം", voiceRef: "manu-default" }, new AbortController().signal);
    assert.deepEqual(transcript, { text: "നമസ്കാരം", language: "ml", inputMode: "voice" });
    assert.equal(calls[0].url, "https://api.elevenlabs.io/v1/speech-to-text");
    assert.equal(calls[0].options.body.get("model_id"), "scribe_v2");
    assert.equal(audio.playable, true);
    assert.deepEqual(audio.bytes, Buffer.from([1, 2, 3]));
    assert.match(calls[1].url, /text-to-speech\/test-voice\?output_format=mp3_44100_128$/u);
    assert.deepEqual(JSON.parse(calls[1].options.body), { text: "നമസ്കാരം", model_id: "eleven_v4", language_code: "ml" });
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test("dialogue adapter sends bounded history to the OpenAI Responses API", async () => {
  const originalFetch = globalThis.fetch;
  let call;
  globalThis.fetch = async (url, options) => {
    call = { url: String(url), options };
    return new Response(JSON.stringify({ output_text: "ശരി, ഞാൻ ഓർത്ത് വെക്കാം." }));
  };
  try {
    const providers = createLiveProviders(config);
    const result = await providers.brain.respond({
      profile: { displayName: "Manu", personality: "friendly", originalBackstory: "likes snacks" },
      transcript: "എനിക്ക് പഴംപൊരി ഇഷ്ടമാണ്",
      history: [{ player: "എനിക്ക് ചായ ഇഷ്ടമാണ്", npc: "ഓർത്ത് വെക്കാം." }],
    }, new AbortController().signal);
    const payload = JSON.parse(call.options.body);
    assert.equal(call.url, "https://api.openai.com/v1/responses");
    assert.equal(payload.store, false);
    assert.equal(payload.model, "test-model");
    assert.match(payload.input, /പഴംപൊരി/u);
    assert.deepEqual(result, { replyMl: "ശരി, ഞാൻ ഓർത്ത് വെക്കാം." });
  } finally {
    globalThis.fetch = originalFetch;
  }
});
