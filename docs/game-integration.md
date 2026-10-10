# GTA San Andreas integration status

## Implemented in the current ASI

`native/src/bootstrap.cpp` builds a 32-bit ASI. GDI+ rasterizes the popup/input/Malayalam text; `native/src/d3d-overlay.cpp` uploads premultiplied BGRA pixels and composites them inside the game's Direct3D9 frame, rather than relying on a layered desktop window. The module hooks the Direct3D factory/device vtables and preserves the game's render state; default-pool resources are released for device Reset. The backend remains on `127.0.0.1`, with a random local bridge token accepted only on conversation routes and never exposed to the browser.

The nearest eligible pedestrian triggers the popup within 3.5 units. Dead pedestrians, vehicle occupants, other areas and different floors are excluded. F6/Enter pins the pedestrian's pool address and generation-bearing handle and opens typed input. Leaving 5 units, entering a vehicle, scene unload, recycled/removed selection, Escape or loss of focus cancels the conversation and speech. Null player/pool pointers during menus are transient, not permanent compatibility failures. Unsupported code fingerprints never enable manual proximity claims.

The selected pool handle plus scene-unload epoch is sent as `sceneGeneration` with `gameSessionId: gta-<pid>`; repeated selection is idempotent, different pedestrians/scenes have isolated memory and stale turns cannot acknowledge a new selection. Speech starts after the subtitle frame is confirmed by the renderer. Subtitle duration follows actual audio duration/playback, with a text-only warning on audio failure. No model output becomes a game command. Typed input is supported in-game; microphone PTT remains browser-only. All eligible pedestrians currently share the original Manu persona/voice; pool identity is not stable across game restarts.

