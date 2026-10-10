# Malayalam NPC Companion

A local Windows prototype for having a Malayalam conversation with the original character Manu. It provides a browser companion, live OpenAI dialogue, ElevenLabs speech recognition and speech generation, and private SQLite conversation memory.

**In-game implementation:** the x86 ASI renders the interaction popup and Malayalam subtitles inside Direct3D9, including exclusive fullscreen. It recognizes GTA SA 1.0 US and the installed 2014 executable through guarded code fingerprints. Approach a living, on-foot pedestrian, press **F6 / Enter**, type a message, and press **Enter** to send. The selected pedestrian stays pinned until you leave; generated speech starts only after its corresponding subtitle has been rendered. The user verified the nearby popup and two consecutive spoken replies with matching subtitles in the installed build. Broader edge-case/mod compatibility coverage remains limited.

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

## NPC admin dashboard

With the companion server running, open **http://127.0.0.1:4177/admin**. The dashboard loads and saves NPC profiles through the authenticated localhost server, including local draft and published-version history. Its roster supports create, duplicate, import, export, and delete; behavior presets and the scenario playground remain local admin tools. Publishing registers a profile with the companion; a published profile can be selected for a new companion session. The tested GTA bridge still binds to the original Manu profile; additional NPC-to-pedestrian bindings are not implemented. Voice preference fields are saved, while actual voice selection is still controlled by the server's configured ElevenLabs voice. Profile data is stored outside the repository in `npc-profiles.json` beside the local SQLite database.

Starting the server also creates a random bridge token under `%LOCALAPPDATA%\MalayalamNpcDemo\bridge-token`; the ASI reads it locally and uses it only for approved conversation routes. The token is not printed or served to the browser. Start the backend before launching the game; the ASI refreshes the token if the backend restarts.

F6, F7, and Escape work while the **companion browser window has focus**. They are not registered as GTA or global hotkeys. The companion does not know which pedestrian is in front of the player.

