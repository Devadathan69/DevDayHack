# Change log

Record each meaningful project change during the event, newest first. Update this file in the same commit as the change. Use a real timestamp with timezone, for example `YYYY-MM-DDTHH:MM:SS+05:30`.

## Event build log

## 2026-10-11T07:59:36+05:30 — Resolve GTA bridge-token discovery

- Changed: The native plugin now resolves Local AppData with `SHGetFolderPathW`, validates token-file reads, and logs safe failure reasons. The native build links Shell32.
- Why: Server diagnostics showed the GTA process sent no bridge-token header at all, even though the token file was valid for the server account.
- Validation: `npm run build:native` succeeded and produced a 370,688-byte x86 ASI.
- Limitations: The updated ASI must replace the current game-folder file and the game must restart before authentication can be verified.

## 2026-10-11T07:50:57+05:30 — Diagnose rejected bridge token length

- Changed: Authentication rejection logs now report whether a bridge-token header arrived and its length, without logging token contents.
- Why: The rebuilt ASI is installed, but its NPC selection still receives HTTP 403; distinguishing an absent header from a stale token will identify the remaining issue.
- Validation: Syntax check pending; retry after restarting the backend.
- Limitations: The server must restart to load the diagnostic, then GTA must retry NPC selection.

## 2026-10-11T07:47:14+05:30 — Fix WinHTTP bridge header handling

- Changed: The native bridge now terminates authentication and content-type headers with CR/LF, checks `WinHttpAddRequestHeaders` results, and stops the request if header insertion fails.
- Why: Server diagnostics showed GTA's NPC selection request arrived without valid bridge authentication.
- Validation: `npm run build:native` succeeded and produced a 369,152-byte x86 ASI; `git diff --check` passed.
- Limitations: Windows denied automatic replacement in the game folder. The rebuilt ASI must be copied over the game-folder copy and GTA restarted to verify selection.

## 2026-10-11T07:44:14+05:30 — Log backend NPC selection failures

- Changed: The server now logs whether a game NPC selection is rejected for bridge authentication, accepted, or fails validation.
- Why: Windows denied write access to the GTA folder, preventing installation of the native diagnostic build; server logs can still distinguish the request path without replacing the plugin.
- Validation: Pending server restart and in-game retry.
- Limitations: A retry is needed to capture the result. Logs contain only the request outcome/error, not bridge tokens or conversation text.

## 2026-10-11T07:42:44+05:30 — Log native NPC selection failures

- Changed: The game bridge now records the HTTP status and safe server error text when NPC selection fails, and logs a missing display name separately.
- Why: The overlay's generic preparation message did not distinguish a rejected server request from a transport or response-parsing failure.
- Validation: `npm run build:native` succeeded and produced `native/build/MalayalamNpcBootstrap.asi` (368,640 bytes); `git diff --check` passed.
- Limitations: The diagnostic ASI has not been installed. GTA must be closed before replacing the loaded plugin; then one retry is needed to capture the specific failure.

## 2026-10-11T05:55:06+05:30 — Discover Visual Studio Insiders for native builds

- Changed: Native build discovery now includes prerelease Visual Studio instances; README setup notes the supported stable and Insiders installations.
- Why: The installed x86/x64 compiler was under Visual Studio Insiders, which `vswhere` omitted without its prerelease option.
- Validation: `npm run build:native` succeeded with the x86 MSVC toolset and produced `native/build/MalayalamNpcBootstrap.asi` (367,616 bytes). The loader and game executable were present and GTA was closed. Installer and a direct temporary-file write to the supplied game directory both failed with `FileNotFoundException`; no game files were changed.
- Limitations: The ASI is built but not installed; in-game loading has not been verified.

## 2026-10-11T04:45:01+05:30 — Connect dashboard voice controls to live speech

