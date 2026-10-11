import test from "node:test";
import assert from "node:assert/strict";
import { createPedestrianProfile, pedestrianProfileId, pedestrianGender, validateBridgeSelection, validatePedestrianMetadata } from "../src/pedestrian-profiles.js";
import { NpcRegistry } from "../src/npc-registry.js";

const base = { id: "demo-npc-001", displayName: "Manu", personality: "friendly",
  originalBackstory: "An original local guide.", voiceRef: "test-voice", language: "ml", memoryPolicy: "private" };
const scope = { worldId: "test-world", gameSessionId: "gta-1234", sceneGeneration: 257 };

function generate(sceneGeneration, metadata = { modelId: 7, pedType: 4 }) {
  return createPedestrianProfile(base, { ...scope, sceneGeneration }, metadata);
}

test("pedestrian metadata validates object shape, integer boundaries and short allowlisted gender", () => {
  assert.equal(validatePedestrianMetadata(undefined), null);
  for (const metadata of [{}, { modelId: 0, pedType: 0, gender: "unknown" }, { modelId: 65535, pedType: 31, modelGender: "female", gender: "female" }]) {
    assert.deepEqual(validatePedestrianMetadata(metadata), metadata);
  }
  const invalid = [null, [], "male", 4, true, { displayName: "Custom" }, { voiceRef: "Custom" }, { traits: {} },
    { gender: "male".repeat(1000) }, { gender: "other" }, { gender: "Male" }, { gender: null }, { gender: [] }];
  for (const field of ["modelId", "pedType"]) {
    for (const value of [-1, field === "modelId" ? 65536 : 32, 1.5, "4", null, true, [], {}, NaN, Infinity]) {
      invalid.push({ [field]: value });
    }
  }
  for (const metadata of invalid) assert.throws(() => validatePedestrianMetadata(metadata), { name: "ValidationError" });
});

test("model gender validates independently and explicit gender must agree with the derived result", () => {
  for (const modelGender of [null, true, 5, [], {}, "Female", "female".repeat(1000)]) {
    assert.throws(() => validatePedestrianMetadata({ modelGender }), { name: "ValidationError" });
  }
  for (let pedType = 0; pedType <= 31; pedType += 1) {
    for (const modelGender of ["male", "female"]) {
      assert.equal(pedestrianGender({ pedType, modelGender, gender: modelGender }), modelGender);
      assert.throws(() => pedestrianGender({ pedType, modelGender, gender: modelGender === "male" ? "female" : "male" }), /gender conflicts/u);
    }
  }
  assert.equal(pedestrianGender({ modelGender: "unknown", pedType: 5 }), "female");
  assert.equal(pedestrianGender({ modelGender: "unknown", pedType: 7 }), "unknown");
  assert.equal(pedestrianGender({ gender: "unknown" }), "unknown");
  assert.throws(() => pedestrianGender({ gender: "male" }), /gender conflicts/u);
});

test("bridge selection rejects unsupported fields, invalid scope types and excessive length", () => {
  const valid = { gameSessionId: scope.gameSessionId, sceneGeneration: scope.sceneGeneration };
  assert.equal(validateBridgeSelection(valid).pedestrian, null);
  assert.equal(validateBridgeSelection({ ...valid, pedestrian: {} }).sceneGeneration, 257);
  for (const input of [
    undefined, null, [], "selection", 257, true,
    { ...valid, npcId: "demo-npc-001" }, { ...valid, worldId: "custom" }, { ...valid, pedestrian: null },
    ...[undefined, null, [], 123, "", " ", "x".repeat(129)].map((gameSessionId) => ({ ...valid, gameSessionId })),
    ...[undefined, null, true, "257", -1, 0.5, Number.MAX_SAFE_INTEGER + 1].map((sceneGeneration) => ({ ...valid, sceneGeneration })),
  ]) assert.throws(() => validateBridgeSelection(input), { name: "ValidationError" });
});

test("reliable model metadata overrides live classifications, then CIV types and neutral fallback apply", () => {
  assert.equal(pedestrianGender({ pedType: 4 }), "male");
  assert.equal(pedestrianGender({ pedType: 5 }), "female");
  assert.throws(() => pedestrianGender({ pedType: 4, gender: "female" }), /gender conflicts/u);
  assert.equal(pedestrianGender({ pedType: 5, gender: "unknown" }), "female");
  assert.equal(pedestrianGender({ pedType: 4, modelGender: "female", gender: "female" }), "female");
  assert.equal(pedestrianGender({ pedType: 5, modelGender: "male", gender: "male" }), "male");
  assert.equal(pedestrianGender({ pedType: 7, modelGender: "female" }), "female");
  assert.equal(pedestrianGender({ pedType: 4, modelGender: "unknown" }), "male");
  assert.throws(() => pedestrianGender({ pedType: 7, gender: "female" }), /gender conflicts/u);
  for (let pedType = 0; pedType <= 31; pedType += 1) {
    if (pedType !== 4 && pedType !== 5) assert.equal(pedestrianGender({ pedType, modelId: 7 }), "unknown");
  }
  for (const modelId of [0, 7, 100, 65535]) assert.equal(generate(257, { modelId }).gender, "unknown");
});

