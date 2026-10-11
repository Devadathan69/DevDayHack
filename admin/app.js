const STORAGE_KEY = "narrative-lab:profiles:v2";
const LEGACY_KEY = "narrative-lab:demo-npc-001";
const SERVER_API = "/api/admin/profiles";
const defaults = {
  id: "demo-npc-001", displayName: "Manu",
  personality: "Friendly, curious, and mildly sarcastic. Keep the humor warm.",
  originalBackstory: "Manu likes chatting with people he meets around town and is proud of his snack recommendations.",
  language: "ml", voiceRef: "manu-default", memoryPolicy: "private",
  traits: { warmth: 78, curiosity: 66, humor: 54, confidence: 62 },
  dialogue: { replyLength: "balanced", language: "ml", creativity: 42, focus: 72, boundary: "Keep it friendly. Never claim to control the game or know things outside this conversation." },
  behavior: { greeting: "familiar", ambient: "occasional", memoryEnabled: true, sceneAware: true, teasing: true },
  voice: { preset: "server-default", mood: "natural", speed: 100 }
};
const presets = {
  welcoming: { label: "Welcoming local", traits: { warmth: 88, curiosity: 62, humor: 48, confidence: 68 }, behavior: { greeting: "familiar", ambient: "occasional", memoryEnabled: true, sceneAware: true, teasing: true } },
  curious: { label: "Curious stranger", traits: { warmth: 58, curiosity: 90, humor: 45, confidence: 52 }, behavior: { greeting: "curious", ambient: "occasional", memoryEnabled: true, sceneAware: true, teasing: false } },
  reserved: { label: "Reserved observer", traits: { warmth: 38, curiosity: 45, humor: 25, confidence: 58 }, behavior: { greeting: "reserved", ambient: "rare", memoryEnabled: true, sceneAware: false, teasing: false } }
};
const scenarios = {
  greeting: { prompt: "ഇന്ന് ഇവിടെ എന്താ വിശേഷം?", reply: (p) => p.traits.warmth > 65 ? "നാട്ടിൽ എല്ലാം പതിവുപോലെ തന്നെ! പക്ഷേ നിങ്ങളെ കണ്ടതോടെ ദിവസം കുറച്ച് കൂടി നന്നായി." : "ഇവിടെ കാര്യങ്ങൾ പതിവുപോലെ. നിങ്ങൾക്ക് എന്താണ് അറിയേണ്ടത്?", translation: "“All’s as usual around here! Though seeing you made the day a little better.”" },
  snack: { prompt: "നല്ലൊരു പലഹാരം എവിടെ കിട്ടും?", reply: (p) => p.traits.humor > 60 ? "അത് ചോദിക്കാൻ ശരിയായ ആളെ തന്നെയാണ് കണ്ടത്! ചായക്കടയിലെ പഴംപൊരി നോക്കൂ — ഒന്ന് കഴിച്ചാൽ രണ്ടാമത്തേത് ഉറപ്പ്." : "ചായക്കടയിൽ ചോദിച്ചു നോക്കൂ. അവിടത്തെ പഴംപൊരി നല്ലതാണ്, എനിക്ക് തോന്നുന്നു.", translation: "“You’ve asked the right person! Try the banana fritters at the tea shop — one usually turns into two.”" },
  stranger: { prompt: "ഹായ്, ഒന്ന് സംസാരിക്കാമോ?", reply: (p) => p.behavior.greeting === "reserved" ? "ഹായ്. പറയൂ, ഞാൻ കേൾക്കുന്നുണ്ട്." : "തീർച്ചയായും! ഒരു നല്ല സംഭാഷണത്തിന് എപ്പോഴും സമയമുണ്ട്.", translation: "“Hi. Sure, I’m listening.”" }
};
const $ = (id) => document.getElementById(id);
let profiles = [];
let selectedId = defaults.id;
let toastTimer;
let storageAvailable = true;
let apiConnected = false;
let apiSaveTimer = null;
let apiSavePending = false;
let apiSaveRevision = 0;
let apiWriteQueue = Promise.resolve();
let apiConnectionError = "";
let companionReady = false;
let gameProfileId = defaults.id;
let voiceOptions = [{ voiceRef: defaults.voiceRef, label: "Server default voice" }];

