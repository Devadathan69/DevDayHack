import { homedir } from "node:os";
import { join, resolve } from "node:path";

function integerSetting(env, name, fallback, min, max) {
  const raw = env[name];
  if (raw === undefined || raw === "") return fallback;
  const value = Number(raw);
  if (!Number.isInteger(value) || value < min || value > max) {
    throw new Error(`${name} must be an integer from ${min} to ${max}.`);
  }
  return value;
}

export function loadConfig(env = process.env) {
  if (env.NPC_MODE !== undefined || process.argv.some((arg) => arg === "--mock" || arg === "--live")) {
    throw new Error("NPC_MODE and provider-mode flags are no longer supported. The app always uses live providers; remove the old setting or flag.");
  }
  const localAppData = env.LOCALAPPDATA ?? join(homedir(), "AppData", "Local");
  const appDataDirectory = join(localAppData, "MalayalamNpcDemo");
  const config = {
    host: "127.0.0.1",
    port: integerSetting(env, "UI_PORT", 4177, 1024, 65535),
    worldId: env.WORLD_ID ?? "demo-world",
    playerId: env.PLAYER_ID ?? "demo-player",
    turnTimeoutMs: integerSetting(env, "TURN_TIMEOUT_MS", 30_000, 5_000, 120_000),
    maxAudioSeconds: integerSetting(env, "MAX_AUDIO_SECONDS", 15, 3, 30),
    maxAudioBytes: integerSetting(env, "MAX_AUDIO_BYTES", 4 * 1024 * 1024, 64 * 1024, 20 * 1024 * 1024),
    liveTurnLimit: integerSetting(env, "LIVE_TURN_LIMIT", 25, 1, 500),
    openAiModel: env.OPENAI_MODEL ?? "gpt-4.1-mini",
    elevenLabsVoiceId: env.ELEVENLABS_VOICE_ID,
    elevenLabsTtsModel: env.ELEVENLABS_TTS_MODEL ?? "eleven_v4",
    dataDirectory: resolve(env.DATA_DIR ?? join(appDataDirectory, "data")),
    bridgeTokenPath: join(appDataDirectory, "bridge-token"),
    openAiApiKey: env.OPENAI_API_KEY,
    elevenLabsApiKey: env.ELEVENLABS_API_KEY,
  };

  for (const [name, value] of [["WORLD_ID", config.worldId], ["PLAYER_ID", config.playerId]]) {
    if (typeof value !== "string" || value.length < 1 || value.length > 128) {
      throw new Error(`${name} must contain 1–128 characters.`);
    }
  }
  const missing = [
    ["OPENAI_API_KEY", config.openAiApiKey],
    ["ELEVENLABS_API_KEY", config.elevenLabsApiKey],
    ["ELEVENLABS_VOICE_ID", config.elevenLabsVoiceId],
  ].filter(([, value]) => !value).map(([name]) => name);
  if (missing.length) throw new Error(`Provider configuration needs ${missing.join(" and ")}. Add them to the local .env file.`);
  if (!/^[A-Za-z0-9_-]{5,128}$/u.test(config.elevenLabsVoiceId)) {
    throw new Error("ELEVENLABS_VOICE_ID must be a valid voice ID.");
  }
  if (!["eleven_v3", "eleven_v4"].includes(config.elevenLabsTtsModel)) {
    throw new Error("ELEVENLABS_TTS_MODEL must be 'eleven_v3' or 'eleven_v4' to support Malayalam.");
  }
  return Object.freeze(config);
}
