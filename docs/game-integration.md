# Integrate the companion with GTA San Andreas

This is the staged Windows integration procedure for the **classic PC** game. The repository now has a local browser companion, live OpenAI/ElevenLabs adapters, and SQLite memory, plus a standalone x86 ASI lifecycle smoke-test plugin in `native/src/bootstrap.cpp`. The plugin can be built with `native/build.ps1`; it only logs initialization/shutdown and does not hook game APIs, inspect entities, send bridge messages, or implement NPC behavior. Its exports passed a load check in an x86 host process. It has not been installed into the game. Live voice behavior has not been verified with a paid provider turn or microphone device.

The local `.env` was checked on 10 October 2026: OpenAI and ElevenLabs returned HTTP 200 to authentication/account-read requests. The live companion reads keys on the backend only. Its server and page start in live mode without generating text or audio; actual transcription, dialogue generation, and synthesis have not been called during this implementation.

## 1. Identify the exact game installation

Do this on the Windows machine that has the game. Do not infer executable compatibility from the label `Build 459558 | Full Version`.

In PowerShell, set `$exePath` to the installed `gta_sa.exe` path and collect metadata without modifying the game:

```powershell
$exePath = 'C:\Games\GTA San Andreas\gta_sa.exe'
$exe = Get-Item -LiteralPath $exePath
$exe | Select-Object FullName, Length
$exe.VersionInfo | Select-Object FileVersion, ProductVersion, OriginalFilename
Get-FileHash -Algorithm SHA256 -LiteralPath $exe.FullName
Get-ChildItem -LiteralPath $exe.DirectoryName -File |
  Where-Object { $_.Name -match '(?i)^(dinput8|d3d8|d3d9|dsound|winmm|version)\.dll$|\.asi$' } |
  Select-Object Name, Length
```

Record the Windows version, installation source, executable metadata/hash, and any existing ASI loader, graphics wrapper, or `.asi` files. Keep paths and hashes in a local compatibility note if you do not want them public. This information is a precondition for choosing an SDK target and loader proxy; it does not by itself prove that a hook is compatible.

### Current local preflight record

Read-only inspection on 10 October 2026 found a 5,971,456-byte x86 PE named `gta-sa.exe`. Its `FileVersion`, `ProductVersion`, and `OriginalFilename` fields are blank. The adjacent `installscript.vdf` refers to `gta_sa.exe`, so the name mismatch needs explanation; do not rename or patch the executable to make it fit a guide. The user reports that this is an official copy, but the executable version and Plugin-SDK compatibility remain unverified. No root-level `.asi` plugin or `dinput8.dll` was found. An existing `xinput1_3.dll` must not be overwritten.

The executable SHA-256 was collected for local compatibility tracking and is intentionally not published in this repository. The x86 ASI build and export check prove only that the plugin binary matches the process architecture; they do not identify the game's supported version.

### Build and inspect the no-op plugin

Run `powershell -NoProfile -File .\native\build.ps1`. This uses the installed Visual Studio x86 toolchain and writes an ignored artifact to `native/build/MalayalamNpcBootstrap.asi`. It does not require Plugin-SDK because the smoke plugin calls no GTA APIs. It appends lifecycle events under `%LOCALAPPDATA%\MalayalamNpcDemo\logs\game-bridge.log` and performs no gameplay work.

The local build was inspected as x86 and exported `InitializeASI` and `ShutdownASI`; an x86 host process loaded it and invoked both exports successfully. The in-game smoke test remains incomplete.

## 2. Build and validate a harmless ASI first

