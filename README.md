# Malayalam NPC Demo

A hackathon prototype for a reusable Malayalam-speaking GTA San Andreas NPC. The current project has a mock-only backend and a native x86 lifecycle smoke-test plugin. Live speech, persistent storage, the game bridge, and in-game NPC behavior remain future steps in [plan.md](plan.md).

## Event

DevDay Exchange Community Hackathon: Kochi, 10–11 October 2026 at JAIN University, Kochi. [Event and registration](https://luma.com/z10nz8gp) · [Rules](RULES.md).

## Current implementation

- One original character profile (`Manu`) is validated and instantiated through a reusable NPC registry/controller.
- The CLI accepts typed Malayalam as mock speech input, runs a mock dialogue response, and acknowledges subtitle delivery.
- The in-memory store scopes recent delivered dialogue by world, player, and NPC. The mock can recall the previous delivered player message when asked.
- Turn IDs, cancellation, scene invalidation, reset generations, response validation, and single-turn ownership are represented in the backend.
- Mock providers can be configured for delays, cancellation, malformed results, and stage failures for future development checks.
- A standalone x86 ASI bootstrap logs its initialization and shutdown outside the game folder. Its exports were loaded by an x86 host smoke check; it does not access game APIs or implement NPC behavior.

The CLI does **not** record or play audio. The response is explicitly labeled as a mock; memory lasts only until the process exits. There is no live OpenAI or ElevenLabs integration, SQLite persistence, companion web UI, native bridge, or in-game NPC behavior. The ASI has not been installed into the game. The executable's architecture is x86, but its version and mod-loader compatibility have not been verified.

Provider credentials in `.env` are not consumed by this CLI; setting them does not turn the mock demo into a live conversation. See [game integration instructions](docs/game-integration.md) for the staged Windows setup; do not add gameplay hooks based on an unverified executable label.

## Run the mock demo

Prerequisite: Node.js 22.18 or newer. There are no third-party package dependencies, so `npm install` is not required.

```sh
npm run demo
```

Type Malayalam into the prompt. Ask `മുമ്പ് ഞാൻ എന്താണ് പറഞ്ഞത്?` after a turn to exercise the mock recall path. Use `/reset` to clear this process's NPC memory and `/quit` to exit.

Run a JavaScript syntax check with:

```sh
npm run check
```

This syntax check is not a test suite and does not verify live providers, storage restart behavior, microphone/audio devices, or GTA integration.

## Build the native smoke-test plugin

Prerequisite: Visual Studio with the x86/x64 C++ build tools installed.

```powershell
powershell -NoProfile -File .\native\build.ps1
```

The script creates `native/build/MalayalamNpcBootstrap.asi`, which is ignored by Git. The plugin only writes lifecycle entries to `%LOCALAPPDATA%\MalayalamNpcDemo\logs\game-bridge.log`; it does not hook gameplay or communicate with the backend. The current build passed an x86 host-process load check, but has not been loaded into the game.

## Project details

- **Project name:** Malayalam NPC Demo
- **Short description:** A profile-driven backend foundation for talking to and remembering a Malayalam-speaking game NPC.
- **Technology:** Node.js ES modules and built-in libraries for the mock backend; standalone Win32 C++ x86 ASI bootstrap built with MSVC.
- **Working demo:** Run the mock CLI as described above.
- **Libraries, APIs, and datasets:** Node.js built-in modules and Win32 APIs. Ultimate ASI Loader v9.7.4 was downloaded from its [official release](https://github.com/ThirteenAG/Ultimate-ASI-Loader/releases/tag/v9.7.4) and hash-checked as the candidate x86 loader, but it has not been installed into the game. No live AI API or dataset is used by the current demo.

See [plan.md](plan.md) for the architecture and milestone gates, [docs/game-integration.md](docs/game-integration.md) for the future native integration procedure, [CHANGELOG.md](CHANGELOG.md) for implementation history, and [AI_USAGE.md](AI_USAGE.md) for assistance records. [SUBMISSION.md](SUBMISSION.md) is still in progress; participant and repository details must be completed before submission.
