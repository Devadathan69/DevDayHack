# Malayalam NPC Demo

A hackathon prototype for a reusable Malayalam-speaking GTA San Andreas NPC. The first implementation milestone is a small mock-only backend demo; live speech, persistent storage, and game integration are later steps in [plan.md](plan.md).

## Event

DevDay Exchange Community Hackathon: Kochi, 10–11 October 2026 at JAIN University, Kochi. [Event and registration](https://luma.com/z10nz8gp) · [Rules](RULES.md).

## Current implementation

- One original character profile (`Manu`) is validated and instantiated through a reusable NPC registry/controller.
- The CLI accepts typed Malayalam as mock speech input, runs a mock dialogue response, and acknowledges subtitle delivery.
- The in-memory store scopes recent delivered dialogue by world, player, and NPC. The mock can recall the previous delivered player message when asked.
- Turn IDs, cancellation, scene invalidation, reset generations, response validation, and single-turn ownership are represented in the backend.
- Mock providers can be configured for delays, cancellation, malformed results, and stage failures for future development checks.

The CLI does **not** record or play audio. The response is explicitly labeled as a mock; memory lasts only until the process exits. There is no live OpenAI or ElevenLabs integration, SQLite persistence, companion web UI, or GTA plugin yet. The installed game executable and mod-loader compatibility have not been verified.

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

## Project details

- **Project name:** Malayalam NPC Demo
- **Short description:** A profile-driven backend foundation for talking to and remembering a Malayalam-speaking game NPC.
- **Technology:** Node.js ES modules and built-in libraries; no third-party packages in this milestone.
- **Working demo:** Run the mock CLI as described above.
- **Libraries, APIs, and datasets:** Node.js built-in modules only so far. No external API or dataset is used by the mock demo.

See [plan.md](plan.md) for the architecture and milestone gates, [CHANGELOG.md](CHANGELOG.md) for implementation history, and [AI_USAGE.md](AI_USAGE.md) for assistance records. [SUBMISSION.md](SUBMISSION.md) is still in progress; participant and repository details must be completed before submission.
