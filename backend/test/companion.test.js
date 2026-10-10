import test from "node:test";
import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import { runInNewContext } from "node:vm";

const source = await readFile(new URL("../../companion/app.js", import.meta.url), "utf8");

async function render(status) {
  const elements = new Map();
  const element = () => ({ textContent: "", classList: { add() {}, remove() {}, toggle() {} },
    addEventListener() {}, append() {}, replaceChildren() {}, hidden: false });
  const document = { getElementById(id) { if (!elements.has(id)) elements.set(id, element()); return elements.get(id); },
    createElement: element };
  const context = { document, window: { addEventListener() {} }, navigator: {},
    fetch: async (path) => ({ ok: true, headers: { get: () => "application/json" },
      json: async () => path === "/api/status" ? status : { turns: [] } }) };
  await runInNewContext(`(async () => { ${source} })()`, context);
  return elements;
}

test("companion displays unlimited allowance without a fabricated remaining count", async () => {
  const elements = await render({ state: "IDLE", selected: false, npc: { name: "Manu" },
    liveTurnLimit: 0, liveTurnsRemaining: null, audioLimitSeconds: 15 });
  assert.equal(elements.get("limitLabel").textContent, "No app turn limit · provider quotas still apply");
  assert.doesNotMatch(elements.get("limitLabel").textContent, /null|undefined|25|remaining/u);
});

test("companion preserves accurate finite allowance including zero remaining", async () => {
  for (const liveTurnsRemaining of [25, 1, 0]) {
    const elements = await render({ state: "IDLE", selected: false, liveTurnLimit: 25, liveTurnsRemaining, audioLimitSeconds: 15 });
    assert.equal(elements.get("limitLabel").textContent, `${liveTurnsRemaining} live turns remaining this run`);
  }
});
