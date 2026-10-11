import test from "node:test";
import assert from "node:assert/strict";
import { createServer, get } from "node:http";
import { once } from "node:events";
import { spawn } from "node:child_process";
import { randomUUID } from "node:crypto";
import { existsSync, mkdirSync, readFileSync, rmSync, statSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { fileURLToPath, pathToFileURL } from "node:url";

const serverPath = fileURLToPath(new URL("../src/server.js", import.meta.url));
const repository = fileURLToPath(new URL("../../", import.meta.url));

async function collisionFixture(t, existingToken) {
  const directory = fileURLToPath(new URL(`./.startup-fixture-${randomUUID()}/`, import.meta.url));
  mkdirSync(directory, { recursive: true });
  const localAppData = join(directory, "local-app-data");
  const tokenPath = join(localAppData, "MalayalamNpcDemo", "bridge-token");
  const guardPath = join(directory, "network-guard.mjs");
  writeFileSync(guardPath, "globalThis.fetch = async () => { throw new Error('Provider network disabled in startup fixture.'); };\n");
  const occupied = createServer((_request, response) => response.end("first-server-fixture"));
  occupied.listen(0, "127.0.0.1");
  await once(occupied, "listening");
  t.after(async () => {
    occupied.closeAllConnections();
    await new Promise((resolve) => occupied.close(resolve));
    rmSync(directory, { recursive: true, force: true });
  });
  const port = occupied.address().port;
  const contents = `token=first-instance-fixture-token\nport=${port}\n`;
  if (existingToken) {
    mkdirSync(join(localAppData, "MalayalamNpcDemo"), { recursive: true });
    writeFileSync(tokenPath, contents);
  }
  const timestamp = existingToken ? statSync(tokenPath, { bigint: true }).mtimeNs : null;
  return { directory, tokenPath, contents, timestamp, port, occupied,
    async runCli() {
      const child = spawn(process.execPath, ["--no-warnings", "--import", pathToFileURL(guardPath).href, serverPath], {
        cwd: repository,
        env: {
          UI_PORT: String(port),
          DATA_DIR: join(directory, "data"),
          LOCALAPPDATA: localAppData,
          OPENAI_API_KEY: "startup-fixture-openai",
          ELEVENLABS_API_KEY: "startup-fixture-elevenlabs",
          ELEVENLABS_VOICE_ID: "fixture-voice",
        },
        stdio: ["ignore", "pipe", "pipe"],
      });
      let stdout = "";
      let stderr = "";
      child.stdout.setEncoding("utf8").on("data", (chunk) => { stdout += chunk; });
      child.stderr.setEncoding("utf8").on("data", (chunk) => { stderr += chunk; });
      let timedOut = false;
      const timer = setTimeout(() => { timedOut = true; child.kill(); }, 5000);
      try {
        const [code, signal] = await once(child, "close");
        assert.equal(timedOut, false, "The failed CLI startup must exit promptly rather than leave a server running.");
        return { code, signal, stdout, stderr };
      } finally {
        clearTimeout(timer);
        if (child.exitCode === null && child.signalCode === null) child.kill();
      }
    },
  };
}

function firstServerResponse(port) {
  return new Promise((resolve, reject) => {
    get(`http://127.0.0.1:${port}/`, (response) => {
      let body = "";
      response.setEncoding("utf8").on("data", (chunk) => { body += chunk; });
      response.on("end", () => resolve({ status: response.statusCode, body }));
      response.on("error", reject);
    }).on("error", reject);
  });
}

for (const existingToken of [true, false]) {
  test(`CLI port collision exits cleanly and ${existingToken ? "preserves the existing" : "does not publish a new"} bridge token`,
    { timeout: 15000 }, async (t) => {
      const fixture = await collisionFixture(t, existingToken);
      const result = await fixture.runCli();
      assert.equal(result.code, 1);
      assert.equal(result.signal, null);
      assert.match(result.stderr, new RegExp(`port ${fixture.port} is already in use`));
      assert.match(result.stderr, /Ctrl\+C/u);
      assert.match(result.stderr, /retry npm start/u);
      assert.match(result.stderr, /free UI_PORT/u);
      assert.match(result.stderr, /Existing bridge credentials were left unchanged/u);
      assert.doesNotMatch(result.stderr, /Unhandled|unhandled|throw er;|node:events|\n\s+at\s|first-instance-fixture-token|startup-fixture-openai|startup-fixture-elevenlabs|Provider network disabled/u);
      assert.doesNotMatch(result.stdout, /Malayalam NPC companion:|GTA bridge token:|Providers:/u);
      assert.equal(fixture.occupied.listening, true);
      assert.deepEqual(await firstServerResponse(fixture.port), { status: 200, body: "first-server-fixture" });
      if (existingToken) {
        assert.equal(readFileSync(fixture.tokenPath, "utf8"), fixture.contents);
        assert.equal(statSync(fixture.tokenPath, { bigint: true }).mtimeNs, fixture.timestamp);
      } else {
        assert.equal(existsSync(fixture.tokenPath), false);
        assert.equal(existsSync(join(fixture.directory, "local-app-data")), false);
      }
    });
}