test("explicit gender cannot contradict reliable game types and omission still derives gender", () => {
  for (const [pedType, gender, contradictory] of [[4, "male", "female"], [5, "female", "male"]]) {
    const consistent = { modelId: 65535, pedType, gender };
    assert.deepEqual(validatePedestrianMetadata(consistent), consistent);
    assert.equal(generate(257, consistent).gender, gender);
    assert.equal(generate(257, { modelId: 0, pedType }).gender, gender);
    assert.equal(generate(257, { pedType, gender: "unknown" }).gender, gender);
    const inconsistent = { ...consistent, gender: contradictory };
    assert.throws(() => validatePedestrianMetadata(inconsistent), /gender conflicts/u);
    assert.throws(() => pedestrianGender(inconsistent), /gender conflicts/u);
    assert.throws(() => generate(257, inconsistent), /gender conflicts/u);
    assert.throws(() => validateBridgeSelection({ gameSessionId: scope.gameSessionId,
      sceneGeneration: scope.sceneGeneration, pedestrian: inconsistent }), /gender conflicts/u);
  }
  assert.equal(pedestrianGender(undefined), "unknown");
});

test("exact pedestrian scope has deterministic original identity with no default-profile mutation", () => {
  const first = generate(257);
  assert.deepEqual(first, generate(257));
  assert.equal(first.id, pedestrianProfileId(scope));
  assert.notEqual(first.id, base.id);
  assert.notEqual(first.displayName, "Manu");
  assert.ok(Object.isFrozen(first));
  assert.ok(Object.isFrozen(first.traits));
  assert.equal(first.voiceRef, base.voiceRef);
  assert.equal(createPedestrianProfile(base, scope, undefined), base);
  assert.equal(base.displayName, "Manu");
  for (const otherScope of [{ ...scope, worldId: "another-world" }, { ...scope, gameSessionId: "gta-9999" }, { ...scope, sceneGeneration: 258 }]) {
    assert.notEqual(createPedestrianProfile(base, otherScope, {}).id, first.id);
  }
});

test("same-gender pedestrians receive distinct names and independently varied traits", () => {
  const profiles = Array.from({ length: 128 }, (_, index) => generate(index));
  assert.equal(new Set(profiles.map((profile) => profile.id)).size, profiles.length);
  assert.equal(new Set(profiles.map((profile) => profile.displayName)).size, profiles.length);
  assert.ok(new Set(profiles.map((profile) => profile.personality)).size > 100);
  for (const trait of ["disposition", "occupation", "hobby", "speakingStyle"]) {
    assert.ok(new Set(profiles.map((profile) => profile.traits[trait])).size > 1, trait);
  }
  for (const [sceneGeneration, male] of profiles.entries()) {
    for (const metadata of [{ modelId: 65535, pedType: 5 }, { modelGender: "female", gender: "female" }, { gender: "unknown" }, {}]) {
      const other = generate(sceneGeneration, metadata);
      assert.deepEqual(other.traits, male.traits);
      assert.equal(other.id, male.id);
      assert.equal(other.displayName, male.displayName);
      assert.equal(other.personality, male.personality);
      assert.equal(other.originalBackstory, male.originalBackstory);
    }
  }
  const occupations = new Map();
  for (const profile of profiles) {
    const styles = occupations.get(profile.traits.occupation) ?? new Set();
    styles.add(profile.traits.speakingStyle);
    occupations.set(profile.traits.occupation, styles);
  }
  assert.ok([...occupations.values()].every((styles) => styles.size > 1));
});

test("residents have warm individual values and quirks without forcing canned biography", () => {
  const profiles = Array.from({ length: 40 }, (_, index) => createPedestrianProfile(base,
    { worldId: "personality-city", gameSessionId: "gta-personas", sceneGeneration: index * 256 + 1 }, { pedType: 4 }));
  assert.ok(new Set(profiles.map((profile) => profile.traits.value)).size > 1);
  assert.ok(new Set(profiles.map((profile) => profile.traits.quirk)).size > 1);
  assert.ok(profiles.some(({ traits }) => /playful banter|gentle teasing|cheeky aside/iu.test(traits.speakingStyle)));
  assert.ok(profiles.some(({ traits }) => /roast|teasing/iu.test(traits.quirk)));
  for (const profile of profiles) {
    assert.ok(profile.personality.length <= 500 && profile.originalBackstory.length <= 1000);
    assert.match(profile.originalBackstory, /not topics they bring into every answer/u);
    assert.doesNotMatch(profile.originalBackstory, /original fictional|invented details|canon game character/u);
    assert.doesNotMatch(profile.traits.speakingStyle, /short, direct phrases/u);
  }
});

