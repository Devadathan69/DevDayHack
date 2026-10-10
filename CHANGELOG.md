# Change log

Record each meaningful project change during the event, newest first. Update this file in the same commit as the change. Use a real timestamp with timezone, for example `YYYY-MM-DDTHH:MM:SS+05:30`.

## Event build log

## 2026-10-11T00:38:26+05:30 — Remove offline mock runtime

- Changed: Made the companion always use OpenAI and ElevenLabs adapters with SQLite memory. Removed the mock provider, in-memory store, terminal mock demo, mock UI states, `NPC_MODE`, and separate `start:live`/demo commands. `npm start` is now the only server start command. Updated README, integration/submission instructions, and project plan.
- Why: User requested complete removal of mock mode so the app cannot silently serve synthetic dialogue.
- Validation: `npm run check` passed. Source search found no mock runtime references outside the explicit rejection of the obsolete `NPC_MODE`, `--mock`, and `--live` settings.
- Limitations: Every app conversation now requires valid provider credentials and may make provider API calls. No provider-backed conversation was run as part of this change; the full test suite was not run.

## 2026-10-11T00:27:30+05:30 — Implement the in-game typed conversation overlay

- Changed: Replaced lifecycle-only behavior with a GDI+ transparent overlay, foreground-only F6/Enter interaction, typed turns, subtitles, optional reply audio, cancellation, and a version-gated pedestrian proximity scan. Added a random local bridge token and route-scoped backend authentication. Updated `README.md`, `docs/game-integration.md`, and `plan.md`. Refreshed only the project's `MalayalamNpcBootstrap.asi` in the game root; the existing loader and other game files were left untouched.
- Why: Continue the partial native integration toward the requested near-NPC prompt and reply flow while disabling game-memory reads for an executable that does not match the supported layout fingerprints.
- Validation: `npm run build:native` succeeded (214,016-byte x86 ASI); `npm run check` and `git diff --check` passed. The installed ASI SHA-256 matches the build output (`C80FE29CA72A33B2B978313099EE3FB01CFB2600523451FEEF364FCCC2F6BAB5`). The existing x86 ASI loader matched the official release sidecar in the earlier inspection. The game's entry-point RVA `0x458DB8` does not match the recognized GTA SA 1.0 US layouts.
- Limitations: The game was not launched, so loader startup, overlay, keys, subtitles, Malayalam shaping, audio, and proximity behavior remain unverified. Automatic proximity is disabled for the installed executable; the compiled fallback is manual F6/Enter chat. A bridge smoke attempt was rejected by desktop policy before the test process started, so no authenticated backend round trip was verified. The in-game adapter accepts typed text only; microphone push-to-talk remains in the browser.

## 2026-10-10T23:26:00+05:30 — Record executable compatibility fingerprint

- Changed: Added the PE timestamp and entry-point fingerprint to `docs/game-integration.md`, including the adjacent installer-script version clue and the comparison against Plugin-SDK's official game-version detector.
- Why: User asked to select the closest target and try game integration; the executable's own version resource is blank, so the target must be checked against known signatures before adding gameplay hooks.
- Validation: Read the executable and `installscript.vdf` without modifying them; parsed x86 PE headers; compared the normalized entry-point value with the Plugin-SDK detector source; `git diff --check` passed.
- Limitations: The executable entry point matches none of the detector's listed San Andreas builds. No target was selected, no gameplay integration was attempted against an unknown binary, and the prior game-folder write was blocked by desktop policy. No game files were changed.

## 2026-10-10T23:02:18+05:30 — Add runnable voice companion and persistent memory