1. Follow the [Plugin-SDK setup](https://github.com/DK22Pac/plugin-sdk/wiki/Set-up-plugin-sdk) and [new-plugin guide](https://github.com/DK22Pac/plugin-sdk/wiki/Creating-a-new-plugin-in-plugin-sdk). Select the GTA San Andreas target and build architecture that match the verified executable. Plugin-SDK lists San Andreas among its supported game families, but the installed executable still needs a compatibility check.
2. Build a no-op plugin that writes a short startup/shutdown line to a project-owned log outside the game folder. It must not hook gameplay or touch entities yet.
3. Implement initialization outside `DllMain`/the Windows loader lock. Ultimate ASI Loader documents the `InitializeASI` callback for this; its default plugin-load behavior is outside `DllMain`. Keep initialization short and start any worker only after a valid plugin initialization callback.
4. Confirm the SDK output architecture and plugin target from the build artifacts before installing anything.

References: [Plugin-SDK README](https://github.com/DK22Pac/plugin-sdk) · [Ultimate ASI Loader README](https://github.com/ThirteenAG/Ultimate-ASI-Loader).

## 3. Choose a loader without overwriting an existing proxy

The [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) accepts `.asi` plugins and supports several proxy DLL names. The correct proxy name and architecture depend on the actual game executable and existing installation. Do not rename a downloaded proxy to `dinput8.dll` (or another name) just because a generic guide says to; first check the loader's current supported-name list and check whether that DLL already belongs to another mod or wrapper.

Before installing, make a manifest of only the files this project will add or replace. Back up any file that must be replaced. If an existing loader is present, determine how it loads plugins and test the no-op plugin through that setup before adding another loader. Never overwrite an unknown proxy DLL.

For the current x86 target, Ultimate ASI Loader v9.7.4's official no-PDB x86 release archive was hash-checked (`14b3a1ad018899571ac9aa01482977f3c6d49e6cba99f552d01c5acacd1315e1`). It contains `dinput8.dll`, and that filename is absent from the inspected game root. This is only a selected candidate; no loader or ASI was installed into the game during this session.

## 4. Add the native adapter in small steps

The future native component belongs in `native/` and should be a thin game-side event bridge. It should not capture microphone audio, call OpenAI/ElevenLabs, access SQLite, or wait on network I/O from the game thread.

Implement in this order:

1. **Startup and shutdown:** log plugin lifecycle and leave the game unchanged when the backend is absent.
2. **Backend connection:** connect from a worker to a backend bound only to `127.0.0.1`. Use the versioned UTF-8 newline-delimited JSON protocol described in [plan.md](../plan.md#7-local-bridge-and-security), bounded message sizes, a local token, and reconnect backoff. Do not put provider API keys in the game process.
3. **Readiness:** send a versioned `hello`; remain unavailable for conversation until the backend and companion both report ready. Reconnect must never resume an old turn.
4. **Single NPC binding:** configure `demo-npc-001` and one repeatable scene/location. Bind it explicitly to a currently validated game entity. Keep the application ID separate from the runtime entity handle, and attach a scene generation to events. Do not use a recycled pointer/handle as permanent identity or select a random nearest pedestrian as durable identity.
5. **Interaction input:** add one interaction key and push-to-talk events. On key release, focus loss, target loss, save/load, or scene transition, stop capture/work and invalidate the turn. Emit only `npc.selected`, `ptt.started`, `ptt.stopped`, `npc.invalidated`, and `scene.changed` style events; never accept model-generated game commands.
6. **Status only:** report selection/readiness to the game as a small indicator if a verified, safe game API exists. If not, keep status in the companion UI.

The backend should verify protocol version, session/scene generation, configured NPC identity, readiness, and message limits before accepting any event. Native queues must be bounded; status updates can be coalesced. Provider results for cancelled or stale generations must be discarded.

## 5. Connect components and test away from live gameplay first

1. Start `npm run start:live` and open the printed `127.0.0.1` URL. Confirm the page reports its actual mode and allow microphone access. Keep `.env` and all provider credentials in the backend environment only.
2. Test the protocol with synthetic messages before loading the plugin. Verify duplicate events, malformed JSON, oversize frames, disconnection, cancellation, and scene-generation changes.
3. Install only the no-op plugin and verify the game launches and shuts down normally. Inspect loader logs and the project-owned plugin log.
4. Add target selection and entity invalidation, still with providers disabled. Verify that an invalid/despawned target cancels interaction without affecting the game.
5. Only after those steps pass, connect the companion PTT event to the selected NPC and test a mock turn.
6. Add live providers after the backend adapters exist. Show Malayalam subtitles independently from audio, and label mock/live status clearly.
7. Verify memory isolation/reset and restart behavior. The current in-memory store loses conversation history on process exit; persistent recall requires the later storage milestone.

Do not claim in-game voice is working until a live exchange has been demonstrated through the verified game binding. The current mock CLI is not connected to the game.

## 6. Install, record, and remove

- Install only the project-built `.asi`, its non-secret configuration, and a loader only if this installation does not already have a compatible one.
- Keep the provider `.env`, database, logs, recordings, and temporary audio outside the game directory and source repository.
- Record exact tested executable hash, SDK revision, loader filename/version, architecture, plugin build, and installation manifest.
- For removal, close the game; remove only files named in this project's manifest; restore any backed-up file that this project replaced. Do not delete other mods, saves, or game files.

No executable, game asset, save, or third-party loader binary should be committed or bundled with this project. Check [RULES.md](../RULES.md) and the licensing/privacy discussion in [plan.md](../plan.md#11-legal-licensing-and-privacy-checklist) before a public demo.