function renderWorld(snapshot, disconnected = false) {
  const view = globalThis.NpcWorldContextView.format(snapshot);
  $("worldContextState").textContent = disconnected ? "Backend disconnected" : view.state;
  $("worldContextState").className = disconnected ? "is-stale" : view.stateClass;
  $("worldContextLocation").textContent = view.location;
  $("worldContextTime").textContent = view.time;
  $("worldContextWeather").textContent = view.weather;
  $("worldContextNote").textContent = disconnected ? "Start the local server; no current world facts are available." : view.note;
}

let worldRequestPending = false;
async function refreshWorld() {
  if (location.protocol === "file:" || worldRequestPending) return;
  worldRequestPending = true;
  const controller = new AbortController();
  const timeout = setTimeout(() => controller.abort(), 2500);
  try {
    const response = await fetch("/api/status", { cache: "no-store", signal: controller.signal });
    if (!response.ok) throw new Error("Status unavailable");
    const status = await response.json();
    renderWorld(status.worldContext);
  } catch {
    renderWorld(null, true);
  } finally {
    clearTimeout(timeout);
    worldRequestPending = false;
  }
}

function deepCopy(value) { return JSON.parse(JSON.stringify(value)); }
function newProfile(data = defaults, versions = [], activeVersion = null) { return { data: deepCopy(data), versions: deepCopy(versions), activeVersion }; }
function current() { return profiles.find((item) => item.data.id === selectedId) ?? profiles[0]; }
function slug(value) { return value.toLocaleLowerCase().normalize("NFKD").replace(/[^a-z0-9]+/g, "-").replace(/^-|-$/g, "").slice(0, 45) || "npc"; }
function uniqueId(base) { const id = slug(base); let candidate = id; let number = 2; while (profiles.some((item) => item.data.id === candidate)) candidate = `${id}-${number++}`; return candidate; }
function profileDataOnly(data) { return { id: data.id, displayName: data.displayName, personality: data.personality, originalBackstory: data.originalBackstory, language: data.language, voiceRef: data.voiceRef, memoryPolicy: data.memoryPolicy, traits: deepCopy(data.traits), dialogue: deepCopy(data.dialogue), behavior: deepCopy(data.behavior), voice: deepCopy(data.voice ?? defaults.voice) }; }

function readForm() {
  const base = current()?.data ?? defaults;
  const traits = Object.fromEntries(["warmth", "curiosity", "humor", "confidence"].map((key) => [key, Number($(key).value)]));
  const personality = `Friendly, ${traits.curiosity >= 60 ? "curious" : "observant"}, and ${traits.humor >= 60 ? "playful" : "mildly sarcastic"}. Keep the humor warm.`;
  return {
    ...profileDataOnly(base), displayName: $("nameInput").value.trim(), personality,
    originalBackstory: $("backstoryInput").value.trim(), traits,
    dialogue: { replyLength: $("replyLength").value, language: $("language").value, creativity: Number($("creativity").value), focus: Number($("focus").value), boundary: $("boundary").value.trim() },
    behavior: { greeting: $("greeting").value, ambient: $("ambient").value, memoryEnabled: $("memoryEnabled").checked, sceneAware: $("sceneAware").checked, teasing: $("teasing").checked },
    voice: { preset: $("voicePreset").value, mood: $("voiceMood").value, speed: Number($("voiceSpeed").value) },
    voiceRef: $("voicePreset").value
  };
}

function setRange(range) {
  const value = Number(range.value);
  const min = Number(range.min || 0); const max = Number(range.max || 100);
  range.style.setProperty("--fill", `${((value - min) / (max - min)) * 100}%`);
  const output = $(`${range.id}Value`);
  if (output) { output.value = value; output.textContent = range.id === "voiceSpeed" ? `${value}%` : value; }
}

