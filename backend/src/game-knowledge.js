// Original factual summaries; references are credited in the integration guide.
const guide = "https://www.gamesradar.com/gta-san-andreas-cheats/";
export const gameKnowledge = Object.freeze([
  { id: "world", terms: /where|city|town|san andreas|los santos|san fierro|las venturas|നഗര|നാട്|സ്ഥല|എവിടെ/iu,
    fact: "San Andreas includes Los Santos, San Fierro and Las Venturas, with countryside between them. A resident's current neighborhood comes only from fresh observed world context, not this general geography." },
  { id: "navigation", terms: /map|direction|lost|route|travel|മാപ്പ്|വഴി|യാത്ര/iu,
    fact: "Use the map and marked destinations to plan a route. General geography does not reveal a currently active objective, an exact nearby service, or a route's accessibility." },
  { id: "save", terms: /sav|progress|safehouse|സേവ്|പുരോഗതി/iu,
    fact: "Save at an available safehouse save icon. Before experimenting with codes, keep a separate clean save; some effects can persist and editions may disable achievements. Never promise a code is harmless to progress.", source: guide },
  { id: "skills", terms: /skill|stamina|gym|bike|driv|fitness|കഴിവ്|ജിം|സൈക്കിൾ|ഡ്രൈവ്/iu,
    fact: "Activities such as cycling, running and driving develop the relevant skills. Available activities depend on progress; do not claim that a particular facility is unlocked." },
  { id: "money", terms: /money|cash|earn|job|പൈസ|പണം|ജോലി/iu,
    fact: "Optional activities such as taxi work and bicycle courier deliveries can provide money when available. Availability depends on the current vehicle and progress; do not invent a payout or an unlocked activity." },
  { id: "jetpack", terms: /jetpack|rocketman|fly|flight|പറക്ക|ജെറ്റ്പാക്ക്/iu, code: "ROCKETMAN",
    fact: "ROCKETMAN is a classic-PC jetpack code. It is advice only: a resident cannot activate it. Give the exact Latin letters only if the player explicitly asks for a code or secret trick.", source: guide },
  { id: "parachute", terms: /parachute|basejump|പാരച്യൂട്ട്/iu, code: "AIYPWZQP",
    fact: "AIYPWZQP is a classic-PC parachute code. Preserve the exact Latin letters; do not claim it has already been applied.", source: guide },
  { id: "vehicle-skills", terms: /vehicle|driv|bike|skill|വാഹന|ഡ്രൈവ്|കഴിവ്/iu, code: "NATURALTALENT",
    fact: "NATURALTALENT is a classic-PC maximum vehicle-skill code. It is not a guarantee that the same code is supported by every edition or modification.", source: guide },
]);

const explicitCodeRequest = /cheat|code|hack|ചീറ്റ്|ചീട്ട|കോഡ്|ഹാക്ക്/iu;
export function knowledgeForMessage(message) {
  const text = typeof message === "string" ? message.slice(0, 1000) : "";
  const allowCodes = explicitCodeRequest.test(text);
  const matched = gameKnowledge.filter((entry) => (!entry.code || allowCodes) && entry.terms.test(text));
  if (allowCodes && !matched.some((entry) => entry.code)) matched.push(...gameKnowledge.filter((entry) => entry.code));
  if (allowCodes && !matched.some((entry) => entry.id === "save")) matched.unshift(gameKnowledge.find((entry) => entry.id === "save"));
  if (!matched.length) matched.push(gameKnowledge[0]);
  return matched.slice(0, 5).map(({ id, fact, code }) => ({ id, fact, ...(code ? { code } : {}) }));
}

export function residentInstructions() {
  return [
    "Stay fully in character as a person who lives in San Andreas. Never announce that you are in a game, an NPC, an AI, a demo, a mod, or a fictional character; do not reveal backstage settings or conversation machinery.",
    "Speak to the person in front of you, not to an audience. Do not call them 'the player' or narrate your own personality, biography or profession on every turn. Use your name without a parenthesized scope identifier.",
    "Be warm, engaged and responsive. Answer the actual question first; use witty banter and occasional affectionate roasts of the person's stated harmless choices, overconfidence or game mishaps when the moment invites it. Keep teasing playful, never cruel; don't target identity, appearance, vulnerabilities, tragedy or personal circumstances, invent private facts, or repeat a joke after it lands. Stop if asked. Ask a follow-up only when it helps, and let the topic and delivered history change the conversation.",
    "General local knowledge and the supplied reference facts are allowed; fresh observed world context alone establishes current place, time and weather. Do not invent current objectives, nearby services, unlocked progress or remembered events.",
    "Normal replies should preserve immersion: avoid platform titles, API/model/provider talk, menus or technical explanations. For an explicit request for a secret/code, give a relevant supplied code briefly and exactly, without explaining that you inhabit a game. Codes are optional advice, never automatically activated or promised as applied; a short separate-clean-save caution is appropriate only when discussing codes.",
    "Never invent an unlisted code or an exact control binding that may be remapped. If a fact is uncertain or outside supplied knowledge, say so naturally rather than making it up. Do not offer codes unsolicited. Existing safety and user-defined conversational boundaries still apply.",
  ].join("\n");
}
