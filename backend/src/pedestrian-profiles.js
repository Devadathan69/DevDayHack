import { createHash } from "node:crypto";
import { ValidationError, validateGameScope, validateProfile } from "./domain.js";
import { selectNpcVoice } from "./configured-voices.js";

const givenNames = ["Aven", "Tavi", "Neri", "Lumi", "Ren", "Sora", "Kavi", "Arin", "Devi", "Mika", "Remi", "Ira", "Ellis", "Robin", "Sage", "Rumi"];
const familyNames = ["Vale", "Reed", "Brook", "Moss", "Stone", "Lane", "Grove", "Lake", "Fern", "Cove", "Wren", "Ash", "Hale", "Lark", "Pine", "Bell"];
const dispositions = ["patient and observant", "curious and energetic", "reserved but thoughtful", "playful and candid", "practical and reassuring", "reflective and optimistic", "precise and inquisitive", "easygoing and quietly witty"];
const occupations = ["bicycle repairer", "bookshop assistant", "radio technician", "community gardener", "map illustrator", "bakery assistant", "ceramics maker", "library archivist"];
const hobbies = ["sketching street signs", "collecting unusual postcards", "solving word puzzles", "growing herbs", "building small model boats", "watching the night sky", "making paper sculptures", "recording everyday city sounds"];
const speakingStyles = ["short, direct phrases", "gentle questions and measured pauses", "warm observations with light wordplay", "careful explanations without jargon", "lively but concise comparisons", "understated humor and plain language", "thoughtful questions before offering advice", "calm, conversational storytelling"];

function derivedGender(metadata) {
  // Installed model metadata outranks live CPed classifications, which can mislabel female models.
  if (metadata.modelGender === "male" || metadata.modelGender === "female") return metadata.modelGender;
  return metadata.pedType === 4 ? "male" : metadata.pedType === 5 ? "female" : "unknown";
}

export function validatePedestrianMetadata(value) {
  if (value === undefined) return null;
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    throw new ValidationError("Pedestrian metadata must be a JSON object.");
  }
  if (Object.keys(value).some((field) => !["modelId", "pedType", "modelGender", "gender"].includes(field))) {
    throw new ValidationError("Pedestrian metadata contains an unsupported field.");
  }
  for (const [field, maximum] of [["modelId", 65535], ["pedType", 31]]) {
    if (value[field] !== undefined && (!Number.isInteger(value[field]) || value[field] < 0 || value[field] > maximum)) {
      throw new ValidationError(`Pedestrian '${field}' must be an integer from 0 to ${maximum}.`);
    }
  }
  for (const field of ["gender", "modelGender"]) {
    if (value[field] !== undefined && !["male", "female", "unknown"].includes(value[field])) {
      throw new ValidationError(`Pedestrian ${field} must be male, female, or unknown game metadata.`);
    }
  }
  if (value.gender !== undefined && value.gender !== "unknown" && value.gender !== derivedGender(value)) {
    throw new ValidationError("Pedestrian gender conflicts with the derived model/CPed gender metadata.");
  }
  return Object.freeze({ ...value });
}

export function validateBridgeSelection(value) {
  if (!value || typeof value !== "object" || Array.isArray(value)) {
    throw new ValidationError("NPC selection must be a JSON object.");
  }
  if (Object.keys(value).some((field) => !["gameSessionId", "sceneGeneration", "pedestrian"].includes(field))) {
    throw new ValidationError("NPC selection contains an unsupported field.");
  }
  return { ...validateGameScope(value), pedestrian: validatePedestrianMetadata(value.pedestrian) };
}

export function pedestrianGender(metadata) {
  const safe = validatePedestrianMetadata(metadata);
  return safe ? derivedGender(safe) : "unknown";
}

function scopeDigest({ worldId, gameSessionId, sceneGeneration }) {
  const safeScope = validateGameScope({ gameSessionId, sceneGeneration });
  return createHash("sha256").update(JSON.stringify([worldId, safeScope.gameSessionId, safeScope.sceneGeneration])).digest();
}

export function pedestrianProfileId(scope) {
  return `ped-${scopeDigest(scope).toString("hex").slice(0, 56)}`;
}

export function createPedestrianProfile(baseProfile, scope, metadata, voiceConfig = {}) {
  const pedestrian = validatePedestrianMetadata(metadata);
  if (!pedestrian) return baseProfile;
  const digest = scopeDigest(scope);
  const { sceneGeneration } = scope;
  const choose = (values, offset) => values[digest.readUInt32BE(offset) % values.length];
  // Scope alone selects traits: changing game gender/model metadata cannot change personality.
  const traits = Object.freeze({ disposition: choose(dispositions, 8), occupation: choose(occupations, 12),
    hobby: choose(hobbies, 16), speakingStyle: choose(speakingStyles, 20) });
  // The compact pool-scope tag guarantees distinct display names within one game session.
  const gender = pedestrianGender(pedestrian);
  const selectedVoice = selectNpcVoice(gender, sceneGeneration, voiceConfig);
  const name = selectedVoice?.name ?? `${choose(givenNames, 0)} ${choose(familyNames, 4)}`;
  const displayName = `${name} (${sceneGeneration.toString(36).toUpperCase()})`;
  const voiceRef = selectedVoice?.voiceRef ?? baseProfile.voiceRef;
  return validateProfile({
    id: `ped-${digest.toString("hex").slice(0, 56)}`,
    displayName,
    gender,
    pedestrian,
    traits,
    personality: `${traits.disposition}; speaks in ${traits.speakingStyle}. Works as a ${traits.occupation} and enjoys ${traits.hobby}.`,
    originalBackstory: `An original fictional city resident who works as a ${traits.occupation}. Outside work, ${displayName} enjoys ${traits.hobby}. These invented details are not claims about a real person or a canon game character.`,
    voiceRef,
    language: baseProfile.language,
    memoryPolicy: baseProfile.memoryPolicy,
  });
}