- Changed: Added a same-origin voice preview endpoint that accepts only server-configured voice references. The dashboard can preview the selected voice; mood and speed now map to per-request ElevenLabs voice settings for companion and generated game NPC speech. Updated the voice panel and its API payload expectation.
- Why: Make voice settings in the admin interface affect actual output and allow a short sample before publishing.
- Validation: `npm run check` and `git diff --check` passed. No automated tests or live provider calls were run.
- Limitations: Preview explicitly makes a provider request; GTA still assigns voices from its configured gender pools, then applies the dashboard's mood and speed settings.

## 2026-10-11T04:38:12+05:30 — Integrate dashboard with latest NPC voice and pedestrian systems

- Changed: Merged current `main` native hold-to-talk, dynamic pedestrian personas, gender-aware voices, and unlimited-turn behavior into `dashboard`. Preserved the authenticated admin UI and server persistence, exposed safe configured voice choices, and applied published Manu dialogue/behavior/personality controls to generated game pedestrians.
- Why: Keep the dashboard usable with the current game and voice runtime instead of losing the admin interface when updating from `main`.
- Validation: `npm run check`, `git diff --check`, package JSON parsing, and conflict-marker scan passed. Automated test suites were not run.
- Limitations: Dashboard profile voice choices apply to companion profiles. In-game pedestrians use the server's configured gender-aware voice pools. Local live-provider credentials and actual gameplay were not exercised in this integration.

## 2026-10-11T04:20:46+05:30 — Deliver distinct voices and native hold-R speech-to-text

- Changed: Integrated the reversible NPC stand/facing utility into the main game-thread callback. Enter/F6 prepares the held NPC and loads a scoped original persona without a paid greeting. Added stable named male/female voice pools, model-gender priority and safe neutral fallback; traits remain independent of gender. Actual account IDs stay only in ignored `.env` and public selection responses expose names, not IDs.
- Changed: Added explicit hold-R WinMM capture on the overlay UI thread, bounded in-memory PCM/WAV, release-to-STT `POST /api/turn` with bridge authentication, recognized-text display and the existing spoken-subtitle pipeline. Tap-R/typing, Escape, lost focus, invalid selection and raw-capture cleanup are handled; input is capped at 15 seconds for memory/device safety. No CJ mission/cutscene translation/dubbing was added after the user chose STT instead.
- Changed: Removed the default 25-turn application cap (`LIVE_TURN_LIMIT=0`, remaining=null), retaining optional positive opt-in limits, provider quotas/errors and accurate companion labels. Updated setup, test commands, status, integration references and submission documentation.
- Why/diagnosis: Civil-type-only and explicit-animation-only classification missed a real female model (64, role type 22, animation `pro`). Metadata-only live logs identified the gap; strictly recognized stock gender-coded model stems now supplement civil/animation metadata. Installed metadata recognises 163 male and 74 female labels. Unknown/custom conventions are not guessed from appearance or occupation. Pool selection decodes slot/flags/epoch; `handle % 4` would otherwise make identical slot flags choose one voice for all NPCs.
- Affected: Backend config/voice resolution/profile/registry/session/provider/server modules and tests; companion budget display; native bootstrap, metadata/interaction/signature/microphone helpers, native build/test wiring and tests; sanitized environment example, README, integration guide, plan and submission status.
- Validation: `npm test` passed 70 offline tests, including realistic sixteen-male-handle distribution, stable reselection, corrected female metadata/voice, cached-correction memory/stale cancellation, authenticated WAV STT, more than 25 turns and companion unlimited labels. `npm run check`, local documentation links, IDE/native diagnostics and diff checks passed. Native tests passed hold/restoration/fault/stale-slot checks, stock gender parsing, installed player/pool/standstill signatures and synthetic microphone chronology/bounds/partial flush/cancellation/restart/failures without opening a microphone. GPU hosts passed 63/64 checks; five distinct production audio workers passed using an existing authorized MP3 fixture, which was subsequently deleted.
- Deployment/live verification: Final `npm run build:native` produced a 367,616-byte x86 ASI; `npm run install:native -- -GameDirectory '..\Grand Theft Auto San Andreas'` copied only this plugin and matched its hash. Read-only account lookups confirmed access/gender labels for all five approved voices without synthesis. Free live bridge checks confirmed all four named male assignments, stable reselection, model-female preference and uncapped status. Game logs confirm metadata/task/main-thread holding/render startup. After correcting the diagnosed stock category, the user confirmed the female voice and hold-R recognition/text/spoken-reply round trip.
- Limitations: Default Windows input/desktop microphone permission is required; permanently broken drivers may retain ring memory for safety. Custom/unlabelled assets can retain neutral fallback, selection is nearest eligible rather than camera-center targeting, and identity remains process/scene/pool scoped. Mouse/gamepad suppression, exhaustive gameplay/other-mod compatibility and pronunciation across voices are not comprehensively verified. Provider usage remains billable; credentials/IDs were not committed. Human functional confirmation received; source review pending.