function snapshot(profile) {
  const { warmth, humor, curiosity } = profile.traits;
  const lead = warmth >= 65 ? "Warm" : warmth <= 35 ? "A little reserved" : "Easygoing";
  const edge = humor >= 65 ? "playful" : humor <= 30 ? "thoughtful" : "witty";
  $("snapshotTitle").textContent = `${lead} with a ${edge} edge`;
  $("snapshotDescription").textContent = curiosity >= 65 ? "A welcoming conversationalist who likes to ask questions and keep things light." : "A " + (warmth >= 60 ? "welcoming" : "measured") + " conversationalist who listens before jumping in.";
}

function renderRoster() {
  const select = $("npcSelect");
  select.replaceChildren(...profiles.map(({ data }) => {
    const option = document.createElement("option"); option.value = data.id; option.textContent = `${data.displayName} · ${data.id}`; return option;
  }));
  select.value = selectedId;
  $("rosterCount").textContent = String(profiles.length).padStart(2, "0");
  const protectedGameProfile = current()?.data.id === defaults.id;
  $("deleteNpcButton").disabled = profiles.length < 2 || protectedGameProfile;
  $("deleteNpcButton").title = protectedGameProfile ? "The GTA bridge profile is protected." : "Delete this local profile";
}

function isPublished(profile) {
  const item = profiles.find((entry) => entry.data.id === profile.id);
  const active = item?.versions?.find((entry) => entry.number === item.activeVersion)?.profile;
  return Boolean(active && JSON.stringify(profileDataOnly(active)) === JSON.stringify(profileDataOnly(profile)));
}

function renderVersions() {
  const item = current(); const versions = item?.versions ?? [];
  $("versionCount").textContent = String(versions.length).padStart(2, "0");
  const list = $("versionList"); list.replaceChildren();
  if (!versions.length) {
    const empty = document.createElement("p"); empty.className = "empty-versions"; empty.textContent = "Publish this profile to create its first version."; list.append(empty); return;
  }
  versions.slice(0, 5).forEach((version) => {
    const row = document.createElement("div"); row.className = "version-row";
    const copy = document.createElement("div"); copy.className = "version-copy";
    const title = document.createElement("strong"); title.textContent = `Version ${version.number}${version.number === versions[0].number ? " · Latest" : ""}`;
    const date = document.createElement("small"); date.textContent = new Date(version.publishedAt).toLocaleString();
    copy.append(title, date);
    const restore = document.createElement("button"); restore.type = "button"; restore.textContent = "Restore"; restore.dataset.restoreVersion = String(version.number); row.append(copy, restore); list.append(row);
  });
}

