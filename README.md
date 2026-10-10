# Malayalam NPC Companion

A local Windows prototype for Malayalam conversations with original NPC personalities. It provides a browser companion (Manu), an in-game native adapter, live OpenAI dialogue, ElevenLabs recognition/speech, and private SQLite memory.

**In-game implementation:** the x86 ASI draws the popup and Malayalam subtitles inside Direct3D9. Approach an eligible pedestrian and press **F6 / Enter**: that NPC stands and faces CJ while its original personality/name and approved voice are selected. Type + Enter, or **hold R, wait for Recording, speak, then release R**. Replies have matching subtitles/speech; recognized microphone text is displayed. The user confirmed the corrected female voice and live hold-R speech-to-text/reply path. Broader edge-case/mod compatibility remains limited.

## Start the companion

Prerequisites: Windows, Node.js 22.18 or newer, a microphone and speakers, provider API access, and a supported browser.

The project has no npm package dependencies. In a fresh checkout, create `.env` from `.env.example` and set `OPENAI_API_KEY`, `ELEVENLABS_API_KEY`, and `ELEVENLABS_VOICE_ID` to a Malayalam-capable voice in your account. Keep those values in `.env`; never put them in a profile or browser setting. The current local checkout already has these settings configured. Optional `ELEVENLABS_MALE_VOICES` and `ELEVENLABS_FEMALE_VOICES` are JSON arrays of `{name, voiceId}` entries for approved account voices; actual IDs stay only in ignored `.env`. The local demo has four approved male voices and one female voice. Single-voice overrides remain available through `ELEVENLABS_MALE_VOICE_ID` / `ELEVENLABS_FEMALE_VOICE_ID`; the default/browser uses `ELEVENLABS_VOICE_ID`. Restart the backend after changing settings.

Every app run uses the live providers; there is no offline response mode. Conversation turns may make OpenAI and ElevenLabs API calls.
Remove any old `NPC_MODE` setting from `.env`; the app rejects the former provider-mode setting and command-line flags.

From the project folder, run:

```powershell
npm run doctor
npm start
```

Open **http://127.0.0.1:4177** in the browser and allow microphone access. The server prints the actual URL if you change `UI_PORT`. Click **Select Manu** (or press **F6** while the companion window is focused), then hold **F7** while speaking Malayalam and release it to send the recording. Press **Escape** to cancel a recording or active turn. Use **Mute voice** to mute generated audio. The typed-message option is available as a fallback. The browser may require a click on **Play reply** before it allows audio playback.

## NPC admin dashboard

With the companion server running, open **http://127.0.0.1:4177/admin**. The dashboard loads and saves NPC profiles through the authenticated localhost server, including local drafts and published-version history. Its roster supports create, duplicate, import, export, and delete; behavior presets and the scenario playground remain local admin tools. Publish a profile to activate it for companion sessions. Published settings on the protected Manu profile are inherited by generated GTA pedestrian personas for dialogue and behavior; GTA continues using the server's gender-aware configured voice pools. The voice selector exposes only configured voices, never provider account IDs. Profile data is stored outside the repository in `npc-profiles.json` beside the local SQLite database.

Starting the server also creates a random bridge token under `%LOCALAPPDATA%\MalayalamNpcDemo\bridge-token`; the ASI reads it locally and uses it only for approved conversation routes. The token is not printed or served to the browser. Start the backend before launching the game; the ASI refreshes the token if the backend restarts.

F6, F7, and Escape work while the **companion browser window has focus**. They are not registered as GTA or global hotkeys. The companion does not know which pedestrian is in front of the player.