- Changed: Added a localhost browser companion, OpenAI Responses and ElevenLabs Scribe v2/Eleven v4 adapters, bounded PTT capture and turn handling, subtitle/audio delivery controls, local SQLite memory with scope isolation/reset, configuration/doctor commands, and deterministic adapter/storage checks. Updated README, integration guide, implementation status, submission notes, and AI usage record.
- Why: Implement the planned end-to-end voice and memory path before attaching it to the game process.
- Validation: `npm run check` passed. `npm test` passed five stubbed provider-adapter/SQLite checks. Mock server completed a two-turn Malayalam recall conversation over localhost HTTP. Live-mode configuration and page startup returned HTTP 200; no paid provider generation request was sent. The existing `.env` was read without printing values; OpenAI model, ElevenLabs model, and Malayalam-labeled voice availability were confirmed using read-only account endpoints.
- Limitations: No live transcription/dialogue/audio generation was exercised; microphone permission and Malayalam pronunciation are unverified. The ASI remains a lifecycle logger and does not connect to GTA. The executable/version/SDK target is unresolved, so there is no in-game binding or game-tested key input. Hotkeys currently work only while the companion browser is focused.

## 2026-10-10T22:30:09+05:30 — Build native lifecycle smoke plugin

- Changed: Added a dependency-free x86 ASI bootstrap and Visual Studio build script under `native/`; ignored native build artifacts; updated the README and integration guide with the current preflight and plugin status; recorded the actual Codex assistance.
- Why: Begin the native integration in the guide's safe order with a no-op lifecycle plugin before using game APIs or adding hooks.
- Validation: `native/build.ps1` built a 120,832-byte x86 ASI. `dumpbin` confirmed the x86 machine type and `InitializeASI`/`ShutdownASI` exports. An x86 host process loaded the plugin and called both exports successfully. The official Ultimate ASI Loader v9.7.4 x86 archive matched the release SHA-256. The attempted copy into the external game folder was rejected by the desktop tool policy; no game files were changed.
- Limitations: The plugin has not been loaded by the game's ASI loader. The executable's version fields are blank and its filename differs from the adjacent install script; gameplay compatibility, backend transport, live providers, memory persistence, and in-game NPC behavior remain unimplemented.

## 2026-10-10T22:10:50+05:30 — Verify provider credentials and document game setup

- Changed: Added `docs/game-integration.md`, linked it from the README, and recorded the local provider-authentication result in project records.
- Why: Give the user a safe, ordered path from the current companion/backend scaffold to a future GTA plugin while distinguishing credential validity from a live conversation.
- Validation: Read-only `GET /v1/models` (OpenAI) and `GET /v1/user` (ElevenLabs) requests using `.env` returned HTTP 200. No key values were printed; no generation endpoint or game process was used. Reviewed Plugin-SDK and Ultimate ASI Loader primary documentation. Markdown local-link check and `git diff --check` passed.
- Limitations: The CLI remains mock-only; no live provider adapter, native plugin, executable compatibility record, or in-game demo exists yet.

## 2026-10-10T22:08:22+05:30 — Add the mock NPC backend slice

- Changed: Added a dependency-free Node.js CLI, one validated original NPC profile, reusable controller/registry/session management, configurable mock speech/dialogue providers, and a bounded in-memory scoped memory store. Updated README, plan status, and ignored local audio/database artifacts.
- Why: Start with the plan's provider-independent backend milestone while Windows game compatibility and live provider choices remain unverified.
- Validation: `npm run check` passed JavaScript syntax checks. `npm run demo` completed a two-turn Malayalam recall walkthrough using piped input. `git diff --check` passed.
- Limitations: Mock-only; no live microphone/TTS, paid providers, SQLite persistence, web companion, native plugin, or deterministic unit tests yet. Memory is lost when the CLI exits.

## Template setup — 10 October 2026

- Changed: Added the event README, rules, agent guide, change log, AI usage record, submission checklist, Git ignore rules, and pull request checklist.
- Why: Give participants a consistent starting point for event rules, basic coding patterns, documented changes, and regular commits.
- Validation: Checked local Markdown links, event detail consistency, required documents, and diff whitespace before publishing.
- Limitations: Documentation scaffolding only; no participant project, application, or working demo is included. GitHub publication and template status are verified separately after creation.
