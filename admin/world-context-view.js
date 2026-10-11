(() => {
  const missing = "Unavailable";
  function format(snapshot) {
    if (!snapshot || snapshot.source !== "gta-native" || !Number.isFinite(snapshot.capturedAt) || snapshot.capturedAt <= 0) {
      return { state: "Waiting for native game", stateClass: "", location: missing, time: missing, weather: missing,
        note: "Interact with an NPC to supply verified game observations. Playground scenarios are simulations." };
    }
    const location = snapshot.location?.name || snapshot.location?.zoneCode || missing;
    const hour = snapshot.time?.hour, minute = snapshot.time?.minute;
    const time = Number.isInteger(hour) && hour >= 0 && hour <= 23 && Number.isInteger(minute) && minute >= 0 && minute <= 59
      ? `${String(hour).padStart(2, "0")}:${String(minute).padStart(2, "0")}` : missing;
    const condition = snapshot.weather?.condition;
    const weather = typeof condition === "string" && condition.trim() ? condition : missing;
    const age = Number.isFinite(snapshot.ageMs) && snapshot.ageMs >= 0 ? Math.floor(snapshot.ageMs / 1000) : null;
    const fresh = snapshot.available === true && age !== null && age <= 10;
    return { state: fresh ? `Verified game snapshot · ${age}s ago` : `Last verified snapshot · ${age === null ? "age unknown" : `${age}s ago`} · stale`,
      stateClass: fresh ? "is-fresh" : "is-stale", location: String(location), time, weather,
      note: fresh ? "Native game observations; live dialogue uses them when the published profile has scene awareness enabled."
        : "Stale or unavailable observations are not treated as current facts by live NPC dialogue." };
  }
  globalThis.NpcWorldContextView = Object.freeze({ format });
})();