function render() {
  if (!profiles.length) return;
  const profile = readForm(); const saved = current().data;
  $("profileName").textContent = profile.displayName || "Unnamed NPC";
  $("breadcrumbName").textContent = profile.displayName || "Unnamed NPC";
  $("previewName").textContent = profile.displayName || "Unnamed NPC";
  $("previewName2").textContent = profile.displayName || "Unnamed NPC";
  $("portraitInitial").textContent = (profile.displayName || "N").trim().charAt(0).toLocaleUpperCase();
  $("previewAvatarInitial").textContent = (profile.displayName || "N").trim().charAt(0).toLocaleUpperCase();
  $("messageNpcName").textContent = (profile.displayName || "NPC").toLocaleUpperCase();
  $("profileDescription").textContent = profile.originalBackstory || "Add a backstory to make this character feel real.";
  $("profileId").textContent = saved.id;
  $("responseStyle").textContent = profile.dialogue.replyLength[0].toLocaleUpperCase() + profile.dialogue.replyLength.slice(1);
  $("translation").hidden = false;
  const completeness = Math.min(100, 60 + Math.round(profile.originalBackstory.length > 50) * 12 + Math.round(profile.dialogue.boundary.length > 25) * 10 + Math.round(profile.displayName.length > 1) * 10);
  $("completeness").textContent = `${completeness}%`; $("completenessBar").style.width = `${completeness}%`;
  const published = isPublished(profile);
  $("statePill").innerHTML = `<i></i> ${published ? "PUBLISHED" : "DRAFT"}`;
  $("publishButton").disabled = published;
  const runtimeTag = $("runtimeTag");
  const activeVersion = current().activeVersion;
  const changedBuiltIn = profile.id === gameProfileId && !activeVersion && JSON.stringify(profileDataOnly(profile)) !== JSON.stringify(profileDataOnly(defaults));
  if (apiConnected && profile.id === gameProfileId) runtimeTag.textContent = activeVersion ? (published ? `GAME · V${activeVersion} ACTIVE` : `GAME · V${activeVersion} ACTIVE · DRAFT`) : changedBuiltIn ? "GAME PROFILE · DRAFT" : "GAME PROFILE · BUILT-IN";
  else if (apiConnected) runtimeTag.textContent = activeVersion && companionReady
    ? (published ? `COMPANION · V${activeVersion} ACTIVE` : `COMPANION · V${activeVersion} ACTIVE · DRAFT`) : "PUBLISH TO ACTIVATE";
  else runtimeTag.textContent = "LOCAL PROFILE";
  if (apiSavePending) runtimeTag.textContent = "SAVING · NOT YET APPLIED";
  else if (apiConnectionError) runtimeTag.textContent = "LOCAL DRAFT · SERVER SAVE UNCONFIRMED";
  $("saveStatus").textContent = apiConnected
    ? (apiSavePending ? "Saving to local server…" : published ? "Published on local server" : "Saved to local server")
    : apiConnectionError ? "Local draft · backend unavailable"
      : storageAvailable ? (published ? "Published snapshot · saved locally" : "Draft saved in this browser") : "Browser storage unavailable";
  $("voicePreviewButton").disabled = !apiConnected;
  $("voicePreviewButton").querySelector("span").textContent = apiConnected ? "Play sample from configured voice" : "Start the local server to preview speech";
  $("voicePreviewButton").closest(".voice-panel").querySelector(".tag").textContent = apiConnected ? "CONNECTED" : "SERVER VOICE";
  $("updatedAt").textContent = saved.updatedAt ? `◷ Edited ${new Date(saved.updatedAt).toLocaleString()}` : "◷ Draft saved locally";
  document.querySelectorAll(".trait-range").forEach(setRange);
  snapshot(profile); renderRoster(); renderVersions();
}

function persist() {
  const data = readForm();
  current().data = { ...data, updatedAt: new Date().toISOString() };
  saveAll(); render();
}

function saveAll() {
  try { localStorage.setItem(STORAGE_KEY, JSON.stringify({ profiles, selectedId })); storageAvailable = true; }
  catch { storageAvailable = false; }
  if (!apiConnected) return;
  clearTimeout(apiSaveTimer);
  apiSavePending = true;
  $("saveStatus").textContent = "Saving to local server…";
  apiSaveTimer = setTimeout(async () => {
    const revision = ++apiSaveRevision;
    const body = JSON.stringify({ profiles, selectedId });
    apiWriteQueue = apiWriteQueue.catch(() => {}).then(async () => {
      const response = await fetch(SERVER_API, { method: "PUT", headers: { "Content-Type": "application/json" }, body });
      const result = await response.json();
      if (!response.ok) throw new Error(result.error ?? `HTTP ${response.status}`);
      companionReady = result.companionReady === true; gameProfileId = result.gameProfileId ?? gameProfileId; apiConnectionError = "";
      if (revision === apiSaveRevision) { apiSavePending = false; render(); }
    }).catch((error) => {
      if (revision === apiSaveRevision) { apiSavePending = false; apiConnectionError = error.message; $("saveStatus").textContent = `Backend save failed: ${error.message}`; }
    });
  }, 300);
}