test("generated voice references select approved gender overrides without changing identity or traits", () => {
  const voiceConfig = { elevenLabsMaleVoiceId: "test-approved-male", elevenLabsFemaleVoiceId: "test-approved-female" };
  for (const [pedType, expected] of [[4, "configured-male"], [5, "configured-female"], [7, "configured-male"]]) {
    const metadata = { modelId: 7, pedType };
    const original = createPedestrianProfile(base, scope, metadata);
    const voiced = createPedestrianProfile(base, scope, metadata, voiceConfig);
    assert.equal(voiced.voiceRef, expected);
    assert.equal(voiced.id, original.id);
    assert.equal(voiced.displayName, original.displayName);
    assert.equal(voiced.personality, original.personality);
    assert.deepEqual(voiced.traits, original.traits);
    assert.equal(createPedestrianProfile(base, scope, metadata, {}).voiceRef, base.voiceRef);
    assert.doesNotMatch(JSON.stringify(voiced), /test-approved/u);
  }
  assert.equal(createPedestrianProfile(base, scope, { pedType: 5 }, {
    elevenLabsMaleVoiceId: "test-approved-male",
  }).voiceRef, base.voiceRef);
  assert.equal(createPedestrianProfile(base, scope, undefined, voiceConfig), base);
});

test("named pools deterministically rotate adjacent GTA slots and retain matching name/voice pairs", () => {
  const voices = { elevenLabsMaleVoices: [{ name: "Miyan", voiceId: "test-miyan" }, { name: "Adith", voiceId: "test-adith" },
    { name: "DT", voiceId: "test-dt-voice" }, { name: "Damu", voiceId: "test-damu" }],
    elevenLabsFemaleVoices: [{ name: "Manju", voiceId: "test-manju" }] };
  for (const generations of [[257, 513, 769, 1025], [1, 2, 3, 4], [2 ** 32 + 257, 2 ** 32 + 513, 2 ** 32 + 769, 2 ** 32 + 1025]]) {
    const profiles = generations.map((sceneGeneration) => createPedestrianProfile(base, { ...scope, sceneGeneration }, { modelGender: "male", pedType: 4 }, voices));
    assert.equal(new Set(profiles.map(({ voiceRef }) => voiceRef)).size, 4);
    for (const [index, profile] of profiles.entries()) {
      const poolIndex = Number(profile.voiceRef.split("-").at(-1));
      assert.equal(profile.displayName, `${voices.elevenLabsMaleVoices[poolIndex].name} (${generations[index].toString(36).toUpperCase()})`);
      assert.deepEqual(profile, createPedestrianProfile(base, { ...scope, sceneGeneration: generations[index] }, { modelGender: "male", pedType: 4 }, voices));
      assert.doesNotMatch(JSON.stringify(profile), /test-miyan|test-adith|test-dt-voice|test-damu/u);
    }
  }
  for (const epoch of [0, 1, 37, 32768]) {
    const profiles = Array.from({ length: 16 }, (_, index) => createPedestrianProfile(base,
      { ...scope, sceneGeneration: epoch * 2 ** 17 + (index + 1) * 256 + 1 },
      { modelGender: "male", pedType: 4 }, voices));
    const counts = new Map();
    for (const profile of profiles) counts.set(profile.voiceRef, (counts.get(profile.voiceRef) ?? 0) + 1);
    assert.equal(counts.size, 4);
    assert.deepEqual([...counts.values()], [4, 4, 4, 4]);
    for (let index = profiles.length - 1; index >= 0; index -= 1) {
      const repeated = createPedestrianProfile(base,
        { ...scope, sceneGeneration: epoch * 2 ** 17 + (index + 1) * 256 + 1 },
        { modelGender: "male", pedType: 4 }, voices);
      assert.equal(repeated.voiceRef, `configured-male-${(index + 2 + epoch) % 4}`);
      assert.deepEqual(repeated, profiles[index]);
    }
  }
  const female = createPedestrianProfile(base, scope, { modelGender: "female", pedType: 4, gender: "female" }, voices);
  assert.equal(female.voiceRef, "configured-female-0");
  assert.match(female.displayName, /^Manju \(/u);
  const neutral = createPedestrianProfile(base, scope, { modelGender: "unknown", pedType: 7 }, voices);
  assert.equal(neutral.gender, "unknown");
  assert.match(neutral.voiceRef, /^configured-male-[0-3]$/u);
  assert.doesNotMatch(neutral.displayName, /Miyan|Adith|DT|Damu|Manju/u);
  assert.deepEqual(neutral.traits, female.traits);
  assert.equal(createPedestrianProfile(base, scope, undefined, voices), base);
});

test("registered pedestrian profiles stay stable on repeated observations", () => {
  const registry = new NpcRegistry([base], {});
  const first = registry.registerProfile(generate(257));
  assert.equal(registry.registerProfile(generate(257, { pedType: 5 })), first);
  assert.equal(registry.getProfile(first.id), first);
  assert.equal(registry.size, 2);
  assert.equal(registry.getProfile(base.id).displayName, "Manu");
});
