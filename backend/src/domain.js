export class ValidationError extends Error {
  constructor(message) {
    super(message);
    this.name = "ValidationError";
  }
}

export class StaleMemoryGenerationError extends Error {
  constructor() {
    super("This turn belongs to memory that has been reset. Start a new turn.");
    this.name = "StaleMemoryGenerationError";
  }
}

export function validateProfile(profile) {
  if (!profile || typeof profile !== "object" || Array.isArray(profile)) {
    throw new ValidationError("NPC profile must be a JSON object.");
  }

  const requiredText = ["id", "displayName", "personality", "originalBackstory", "voiceRef"];
  for (const field of requiredText) {
    if (typeof profile[field] !== "string" || profile[field].trim().length === 0) {
      throw new ValidationError(`NPC profile field '${field}' must be a non-empty string.`);
    }
  }
  if (!/^[a-z0-9][a-z0-9-]{1,63}$/.test(profile.id)) {
    throw new ValidationError("NPC profile id must be 2–64 lowercase letters, digits, or hyphens.");
  }
  if (profile.displayName.length > 80 || profile.personality.length > 500 || profile.originalBackstory.length > 1000) {
    throw new ValidationError("NPC profile text exceeds its configured size limit.");
  }
  if (profile.language !== "ml") {
    throw new ValidationError("V1 NPC profiles must use Malayalam (language: 'ml').");
  }
  if (profile.memoryPolicy !== "private") {
    throw new ValidationError("V1 only supports private per-player NPC memory.");
  }
  return Object.freeze({ ...profile });
}

export function validateConversationKey(key) {
  if (!key || typeof key !== "object") {
    throw new ValidationError("Conversation key is required.");
  }
  for (const field of ["worldId", "playerId", "npcId"]) {
    if (typeof key[field] !== "string" || key[field].trim().length === 0 || key[field].length > 128) {
      throw new ValidationError(`Conversation key field '${field}' must contain 1–128 characters.`);
    }
  }
  return { worldId: key.worldId, playerId: key.playerId, npcId: key.npcId };
}

export function validateTurnContext(context) {
  const key = validateConversationKey(context);
  if (typeof context.turnId !== "string" || context.turnId.length === 0 || context.turnId.length > 128) {
    throw new ValidationError("Turn context needs a valid turnId.");
  }
  if (typeof context.gameSessionId !== "string" || context.gameSessionId.length === 0) {
    throw new ValidationError("Turn context needs a gameSessionId.");
  }
  for (const field of ["sceneGeneration", "memoryGeneration"]) {
    if (!Number.isSafeInteger(context[field]) || context[field] < 0) {
      throw new ValidationError(`Turn context field '${field}' must be a non-negative integer.`);
    }
  }
  return { ...key, turnId: context.turnId, gameSessionId: context.gameSessionId,
    sceneGeneration: context.sceneGeneration, memoryGeneration: context.memoryGeneration };
}

export function validateTranscript(value) {
  if (!value || typeof value !== "object" || typeof value.text !== "string") {
    throw new ValidationError("Speech recognition returned an invalid transcript.");
  }
  const text = value.text.trim();
  if (text.length === 0) throw new ValidationError("No speech was recognized. Try again.");
  if (text.length > 1000) throw new ValidationError("The recognized transcript is longer than 1,000 characters.");
  return { text, language: "ml" };
}

export function validateDialogueReply(value) {
  if (!value || typeof value !== "object" || typeof value.replyMl !== "string") {
    throw new ValidationError("Dialogue provider returned an invalid reply.");
  }
  const replyMl = value.replyMl.trim();
  if (replyMl.length === 0 || replyMl.length > 400) {
    throw new ValidationError("Malayalam reply must contain 1–400 characters.");
  }
  if (!/[\u0D00-\u0D7F]/u.test(replyMl)) {
    throw new ValidationError("Dialogue reply must include Malayalam script.");
  }
  return { replyMl };
}

export function assertNotAborted(signal) {
  if (signal.aborted) {
    throw signal.reason instanceof Error ? signal.reason : new DOMException("Turn cancelled", "AbortError");
  }
}

export function isAbortError(error) {
  return error?.name === "AbortError" || error?.name === "StaleMemoryGenerationError";
}
