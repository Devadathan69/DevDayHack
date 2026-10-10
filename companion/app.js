const $ = (id) => document.getElementById(id);
const elements = {
  providerBadge: $("providerBadge"), gameMessage: $("gameMessage"), selectionState: $("selectionState"),
  selectButton: $("selectButton"), resetButton: $("resetButton"), messages: $("messages"),
  emptyState: $("emptyState"), pttButton: $("pttButton"), pttLabel: $("pttLabel"),
  micState: $("micState"), cancelButton: $("cancelButton"), textToggle: $("textToggle"),
  pttHint: $("pttHint"),
  muteButton: $("muteButton"),
  textForm: $("textForm"), textInput: $("textInput"), turnStatus: $("turnStatus"),
  replyActions: $("replyActions"), playButton: $("playButton"), readButton: $("readButton"),
  toast: $("toast"), limitLabel: $("limitLabel"),
};

let status = null;
let mediaStream = null;
let recorder = null;
let pendingRecorder = null;
let chunks = [];
let maxAudioTimer = null;
let activeTurnId = null;
let audioElement = null;
let toastTimer = null;
let busy = false;
let voiceMuted = false;
let pttHeld = false;
let permissionPending = false;

async function request(path, options = {}) {
  const response = await fetch(path, { cache: "no-store", ...options });
  const contentType = response.headers.get("content-type") ?? "";
  const result = contentType.includes("application/json") ? await response.json() : null;
  if (!response.ok) throw new Error(result?.error ?? `Request failed (HTTP ${response.status}).`);
  return result;
}

function showToast(message) {
  elements.toast.textContent = message;
  elements.toast.classList.add("visible");
  clearTimeout(toastTimer);
  toastTimer = setTimeout(() => elements.toast.classList.remove("visible"), 3800);
}

function setStatus(message, type = "") {
  elements.turnStatus.textContent = message;
  elements.turnStatus.className = `turn-status ${type}`.trim();
}

function updateControls() {
  const selected = status?.selected === true;
  elements.selectionState.textContent = selected ? "Selected" : "Not selected";
  elements.selectionState.classList.toggle("selected", selected);
  elements.selectButton.textContent = selected ? "End conversation" : "Select Manu";
  const key = document.createElement("kbd");
  key.textContent = "F6";
  elements.selectButton.append(key);
  const micAvailable = status !== null;
  elements.pttButton.disabled = !selected || !micAvailable || status?.state !== "SELECTED" || busy || permissionPending || pendingRecorder !== null;
  elements.pttHint.textContent = micAvailable
    ? `Hold F7 while this companion window is focused, or press and hold the button. Release to send a recording (up to ${status.audioLimitSeconds} seconds).`
    : "Waiting for the local provider service…";
  elements.resetButton.disabled = !selected || busy;
  elements.cancelButton.disabled = !busy && !recorder && !activeTurnId && !audioElement && !permissionPending;
  elements.muteButton.disabled = false;
  elements.muteButton.textContent = voiceMuted ? "Unmute voice" : "Mute voice";
  if (!recorder) elements.pttLabel.textContent = busy
    ? "Waiting for reply…"
    : micAvailable ? "Hold F7 to speak" : "Connecting…";
}

async function refreshStatus() {
  status = await request("/api/status");
  elements.providerBadge.textContent = "LIVE PROVIDERS";
  elements.providerBadge.className = "badge live";
  elements.gameMessage.textContent = status.gameMessage;
  elements.limitLabel.textContent = status.liveTurnsRemaining === null
    ? "No app turn limit · provider quotas still apply"
    : `${status.liveTurnsRemaining} live turns remaining this run`;
  $("privacyFooter").textContent = "Live providers · audio goes to ElevenLabs; transcript/history go to OpenAI; memory stays in local SQLite";
  updateControls();
}

function currentNpcName() {
  return status?.npc?.name ?? "Manu";
}