Dialogue uses OpenAI's [Responses API](https://platform.openai.com/docs/api-reference/responses) with `gpt-4.1-mini` by default. ElevenLabs handles [speech transcription](https://elevenlabs.io/docs/api-reference/speech-to-text/convert) with Scribe v2 and [speech generation](https://elevenlabs.io/docs/api-reference/text-to-speech/convert) with Eleven v4. The current `.env` is configured with a Malayalam-labeled voice from the connected ElevenLabs account; choose a different account voice with `ELEVENLABS_VOICE_ID`. Eleven v4's [supported language list includes Malayalam](https://elevenlabs.io/docs/help-center/other/what-languages-do-you-support). The native microphone/provider round trip is user-confirmed; pronunciation quality across all voices remains subjective and is not comprehensively assessed.

The former 25-turn application cap is removed: `LIVE_TURN_LIMIT=0` is the default and means **unlimited application turns**. A positive value is an optional opt-in cap. This does not bypass provider quotas or make calls free; voice turns may invoke recognition, dialogue and synthesis, and provider errors remain visible. OpenAI and ElevenLabs can receive the transcript and bounded dialogue context; ElevenLabs receives the captured audio and generated reply text. Raw microphone audio is not written to disk. Delivered transcripts and replies are saved outside the repository under `%LOCALAPPDATA%\MalayalamNpcDemo\data\memory.sqlite`; generated audio is held temporarily in memory and deleted when delivered, cancelled, reset, or expired. Use **Reset memory** to erase Manu's local history. Provider-side retention follows each provider's account and service settings.

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

- **F6** or **Enter** opens chat and stops the selected NPC walking; it faces CJ until the conversation ends. Selection loads the persona without a paid greeting.
- Type a message and press **Enter**, or hold **R** from an empty input, wait for **Recording**, speak Malayalam, and release **R** to send it through STT and the existing reply pipeline.
- A quick R tap types `r` rather than recording; after other text is entered, R remains a normal letter. Recordings are bounded to **15 seconds** and remain in memory; at the cap, release R to send or Escape to discard.
- Enable Windows microphone access for desktop apps and choose the intended default input device. Headphones help avoid capturing game/speaker audio.
- **Backspace** edits; **Escape**, lost focus, leaving range or a changed/removed NPC cancels recording/turn/speech and releases the NPC. Its original AI tasks are preserved, not deleted.
- Keyboard controls are suppressed during chat; Windows, Alt+Tab and Alt+F4 remain available. Mouse/gamepad input is not modified.
- Malayalam reply text remains visible throughout speech. If speech cannot be generated or played, a visible warning accompanies the text-only reply.

Each pedestrian gets a stable original personality and scoped name/voice; traits vary independently of gender. Gender preference uses installed model/animation metadata and recognized stock model-name stems, then reliable civil pedestrian types. Unsupported/custom assets stay neutral rather than being classified from appearance. This corrected stock model 64, whose role and `pro` animation label alone did not identify it as female. The popup shows the currently selected target's known gender; selection is still the nearest eligible pedestrian, not necessarily the one centered in the camera.

Approved named male voices are distributed by the decoded pool slot/generation/scene, not a per-message playlist; returning to the same NPC scope retains its voice. Female metadata chooses the approved female pool; unknown metadata keeps a neutral persona with a stable male-pool fallback voice (or the default voice when no pool is configured), not a claim that gender was detected. The browser's default Manu profile stays compatible. Real voice IDs and keys are never served in selection responses or committed.

Native playback caches the latest MP3 at `%TEMP%\MalayalamNpcDemo\npc_reply.mp3`, replacing it on each reply and deleting it on orderly shutdown (forced termination may leave it behind). Memory is scoped by game process, scene epoch and pool handle, not persistent character identity across game restarts. Keep the live backend running and avoid simultaneous browser/game conversations: the prototype has one active session. CJ mission/cutscene dialogue replacement is not implemented; the requested input mode is microphone STT or typing.

## Checks and native build

```powershell
npm run check
npm test
npm run test:native
npm run build:native
```

`npm test` exercises stubbed provider requests, SQLite persistence, and authenticated native HTTP routes, including cancellation and pedestrian scope changes. It does not call paid APIs. `npm run test:native` compiles/runs x86 proximity, gender metadata, identity/timer/signature, reversible NPC hold, and synthetic microphone/WAV/cancellation checks without opening a microphone. To verify the installed 2014 executable without modifying it:

```powershell
npm run test:native -- -GameExecutable '..\Grand Theft Auto San Andreas\gta-sa.exe'
```

To check repeated speech without another provider call, supply an existing test MP3:

```powershell
npm run test:native -- -AudioFile 'C:\Temp\test-reply.mp3'
```

This additionally reproduces MCI's file-lock/thread-local alias behavior and tests the actual production audio dispatcher across five separate turn workers, cancellation and stale requests. No speech fixture is bundled or committed. Direct3D GPU smoke tests and their build commands are documented in the integration guide. `npm run test:native -- -Renderer` also builds/runs them through the native runner. The native runner covers the integrated hold and microphone modules; hardware capture occurs only when the user explicitly holds R in an active game conversation.

Native checks/builds require Visual Studio's Desktop development with C++ workload. Builds do not change game files; installation is a separate explicit command.

## Project status

- **Implemented:** in-frame nearby popup; reversible NPC stand/facing; stable distinct personas and approved gender voice pools; typed or native hold-R STT input; spoken subtitles; cancellation/memory; unlimited application turns by default.
- **Automated verification:** `npm test` passed 70 offline backend/config/persona/voice/budget/companion tests. Native hold/gender/installed-task-signature and synthetic microphone tests passed; GPU hosts passed 63/64 checks and five distinct production audio workers passed. No paid calls occur in these suites.
- **Live verification:** the initial ElevenLabs HTTP 401 was resolved after the user corrected local credentials. One explicitly authorized speech-only check returned valid MP3 audio; Windows MCI playback succeeded. Credentials were never printed or committed.
- **Verified in game (user-confirmed):** nearby interaction and repeat speech; corrected female voice after the stock-model diagnosis; and holding R, speaking, releasing, seeing recognized text and receiving a spoken reply. The final 367,616-byte ASI is installed with a matching build hash; logs confirm model metadata, task bindings, actual game frames and game-thread NPC holding. Free live bridge checks confirmed all four male names, stable reselection and uncapped status.
- **Remaining coverage:** distance/vehicle/scene eligibility and cancellation have native/backend regression coverage, but a complete gameplay edge-case walkthrough, pronunciation assessment, mouse/gamepad behavior and other-mod compatibility are not verified. This is a working prototype, not a guarantee of perfection on every executable or mod combination.

See [plan.md](plan.md), [game integration](docs/game-integration.md), [CHANGELOG.md](CHANGELOG.md), and [AI_USAGE.md](AI_USAGE.md). Submission details in [SUBMISSION.md](SUBMISSION.md) still need participant and repository information.
