import { readFile } from "node:fs/promises";
import { createInterface } from "node:readline/promises";
import { stdin, stdout } from "node:process";
import { fileURLToPath } from "node:url";
import { InMemoryMemoryStore } from "./memory/in-memory-memory-store.js";
import { NpcRegistry } from "./npc-registry.js";
import { createMockProviders } from "./providers/mock-providers.js";
import { SessionManager } from "./session-manager.js";

const profilePath = new URL("../../profiles/demo-npc-001.json", import.meta.url);
const profile = JSON.parse(await readFile(fileURLToPath(profilePath), "utf8"));
const memory = new InMemoryMemoryStore();
const manager = new SessionManager({
  registry: new NpcRegistry([profile], { ...createMockProviders(), memory }),
  memory,
  events: ({ state }) => {
    if (["TRANSCRIBING", "THINKING", "SYNTHESIZING"].includes(state)) stdout.write(`  [${state.toLowerCase()}]\n`);
  },
});
const selected = manager.selectNpc({
  npcId: profile.id,
  worldId: "demo-world",
  playerId: "demo-player",
  gameSessionId: "local-cli-session",
  sceneGeneration: 0,
});
const readline = createInterface({ input: stdin, output: stdout });

stdout.write(`Mock demo — ${selected.displayName}\n`);
stdout.write("This uses mock speech, mock dialogue, and memory that lasts only until exit.\n");
stdout.write("Type Malayalam text and press Enter. Commands: /reset, /quit.\n\n");

try {
  stdout.write("நீங்கள்: ");
  for await (const line of readline) {
    const input = line.trim();
    if (input === "/quit") break;
    if (input === "/reset") {
      const generation = await manager.resetSelectedMemory();
      stdout.write(`Memory cleared (generation ${generation}).\n\n`);
      stdout.write("நீங்கள்: ");
      continue;
    }
    if (!input) {
      stdout.write("நீங்கள்: ");
      continue;
    }

    try {
      const result = await manager.startTurn({ text: input });
      stdout.write(`Heard: ${result.transcript.text}\n`);
      stdout.write(`${selected.displayName}: ${result.reply.replyMl}\n`);
      if (result.speechError) stdout.write(`Speech unavailable: ${result.speechError}\n`);
      else stdout.write("[Mock voice: no audio generated]\n");
      await manager.acknowledgeDelivery({ subtitleDisplayed: true, audioPlayed: false });
      stdout.write("\n");
    } catch (error) {
      stdout.write(`Turn failed: ${error.message}\n\n`);
    }
    stdout.write("நீங்கள்: ");
  }
} finally {
  readline.close();
}
