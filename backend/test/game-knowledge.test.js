import test from "node:test";
import assert from "node:assert/strict";
import { gameKnowledge, knowledgeForMessage, residentInstructions } from "../src/game-knowledge.js";

for (const message of ["Hello, how are you?", "Tell me about local secrets", "How can I fly with a jetpack?", "Where are we?"])
  test(`ordinary conversation never injects cheat codes: ${message}`, () => {
    const facts = knowledgeForMessage(message);
    assert.ok(facts.length > 0 && facts.length <= 5);
    assert.ok(facts.every((fact) => !fact.code));
  });

test("explicit English and Malayalam code requests retrieve exact verified advice", () => {
  for (const message of ["jetpack cheat code", "ജെറ്റ്പാക്ക് കോഡ് എന്താണ്?"]) {
    const facts = knowledgeForMessage(message);
    assert.ok(facts.some((fact) => fact.code === "ROCKETMAN"));
    assert.ok(facts.some((fact) => fact.id === "save"));
  }
  assert.ok(knowledgeForMessage("parachute code").some((fact) => fact.code === "AIYPWZQP"));
  assert.ok(knowledgeForMessage("driving skill code").some((fact) => fact.code === "NATURALTALENT"));
});

test("codes are sourced, bounded and never presented as already executed", () => {
  for (const entry of gameKnowledge.filter((fact) => fact.code)) {
    assert.match(entry.source, /^https:\/\//u);
    assert.match(entry.code, /^[A-Z]+$/u);
    assert.match(entry.fact, /classic-PC/u);
  }
  assert.ok(knowledgeForMessage("hack " + "x".repeat(3000)).length <= 5);
});

test("immersion instructions forbid fourth-wall disclosure while permitting grounded knowledge", () => {
  const instructions = residentInstructions();
  assert.match(instructions, /Never announce that you are in a game/u);
  assert.match(instructions, /not to an audience/u);
  assert.match(instructions, /Do not offer codes unsolicited/u);
  assert.match(instructions, /never automatically activated/u);
  assert.match(instructions, /fresh observed world context alone/u);
  assert.match(instructions, /Never invent an unlisted code/u);
});
