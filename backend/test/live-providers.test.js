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
    assert.equal(calls[0].options.headers["xi-api-key"], config.elevenLabsApiKey);
    assert.equal(calls[1].options.headers["xi-api-key"], config.elevenLabsApiKey);
    assert.equal(audio.playable, true);
    assert.deepEqual(audio.bytes, Buffer.from([1, 2, 3]));
    assert.match(calls[1].url, /text-to-speech\/test-voice\?output_format=mp3_44100_128$/u);
    assert.deepEqual(JSON.parse(calls[1].options.body), { text: "നമസ്കാരം", model_id: "eleven_v4", language_code: "ml" });
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test("MP3 microphone uploads retain an MP3 filename", async () => {
  const originalFetch = globalThis.fetch;
  let file;
  globalThis.fetch = async (_url, options) => {
    file = options.body.get("file");
    return new Response(JSON.stringify({ text: "നമസ്കാരം", language_code: "ml" }));
  };
  try {
    await createLiveProviders(config).stt.transcribe({ bytes: Buffer.from([1]), mimeType: "audio/mp3" }, new AbortController().signal);
    assert.equal(file.name, "utterance.mp3");
    assert.equal(file.type, "audio/mp3");
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test("speech synthesis rejects non-audio responses rather than advertising playable MP3", async () => {
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async () => new Response("{}", { headers: { "Content-Type": "application/json" } });
  try {
    await assert.rejects(createLiveProviders(config).tts.synthesize({ text: "നമസ്കാരം" }, new AbortController().signal), /not MP3 audio/u);
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test("ElevenLabs errors map only allowlisted reason codes to useful actions", async (t) => {
  const reasons = [
    ["invalid_api_key", 401, /ELEVENLABS_API_KEY/u],
    ["missing_api_key", 401, /Set ELEVENLABS_API_KEY/u],
    ["missing_permissions", 401, /permission for the requested operation/u],
    ["detected_unusual_activity", 401, /contact ElevenLabs support/u],
    ["quota_exceeded", 429, /credit quota is exhausted/u],
    ["insufficient_credits", 401, /insufficient credits/u],
    ["voice_not_found", 404, /ELEVENLABS_VOICE_ID/u],
    ["model_not_found", 400, /ELEVENLABS_TTS_MODEL/u],
    ["invalid_model_id", 400, /ELEVENLABS_TTS_MODEL/u],
  ];
  for (const [reason, status, action] of reasons) {
    await t.test(reason, async () => {
      const originalFetch = globalThis.fetch;
      globalThis.fetch = async () => new Response(JSON.stringify({
        detail: { status: reason, message: "untrusted-provider-message-with-fake-secret" },
      }), { status, headers: { "request-id": "test-request-1234" } });
      try {
        await assert.rejects(createLiveProviders(config).tts.synthesize({ text: "നമസ്കാരം" }, new AbortController().signal), (error) => {
          assert.match(error.message, new RegExp(`HTTP ${status}`, "u"));
          assert.ok(error.message.includes(`reason ${reason}`));
          assert.match(error.message, action);
          assert.match(error.message, /test-request-1234/u);
          assert.doesNotMatch(error.message, /untrusted-provider-message|fake-secret/u);
          return true;
        });
      } finally {
        globalThis.fetch = originalFetch;
      }
    });
  }
});

test("unknown, malformed and oversized ElevenLabs errors never reflect provider bodies", async (t) => {
  const bodies = [
    JSON.stringify({ detail: { status: "untrusted-status-with-fake-secret", message: "private-provider-message" } }),
    JSON.stringify({ detail: { status: "__proto__", message: "private-provider-message" } }),
    JSON.stringify({ detail: "private-provider-message" }),
    "not-json-private-provider-message",
    JSON.stringify({ detail: { status: "invalid_api_key", message: "private-provider-message".repeat(1000) } }),
  ];
  for (const [index, body] of bodies.entries()) {
    await t.test(`unrecognized response ${index + 1}`, async () => {
      const originalFetch = globalThis.fetch;
      globalThis.fetch = async () => new Response(body, { status: 401 });
      try {
        await assert.rejects(createLiveProviders(config).tts.synthesize({ text: "നമസ്കാരം" }, new AbortController().signal), (error) => {
          assert.match(error.message, /HTTP 401/u);
          assert.match(error.message, /Check ELEVENLABS_API_KEY and key permissions/u);
          assert.doesNotMatch(error.message, /private-provider-message|untrusted-status|fake-secret|__proto__|reason /u);
          return true;
        });
      } finally {
        globalThis.fetch = originalFetch;
      }
    });
  }
});

test("speech-recognition authentication errors use the same sanitized reason mapping", async () => {
  const originalFetch = globalThis.fetch;
  globalThis.fetch = async () => new Response(JSON.stringify({ detail: { status: "missing_permissions" } }), { status: 401 });
  try {
    await assert.rejects(createLiveProviders(config).stt.transcribe({ bytes: Buffer.from([1]), mimeType: "audio/webm" }, new AbortController().signal), /speech recognition request failed \(HTTP 401, reason missing_permissions\)/u);
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
