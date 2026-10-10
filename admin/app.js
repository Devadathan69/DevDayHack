const STORAGE_KEY = "narrative-lab:demo-npc-001";
const defaults = {
  id: "demo-npc-001", displayName: "Manu",
  personality: "Friendly, curious, and mildly sarcastic. Keep the humor warm.",
  originalBackstory: "Manu likes chatting with people he meets around town and is proud of his snack recommendations.",
  language: "ml", voiceRef: "mock-malayalam", memoryPolicy: "private",
  traits: { warmth: 78, curiosity: 66, humor: 54, confidence: 62 },
  dialogue: { replyLength: "balanced", language: "ml", creativity: 42, focus: 72, boundary: "Keep it friendly. Never claim to control the game or know things outside this conversation." },
  behavior: { greeting: "familiar", ambient: "occasional", memoryEnabled: true, sceneAware: true, teasing: true }
};
let toastTimer;
let previewIndex = 0;
const replies = [
  { ml: "നാട്ടിൽ എല്ലാം പതിവുപോലെ തന്നെ! പക്ഷേ നല്ലൊരു പലഹാരം എവിടെയെന്ന് ചോദിച്ചാൽ, അതിൽ എനിക്ക് ചില നിർദേശങ്ങളുണ്ട്.", en: "“Around here, all’s as usual! But if you’re asking where to find a good snack, I have a few suggestions.”" },
  { ml: "ഇവിടെ പുതിയൊന്നുമില്ലെന്ന് പറയാനായിരുന്നു... പിന്നെ ഓർത്തു, ചായക്കടയിലെ പഴംപൊരി ഇന്ന് നേരത്തേ തീർന്നു!", en: "“I was going to say nothing new... then I remembered the banana fritters sold out early today!”" },
  { ml: "വിശേഷം ഉണ്ടാക്കണോ? ആദ്യം ഒരു ചായ, പിന്നെ നല്ലൊരു കഥ. രണ്ടും ഇവിടെ അടുത്തുതന്നെയുണ്ട്.", en: "“Want some excitement? First tea, then a good story. Both are just around the corner.”" }
];
const $ = (id) => document.getElementById(id);

function readForm() {
  return {
    ...defaults,
    displayName: $("nameInput").value.trim() || defaults.displayName,
    originalBackstory: $("backstoryInput").value.trim(),
    traits: Object.fromEntries(["warmth", "curiosity", "humor", "confidence"].map((key) => [key, Number($(key).value)])),
    dialogue: {
      replyLength: $("replyLength").value, language: $("language").value,
      creativity: Number($("creativity").value), focus: Number($("focus").value),
      boundary: $("boundary").value.trim()
    },
    behavior: {
      greeting: $("greeting").value, ambient: $("ambient").value,
      memoryEnabled: $("memoryEnabled").checked, sceneAware: $("sceneAware").checked,
      teasing: $("teasing").checked
    }
  };
}

function setRange(range) {
  const value = Number(range.value);
  range.style.setProperty("--fill", `${value}%`);
  $(`${range.id}Value`).value = value;
  $(`${range.id}Value`).textContent = value;
}

function snapshot(profile) {
  const { warmth, humor, curiosity } = profile.traits;
  const lead = warmth >= 65 ? "Warm" : warmth <= 35 ? "A little reserved" : "Easygoing";
  const edge = humor >= 65 ? "playful" : humor <= 30 ? "thoughtful" : "witty";
  const description = curiosity >= 65
    ? "A welcoming conversationalist who likes to ask questions and keep things light."
    : "A " + (warmth >= 60 ? "welcoming" : "measured") + " conversationalist who listens before jumping in.";
  $("snapshotTitle").textContent = `${lead} with a ${edge} edge`;
  $("snapshotDescription").textContent = description;
}

