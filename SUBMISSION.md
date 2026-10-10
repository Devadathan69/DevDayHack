# Project submission

Complete this file during the hackathon and use it to prepare your submission through the method announced by the organizers. Do not submit your project to the template repository.

## Project

- **Project name:** Malayalam NPC Demo
- **Short description:** A local Malayalam NPC companion with nearby in-game interaction, reversible NPC standing/facing, distinct original personas and approved gender voice pools, typed or hold-R speech-to-text input, spoken subtitles, OpenAI/ElevenLabs and SQLite memory. Corrected female voice and native microphone recognition/reply behavior were confirmed by the user; broader compatibility remains limited.
- **Code repository URL:** TODO
- **Final submitted implementation commit SHA:** TODO
- **Working demo URL or local demo steps:** Configure approved provider keys/voices locally, build and install the ASI as documented in README, run `npm start`, launch GTA and load a save. Approach a pedestrian, press F6/Enter, then type + Enter or hold R until Recording, speak Malayalam and release. The companion browser remains an alternative demo. Live provider calls incur account usage.
- **Demo access requirements:** Windows, Node.js 22.18+, a recognized GTA executable with x86 ASI loader for the game demo, Visual Studio C++ tools for rebuilding, a default microphone with desktop-app access, and approved OpenAI/ElevenLabs accounts. The browser-only companion does not require GTA.

## Participants

Participate solo or in a team of up to 4. Remove unused rows.

| Participant name | GitHub handle | Contribution |
| --- | --- | --- |
| TODO | TODO | TODO |

## Run and demonstrate

- **Prerequisites and versions:** Windows and Node.js 22.18+, approved OpenAI/ElevenLabs credentials, recognized GTA/x86 loader for native interaction and Windows desktop microphone access. Rebuilding requires Visual Studio C++ tools; browser fallback requires browser microphone permission.
- **Install command:** None; there are no npm package dependencies.
- **Required environment variable names:** `OPENAI_API_KEY`, `ELEVENLABS_API_KEY`, and `ELEVENLABS_VOICE_ID`; named gender pools use optional `ELEVENLABS_MALE_VOICES` / `ELEVENLABS_FEMALE_VOICES`. `LIVE_TURN_LIMIT=0` is unlimited application turns, not free/unlimited provider credits. See the sanitized `.env.example`; real keys/IDs stay out of commits.
- **Start command:** `npm start` for provider-backed speech and dialogue.
- **Demo walkthrough:** In game, select a nearby NPC and observe it standing/facing CJ; demonstrate typing or hold-R input and recognized text plus spoken subtitles. Select another NPC to show a different original persona/approved voice. Escape leaves and releases the hold. Browser fallback: select Manu and use F7. Only delivered turns enter scoped SQLite memory.
- **Checks performed and results:** `npm test` passed 70 offline tests; JS syntax checks, native hold/gender/task-signature and synthetic microphone tests passed. GPU smoke hosts passed 63/64 checks; repeated production audio workers passed. Final ASI install hash matched the build. The user confirmed the corrected female voice and live native hold-R recognition/spoken reply; runtime logs confirm the actual game thread/renderer.
- **Known limitations or incomplete features:** Custom/unlabelled skins may retain neutral gender fallback; persona identity is game-process/scene/pool scoped, not stable across game restarts. Mouse/gamepad suppression, all gameplay edges, voice pronunciation and other-mod/version compatibility are not comprehensively verified. Clips are bounded to 15 seconds. No CJ mission/cutscene dubbing or offline provider mode is implemented. See [README.md](README.md).
- **Libraries, APIs, and data sources used:** Node built-ins/SQLite, OpenAI Responses, ElevenLabs STT/TTS and Windows SDK Direct3D9/GDI+/WinMM. Public Plugin-SDK/GTA Reversed layout/behavior references are credited in the integration guide; installed model metadata is parsed locally without distributing game assets.
- **AI usage record:** [AI_USAGE.md](AI_USAGE.md)
- **Development change log:** [CHANGELOG.md](CHANGELOG.md)

## Submission checklist

- [ ] The project implementation started from scratch at the event.
- [ ] I participated solo or with no more than 4 team members in total.
- [ ] Codex was used during development and its usage is documented.
- [ ] The code repository URL, short description, and working demo are provided.
- [ ] README setup and demo instructions match the submitted code.
- [ ] Actual checks and known limitations are documented.
- [ ] Meaningful changes are documented and committed throughout development.
- [ ] The final implementation is committed and pushed, and its SHA is recorded above.
- [ ] No credentials or private data are included in the repository or demo instructions.
- [ ] The organizers' announced submission method and timing have been followed.

Event reference: [DevDay Exchange Community: Kochi on Luma](https://luma.com/z10nz8gp). The event wraps up by **10:00 AM IST on 11 October 2026**.