function addMessage(speaker, text, kind = "") {
  elements.emptyState.hidden = true;
  const article = document.createElement("div");
  article.className = `message ${speaker === "You" ? "player" : "npc"} ${kind}`.trim();
  const label = document.createElement("span");
  label.className = "speaker";
  label.textContent = speaker;
  const content = document.createElement("span");
  content.textContent = text;
  article.append(label, content);
  elements.messages.append(article);
  elements.messages.scrollTop = elements.messages.scrollHeight;
}

async function loadHistory() {
  const history = await request("/api/history");
  elements.messages.replaceChildren();
  if (!history.turns.length) {
    elements.emptyState.hidden = false;
    elements.messages.append(elements.emptyState);
    return;
  }
  elements.emptyState.hidden = true;
  for (const turn of history.turns) {
    addMessage("You", turn.player);
    addMessage(currentNpcName(), turn.npc);
  }
}

function chooseRecordingType() {
  return ["audio/webm;codecs=opus", "audio/ogg;codecs=opus", "audio/mp4"]
    .find((type) => MediaRecorder.isTypeSupported(type)) ?? "";
}

function stopMicrophone() {
  mediaStream?.getTracks().forEach((track) => track.stop());
  mediaStream = null;
  elements.micState.textContent = "Microphone off · starts while PTT is held";
  elements.micState.style.color = "var(--muted)";
}

async function enableMicrophone() {
  if (!navigator.mediaDevices?.getUserMedia || !window.MediaRecorder) {
    throw new Error("This browser does not support microphone recording. Use the text fallback instead.");
  }
  mediaStream ??= await navigator.mediaDevices.getUserMedia({ audio: { echoCancellation: true, noiseSuppression: true, channelCount: 1 } });
  elements.micState.textContent = "Microphone ready";
  elements.micState.style.color = "var(--teal)";
}

async function beginRecording() {
  if (!status?.selected || status.state !== "SELECTED" || busy || recorder) return;
  permissionPending = true;
  updateControls();
  try {
    await enableMicrophone();
    if (!pttHeld || !status?.selected || status.state !== "SELECTED") {
      stopMicrophone();
      return;
    }
    const mimeType = chooseRecordingType();
    recorder = new MediaRecorder(mediaStream, mimeType ? { mimeType } : undefined);
    chunks = [];
    recorder.addEventListener("dataavailable", (event) => { if (event.data.size) chunks.push(event.data); });
    recorder.addEventListener("stop", finishRecording, { once: true });
    recorder.start(200);
    elements.pttButton.classList.add("recording");
    elements.pttLabel.textContent = "Recording… release to send";
    setStatus("Recording. Release F7 when you finish.", "busy");
    maxAudioTimer = setTimeout(() => stopRecording(true), (status.audioLimitSeconds ?? 15) * 1000);
    updateControls();
  } catch (error) {
    recorder = null;
    pttHeld = false;
    stopMicrophone();
    setStatus(error.message, "error");
    showToast(error.message);
  } finally {
    permissionPending = false;
    updateControls();
  }
}

function stopRecording(send = true) {
  pttHeld = false;
  if (!recorder) {
    if (pendingRecorder && !send) pendingRecorder.__sendAfterStop = false;
    return;
  }
  clearTimeout(maxAudioTimer);
  const current = recorder;
  recorder = null;
  pendingRecorder = current;
  elements.pttButton.classList.remove("recording");
  current.__sendAfterStop = send;
  current.stop();
  updateControls();
}

async function finishRecording(event) {
  const current = event.currentTarget;
  if (pendingRecorder === current) pendingRecorder = null;
  stopMicrophone();
  updateControls();
  if (!current.__sendAfterStop) {
    chunks = [];
    setStatus("Recording cancelled.");
    updateControls();
    return;
  }
  const blob = new Blob(chunks, { type: event.currentTarget.mimeType || "audio/webm" });
  chunks = [];
  if (blob.size === 0) {
    setStatus("That recording was too short. Hold F7 and speak, then release.", "error");
    updateControls();
    return;
  }
  await submitTurn("/api/turn", { method: "POST", headers: { "Content-Type": blob.type.split(";")[0] }, body: blob });
}