function render() {
  const profile = readForm();
  $("profileName").textContent = profile.displayName;
  $("previewName").textContent = profile.displayName;
  $("previewName2").textContent = profile.displayName;
  $("messageNpcName").textContent = profile.displayName.toLocaleUpperCase();
  $("profileDescription").textContent = profile.originalBackstory;
  $("responseStyle").textContent = profile.dialogue.replyLength[0].toLocaleUpperCase() + profile.dialogue.replyLength.slice(1);
  $("translation").hidden = profile.dialogue.language === "ml";
  $("completeness").textContent = `${Math.min(100, 70 + Math.round((profile.originalBackstory.length > 50) * 8) + Math.round(profile.dialogue.boundary.length > 25 ? 6 : 0))}%`;
  $("completenessBar").style.width = $("completeness").textContent;
  document.querySelectorAll(".trait-range").forEach(setRange);
  snapshot(profile);
}

function persist() {
  const profile = readForm();
  try {
    localStorage.setItem(STORAGE_KEY, JSON.stringify(profile));
    $("saveStatus").textContent = "Saved in this browser";
  } catch {
    $("saveStatus").textContent = "Could not save draft";
  }
  render();
}

function hydrate(profile) {
  $("nameInput").value = profile.displayName ?? defaults.displayName;
  $("backstoryInput").value = profile.originalBackstory ?? defaults.originalBackstory;
  for (const key of Object.keys(defaults.traits)) $(key).value = profile.traits?.[key] ?? defaults.traits[key];
  for (const key of ["replyLength", "language", "creativity", "focus", "boundary"]) {
    const value = profile.dialogue?.[key] ?? defaults.dialogue[key];
    if ($(key)) $(key).value = value;
  }
  for (const key of Object.keys(defaults.behavior)) {
    if ($(key).type === "checkbox") $(key).checked = profile.behavior?.[key] ?? defaults.behavior[key];
    else $(key).value = profile.behavior?.[key] ?? defaults.behavior[key];
  }
  render();
}

function showToast(message) {
  const toast = $("toast");
  toast.textContent = message;
  toast.classList.add("show");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => toast.classList.remove("show"), 2200);
}

function downloadProfile() {
  const profile = readForm();
  const exportProfile = {
    id: profile.id, displayName: profile.displayName,
    personality: `Friendly, ${profile.traits.curiosity >= 60 ? "curious" : "observant"}, and ${profile.traits.humor >= 60 ? "playful" : "mildly sarcastic"}. Keep the humor warm.`,
    originalBackstory: profile.originalBackstory, language: "ml", voiceRef: "mock-malayalam", memoryPolicy: "private",
    traits: profile.traits, dialogue: profile.dialogue, behavior: profile.behavior
  };
  const blob = new Blob([`${JSON.stringify(exportProfile, null, 2)}\n`], { type: "application/json" });
  const url = URL.createObjectURL(blob);
  const link = document.createElement("a");
  link.href = url;
  link.download = `${profile.id}.json`;
  link.click();
  URL.revokeObjectURL(url);
  showToast("Profile exported as JSON");
}

document.querySelectorAll("input, textarea, select").forEach((control) => {
  control.addEventListener("input", persist);
  control.addEventListener("change", persist);
});
$("exportButton").addEventListener("click", downloadProfile);
$("resetButton").addEventListener("click", () => {
  hydrate(defaults);
  localStorage.removeItem(STORAGE_KEY);
  $("saveStatus").textContent = "Default profile restored";
  showToast("Restored the default profile");
});
$("refreshPreview").addEventListener("click", () => {
  previewIndex = (previewIndex + 1) % replies.length;
  $("previewReply").textContent = replies[previewIndex].ml;
  $("translation").textContent = replies[previewIndex].en;
});

try {
  const saved = localStorage.getItem(STORAGE_KEY);
  hydrate(saved ? { ...defaults, ...JSON.parse(saved) } : defaults);
} catch {
  hydrate(defaults);
  $("saveStatus").textContent = "Local storage unavailable";
}
