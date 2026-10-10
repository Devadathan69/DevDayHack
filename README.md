# Malayalam NPC Companion

A local Windows prototype for having a Malayalam conversation with the original character Manu. It provides a browser companion, live OpenAI dialogue, ElevenLabs speech recognition and speech generation, and private SQLite conversation memory.

**Current boundary:** the native x86 ASI now draws a transparent overlay, sends typed turns through an authenticated localhost bridge, and displays reply subtitles with optional audio. Automatic nearby-pedestrian detection is enabled only for the GTA SA 1.0 US executable layouts recognized by Plugin-SDK. The installed `gta-sa.exe` does not match those fingerprints, so it falls back to manual F6/Enter chat and cannot claim NPC proximity on this installation. The rebuilt ASI and verified x86 ASI loader are present in the game root, but the game has not been launched with them.

## Start the companion

Prerequisites: Windows, Node.js 22.18 or newer, a microphone and speakers, provider API access, and a supported browser.

The project has no npm package dependencies. In a fresh checkout, create `.env` from `.env.example` and set `OPENAI_API_KEY`, `ELEVENLABS_API_KEY`, and `ELEVENLABS_VOICE_ID` to a Malayalam-capable voice in your account. Keep those values in `.env`; never put them in a profile or browser setting. The current local checkout already has these settings configured.

Every app run uses the live providers; there is no offline response mode. Conversation turns may make OpenAI and ElevenLabs API calls.
Remove any old `NPC_MODE` setting from `.env`; the app rejects the former provider-mode setting and command-line flags.

From the project folder, run:

```powershell
npm run doctor
npm start
```

Open **http://127.0.0.1:4177** in the browser and allow microphone access. The server prints the actual URL if you change `UI_PORT`. Click **Select Manu** (or press **F6** while the companion window is focused), then hold **F7** while speaking Malayalam and release it to send the recording. Press **Escape** to cancel a recording or active turn. Use **Mute voice** to mute generated audio. The typed-message option is available as a fallback. The browser may require a click on **Play reply** before it allows audio playback.

Starting the server also creates a random bridge token under `%LOCALAPPDATA%\MalayalamNpcDemo\bridge-token`; the ASI reads it locally and uses it only for approved conversation routes. The token is not printed or served to the browser. Start the backend before launching the game; the ASI refreshes the token if the backend restarts.

F6, F7, and Escape work while the **companion browser window has focus**. They are not registered as GTA or global hotkeys. The companion does not know which pedestrian is in front of the player.

Dialogue uses OpenAI's [Responses API](https://platform.openai.com/docs/api-reference/responses) with `gpt-4.1-mini` by default. ElevenLabs handles [speech transcription](https://elevenlabs.io/docs/api-reference/speech-to-text/convert) with Scribe v2 and [speech generation](https://elevenlabs.io/docs/api-reference/text-to-speech/convert) with Eleven v4. The current `.env` is configured with a Malayalam-labeled voice from the connected ElevenLabs account; choose a different account voice with `ELEVENLABS_VOICE_ID`. Eleven v4's [supported language list includes Malayalam](https://elevenlabs.io/docs/help-center/other/what-languages-do-you-support). Recognition and pronunciation still need a real microphone/provider check.

Each server run allows 25 provider-backed turns by default; change `LIVE_TURN_LIMIT` in `.env` to adjust the cap. The turn cap limits repeated API calls but is not a monetary cost estimate. OpenAI and ElevenLabs can receive the transcript and bounded dialogue context; ElevenLabs receives the captured audio and generated reply text. Raw microphone audio is not written to disk. Delivered transcripts and replies are saved outside the repository under `%LOCALAPPDATA%\MalayalamNpcDemo\data\memory.sqlite`; generated audio is held temporarily in memory and deleted when delivered, cancelled, reset, or expired. Use **Reset memory** to erase Manu's local history. Provider-side retention follows each provider's account and service settings.

To stop the companion, press **Ctrl+C** in its terminal.

## In-game overlay and keys

Build the native plugin with `npm run build:native`; the output is `native/build/MalayalamNpcBootstrap.asi`. The overlay uses GDI+ and a transparent layered window, so it does not hook D3D9. On a recognized GTA SA 1.0 US layout, it prompts when CJ is within 3.5 game units of an ambient pedestrian. On the currently installed unrecognized executable, it shows a manual F6/Enter chat prompt and explicitly says proximity is unavailable. Text entry and Escape cancellation are handled only while the GTA process is foreground.

In the in-game overlay:

- **F6** or **Enter** starts the chat from the prompt.
- Type a message and press **Enter** to send it.
- **Backspace** edits the input; **Escape** cancels the active turn and leaves the conversation.
- GTA controls are suppressed while typing; Windows, Alt+Tab, and Alt+F4 remain available.

The in-game adapter currently supports typed turns only; microphone push-to-talk remains in the browser companion. This installation uses the manual F6/Enter fallback because its executable is unidentified. The ASI loader and plugin are installed, but in-game rendering, Malayalam shaping, audio, and key behavior remain unverified. In-game replies use the configured OpenAI and ElevenLabs services and require valid provider credentials.

## Checks and native build

```powershell
npm run check
npm test
npm run build:native
```

`npm test` exercises provider request construction with stubbed HTTP responses and checks SQLite persistence, delivery state, reset, and scope isolation. It does not call paid APIs. `npm run build:native` builds the x86 overlay ASI with Visual Studio C++ tools. Its game-memory reader is strictly gated to the recognized GTA SA 1.0 US entry points. The build does not change game files.

## Project status

- **Implemented:** profile-based NPC controller, browser companion and live provider adapters, microphone PTT, typed input, subtitles/audio, cancellation and memory; plus an x86 GDI+ overlay, guarded nearby-pedestrian scan, local-token bridge authentication, typed chat, subtitle display, and optional audio playback.
- **Validated here:** `npm run check` passed; `npm run build:native` built the 214,016-byte x86 ASI; the game-root copy matches the build artifact; and the installed x86 ASI loader matches its official SHA-512 sidecar. The installed executable fails the native layout fingerprint check, so the plugin selects manual F6/Enter fallback.
- **Not validated:** bridge conversation against a running backend, ASI loader behavior, any in-game interaction, live provider turns, Malayalam shaping/pronunciation, or audio/key behavior with GTA focused.
- **Still required:** identify and install a compatible GTA executable/loader combination, load the ASI in game, then validate pedestrian proximity, interaction, reply subtitles/audio, and cancellation. The current installation is not recognized for automatic proximity.

See [plan.md](plan.md), [game integration](docs/game-integration.md), [CHANGELOG.md](CHANGELOG.md), and [AI_USAGE.md](AI_USAGE.md). Submission details in [SUBMISSION.md](SUBMISSION.md) still need participant and repository information.