## 2026-10-11T02:53:24+05:30 — Checkpoint reversible pedestrian hold and game-thread hooks

- Changed: Added the native pedestrian hold utility and x86 regression fixtures, plus a Direct3D game-frame callback that runs on the device-owning thread and remains available for restoration after renderer shutdown. Native build/test scripts compile the utility and expose optional `-Renderer` smoke checks.
- Why: Conversation participants must stop walking, face CJ and resume safely without replacing/deleting their existing game-owned AI tasks. Game memory changes cannot run on HTTP or overlay worker threads.
- Validation: `npm run test:native -- -GameExecutable '..\Grand Theft Auto San Andreas\gta-sa.exe' -Renderer` passed hold/facing/restoration, recycled-slot/model/area/range/vehicle/death exclusions and fault rollback; the GPU hosts passed 63/64 checks including callback thread/helper/shutdown behavior. The working-tree integration also built successfully and the user reported interaction working, but that unfinished integration is not part of this utility checkpoint.
- Limitations/state: Per-model gender correction, named male/female voice pools, removal of the default turn cap and newly requested hold-R speech-to-text are still being integrated and are not claimed verified by this checkpoint. Existing native/voice integration edits remain in the working tree; source review pending.

## 2026-10-11T01:52:48+05:30 — Deliver nearby in-game interaction with repeatable spoken subtitles

- Changed: Added a native Direct3D9 in-frame popup/subtitle renderer with state restoration, Reset handling, actual presentation acknowledgement and GPU smoke hosts. Added guarded support for the installed 2014 executable's ASLR-relocated player/pool references while retaining the classic 1.0 US gate. Nearest living/on-foot/same-area selection is pinned to a generation-bearing pool handle and scene epoch; scene loading is transient, and leaving range, removal, vehicles, Escape or focus loss cancels the conversation. F6/Enter opens typed input without an automatic paid greeting; held send keys are debounced.
- Changed: Speech starts after subtitle presentation and stays paired with it. All WinMM/MCI open/play/status/close and MP3 replacement now execute on the persistent overlay-owner thread. Failures show actionable subtitle-only warnings, and undelivered turns are cancelled rather than falsely acknowledged. Added reproducible native tests, explicit ASI install/hash verification, and updated setup, status and submission documentation.
- Why: The original external window could not reliably overlay fullscreen, the installed executable had unsupported fixed addresses, and successive replies lost speech. Live diagnosis found ASLR at runtime base `0xB00000`, stopped MP3 file locks, and thread-local MCI aliases (cross-thread close error 263). A file-close-only attempt passed same-thread tests but failed user retesting; the final owner-thread dispatcher addresses both causes.
- Affected: `native/src/bootstrap.cpp`, new renderer/layout/interaction/audio headers and renderer source, native build/install/test scripts, interaction/audio-owner/GPU tests, `package.json`, README, integration guide, implementation plan and submission status.
- Validation: `npm run check`, related Markdown link checks, IDE diagnostics and `git diff --check` passed. `npm run build:native` produced the final 248,832-byte x86 ASI; `npm run install:native -- -GameDirectory '..\Grand Theft Auto San Andreas'` copied only that plugin and matched SHA-256. Factory/fallback GPU smoke hosts passed 59/60 checks, including pixels, blending, state restoration, Reset, concurrency and presentation counters. `$audio = Join-Path $env:TEMP 'MalayalamNpcDemo\npc_reply.mp3'; npm run test:native -- -GameExecutable '..\Grand Theft Auto San Andreas\gta-sa.exe' -AudioFile "$audio"` passed eligibility/identity/timer/ASLR/installed-file checks, reproduced both MCI hazards, and passed five distinct production-path turn workers plus cancellation/stale requests without new provider calls.
- Live validation: After local credential correction, one explicitly authorized speech-only ElevenLabs request returned 30,555 bytes of verified MPEG audio (no OpenAI request); Windows MCI opened and played it (1,910 ms). The final game launch had a real window and logged recognized runtime fingerprints and actual Direct3D game frames. The user confirmed the nearby popup and two consecutive audible replies with matching readable subtitles after the final install. Earlier unsuccessful attempts were not treated as success.
- Limitations: Typed in-game input only; microphone PTT remains browser-only. Pedestrians share Manu's persona/voice, continue their own AI, and pool identity is not persistent across game restarts. Mouse/gamepad suppression, broader gameplay edge cases, pronunciation assessment and other-mod/executable compatibility remain unverified. Keep the backend running; no promise is made that the verification process survives session shutdown. Human functional confirmation received; source-code review pending.