async function submitTurn(path, options) {
  if (busy) return;
  busy = true;
  elements.replyActions.hidden = true;
  setStatus(`Listening and preparing ${currentNpcName()}’s reply…`, "busy");
  updateControls();
  try {
    const result = await request(path, options);
    activeTurnId = result.turnId;
    addMessage("You", result.transcript, "transcript");
    addMessage(currentNpcName(), result.replyMl);
    const timingText = result.timings
      ? ` · STT ${(result.timings.speechRecognitionMs / 1000).toFixed(1)}s, reply ${(result.timings.dialogueMs / 1000).toFixed(1)}s, voice ${(result.timings.speechSynthesisMs / 1000).toFixed(1)}s`
      : "";
    if (result.speechError) {
      elements.replyActions.hidden = false;
      elements.playButton.hidden = true;
      setStatus(`Subtitles are ready. Voice unavailable: ${result.speechError}`, "error");
      return;
    }
    if (result.audioUrl) {
      audioElement = new Audio(result.audioUrl);
      audioElement.muted = voiceMuted;
      elements.playButton.hidden = false;
      elements.replyActions.hidden = false;
      elements.playButton.onclick = () => playReply();
      setStatus(`Reply ready${timingText}. Play it, or continue with subtitles.`);
      try { await playReply(); }
      catch {
        if (activeTurnId) setStatus(`Reply ready${timingText}. Use Play reply, or continue with subtitles.`);
      }
    } else {
      await acknowledge({ subtitleDisplayed: true, audioPlayed: false });
      setStatus(`Reply shown with subtitles${timingText}.`);
    }
  } catch (error) {
    setStatus(error.message, "error");
    showToast(error.message);
  } finally {
    busy = false;
    try { await refreshStatus(); } catch { /* Keep the last status if the local server closed. */ }
    updateControls();
  }
}

async function playReply() {
  if (!audioElement || !activeTurnId) return;
  setStatus("Playing Manu’s reply…", "busy");
  await waitForAudioPlayback(audioElement);
  if (!activeTurnId) return;
  await acknowledge({ subtitleDisplayed: true, audioPlayed: !voiceMuted });
}

function waitForAudioPlayback(audio) {
  return new Promise((resolve, reject) => {
    const cleanup = () => {
      audio.removeEventListener("ended", onEnded);
      audio.removeEventListener("error", onError);
      audio.removeEventListener("pause", onPause);
    };
    const onEnded = () => { cleanup(); resolve(); };
    const onError = () => { cleanup(); reject(new Error("Audio playback failed.")); };
    const onPause = () => {
      if (audio.ended) return;
      cleanup();
      reject(new Error("Audio playback stopped."));
    };
    audio.addEventListener("ended", onEnded, { once: true });
    audio.addEventListener("error", onError, { once: true });
    audio.addEventListener("pause", onPause, { once: true });
    audio.play().catch((error) => { cleanup(); reject(error); });
  });
}

async function acknowledge(receipt) {
  if (!activeTurnId) return;
  const turnId = activeTurnId;
  await request("/api/delivery", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ turnId, ...receipt }) });
  activeTurnId = null;
  audioElement = null;
  elements.replyActions.hidden = true;
  setStatus(receipt.audioPlayed ? "Voice reply delivered." : "Reply delivered as subtitles.");
  await loadHistory();
  await refreshStatus();
}

async function cancelConversation() {
  pttHeld = false;
  clearTimeout(maxAudioTimer);
  if (recorder || pendingRecorder) stopRecording(false);
  if (audioElement) { audioElement.pause(); audioElement.currentTime = 0; }
  try { await request("/api/cancel", { method: "POST" }); }
  catch { /* A turn may not have reached the server yet. */ }
  activeTurnId = null;
  audioElement = null;
  elements.replyActions.hidden = true;
  busy = false;
  setStatus("Cancelled.");
  updateControls();
}