function hydrate(profile) {
  const normalized = { ...deepCopy(defaults), ...deepCopy(profile), traits: { ...defaults.traits, ...(profile.traits ?? {}) }, dialogue: { ...defaults.dialogue, ...(profile.dialogue ?? {}) }, behavior: { ...defaults.behavior, ...(profile.behavior ?? {}) }, voice: { ...defaults.voice, ...(profile.voice ?? {}) } };
  $("nameInput").value = normalized.displayName;
  $("backstoryInput").value = normalized.originalBackstory;
  $("idInput").value = normalized.id;
  for (const key of Object.keys(defaults.traits)) $(key).value = normalized.traits[key];
  for (const key of ["replyLength", "language", "creativity", "focus", "boundary"]) $(key).value = normalized.dialogue[key];
  for (const key of Object.keys(defaults.behavior)) $(key).type === "checkbox" ? $(key).checked = normalized.behavior[key] : $(key).value = normalized.behavior[key];
  if (![...$("voicePreset").options].some((option) => option.value === normalized.voiceRef)) {
    const option = document.createElement("option"); option.value = normalized.voiceRef; option.textContent = "Saved configured voice"; $("voicePreset").append(option);
  }
  $("voicePreset").value = normalized.voiceRef; $("voiceMood").value = normalized.voice.mood; $("voiceSpeed").value = normalized.voice.speed;
  render();
}

function showToast(message) {
  const toast = $("toast"); toast.textContent = message; toast.classList.add("show");
  clearTimeout(toastTimer); toastTimer = setTimeout(() => toast.classList.remove("show"), 2400);
}

async function previewVoice() {
  const button = $("voicePreviewButton");
  let audioUrl = null;
  button.disabled = true;
  button.querySelector("span").textContent = "Generating sample…";
  try {
    const response = await fetch("/api/admin/voice-preview", {
      method: "POST", headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ voiceRef: $("voicePreset").value, voice: { mood: $("voiceMood").value, speed: Number($("voiceSpeed").value) } })
    });
    if (!response.ok) {
      const result = await response.json().catch(() => ({}));
      throw new Error(result.error ?? `HTTP ${response.status}`);
    }
    audioUrl = URL.createObjectURL(await response.blob());
    const player = new Audio(audioUrl);
    player.addEventListener("ended", () => { URL.revokeObjectURL(audioUrl); audioUrl = null; }, { once: true });
    player.addEventListener("error", () => { URL.revokeObjectURL(audioUrl); audioUrl = null; }, { once: true });
    await player.play();
  } catch (error) {
    if (audioUrl) URL.revokeObjectURL(audioUrl);
    showToast(`Voice preview failed: ${error.message}`);
  } finally {
    button.disabled = !apiConnected;
    button.querySelector("span").textContent = apiConnected ? "Play sample from configured voice" : "Start the local server to preview speech";
  }
}

function downloadProfile(profile = readForm()) {
  const blob = new Blob([`${JSON.stringify(profileDataOnly(profile), null, 2)}\n`], { type: "application/json" });
  const url = URL.createObjectURL(blob); const link = document.createElement("a");
  link.href = url; link.download = `${profile.id}.json`; link.click(); URL.revokeObjectURL(url);
  showToast("Profile exported as JSON");
}

function validateProfile(profile) {
  if (!profile || typeof profile !== "object" || typeof profile.displayName !== "string" || !profile.displayName.trim() || profile.displayName.length > 80) throw new Error("Profile needs a name (1–80 characters).");
  if (typeof profile.originalBackstory !== "string" || !profile.originalBackstory.trim() || profile.originalBackstory.length > 1000) throw new Error("Backstory must contain 1–1,000 characters.");
  if (profile.traits && Object.values(profile.traits).some((value) => !Number.isFinite(value) || value < 0 || value > 100)) throw new Error("Trait values must be between 0 and 100.");
}

function publish() {
  const profile = readForm();
  try { validateProfile(profile); } catch (error) { showToast(error.message); return; }
  const item = current(); const number = (item.versions[0]?.number ?? 0) + 1;
  item.data = { ...profile, updatedAt: new Date().toISOString() };
  item.versions.unshift({ number, publishedAt: new Date().toISOString(), profile: profileDataOnly(profile) });
  item.activeVersion = number;
  item.versions = item.versions.slice(0, 20); saveAll(); render(); showToast(`Published local snapshot · v${number}`);
}

function makeProfile(name, source = defaults) {
  const profile = profileDataOnly(source); profile.id = uniqueId(name); profile.displayName = name; return newProfile(profile);
}