Dialogue uses OpenAI's [Responses API](https://platform.openai.com/docs/api-reference/responses) with `gpt-4.1-mini` by default. ElevenLabs handles [speech transcription](https://elevenlabs.io/docs/api-reference/speech-to-text/convert) with Scribe v2 and [speech generation](https://elevenlabs.io/docs/api-reference/text-to-speech/convert) with Eleven v4. The current `.env` is configured with a Malayalam-labeled voice from the connected ElevenLabs account; choose a different account voice with `ELEVENLABS_VOICE_ID`. Eleven v4's [supported language list includes Malayalam](https://elevenlabs.io/docs/help-center/other/what-languages-do-you-support). Recognition and pronunciation still need a real microphone/provider check.

Each server run allows 25 provider-backed turns by default; change `LIVE_TURN_LIMIT` in `.env` to adjust the cap. The turn cap limits repeated API calls but is not a monetary cost estimate. OpenAI and ElevenLabs can receive the transcript and bounded dialogue context; ElevenLabs receives the captured audio and generated reply text. Raw microphone audio is not written to disk. Delivered transcripts and replies are saved outside the repository under `%LOCALAPPDATA%\MalayalamNpcDemo\data\memory.sqlite`; generated audio is held temporarily in memory and deleted when delivered, cancelled, reset, or expired. Use **Reset memory** to erase Manu's local history. Provider-side retention follows each provider's account and service settings.

To stop the companion, press **Ctrl+C** in its terminal.

## In-game overlay and keys

Close GTA, build and install only the project ASI, then start the backend and launch the game:

```powershell
npm run build:native
npm run install:native -- -GameDirectory '..\Grand Theft Auto San Andreas'
npm start
```

Use your actual game directory if different. Installation verifies the copied ASI hash and never replaces game executables or loader DLLs; a compatible x86 ASI loader must already be installed. Restart GTA after installing a new build.

Load a save/new game and move CJ within **3.5 game units** of a living, on-foot pedestrian in the same area. The popup is drawn in the game frame rather than a separate topmost window. The nearest eligible pedestrian is selected; entering a car, leaving **5 units**, a removed/recycled pedestrian, unloading the scene, losing focus, or pressing Escape ends the conversation. Unsupported fingerprints disable proximity safely; loading screens do not permanently disable the reader.

In the in-game overlay:

- **F6** or **Enter** opens the input from the nearby-NPC popup; it does not spend a provider turn on an automatic greeting.
- Type a message and press **Enter** to send it. Holding the interaction/send key cannot submit repeated turns.
- **Backspace** edits the input; **Escape** cancels the active turn, speech, and selection.
- Keyboard controls are suppressed during the conversation; Windows, Alt+Tab, and Alt+F4 remain available. NPCs are not frozen, and this plugin does not control GTA mouse/gamepad input.
- Malayalam reply text remains visible throughout speech. If speech cannot be generated or played, a visible warning accompanies the text-only reply.

The in-game adapter supports typed turns only; microphone push-to-talk remains in the browser companion. Native playback temporarily caches the latest generated MP3 at `%TEMP%\MalayalamNpcDemo\npc_reply.mp3`; it is replaced on each reply and deleted on orderly plugin shutdown (forced termination may leave it behind). All pedestrians currently use the original Manu persona and configured voice, not the game's original character voices. Memory is isolated by game process, scene epoch and pedestrian pool handle; it is not persistent character identity across game restarts. In-game replies require the configured live provider credentials. Do not run browser conversations concurrently with in-game conversations: the prototype has one active backend session.

## Checks and native build

```powershell
npm run check
npm test
npm run test:native
npm run build:native
```

`npm test` exercises stubbed provider requests, SQLite persistence, and authenticated native HTTP routes, including cancellation and pedestrian scope changes. It does not call paid APIs. `npm run test:native` compiles and runs x86 proximity, identity, timer, and executable-fingerprint checks. To verify the installed 2014 executable without modifying it:

```powershell
npm run test:native -- -GameExecutable '..\Grand Theft Auto San Andreas\gta-sa.exe'
```

To check repeated speech without another provider call, supply an existing test MP3:

```powershell
npm run test:native -- -AudioFile 'C:\Temp\test-reply.mp3'
```

This additionally reproduces MCI's file-lock/thread-local alias behavior and tests the actual production audio dispatcher across five separate turn workers, cancellation and stale requests. No speech fixture is bundled or committed. Direct3D GPU smoke tests and their build commands are documented in the integration guide.

Native checks/builds require Visual Studio's Desktop development with C++ workload. Builds do not change game files; installation is a separate explicit command.

## Project status

- **Implemented:** live browser companion and x86 in-frame NPC popup, nearest eligible pedestrian selection, scoped bridge turns, typed chat, reply subtitles/speech, cancellation and memory.
- **Automated verification:** JS syntax checks and 39 backend HTTP/provider/memory tests passed; native interaction tests and the installed executable's two code fingerprints passed without modifying the executable. See the changelog for the final build, renderer smoke, deployment and live-provider results.
- **Live verification:** the initial ElevenLabs HTTP 401 was resolved after the user corrected local credentials. One explicitly authorized speech-only check returned valid MP3 audio; Windows MCI playback succeeded. Credentials were never printed or committed.
- **Verified in game (user-confirmed):** the nearby interaction popup appears, and two consecutive replies are audible with matching readable subtitles after the stable-thread audio fix. The final 248,832-byte ASI is installed with a matching build hash; runtime logs confirm the relocated executable fingerprints and actual game-frame rendering.
- **Remaining coverage:** distance/vehicle/scene eligibility and cancellation have native/backend regression coverage, but a complete gameplay edge-case walkthrough, pronunciation assessment, mouse/gamepad behavior and other-mod compatibility are not verified. This is a working prototype, not a guarantee of perfection on every executable or mod combination.

See [plan.md](plan.md), [game integration](docs/game-integration.md), [CHANGELOG.md](CHANGELOG.md), and [AI_USAGE.md](AI_USAGE.md). Submission details in [SUBMISSION.md](SUBMISSION.md) still need participant and repository information.
