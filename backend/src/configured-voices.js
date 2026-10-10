export function selectNpcVoice(gender, sceneGeneration, config) {
  const group = gender === "female" ? "female" : "male";
  const pool = group === "female" ? config.elevenLabsFemaleVoices : config.elevenLabsMaleVoices;
  if (pool?.length) {
    // Native scope packs (epoch << 17) | (slotIndex * 256 + slotFlags).
    // Use the slot, not scope % pool.length: common flags would otherwise select one voice.
    const handle = sceneGeneration % 2 ** 17;
    const slot = Math.floor(handle / 256);
    const flags = handle % 256;
    const epoch = Math.floor(sceneGeneration / 2 ** 17);
    const index = (slot + flags + epoch) % pool.length;
    return { voiceRef: `configured-${group}-${index}`, name: gender === "unknown" ? null : pool[index].name };
  }
  const singleVoice = group === "female" ? config.elevenLabsFemaleVoiceId : config.elevenLabsMaleVoiceId;
  return singleVoice ? { voiceRef: `configured-${group}`, name: null } : null;
}

export function resolveConfiguredVoice(voiceRef, config) {
  const pooled = /^configured-(male|female)-(0|[1-9][0-9]?)$/u.exec(voiceRef ?? "");
  if (pooled) {
    const pool = pooled[1] === "male" ? config.elevenLabsMaleVoices : config.elevenLabsFemaleVoices;
    return pool?.[Number(pooled[2])]?.voiceId ?? config.elevenLabsVoiceId;
  }
  if (voiceRef === "configured-male") return config.elevenLabsMaleVoiceId ?? config.elevenLabsMaleVoices?.[0]?.voiceId ?? config.elevenLabsVoiceId;
  if (voiceRef === "configured-female") return config.elevenLabsFemaleVoiceId ?? config.elevenLabsFemaleVoices?.[0]?.voiceId ?? config.elevenLabsVoiceId;
  return config.elevenLabsVoiceId;
}