function scenarioReply() {
  const profile = readForm(); const selected = scenarios[$("scenarioSelect").value] ?? scenarios.greeting;
  $("scenarioPrompt").textContent = selected.prompt;
  const variant = selected.reply(profile);
  const languageMix = profile.dialogue.language === "ml-en" && profile.traits.curiosity > 60 ? " By the way, have you tried asking the tea shop owner?" : "";
  $("previewReply").textContent = `${variant}${languageMix}`;
  $("translation").textContent = selected.translation;
  $("translation").hidden = false;
}

function applyPreset(key) {
  if (key === "custom") { $("presetNote").textContent = "Your current settings are custom."; return; }
  const preset = presets[key]; if (!preset) return;
  for (const [trait, value] of Object.entries(preset.traits)) $(trait).value = value;
  for (const [setting, value] of Object.entries(preset.behavior)) $(setting).type === "checkbox" ? $(setting).checked = value : $(setting).value = value;
  $("presetNote").textContent = `Applied ${preset.label}. Tweak any setting to make it yours.`;
  persist(); showToast(`${preset.label} preset applied`);
}

function restoreVersion(number) {
  const version = current().versions.find((entry) => entry.number === number); if (!version) return;
  current().data = { ...deepCopy(version.profile), updatedAt: new Date().toISOString() };
  saveAll(); hydrate(current().data); showToast(`Restored version ${number} as a draft`);
}

async function init() {
  try {
    const stored = localStorage.getItem(STORAGE_KEY);
    if (stored) {
      const state = JSON.parse(stored); profiles = (state.profiles ?? []).filter((item) => item?.data?.id).map((item) => ({ data: { ...deepCopy(defaults), ...item.data }, versions: item.versions ?? [], activeVersion: item.activeVersion ?? null })); selectedId = state.selectedId;
    }
    if (!profiles.length) {
      let initial = deepCopy(defaults);
      const legacy = localStorage.getItem(LEGACY_KEY); if (legacy) initial = { ...initial, ...JSON.parse(legacy) };
      profiles = [newProfile(initial)]; selectedId = initial.id; saveAll();
    }
  } catch { profiles = [newProfile(defaults)]; selectedId = defaults.id; storageAvailable = false; }
  if (!profiles.some((item) => item.data.id === selectedId)) selectedId = profiles[0].data.id;
  renderRoster(); hydrate(current().data);
  if (location.protocol !== "file:" && location.pathname.startsWith("/admin")) {
    try {
      const response = await fetch(SERVER_API, { cache: "no-store" });
      const state = await response.json();
      if (!response.ok) throw new Error(state.error ?? `HTTP ${response.status}`);
      if (Array.isArray(state.profiles) && state.profiles.length) {
        voiceOptions = Array.isArray(state.voiceOptions) && state.voiceOptions.length ? state.voiceOptions : voiceOptions;
        $("voicePreset").replaceChildren(...voiceOptions.map((voice) => {
          const option = document.createElement("option"); option.value = voice.voiceRef; option.textContent = voice.label; return option;
        }));
        profiles = state.profiles.map((item) => ({ data: { ...deepCopy(defaults), ...item.data }, versions: item.versions ?? [], activeVersion: item.activeVersion ?? null }));
        selectedId = state.selectedId;
        companionReady = state.companionReady === true; gameProfileId = state.gameProfileId ?? defaults.id;
        if (!profiles.some((item) => item.data.id === selectedId)) selectedId = profiles[0].data.id;
        apiConnected = true;
        try { localStorage.setItem(STORAGE_KEY, JSON.stringify({ profiles, selectedId })); } catch { storageAvailable = false; }
        renderRoster(); hydrate(current().data);
      }
    } catch (error) {
      apiConnectionError = error.message;
    }
    render();
  }
}

