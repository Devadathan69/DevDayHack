import { createConnection } from "node:net";
import { loadConfig } from "./config.js";

let config;
try {
  config = loadConfig();
} catch (error) {
  console.error(`Configuration: ${error.message}`);
  process.exit(1);
}

console.log(`Local companion: http://${config.host}:${config.port}`);
console.log(`Conversation scope: world=${config.worldId}, player=${config.playerId}, NPC=demo-npc-001`);
console.log(`Memory location: ${config.dataDirectory}`);
console.log("Provider credentials: present (values hidden)");
console.log("GTA integration: not checked by this command; compatibility and in-game behavior remain unverified.");

const socket = createConnection({ host: config.host, port: config.port, timeout: 1_000 });
socket.once("connect", () => {
  console.log("Port readiness: already in use by a local service.");
  socket.destroy();
});
socket.once("error", (error) => {
  if (error.code === "ECONNREFUSED") console.log("Port readiness: available for the companion.");
  else console.log(`Port readiness: unable to check (${error.code ?? "unknown error"}).`);
});