The source project for the address/version conventions is [Plugin-SDK](https://github.com/DK22Pac/plugin-sdk), including its [GTA executable detector](https://github.com/DK22Pac/plugin-sdk/blob/master/injector/injector.hpp#L2987-L3112), [`CPools::ms_pPedPool`](https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_sa/game_sa/CPools.cpp), and [`CPool` layout](https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_sa/game_sa/CPool.h). The detector lists the supported GTA SA 1.0 US entry points as `0x824570` and `0x82457C`; the ASI checks their preferred-base RVAs before touching game memory. The installed 2014 layout is additionally gated by image size and two exact instruction fingerprints that reference the player-info array and pedestrian pool (including the 0x7C4 stride). Code/global addresses and embedded pointer operands are resolved against the actual runtime module base: this executable was observed at `0xB00000`, not its preferred `0x400000`, due to ASLR. The classic 1.0 US fixed-address path remains restricted to its original base. The player-info layout and vehicle/health field conventions were cross-checked against [SilentPatch's player bindings](https://github.com/CookiePLMonster/SilentPatch/blob/master/SilentPatchSA/PlayerInfoSA.cpp) and [Plugin-SDK CPed](https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_sa/game_sa/CPed.h). No external project or game executable was imported; only layout conventions were consulted.

## Installed executable and compatibility

Read-only inspection found a 5,971,456-byte x86 PE named `gta-sa.exe`. Its `FileVersion`, `ProductVersion`, and `OriginalFilename` fields are blank. The PE timestamp is 1 December 2014 and its entry-point RVA is `0x458DB8` (preferred-base address `0x858DB8`). The adjacent `installscript.vdf` contains a `1.00.00001` registry key but refers to `gta_sa.exe`; that clue does not establish the executable version. The user reports this is an official copy. The entry point matches none of Plugin-SDK's listed targets, so the earlier entry-point-only reader disabled detection. The current implementation recognizes this exact layout with additional verified player/pool code fingerprints. Its SHA-256 is `AA07FCDC8723A2831F77A35491DEA6CDD58F6733E3AC69A10DDF5CDF95034FE3`; this identifies the checked local file, not its licensing or compatibility with other mods.

The root's existing `dinput8.dll` is Ultimate ASI Loader v9.7.4 x86 (5,418,336 bytes; previously matched against its official SHA-512 sidecar). The game imports `DINPUT8.dll`, `D3D9.dll`, and `XINPUT1_3.dll`; the existing loader and all other game files are left untouched. An earlier launch verified ASI loader startup but exposed no targetable game window; that earlier result is not evidence for the new in-frame renderer. A loaded save/new game and real pedestrian approach are required for runtime verification.

## Build and run

Build with the installed Visual Studio x86 C++ tools:

```powershell
npm run build:native
```

The generated file is `native/build/MalayalamNpcBootstrap.asi`. Close GTA and explicitly install that file, then start the backend before launching the game:

```powershell
npm run install:native -- -GameDirectory '..\Grand Theft Auto San Andreas'
npm start
```

Use the correct game directory on other installations. The install script copies only this project's ASI, requires an already installed x86 loader, and verifies SHA-256. Restart the game to load the new ASI; keep the backend terminal running. Avoid browser and GTA conversations at the same time because the backend has one active session.

The backend writes its bridge token under the current user's local application data. Keep `.env`, the token, database, logs, and temporary audio outside the game folder and repository. `UI_PORT` can change the port; the ASI reads it from the token file. In-game replies use OpenAI and ElevenLabs and require the provider credentials in `.env`.

Runtime diagnostics are appended to `%LOCALAPPDATA%\MalayalamNpcDemo\logs\game-bridge.log` in UTF-16. Separate messages identify successful code fingerprints, a visible game window and actual Direct3D game frames; timer/plugin initialization alone does not prove an on-screen popup. No transcripts or bridge credentials are logged. The install/build and gameplay results for this revision are recorded in CHANGELOG.md.

## In-game controls

- Load a save/new game. Approach an eligible pedestrian; **Interact with Manu [F6 / Enter]** appears within 3.5 units on either recognized layout.
- Press **F6** or **Enter** once, type, then press **Enter** to send; no automatic greeting/provider call is made on selection.
- **Backspace** edits; **Escape** cancels speech/turn and leaves. Held interaction/send keys are debounced.
- The keyboard hook acts only while GTA is foreground; Windows, Alt+Tab and Alt+F4 remain available. Mouse/gamepad controls and pedestrian AI are not modified.
- Keep the selected pedestrian within 5 units. Reply subtitles accompany the audio and remain until playback finishes. Voice failure is visibly reported rather than claimed as success.

## Validation and remaining work

Run `npm run check`, `npm test`, `npm run test:native`, and `npm run build:native`. Optional read-only local-file validation: `npm run test:native -- -GameExecutable '..\Grand Theft Auto San Andreas\gta-sa.exe'`. Backend tests use stub providers, not an offline/demo runtime. The native tests check eligibility/range, slot generation, nearest-distance ordering, wrap-safe timers and rejected fingerprints; the supplied installed executable passes both exact code signatures. Final commands/results, renderer smoke evidence and copied-file verification are recorded in CHANGELOG.md.

Initial live dialogue succeeded while ElevenLabs returned HTTP 401. The user corrected local credentials; one authorized speech-only request then returned 30,555 bytes of verified MPEG Layer III audio, and Windows MCI playback succeeded (1,910 ms). No OpenAI request was made during that speech-only check. Restart the backend after changing `.env`; never put provider keys in chat or logs.

Gameplay exposed two additional native failures that offline/single-turn checks missed: the executable is ASLR-relocated, and MCI aliases belong to their opening thread. The first address-base fix enabled real proximity; the first file-close fix alone did not solve repeated speech because successive HTTP workers could not resolve each other's MCI aliases (error 263). All MCI open/play/status/close and MP3 replacement now run on the persistent overlay-owner thread. An x86 test exercises the actual production dispatch from five distinct workers and checks cancellation/stale requests. The user confirmed the popup and two consecutive spoken, subtitled replies after installing the final build. The copied 248,832-byte ASI hash matches the build; logs confirm recognized runtime fingerprints and actual game frames.

The core popup/typed reply/speech/subtitle walkthrough is user-confirmed. For broader coverage, additionally verify removal/leaving range, Escape/Alt+Tab, selecting a second pedestrian, provider failures, resolution changes and fullscreen behavior across other supported machines/mod combinations. Pedestrian AI continues moving; no mouse/gamepad suppression or persistent persona assignment across game restarts is implemented. For removal, close GTA and remove only `MalayalamNpcBootstrap.asi`; do not remove saves, other plugins, executables or existing proxy DLLs.

## GPU smoke tests

From an **x86 Visual Studio Developer PowerShell**, at repository root:

```powershell
Set-Location native\build
cl.exe /nologo /MT /EHsc /std:c++17 /W4 /WX /DUNICODE /D_UNICODE /Fe:d3d-overlay-smoke.exe ..\test\d3d-overlay.test.cpp ..\src\d3d-overlay.cpp /link /MACHINE:X86 d3d9.lib user32.lib
.\d3d-overlay-smoke.exe
cl.exe /nologo /MT /EHsc /std:c++17 /W4 /WX /DUNICODE /D_UNICODE /DOVERLAY_FALLBACK_ONLY /Fe:d3d-overlay-fallback-smoke.exe ..\test\d3d-overlay.test.cpp ..\src\d3d-overlay.cpp /link /MACHINE:X86 d3d9.lib user32.lib
.\d3d-overlay-fallback-smoke.exe
```

The factory and fallback hosts passed 59 and 60 checks respectively: actual backbuffer pixels/blending, state restoration, Reset, helper-device exclusion, concurrent frame submissions and presentation counters. These tests require working local Direct3D9 hardware/runtime and never call providers or modify GTA. The fallback alone cannot discover a device with a private vtable created before interception; factory interception is the primary path.

Optional production audio-owner regression, using an existing MP3 and no provider call:

```powershell
npm run test:native -- -AudioFile 'C:\Temp\test-reply.mp3'
```

Keep generated speech fixtures outside the repository; no fixture is bundled.