document.querySelectorAll(".editor-column input:not([disabled]), .editor-column textarea, .editor-column select").forEach((control) => {
  control.addEventListener("input", persist); control.addEventListener("change", persist);
});
$("npcSelect").addEventListener("change", () => { selectedId = $("npcSelect").value; saveAll(); hydrate(current().data); $("behaviorPreset").value = "custom"; });
function openRosterDialog(action) {
  const dialog = $("rosterDialog");
  dialog.dataset.action = action;
  dialog.dataset.profileId = current().data.id;
  const creating = action === "create";
  $("rosterDialogTitle").textContent = creating ? "Create NPC" : "Delete NPC";
  $("rosterDialogMessage").textContent = creating ? "Create a new local profile; publishing activates its supported runtime settings." : `Delete ${current().data.displayName} and its version history? This cannot be undone.`;
  $("rosterDialogNameLabel").hidden = !creating;
  $("rosterDialogName").required = creating;
  $("rosterDialogName").value = creating ? "New NPC" : "";
  $("rosterDialogSubmit").textContent = creating ? "Create NPC" : "Delete NPC";
  dialog.showModal();
  if (creating) { $("rosterDialogName").focus(); $("rosterDialogName").select(); }
}
$("rosterDialogCancel").addEventListener("click", () => $("rosterDialog").close());
$("rosterDialogForm").addEventListener("submit", (event) => {
  event.preventDefault();
  const dialog = $("rosterDialog");
  if (dialog.dataset.action === "create") {
    const name = $("rosterDialogName").value.trim();
    if (!name) return;
    const item = makeProfile(name); profiles.push(item); selectedId = item.data.id;
    saveAll(); hydrate(item.data); showToast("NPC profile created");
  } else {
    const id = dialog.dataset.profileId;
    if (id === defaults.id || profiles.length < 2) return;
    const entry = profiles.find((item) => item.data.id === id);
    if (!entry) return;
    profiles = profiles.filter((item) => item.data.id !== id);
    selectedId = profiles[0].data.id; saveAll(); hydrate(current().data);
    showToast(`${entry.data.displayName} deleted`);
  }
  dialog.close();
});
$("newNpcButton").addEventListener("click", () => openRosterDialog("create"));
$("duplicateNpcButton").addEventListener("click", () => {
  const item = makeProfile(`${current().data.displayName} copy`, readForm()); profiles.push(item); selectedId = item.data.id; saveAll(); hydrate(item.data); showToast("NPC profile duplicated");
});
$("deleteNpcButton").addEventListener("click", () => {
  if (profiles.length < 2) { showToast("Keep at least one NPC profile"); return; }
  if (current().data.id === defaults.id) { showToast("The GTA bridge profile is protected."); return; }
  openRosterDialog("delete");
});
$("exportButton").addEventListener("click", () => downloadProfile());
$("publishButton").addEventListener("click", publish);
$("resetButton").addEventListener("click", () => {
  const id = current().data.id; const profile = { ...deepCopy(defaults), id, displayName: id === defaults.id ? defaults.displayName : current().data.displayName };
  current().data = profile; $("behaviorPreset").value = "custom"; saveAll(); hydrate(profile); showToast("Restored default settings as a draft");
});
$("behaviorPreset").addEventListener("change", () => applyPreset($("behaviorPreset").value));
$("refreshPreview").addEventListener("click", scenarioReply);
$("voicePreviewButton").addEventListener("click", previewVoice);
$("scenarioSelect").addEventListener("change", scenarioReply);
$("versionList").addEventListener("click", (event) => { const button = event.target.closest("[data-restore-version]"); if (button) restoreVersion(Number(button.dataset.restoreVersion)); });
$("importButton").addEventListener("click", () => $("importInput").click());
$("importInput").addEventListener("change", async (event) => {
  const file = event.target.files?.[0]; if (!file) return;
  try { const imported = JSON.parse(await file.text()); validateProfile(imported); imported.id = profiles.some((item) => item.data.id === imported.id) ? uniqueId(imported.displayName) : slug(imported.id || imported.displayName); if (profiles.some((item) => item.data.id === imported.id)) imported.id = uniqueId(imported.displayName); const item = newProfile({ ...deepCopy(defaults), ...imported }); profiles.push(item); selectedId = item.data.id; saveAll(); hydrate(item.data); showToast("Profile imported as a new NPC"); }
  catch (error) { showToast(`Import failed: ${error.message}`); }
  event.target.value = "";
});

init();
renderWorld(null);
refreshWorld();
setInterval(() => { if (!document.hidden) refreshWorld(); }, 2000);
