# GTA San Andreas integration status

## Implemented in the current ASI

`native/src/bootstrap.cpp` builds a 32-bit ASI. GDI+ rasterizes the popup/input/Malayalam text; `native/src/d3d-overlay.cpp` uploads premultiplied BGRA pixels and composites them inside the game's Direct3D9 frame, rather than relying on a layered desktop window. The module hooks the Direct3D factory/device vtables and preserves the game's render state; default-pool resources are released for device Reset. The backend remains on `127.0.0.1`, with a random local bridge token accepted only on conversation routes and never exposed to the browser.

The nearest eligible pedestrian triggers the popup within 3.5 units. Dead pedestrians, vehicle occupants, other areas and different floors are excluded. F6/Enter pins the pool address and generation-bearing handle, holds the selected NPC standing/facing CJ on the game's main device thread, loads its scoped persona, and opens typed or hold-R input. Leaving 5 units, entering a vehicle, scene unload, recycled/removed selection, Escape or loss of focus cancels the conversation and speech. Null player/pool pointers during menus are transient, not permanent compatibility failures. Unsupported code fingerprints never enable manual proximity claims.

The selected pool handle plus scene-unload epoch is sent as `sceneGeneration` with `gameSessionId: gta-<pid>`; repeated selection is idempotent, different pedestrians/scenes have isolated memory and stale turns cannot acknowledge a new selection. Speech starts after the subtitle frame is confirmed by the renderer. Subtitle duration follows actual audio duration/playback, with a text-only warning on audio failure. No model output becomes a game command. Typed input and native hold-R microphone PTT are supported. Native voice uploads are authenticated raw `audio/wav` POSTs to `/api/turn`; upstream STT formatting remains in the backend. Personas/traits and approved named voice choices are scoped and stable per NPC, not a per-message playlist. Pool identity is not persistent across game restarts; the browser/default Manu path remains compatible.

