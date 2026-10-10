# GTA San Andreas integration status

## Implemented in the current ASI

`native/src/bootstrap.cpp` builds a 32-bit ASI with a GDI+ layered-window overlay; it does not hook D3D9. The overlay supports the F6/Enter prompt, typed input, Malayalam reply subtitles, optional backend audio, and Escape cancellation. The backend uses a random token written to `%LOCALAPPDATA%\MalayalamNpcDemo\bridge-token`; the token is accepted only on the local conversation routes and is never returned to the browser. The server remains bound to `127.0.0.1`.

Automatic pedestrian proximity is limited to the GTA SA 1.0 US executable layouts recognized by Plugin-SDK. It reads the player and pedestrian-pool memory only after checking the executable entry point; it offers the prompt within 3.5 game units of an ambient pedestrian. The in-game adapter currently accepts typed messages; microphone push-to-talk remains in the browser companion. No D3D hook, game modification, or model-driven game command is used.

The source project for the address/version conventions is [Plugin-SDK](https://github.com/DK22Pac/plugin-sdk), including its [GTA executable detector](https://github.com/DK22Pac/plugin-sdk/blob/master/injector/injector.hpp#L2987-L3112), [`CPools::ms_pPedPool`](https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_sa/game_sa/CPools.cpp), and [`CPool` layout](https://github.com/DK22Pac/plugin-sdk/blob/master/plugin_sa/game_sa/CPool.h). The detector lists the supported GTA SA 1.0 US entry points as `0x824570` and `0x82457C`; the ASI checks their preferred-base RVAs before touching game memory.

## Installed executable and compatibility

Read-only inspection found a 5,971,456-byte x86 PE named `gta-sa.exe`. Its `FileVersion`, `ProductVersion`, and `OriginalFilename` fields are blank. The PE timestamp is 1 December 2014 and its entry-point RVA is `0x458DB8` (preferred-base address `0x858DB8`). The adjacent `installscript.vdf` contains a `1.00.00001` registry key but refers to `gta_sa.exe`; that clue does not establish the executable version. The user reports this is an official copy. The entry point matches none of Plugin-SDK's listed San Andreas targets, so automatic pedestrian detection is disabled for this executable.

When the ASI is loaded into this unrecognized build, its visible prompt explicitly switches to manual F6/Enter chat and labels proximity as unavailable. The rebuilt project ASI is now in the game root. The root's `dinput8.dll` is Ultimate ASI Loader v9.7.4 x86 (5,418,336 bytes; its SHA-512 matched the sidecar in the official per-file release archive). The game imports `DINPUT8.dll`, `D3D9.dll`, and `XINPUT1_3.dll`; the existing `xinput1_3.dll` reports Microsoft Common Controller API and was left untouched. This confirms the loader file and project ASI are present, but the game has not been launched and overlay/key/audio behavior has not been tested.

## Build and run

Build with the installed Visual Studio x86 C++ tools:

```powershell
npm run build:native
```

The generated file is `native/build/MalayalamNpcBootstrap.asi`. Start the backend before launching the game:

```powershell
npm start
```

The backend writes its bridge token under the current user's local application data. Keep `.env`, the token, database, logs, and temporary audio outside the game folder and repository. `UI_PORT` can change the port; the ASI reads it from the token file. In-game replies use OpenAI and ElevenLabs and require the provider credentials in `.env`.

The current game root contains the project-built `MalayalamNpcBootstrap.asi` (214,016 bytes) and the already-present official x86 `dinput8.dll` loader. The prior project ASI (120,832 bytes) was replaced at that exact path; no other game file was changed. An earlier write attempt was rejected by desktop policy, but the later direct copy of the named project ASI succeeded. The loader already existed and matched the official per-file archive; it was not copied or replaced. The executable remains unidentified, so the ASI's automatic proximity path stays disabled and the game has not been launched.

## In-game controls

- On a recognized GTA SA 1.0 US build, approach an ambient pedestrian; the prompt appears within 3.5 game units. Press **F6** or **Enter** to start.
- On the current unrecognized build, the prompt is manual and remains available while the game is focused; **F6** or **Enter** starts chat without proximity detection.
- Type a message and press **Enter** to send; **Backspace** edits; **Escape** cancels the active turn and leaves the chat.
- The keyboard hook acts only while the game process is foreground. While typing, it suppresses GTA controls, while Windows, Alt+Tab, and Alt+F4 remain available.

## Validation and remaining work

`npm run check` passed and `npm run build:native` produced a 214,016-byte x86 ASI. The current game executable does not pass the automatic-proximity compatibility check. No bridge round trip, ASI loader startup, game launch, overlay rendering, subtitle shaping, audio playback, microphone interaction, or in-game key behavior has been validated. Do not describe the mod as working in-game until those checks pass on a recognized executable.

Before a compatible in-game demo, verify the exact executable hash and loader, then validate startup/shutdown, nearby-ped selection, leaving range, cancellation, a live reply, subtitles/audio, and provider failure handling. Never use model output as a game command. For removal, close the game and remove only this project's manifest-listed files; do not remove saves, other plugins, or existing proxy DLLs.