## 2026-10-11T01:19:48+05:30 — Repair native bridge lifecycle and speech failure reporting

- Changed: Backend conversation routes now have testable server construction, idempotent scoped selection, isolated pedestrian/scene memory, cancellation/deselection, authenticated audio delivery, and strict matching delivery acknowledgements. Provider requests respect cancellation; ElevenLabs failures expose only allowlisted actionable reason codes rather than arbitrary provider messages.
- Why: In-game interactions must not apply an old pedestrian's reply/audio to a new selection or silently claim speech succeeded when the provider rejects it.
- Affected: `backend/src/server.js`, `backend/src/session-manager.js`, `backend/src/npc-controller.js`, `backend/src/providers/live-providers.js`, and their provider/memory/new HTTP route regression tests.
- Validation: `node --test backend\test\live-providers.test.js backend\test\server.test.js backend\test\sqlite-memory-store.test.js` passed 39 tests with stub providers. `npm run check` and `git diff --check` passed. The live backend responded HTTP 200 after restart. Two user-authorized live turns returned Malayalam dialogue; ElevenLabs TTS failed with HTTP 401. An intentionally unrendered false/false delivery was rejected with HTTP 400; selection/audio were then cleared by deselect, so no game delivery was claimed.
- Limitations: The user chose to correct the ElevenLabs key/account locally; successful live speech is still unverified. Native fullscreen/nearby interaction changes are a separate in-progress milestone; these backend checks are not proof of a working GTA demo. Human review pending.

## 2026-10-11T00:44:17+05:30 — Attempt in-game overlay verification

- Changed: Updated README, plan, integration status, changelog, and AI usage notes with the first game-launch result. Stopped a stale project backend process that was still running the removed `--mock` mode.
- Why: User asked to confirm the overlay on the game screen and had previously requested mock mode be removed completely.
- Validation: Launched the installed `gta-sa.exe` through the desktop app integration. The game bridge log records ASI initialization, an unsupported-executable/manual-chat fallback, and overlay initialization. The process remained alive but Windows reported `MainWindowHandle=0`; the desktop window list exposed no GTA window, so no screenshot or visible overlay check was possible. Confirmed the old mock process exited. No backend conversation or provider request was made; no game files or security settings were changed in this verification step.
- Limitations: Automatic pedestrian proximity is disabled for this executable. Startup logging does not prove the overlay is visible. F6/Enter, subtitles, Malayalam text, audio, and bridge behavior remain unverified until GTA exposes a visible foreground window.

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