The source project for the address/version conventions is [Plugin-SDK](https://github.com/DK22Pac/plugin-sdk), including its [GTA executable detector](https://github.com/DK22Pac/plugin-sdk/blob/master/injector/injector.hpp#L2987-L3112), [`CPools::ms_pPedPool`](https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_sa/game_sa/CPools.cpp), and [`CPool` layout](https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_sa/game_sa/CPool.h). The detector lists the supported GTA SA 1.0 US entry points as `0x824570` and `0x82457C`; the ASI checks their preferred-base RVAs before touching game memory. The installed 2014 layout is additionally gated by image size and two exact instruction fingerprints that reference the player-info array and pedestrian pool (including the 0x7C4 stride). Code/global addresses and embedded pointer operands are resolved against the actual runtime module base: this executable was observed at `0xB00000`, not its preferred `0x400000`, due to ASLR. The classic 1.0 US fixed-address path remains restricted to its original base. The player-info layout and vehicle/health field conventions were cross-checked against [SilentPatch's player bindings](https://github.com/CookiePLMonster/SilentPatch/blob/master/SilentPatchSA/PlayerInfoSA.cpp) and [Plugin-SDK CPed](https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_sa/game_sa/CPed.h). No external project or game executable was imported; only layout conventions were consulted.

## Conversation hold, gender and microphone

Game-owned memory changes run through the game-frame callback only after verifying the window-owning main thread. The installed standstill routine and task-type signatures are checked in addition to player/pool code; its preferred address is `0x651490`, resolved for ASLR. The hold utility uses the game's standstill processing on a temporary stack task without inserting/removing game-owned AI tasks, and restores only its own bits/fields for the same validated live slot. It never restores into recycled/replaced entities; death or vehicle transitions release only owned hold bits without forcing an old walking/heading state. The reference semantics are [GTA Reversed standstill](https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/Tasks/TaskTypes/TaskSimpleStandStill.cpp) and [character freeze](https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/Scripts/Commands/CharacterCommands.cpp); no external implementation was imported.

Gender uses locally read `data/peds.ide` civil/animation tags and strictly recognized stock model-name stems, then reliable live civil types. The first classifier missed female model 64 because its role type 22 and `pro` animation label were not explicit sex fields. Live metadata-only diagnosis exposed that gap; its stock stem encodes female. The current installed file recognizes 163 male and 74 female labels, with unknown/custom conventions kept neutral rather than guessing from appearance or occupation. No game asset table is copied into the repository.

The popup shows the selected target's known gender. Metadata-only diagnostics log numeric model/type, classification and nearest eligible gender distances, never transcripts, voice IDs or provider keys. Model gender overrides generic live classifications in `/api/select`; correcting cached metadata retains memory but cancels stale old-voice work. Named pool choice decodes slot index/flags/scene epoch rather than using `handle % 4` (which would choose the same voice for typical identical slot flags). Approved names/IDs stay in local `.env`; source examples have no real IDs.

WinMM capture runs on the persistent overlay UI thread and only after an explicit held-R gesture in an active, focused chat. A tap types `r`; the Recording indicator marks actual capture. PCM16 mono/16 kHz is collected in a bounded ring and encoded as an in-memory WAV. Release uploads through STT; Escape/focus/range/scene loss discards it. A 15-second cap closes hardware but retains the clip for release. Raw input is not saved. Default-device/permission/driver failures are shown; a permanently broken driver may retain ring memory to avoid use-after-free. No microphone is opened by synthetic regression tests.

`LIVE_TURN_LIMIT=0` is unlimited application turns (status remaining is null), not unlimited provider credits. Positive limits are opt-in, and provider quotas/timeouts/error handling remain enforced. No CJ mission/cutscene dubbing was added; the user chose microphone STT instead.

## Verified world context and dashboard integration

The `dashboard` branch (`f844fac`, containing the prior `3130f95` implementation) was fetched and fast-forwarded onto main without rewriting history. Profile create/save/publish/delete were exercised in the VS Code browser using a temporary profile, then cleaned up while retaining protected Manu. Unsupported browser-native prompt/confirm dialogs were replaced by accessible in-page dialogs; pending/server-confirmed publication states and local-simulation labels are explicit. Dashboard voice previews remain opt-in, provider-backed and billable.

`native/src/world-context.cpp` resolves the installed game clock, current/target weather and measured rain from independently verified references: hours `0xBFD4C7`, minutes `0xBFD4C6`, current weather `0xD0D070`, target `0xD0D06C`, rain `0xD0D074` at the preferred file base. Actual pointers use module-base plus verified RVAs. Six relocated instruction fingerprints, PE identity and section permissions are mandatory; unverified layouts fail closed.

Live diagnosis exposed a loader-rewritten PE ImageBase: both header and module were `0xB00000`, not the file preference `0x400000`. The final reader permits only the known file base or exactly the real module base, keeps RVA calculations anchored to the known file base, and still validates all six fingerprints. A once-visible initialization retry is safe but never skips validation. Local logs confirm successful initialization and equal live bases at 07:31 IST. The user confirmed all three real world values and that the feature worked after the correction.

Location comes from the smallest containing outdoor AABB in local `data/info.zon` / `data/map.zon`, with supported labels resolved from bounded `text/american.gxt` MAIN TKEY/TDAT data. For example, zone GAN1 uses GXT key GAN, which resolves to Ganton. Missing/unsupported text remains an explicit zone code; interiors are not invented. No zone/text/asset table is redistributed. Clock values are game time, not Windows time; Windows UTC is used only to timestamp observation freshness.

Actual rain intensity controls observed rainy/transitioning conditions. The engine's next weather ID is retained separately for dashboard use and removed before brain/provider requests, avoiding false claims that a target transition has finished. The backend accepts `worldContext` only with native bridge authentication, validates bounded optional signals and scopes them to game session/selected pedestrian. Same-NPC refresh preserves valid turns/voice/memory. Facts expire after 10 seconds; missing, stale, browser-authored or unrelated-scope facts never become current game knowledge. Published `sceneAware:false` suppresses dialogue facts without hiding the dashboard's last verified observation.

Release validation on 11 October 2026: `npm run check` passed; `npm test` passed 95/95 offline checks; `npm run test:native -- -GameExecutable '..\Grand Theft Auto San Andreas\gta-sa.exe' -Renderer` passed installed-file/GXT/world-wire/ASLR/runtime-header, hold and synthetic capture checks; GPU hosts passed 63/64 checks. `npm run build:native` and the explicit install command passed; browser verification created, persisted, published and deleted a temporary profile without paid voice previews. The final installed ASI is 448,512 bytes with a matching build hash. The user confirmed the location/game-time/weather HUD and world-aware interaction after the strict runtime-header correction. A live backend startup conflict was resolved by stopping only the known verification server; regression tests now ensure EADDRINUSE gives guidance and never replaces a running server's token. Favor + Trust remains a named local stash, not submitted code.

Public references consulted (original implementation, no project/code import): [Plugin-SDK CClock](https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_sa/game_sa/CClock.cpp), [Plugin-SDK CWeather](https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_sa/game_sa/CWeather.cpp), [GTA Reversed Clock](https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/Clock.cpp), and [GTA Reversed Weather](https://github.com/gta-reversed/gta-reversed/blob/master/source/game_sa/Weather.cpp). Local game files remain prerequisites owned/provided by the user, not a bundled public dataset.

## Immersive personality follow-up

The backend follow-up removes demo-style self-introductions and rigid sentence quotas from generated residents. Stable scope-based dispositions, values and quirks are independent of gender/voice. Residents speak in character and are instructed never to disclose that they are in a game, an NPC, AI, mod or demo. General reference knowledge is distinct from remembered player events and current verified world facts.

`backend/src/game-knowledge.js` contains original small factual summaries and selectively supplies optional classic-PC codes only for explicit code requests. The source for supported code facts and save/version cautions is [GamesRadar's San Andreas cheat reference](https://www.gamesradar.com/gta-san-andreas-cheats/); no guide paragraphs or canonical dialogue are reproduced. The mod never applies a code, spawns an object from model text or invents a current objective. These optional references are not guaranteed across editions/modifications.

The team approved the existing default-model change to `gpt-5.4-mini`; the environment example matches it. GPT-5 family requests reserve low reasoning/answer headroom, while ordinary model requests avoid unsupported reasoning parameters. The 400-character Malayalam reply validation, delivery/memory/world scoping and voice routes remain intact. Targeted tests passed 47/47 and the full suite passed 105/105 with stubs; no additional paid conversation was made for this follow-up. Live tone/immersion is prompt-guided and should be checked after one backend restart.

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

- Load a save/new game. Approach an eligible pedestrian; the popup identifies the selected target's known gender within 3.5 units.
- **F6 / Enter** selects and holds that NPC facing CJ and loads its persona without a paid greeting. Type + **Enter**, or hold **R**, wait for **Recording**, speak and release **R** for STT/reply.
- **Backspace** edits; a quick R tap types `r`. **Escape**, lost focus or invalid selection cancels recording/turn/speech and releases the NPC. Held send keys are debounced.
- The hook operates only with GTA foreground. Windows, Alt+Tab/Alt+F4 remain available; mouse/gamepad input is untouched. Existing AI tasks are retained and resume when the reversible hold is released.
- Keep the selected pedestrian within 5 units. Reply subtitles accompany the audio and remain until playback finishes. Voice failure is visibly reported rather than claimed as success.

## Validation and remaining work

Run `npm run check`, `npm test`, `npm run test:native`, and `npm run build:native`. Optional read-only local-file validation: `npm run test:native -- -GameExecutable '..\Grand Theft Auto San Andreas\gta-sa.exe'`. Backend tests use stub providers, not an offline/demo runtime. The native tests check eligibility/range, slot identity, stock gender parsing, reversible hold/fault rollback, microphone/WAV bounds and cancelled/restarted capture using a synthetic driver, wrap-safe R gestures/timers, and player/pool/standstill signatures. No microphone is opened during these tests. Final commands/results, renderer smoke evidence and copied-file verification are recorded in CHANGELOG.md.

Initial live dialogue succeeded while ElevenLabs returned HTTP 401. The user corrected local credentials; one authorized speech-only request then returned 30,555 bytes of verified MPEG Layer III audio, and Windows MCI playback succeeded (1,910 ms). No OpenAI request was made during that speech-only check. Restart the backend after changing `.env`; never put provider keys in chat or logs.

Gameplay exposed two additional native failures that offline/single-turn checks missed: the executable is ASLR-relocated, and MCI aliases belong to their opening thread. The first address-base fix enabled real proximity; the first file-close fix alone did not solve repeated speech because successive HTTP workers could not resolve each other's MCI aliases (error 263). All MCI open/play/status/close and MP3 replacement now run on the persistent overlay-owner thread. An x86 test exercises the actual production dispatch from five distinct workers and checks cancellation/stale requests. The user confirmed the popup and two consecutive spoken, subtitled replies after installing the final build. That earlier milestone established repeated audio. That voice/PTT milestone used a 367,616-byte ASI. The submitted world-aware integration is the 448,512-byte build described above; both milestones were hash-verified separately. The user additionally confirmed the corrected female voice and native hold-R recognition plus spoken reply; `npm test` passed 70 offline regressions.

The core popup/typed reply/speech/subtitle walkthrough is user-confirmed. For broader coverage, additionally verify removal/leaving range, Escape/Alt+Tab, selecting a second pedestrian, provider failures, resolution changes and fullscreen behavior across other supported machines/mod combinations. The selected pedestrian is held while chatting; its AI tasks are preserved. Mouse/gamepad suppression, custom-skin gender recognition and persistent identity across game restarts remain outside verified scope. For removal, close GTA and remove only `MalayalamNpcBootstrap.asi`; do not remove saves, other plugins, executables or existing proxy DLLs.

## GPU smoke tests

From an **x86 Visual Studio Developer PowerShell**, at repository root:

```powershell
Set-Location native\build
cl.exe /nologo /MT /EHsc /std:c++17 /W4 /WX /DUNICODE /D_UNICODE /Fe:d3d-overlay-smoke.exe ..\test\d3d-overlay.test.cpp ..\src\d3d-overlay.cpp /link /MACHINE:X86 d3d9.lib user32.lib
.\d3d-overlay-smoke.exe
cl.exe /nologo /MT /EHsc /std:c++17 /W4 /WX /DUNICODE /D_UNICODE /DOVERLAY_FALLBACK_ONLY /Fe:d3d-overlay-fallback-smoke.exe ..\test\d3d-overlay.test.cpp ..\src\d3d-overlay.cpp /link /MACHINE:X86 d3d9.lib user32.lib
.\d3d-overlay-fallback-smoke.exe
```

The factory and fallback hosts passed 63 and 64 checks respectively, including game-thread callback/helper exclusion/restoration-after-shutdown: actual backbuffer pixels/blending, state restoration, Reset, helper-device exclusion, concurrent frame submissions and presentation counters. These tests require working local Direct3D9 hardware/runtime and never call providers or modify GTA. The fallback alone cannot discover a device with a private vtable created before interception; factory interception is the primary path.

Optional production audio-owner regression, using an existing MP3 and no provider call:

```powershell
npm run test:native -- -AudioFile 'C:\Temp\test-reply.mp3'
```

Keep generated speech fixtures outside the repository; no fixture is bundled.
