# Integrate the companion with GTA San Andreas

This is the planned Windows integration procedure for the **classic PC** game. It is an implementation guide, not a claim that this repository already has a native plugin. Today, the repository has a mock CLI and backend only: it does not build an ASI, inspect game entities, send bridge messages, or alter the game installation.

The local `.env` was checked on 10 October 2026: OpenAI and ElevenLabs returned HTTP 200 to authentication/account-read requests. Those checks did not generate text or audio. The current CLI does not load these variables and still uses mock providers.

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

## 2. Build and validate a harmless ASI first

1. Follow the [Plugin-SDK setup](https://github.com/DK22Pac/plugin-sdk/wiki/Set-up-plugin-sdk) and [new-plugin guide](https://github.com/DK22Pac/plugin-sdk/wiki/Creating-a-new-plugin-in-plugin-sdk). Select the GTA San Andreas target and build architecture that match the verified executable. Plugin-SDK lists San Andreas among its supported game families, but the installed executable still needs a compatibility check.
2. Build a no-op plugin that writes a short startup/shutdown line to a project-owned log outside the game folder. It must not hook gameplay or touch entities yet.
3. Implement initialization outside `DllMain`/the Windows loader lock. Ultimate ASI Loader documents the `InitializeASI` callback for this; its default plugin-load behavior is outside `DllMain`. Keep initialization short and start any worker only after a valid plugin initialization callback.
4. Confirm the SDK output architecture and plugin target from the build artifacts before installing anything.

References: [Plugin-SDK README](https://github.com/DK22Pac/plugin-sdk) · [Ultimate ASI Loader README](https://github.com/ThirteenAG/Ultimate-ASI-Loader).

## 3. Choose a loader without overwriting an existing proxy

The [Ultimate ASI Loader](https://github.com/ThirteenAG/Ultimate-ASI-Loader) accepts `.asi` plugins and supports several proxy DLL names. The correct proxy name and architecture depend on the actual game executable and existing installation. Do not rename a downloaded proxy to `dinput8.dll` (or another name) just because a generic guide says to; first check the loader's current supported-name list and check whether that DLL already belongs to another mod or wrapper.

Before installing, make a manifest of only the files this project will add or replace. Back up any file that must be replaced. If an existing loader is present, determine how it loads plugins and test the no-op plugin through that setup before adding another loader. Never overwrite an unknown proxy DLL.

## 4. Add the native adapter in small steps

The future native component belongs in `native/` and should be a thin game-side event bridge. It should not capture microphone audio, call OpenAI/ElevenLabs, access SQLite, or wait on network I/O from the game thread.

Implement in this order:

1. **Startup and shutdown:** log plugin lifecycle and leave the game unchanged when the backend is absent.
2. **Backend connection:** connect from a worker to a backend bound only to `127.0.0.1`. Use the versioned UTF-8 newline-delimited JSON protocol described in [plan.md](../plan.md#7-local-bridge-and-security), bounded message sizes, a local token, and reconnect backoff. Do not put provider API keys in the game process.
3. **Readiness:** send a versioned `hello`; remain unavailable for conversation until the backend and companion both report ready. Reconnect must never resume an old turn.
4. **Single NPC binding:** configure `demo-npc-001` and one repeatable scene/location. Bind it explicitly to a currently validated game entity. Keep the application ID separate from the runtime entity handle, and attach a scene generation to events. Do not use a recycled pointer/handle as permanent identity or select a random nearest pedestrian as durable identity.
5. **Configurable interaction input:** read `INTERACTION_KEY` and `PTT_KEY` from the plugin's local config. Suggested defaults are `E` to open/select the nearby NPC and hold `V` to record; confirm no conflicts on the verified game setup. Detect press/release edges to avoid duplicate events. On PTT release, focus loss, target loss, save/load, or scene transition, stop capture/work and invalidate the turn.
6. **In-game dialogue box:** render a separate panel from the game-side UI hook. The backend sends validated `dialogue.reply` text to the native bridge; queue it on the network worker and draw it on the game/render thread. Display the reply as soon as text arrives, without waiting for ElevenLabs audio. Test Malayalam rendering early; if the game's built-in font fails, use a custom Direct3D overlay/font renderer.
7. Emit validated `npc.selected`, `ptt.started`, `ptt.stopped`, `npc.invalidated`, `scene.changed`, and backend-to-native `dialogue.reply` events; never accept model-generated game commands. The companion remains responsible for microphone capture and audio playback.

The backend should verify protocol version, session/scene generation, configured NPC identity, readiness, and message limits before accepting any event. Native queues must be bounded; status updates can be coalesced. Provider results for cancelled or stale generations must be discarded.

## 5. Connect components and test away from live gameplay first

1. Start the backend and companion and confirm they report their actual mode. Keep `.env` and all provider credentials in the backend environment only.
2. Test the protocol with synthetic messages before loading the plugin. Verify duplicate events, malformed JSON, oversize frames, disconnection, cancellation, and scene-generation changes.
3. Install only the no-op plugin and verify the game launches and shuts down normally. Inspect loader logs and the project-owned plugin log.
4. Add target selection and entity invalidation, still with providers disabled. Verify that an invalid/despawned target cancels interaction without affecting the game.
5. Only after those steps pass, connect the configured interaction/PTT keys to the selected NPC and test a mock turn with the separate in-game dialogue box.
6. Add live providers after the backend adapters exist. Show the backend reply in the GTA dialogue box independently from audio, and label mock/live status clearly.
7. Verify memory isolation/reset and restart behavior. The current in-memory store loses conversation history on process exit; persistent recall requires the later storage milestone.

Do not claim in-game voice is working until a live exchange has been demonstrated through the verified game binding. The current mock CLI is not connected to the game.

## 6. Install, record, and remove

- Install only the project-built `.asi`, its non-secret configuration, and a loader only if this installation does not already have a compatible one.
- Keep the provider `.env`, database, logs, recordings, and temporary audio outside the game directory and source repository.
- Record exact tested executable hash, SDK revision, loader filename/version, architecture, plugin build, and installation manifest.
- For removal, close the game; remove only files named in this project's manifest; restore any backed-up file that this project replaced. Do not delete other mods, saves, or game files.

No executable, game asset, save, or third-party loader binary should be committed or bundled with this project. Check [RULES.md](../RULES.md) and the licensing/privacy discussion in [plan.md](../plan.md#11-legal-licensing-and-privacy-checklist) before a public demo.
