import test from "node:test";
import assert from "node:assert/strict";
import { loadConfig } from "../src/config.js";

const environment = {
  OPENAI_API_KEY: "test-openai-key", ELEVENLABS_API_KEY: "test-elevenlabs-key",
  ELEVENLABS_VOICE_ID: "test-default-voice",
};

test("optional account voice overrides are absent or empty by default", () => {
  for (const values of [{}, { ELEVENLABS_MALE_VOICE_ID: "", ELEVENLABS_FEMALE_VOICE_ID: "" }]) {
    const config = loadConfig({ ...environment, ...values });
    assert.equal(config.elevenLabsVoiceId, "test-default-voice");
    assert.equal(config.elevenLabsMaleVoiceId, undefined);
    assert.equal(config.elevenLabsFemaleVoiceId, undefined);
    assert.ok(Object.isFrozen(config));
  }
});

test("optional account voice overrides accept independent safe identifiers", () => {
  const maleOnly = loadConfig({ ...environment, ELEVENLABS_MALE_VOICE_ID: "test-male-voice" });
  assert.equal(maleOnly.elevenLabsMaleVoiceId, "test-male-voice");
  assert.equal(maleOnly.elevenLabsFemaleVoiceId, undefined);
  const both = loadConfig({ ...environment, ELEVENLABS_MALE_VOICE_ID: "a".repeat(5),
    ELEVENLABS_FEMALE_VOICE_ID: "Z".repeat(128) });
  assert.equal(both.elevenLabsMaleVoiceId, "a".repeat(5));
  assert.equal(both.elevenLabsFemaleVoiceId, "Z".repeat(128));
});

test("named voice pools validate and freeze approved entries without changing browser defaults", () => {
  const male = [{ name: "Miyan", voiceId: "test-miyan" }, { name: "Adith", voiceId: "test-adith" },
    { name: "DT", voiceId: "test-dt-voice" }, { name: "Damu", voiceId: "test-damu" }];
  const female = [{ name: "Manju", voiceId: "test-manju" }];
  const config = loadConfig({ ...environment, ELEVENLABS_MALE_VOICES: JSON.stringify(male), ELEVENLABS_FEMALE_VOICES: JSON.stringify(female) });
  assert.deepEqual(config.elevenLabsMaleVoices, male);
  assert.deepEqual(config.elevenLabsFemaleVoices, female);
  assert.ok(Object.isFrozen(config.elevenLabsMaleVoices));
  assert.ok(Object.isFrozen(config.elevenLabsMaleVoices[0]));
  assert.equal(config.elevenLabsVoiceId, environment.ELEVENLABS_VOICE_ID);
  assert.deepEqual(loadConfig(environment).elevenLabsMaleVoices, []);
  assert.deepEqual(loadConfig({ ...environment, ELEVENLABS_FEMALE_VOICES: "" }).elevenLabsFemaleVoices, []);
});

test("malformed named pools reject safely without reflecting account IDs or JSON bodies", () => {
  const valid = { name: "Miyan", voiceId: "test-private-pool-id" };
  const invalid = ["not-json-private", "null", "{}", "42", true, [], "x".repeat(16385),
    ...[null, 42, [], {}, { ...valid, name: "" }, { ...valid, name: "<script>" }, { ...valid, name: "n".repeat(41) },
      { ...valid, name: "Name\nInjected" }, { ...valid, voiceId: "bad/path-private" }, { ...valid, voiceId: 12345 },
      { ...valid, extra: "private" }].map((entry) => JSON.stringify([entry])),
    JSON.stringify([valid, valid]), JSON.stringify([valid, { ...valid, name: "Another" }]),
    JSON.stringify([valid, { ...valid, name: "miyan", voiceId: "another-private-id" }]),
    JSON.stringify(Array.from({ length: 17 }, (_, index) => ({ name: `Voice ${index}`, voiceId: `test-id-${index}` }))),
  ];
  for (const name of ["ELEVENLABS_MALE_VOICES", "ELEVENLABS_FEMALE_VOICES"]) {
    for (const value of invalid) {
      assert.throws(() => loadConfig({ ...environment, [name]: value }), (error) => {
        assert.ok(error.message.startsWith(name));
        assert.doesNotMatch(error.message, /private|test-openai-key|test-elevenlabs-key|Injected/u);
        return true;
      });
    }
  }
});

test("app turn limits default to unlimited and still allow explicit finite safety limits", () => {
  assert.equal(loadConfig(environment).liveTurnLimit, 0);
  assert.equal(loadConfig({ ...environment, LIVE_TURN_LIMIT: "0" }).liveTurnLimit, 0);
  assert.equal(loadConfig({ ...environment, LIVE_TURN_LIMIT: "" }).liveTurnLimit, 0);
  assert.equal(loadConfig({ ...environment, LIVE_TURN_LIMIT: "25" }).liveTurnLimit, 25);
  for (const value of ["-1", "1.5", "501", "unlimited"]) {
    assert.throws(() => loadConfig({ ...environment, LIVE_TURN_LIMIT: value }), /LIVE_TURN_LIMIT/u);
  }
});

test("unsafe optional voice identifiers reject without disclosing IDs or credentials", () => {
  for (const setting of ["ELEVENLABS_MALE_VOICE_ID", "ELEVENLABS_FEMALE_VOICE_ID"]) {
    for (const value of [null, true, 12345, [], {}, " ", "abcd", "a".repeat(129),
      "fake-private-id/path", "fake-private-id?query", "fake-private-id\n", "fake-private-id%20", "fake-private-id😀"]) {
      assert.throws(() => loadConfig({ ...environment, [setting]: value }), (error) => {
        assert.equal(error.message, `${setting} must be a valid voice ID or empty.`);
        assert.doesNotMatch(error.message, /fake-private-id|test-openai-key|test-elevenlabs-key/u);
        return true;
      });
    }
  }
});
