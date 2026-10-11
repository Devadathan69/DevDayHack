import test from "node:test";
import assert from "node:assert/strict";
import { createLiveProviders } from "../src/providers/live-providers.js";
import { describeWorldContext, validateWorldSnapshot, WORLD_CONTEXT_TTL_MS } from "../src/world-context.js";

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
    assert.deepEqual(JSON.parse(calls[1].options.body), {
      text: "നമസ്കാരം", model_id: "eleven_v4", language_code: "ml",
      voice_settings: { stability: 0.55, style: 0, similarity_boost: 0.75, use_speaker_boost: true, speed: 1 },
    });
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test("speech profiles use only configured male/female voices and keep neutral/default behavior", async () => {
  const originalFetch = globalThis.fetch;
  const calls = [];
  globalThis.fetch = async (url, options) => {
    calls.push({ url: String(url), options });
    return new Response(new Uint8Array([1]), { headers: { "Content-Type": "audio/mpeg" } });
  };
  try {
    const voicedConfig = { ...config, elevenLabsMaleVoiceId: "test-male-voice", elevenLabsFemaleVoiceId: "test-female-voice" };
    const providers = createLiveProviders(voicedConfig);
    for (const [voiceRef, expected] of [["configured-male", "test-male-voice"], ["configured-female", "test-female-voice"],
      ["manu-default", "test-voice"], [undefined, "test-voice"], ["arbitrary-caller-voice-id", "test-voice"],
      ["test-male-voice", "test-voice"]]) {
      await providers.tts.synthesize({ text: "നമസ്കാരം", voiceRef }, new AbortController().signal);
      assert.equal(calls.at(-1).url, `https://api.elevenlabs.io/v1/text-to-speech/${expected}?output_format=mp3_44100_128`);
      assert.equal(calls.at(-1).options.headers["xi-api-key"], config.elevenLabsApiKey);
    }
    for (const voiceRef of ["configured-male", "configured-female"]) {
      await createLiveProviders(config).tts.synthesize({ text: "നമസ്കാരം", voiceRef }, new AbortController().signal);
      assert.match(calls.at(-1).url, /text-to-speech\/test-voice\?/u);
    }
    await createLiveProviders({ ...config, elevenLabsMaleVoiceId: "test-male-voice" }).tts.synthesize({
      text: "നമസ്കാരം", voiceRef: "configured-female",
    }, new AbortController().signal);
    assert.match(calls.at(-1).url, /text-to-speech\/test-voice\?/u);
  } finally {
    globalThis.fetch = originalFetch;
  }
});

test("named voice pool aliases resolve only approved account entries", async () => {
  const originalFetch = globalThis.fetch;
  const calls = [];
  globalThis.fetch = async (url) => {
    calls.push(String(url));
    return new Response(new Uint8Array([1]), { headers: { "Content-Type": "audio/mpeg" } });
  };
  const pools = { ...config,
    elevenLabsMaleVoices: [{ name: "Miyan", voiceId: "test-miyan" }, { name: "Adith", voiceId: "test-adith" },
      { name: "DT", voiceId: "test-dt-voice" }, { name: "Damu", voiceId: "test-damu" }],
    elevenLabsFemaleVoices: [{ name: "Manju", voiceId: "test-manju" }] };
  try {
    const providers = createLiveProviders(pools);
    for (const [voiceRef, voiceId] of [["configured-male-0", "test-miyan"], ["configured-male-1", "test-adith"],
      ["configured-male-2", "test-dt-voice"], ["configured-male-3", "test-damu"], ["configured-female-0", "test-manju"],
      ["configured-male", "test-miyan"], ["configured-female", "test-manju"], ["manu-default", "test-voice"],
      ["configured-male-99", "test-voice"], ["configured-male-01", "test-voice"], ["test-manju", "test-voice"],
      ["configured-male-0/private", "test-voice"]]) {
      await providers.tts.synthesize({ text: "നമസ്കാരം", voiceRef }, new AbortController().signal);
      assert.equal(calls.at(-1), `https://api.elevenlabs.io/v1/text-to-speech/${voiceId}?output_format=mp3_44100_128`);
    }
  } finally { globalThis.fetch = originalFetch; }
});

