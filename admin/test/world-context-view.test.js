import test from "node:test";
import assert from "node:assert/strict";
import "../world-context-view.js";

const { format } = globalThis.NpcWorldContextView;
const observed = (changes = {}) => ({
  source: "gta-native", available: true, ageMs: 1200, capturedAt: 1791679019000,
  location: { name: "Ganton", zoneCode: "GANTON" },
  time: { hour: 22, minute: 9 }, weather: { condition: "Rainy", rainIntensity: 0.6 },
  ...changes,
});

test("dashboard shows only verified native facts, not playground simulations", () => {
  for (const value of [null, undefined, { ...observed(), source: "playground" }, observed({ capturedAt: null })]) {
    const view = format(value);
    assert.equal(view.location, "Unavailable");
    assert.equal(view.time, "Unavailable");
    assert.equal(view.weather, "Unavailable");
    assert.match(view.state, /Waiting/);
  }
});

test("fresh world view uses actual zone, game clock and weather with observed age", () => {
  const view = format(observed());
  assert.equal(view.location, "Ganton");
  assert.equal(view.time, "22:09");
  assert.equal(view.weather, "Rainy");
  assert.equal(view.stateClass, "is-fresh");
  assert.match(view.state, /1s ago/);
  assert.match(view.note, /published profile/);
});

test("midnight is valid while absent and out-of-range game time never use PC time", () => {
  assert.equal(format(observed({ time: { hour: 0, minute: 0 } })).time, "00:00");
  for (const time of [null, {}, { hour: 24, minute: 0 }, { hour: 2, minute: 60 }, { hour: 2.5, minute: 1 }]) {
    assert.equal(format(observed({ time })).time, "Unavailable");
  }
});

test("last observations are explicitly stale and not claimed as current", () => {
  for (const changes of [{ available: false }, { ageMs: 11000 }, { ageMs: null }, { ageMs: -1 }]) {
    const view = format(observed(changes));
    assert.equal(view.stateClass, "is-stale");
    assert.equal(view.location, "Ganton");
    assert.match(view.state, /stale/);
    assert.match(view.note, /not treated as current/);
  }
});

test("partial world data preserves genuine zone codes without inventing missing signals", () => {
  const view = format(observed({ location: { zoneCode: "ZONE_TEST" }, time: null, weather: null }));
  assert.equal(view.location, "ZONE_TEST");
  assert.equal(view.time, "Unavailable");
  assert.equal(view.weather, "Unavailable");
});