elements.selectButton.addEventListener("click", async () => {
  try {
    if (status?.selected) {
      const active = busy || recorder || pendingRecorder || permissionPending || activeTurnId || audioElement;
      if (active && !window.confirm("End the conversation and cancel the active recording or reply?")) return;
      if (active) await cancelConversation();
      await request("/api/deselect", { method: "POST" });
      stopMicrophone();
      setStatus("Conversation ended.");
    } else {
      await request("/api/select", { method: "POST" });
      setStatus("Manu selected. Hold F7 to speak.");
      await loadHistory();
    }
    await refreshStatus();
  } catch (error) { showToast(error.message); }
});

elements.pttButton.addEventListener("pointerdown", (event) => {
  if (event.button !== 0) return;
  event.preventDefault();
  pttHeld = true;
  elements.pttButton.setPointerCapture?.(event.pointerId);
  beginRecording();
});
elements.pttButton.addEventListener("pointerup", () => stopRecording(true));
elements.pttButton.addEventListener("pointercancel", () => stopRecording(false));
elements.pttButton.addEventListener("lostpointercapture", () => { if (pttHeld) stopRecording(false); });
elements.cancelButton.addEventListener("click", cancelConversation);
elements.muteButton.addEventListener("click", () => {
  voiceMuted = !voiceMuted;
  if (audioElement) audioElement.muted = voiceMuted;
  updateControls();
});
elements.textToggle.addEventListener("click", () => {
  elements.textForm.hidden = !elements.textForm.hidden;
  if (!elements.textForm.hidden) elements.textInput.focus();
  elements.textToggle.textContent = elements.textForm.hidden ? "Use text instead" : "Hide text input";
});
elements.textForm.addEventListener("submit", async (event) => {
  event.preventDefault();
  const text = elements.textInput.value.trim();
  if (!text) return;
  elements.textInput.value = "";
  await submitTurn("/api/text-turn", { method: "POST", headers: { "Content-Type": "application/json" }, body: JSON.stringify({ text }) });
});
elements.resetButton.addEventListener("click", async () => {
  if (!window.confirm(`Erase ${currentNpcName()}’s saved conversation memory for this demo player? This cannot be undone.`)) return;
  try {
    if (busy || recorder || pendingRecorder || permissionPending || activeTurnId || audioElement) await cancelConversation();
    const result = await request("/api/reset", { method: "POST" });
    await loadHistory();
    setStatus(`Memory cleared (generation ${result.generation}).`);
  } catch (error) { showToast(error.message); }
});
elements.readButton.addEventListener("click", async () => {
  if (audioElement) {
    audioElement.pause();
    audioElement.currentTime = 0;
  }
  await acknowledge({ subtitleDisplayed: true, audioPlayed: false });
});

window.addEventListener("keydown", (event) => {
  if (event.repeat) return;
  if (event.key === "Escape") {
    if (recorder || pendingRecorder || busy || activeTurnId || audioElement || permissionPending) { event.preventDefault(); cancelConversation(); }
    return;
  }
  if (["INPUT", "TEXTAREA"].includes(document.activeElement?.tagName)) return;
  if (event.key === "F6") {
    event.preventDefault();
    elements.selectButton.click();
  } else if (event.key === "F7") {
    event.preventDefault();
    pttHeld = true;
    beginRecording();
  }
});
window.addEventListener("keyup", (event) => {
  if (event.key === "F7") { event.preventDefault(); stopRecording(true); }
});
window.addEventListener("blur", () => { pttHeld = false; if (recorder || pendingRecorder || permissionPending) cancelConversation(); });
window.addEventListener("beforeunload", () => {
  stopMicrophone();
  audioElement?.pause();
});

try {
  await refreshStatus();
  if (status.selected) await loadHistory();
  setStatus("Live providers ready. Select Manu to begin.");
} catch (error) {
  setStatus(error.message, "error");
}