test("override voice failures never echo voice IDs, credentials or provider messages", async () => {
  const originalFetch = globalThis.fetch;
  const voicedConfig = { ...config, elevenLabsMaleVoiceId: "test-private-male-id", elevenLabsFemaleVoiceId: "test-private-female-id" };
  globalThis.fetch = async () => new Response(JSON.stringify({ detail: {
    status: "voice_not_found", message: `${voicedConfig.elevenLabsMaleVoiceId} ${voicedConfig.elevenLabsFemaleVoiceId} ${config.elevenLabsApiKey}`,
  } }), { status: 404 });
  try {
    for (const voiceRef of ["configured-male", "configured-female"]) {
      await assert.rejects(createLiveProviders(voicedConfig).tts.synthesize({ text: "നമസ്കാരം", voiceRef }, new AbortController().signal), (error) => {
        assert.match(error.message, /ELEVENLABS_MALE_VOICE_ID\/ELEVENLABS_FEMALE_VOICE_ID/u);
        assert.doesNotMatch(error.message, /test-private|test-elevenlabs-key/u);
        return true;
      });
    }
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

test("resident dialogue stays immersive, adapts detail and receives requested factual tips only", async () => {
  const originalFetch = globalThis.fetch;
  const calls = [];
  globalThis.fetch = async (url, options) => {
    calls.push(JSON.parse(options.body));
    return new Response(JSON.stringify({ output_text: "ഒരു ചെറിയ രഹസ്യം: ROCKETMAN. പഴയ സേവ് സുരക്ഷിതമായി വെക്കൂ." }));
  };
  try {
    const base = { displayName: "Damu (1X)", personality: "warm and curious", originalBackstory: "repairs bicycles",
      dialogue: { replyLength: "balanced", language: "ml", boundary: "Keep it friendly. Never claim to control the game or know things outside this conversation." } };
    await createLiveProviders(config).brain.respond({ profile: base, transcript: "jetpack code", history: [] }, new AbortController().signal);
    const request = calls[0], input = JSON.parse(request.input);
    assert.match(request.instructions, /resident of San Andreas/u);
    assert.match(request.instructions, /never claim you are in a game/u);
    assert.doesNotMatch(request.instructions, /original fictional NPC in a local game demo/u);
    assert.doesNotMatch(request.instructions, /know things outside this conversation/u);
    assert.match(request.instructions, /not a rigid sentence quota/u);
    assert.match(request.instructions, /without a parenthesized scope identifier/u);
    assert.match(request.instructions, /general reference facts are knowledge/u);
    assert.ok(input.localReferenceFacts.some((fact) => fact.code === "ROCKETMAN"));
    assert.ok(input.localReferenceFacts.some((fact) => fact.id === "save"));
    await createLiveProviders(config).brain.respond({ profile: base, transcript: "How is your day?", history: [] }, new AbortController().signal);
    assert.ok(JSON.parse(calls[1].input).localReferenceFacts.every((fact) => !fact.code));
    assert.equal(calls.length, 2);
  } finally { globalThis.fetch = originalFetch; }
});

test("reasoning models reserve answer tokens without adding unsupported controls to other models", async () => {
  const originalFetch = globalThis.fetch;
  const calls = [];
  globalThis.fetch = async (url, options) => {
    calls.push(JSON.parse(options.body));
    return new Response(JSON.stringify({ output_text: "ശരി, പറയൂ." }));
  };
  try {
    const input = { profile: { displayName: "Manu", personality: "warm", originalBackstory: "a local resident" }, transcript: "Hello", history: [] };
    await createLiveProviders({ ...config, openAiModel: "gpt-5.4-mini" }).brain.respond(input, new AbortController().signal);
    assert.deepEqual(calls[0].reasoning, { effort: "low" });
    assert.equal(calls[0].max_output_tokens, 1000);
    await createLiveProviders(config).brain.respond(input, new AbortController().signal);
    assert.equal(calls[1].reasoning, undefined);
    assert.equal(calls[1].max_output_tokens, 450);
  } finally { globalThis.fetch = originalFetch; }
});

test("dialogue JSON includes bounded grounded observations and history without extra calls or changing published preferences", async () => {
  const originalFetch = globalThis.fetch;
  const calls = [];
  globalThis.fetch = async (url, options) => {
    calls.push({ url: String(url), options });
    return new Response(JSON.stringify({ output_text: "ഇപ്പോൾ ഇവിടെ മഴയാണ്." }));
  };
  try {
    const now = Date.now();
    const worldContext = describeWorldContext(validateWorldSnapshot({ capturedAt: now,
      location: { name: "Ganton", zoneCode: "GANTON", position: { x: 2490, y: -1670, z: 13, area: 0 } },
      time: { hour: 21, minute: 7 }, weather: { id: 8, rainIntensity: 0.75 } }),
    { gameSessionId: "gta-grounded", sceneGeneration: 257 });
    const boundary = "Never claim to know things outside the conversation.";
    await createLiveProviders(config).brain.respond({
      profile: { displayName: "Manu", personality: "friendly", originalBackstory: "A fictional guide from Invented Town.",
        dialogue: { replyLength: "detailed", language: "ml-en", boundary }, behavior: { sceneAware: true } },
      transcript: "Where are we and what is the weather?",
      history: Array.from({ length: 25 }, (_, index) => ({ player: `${index}:` + "p".repeat(1100), npc: "ന".repeat(500),
        turnId: "private-turn-id", privateToken: "private-history-token" })),
      worldContext: { ...worldContext, privateToken: "private-world-token" },
    }, new AbortController().signal);
    assert.equal(calls.length, 1);
    assert.equal(calls[0].url, "https://api.openai.com/v1/responses");
    const request = JSON.parse(calls[0].options.body);
    const payload = JSON.parse(request.input);
    assert.equal(request.store, false);
    assert.equal(payload.deliveredHistory.length, 12);
    assert.ok(payload.deliveredHistory[0].player.startsWith("13:"));
    assert.ok(payload.deliveredHistory.every((turn) => turn.player.length <= 1000 && turn.npc.length <= 400));
    assert.deepEqual(Object.keys(payload.deliveredHistory[0]), ["player", "npc"]);
    assert.equal(payload.currentWorldContext.source, "gta-native");
    assert.equal(payload.currentWorldContext.location.name, "Ganton");
    assert.deepEqual(payload.currentWorldContext.time, { hour: 21, minute: 7 });
    assert.equal(payload.currentWorldContext.weather.condition, "rainy");
    assert.equal(payload.currentWorldContext.weather.rainIntensity, 0.75);
    assert.ok(JSON.stringify(payload.currentWorldContext).length < 1500);
    assert.doesNotMatch(request.input, /private-world-token|private-history-token|private-turn-id/u);
    assert.match(request.instructions, /fictional biography/u);
    assert.match(request.instructions, /explicitly provided conversation context/u);
    assert.match(request.instructions, /never the computer clock/u);
    assert.match(request.instructions, /earlier scene statements are not current facts/u);
    assert.match(request.instructions, /never arbitrary game commands/u);
    assert.ok(request.instructions.includes(boundary));
    assert.match(request.instructions, /natural English code-switching/u);
    assert.match(request.instructions, /compact explanation or a few useful steps/u);
    assert.match(request.instructions, /not a rigid sentence quota/u);
    assert.doesNotMatch(request.instructions, /No descriptive game-scene data is currently provided/u);
  } finally { globalThis.fetch = originalFetch; }
});

test("dialogue treats missing/stale/browser context as unknown and sceneAware false suppresses even fresh native facts", async () => {
  const originalFetch = globalThis.fetch;
  const calls = [];
  globalThis.fetch = async (_url, options) => {
    calls.push(JSON.parse(options.body));
    return new Response(JSON.stringify({ output_text: "ഇപ്പോഴത്തെ സമയം അറിയില്ല." }));
  };
  try {
    const now = Date.now();
    const scope = { gameSessionId: "gta-grounded", sceneGeneration: 1 };
    const fresh = describeWorldContext(validateWorldSnapshot({ capturedAt: now,
      location: { name: "Ganton" }, time: { hour: 12, minute: 30 }, weather: { id: 1 } }), scope);
    const stale = describeWorldContext({ ...validateWorldSnapshot({ capturedAt: now, time: { hour: 1, minute: 2 } }),
      capturedAt: now - WORLD_CONTEXT_TTL_MS - 1000 }, scope);
    for (const [worldContext, sceneAware] of [[undefined, true], [stale, true], [{ ...fresh, source: "browser" }, true], [fresh, false]]) {
      await createLiveProviders(config).brain.respond({ profile: { displayName: "Manu", personality: "friendly",
        originalBackstory: "A fictional biography, not the current scene.", behavior: { sceneAware } },
      transcript: "What time is it?", history: [{ player: "Earlier we were in Ganton", npc: "ശരി." }], worldContext }, new AbortController().signal);
      const payload = JSON.parse(calls.at(-1).input);
      assert.equal(payload.currentWorldContext.available, false);
      assert.equal(payload.currentWorldContext.location, null);
      assert.equal(payload.currentWorldContext.time, null);
      assert.equal(payload.currentWorldContext.weather, null);
      assert.match(calls.at(-1).instructions, /Say you cannot currently tell/u);
    }
    assert.equal(calls.length, 4);
    assert.match(calls.at(-1).instructions, /Scene awareness is disabled/u);
  } finally { globalThis.fetch = originalFetch; }
});

test("dialogue JSON uses actual rain/transition observations and never forwards a target-weather forecast", async () => {
  const originalFetch = globalThis.fetch;
  const calls = [];
  globalThis.fetch = async (_url, options) => {
    calls.push(JSON.parse(options.body));
    return new Response(JSON.stringify({ output_text: "കാലാവസ്ഥ മാറിക്കൊണ്ടിരിക്കുന്നു." }));
  };
  try {
    const now = Date.now();
    const scope = { gameSessionId: "gta-actual-weather", sceneGeneration: 257 };
    const cases = [
      [{ id: 1, targetId: 8, condition: "transitioning", rainIntensity: 0.05 }, "transitioning"],
      [{ id: 1, targetId: 1, condition: "rainy", rainIntensity: 0.4 }, "rainy"],
      [{ id: 8, targetId: 8, condition: "transitioning", rainIntensity: 0 }, "transitioning"],
      [{ id: 1, targetId: 8, condition: null, rainIntensity: null }, null],
      [{ targetId: 8 }, null],
    ];
    for (const [weather, expected] of cases) {
      const scene = describeWorldContext(validateWorldSnapshot({ capturedAt: now, weather }), scope);
      await createLiveProviders(config).brain.respond({ profile: { displayName: "Manu", personality: "friendly",
        originalBackstory: "Fictional biography." }, transcript: "What weather is actually happening?", history: [], worldContext: scene },
      new AbortController().signal);
      const request = calls.at(-1);
      const actual = JSON.parse(request.input).currentWorldContext.weather;
      if (weather.id === undefined) assert.equal(actual, null);
      else {
        assert.equal(actual.id, weather.id);
        assert.equal(actual.condition, expected);
        assert.equal(actual.rainIntensity, weather.rainIntensity);
        assert.equal(Object.hasOwn(actual, "targetId"), false);
      }
      assert.doesNotMatch(request.input, /targetId|targetWeatherId/u);
      assert.match(request.instructions, /greater than 0\.05 confirms observed rain/u);
      assert.match(request.instructions, /not that a target condition has occurred/u);
      assert.match(request.instructions, /Never derive a condition from an enum alone/u);
    }
    assert.equal(calls.length, cases.length);
  } finally { globalThis.fetch = originalFetch; }
});
