# Project submission

Final local-demo submission record for DevDay Exchange Community Hackathon, Kochi (10–11 October 2026). This document and repository are ready for the team to submit using the organizers' announced channel; GitHub publication alone is not a claim that organizer submission is complete.

## Project

- **Project name:** Malayalam NPC Demo
- **Short description:** A world-aware Malayalam conversation mod for GTA San Andreas. Nearby pedestrians stand/focus during chat, use distinct original personalities and approved voices, and respond to typed or hold-R speech with spoken subtitles. Verified location, game time and measured weather ground live dialogue; private SQLite memory and a localhost NPC configuration dashboard complete the local demo.
- **Code repository URL:** https://github.com/Devadathan69/DevDayHack
- **Final submitted implementation commit SHA:** [`5f6d19717539a7dc098c236f32173f3f6b9f9635`](https://github.com/Devadathan69/DevDayHack/commit/5f6d19717539a7dc098c236f32173f3f6b9f9635). This is the verified demo implementation; the following documentation-only revision records its SHA without adding later implementation changes.
- **Working demo:** Local Windows installation, not a public hosted deployment. Start one backend with `npm start`; companion **http://127.0.0.1:4177**, dashboard **http://127.0.0.1:4177/admin**. Build/install the ASI as documented below, launch GTA manually, load a save, approach a pedestrian, press F6/Enter and type + Enter or hold R until Recording, speak Malayalam and release. Live providers consume account usage.
- **Demo access requirements:** Windows, Node.js 22.18+, a recognized GTA executable with x86 ASI loader for the game demo, Visual Studio C++ tools for rebuilding, a default microphone with desktop-app access, and approved OpenAI/ElevenLabs accounts. The browser-only companion does not require GTA.

## Participants

Public credits supplied and approved by the team; four participants, within the event limit.

| Participant name | GitHub handle | Contribution |
| --- | --- | --- |
| Devadathan M R | [Devadathan69](https://github.com/Devadathan69) | Full mod integration into the game |
| Mathew Joseph | [mathew1046](https://github.com/mathew1046) | Backend and voice pipeline |
| Vishnu Naveen Nair | [vishnu912py](https://github.com/vishnu912py) | Key bindings |
| Sreerag C Madhu | [sreerag-47](https://github.com/sreerag-47) | Dashboard |

## Run and demonstrate

- **Prerequisites and versions:** Windows and Node.js 22.18+, approved OpenAI/ElevenLabs credentials, recognized GTA/x86 loader for native interaction and Windows desktop microphone access. Rebuilding requires Visual Studio C++ tools; browser fallback requires browser microphone permission.
- **Install/build:** No npm package dependencies to install. Create local `.env` from the sanitized example; use `npm run build:native`, then `npm run install:native -- -GameDirectory 'C:\Games\Your GTA San Andreas'` with GTA closed. A compatible x86 ASI loader/game executable must already be present; no game executable, loader or licensed assets are bundled.
- **Required environment variable names:** `OPENAI_API_KEY`, `ELEVENLABS_API_KEY`, and `ELEVENLABS_VOICE_ID`; named gender pools use optional `ELEVENLABS_MALE_VOICES` / `ELEVENLABS_FEMALE_VOICES`. `LIVE_TURN_LIMIT=0` is unlimited application turns, not free/unlimited provider credits. See the sanitized `.env.example`; real keys/IDs stay out of commits.
- **Start command:** `npm start` for provider-backed speech and dialogue.
- **Demo walkthrough:** (1) Load a save and show the actual location/time/weather HUD. (2) Select a nearby NPC and observe standing/facing; ask in Malayalam about the location or weather using hold R, then show recognized text, spoken reply and matching subtitles. (3) Revisit that NPC during the same game scope for memory, and select another for a different persona/approved voice. (4) Show dashboard save/publish/version controls and the verified/stale context card; its playground is explicitly a local simulation. (5) Escape leaves and releases the hold. Only delivered turns enter scoped memory.
- **Release checks and results:** `npm run check` **passed**; `npm test` **105/105 passed** without paid API calls after the immersive-personality/curated-advice follow-up; `npm run test:native -- -GameExecutable '..\Grand Theft Auto San Andreas\gta-sa.exe' -Renderer` **passed** (hold/gender/capture/PE/GXT/world-wire and GPU **63/64** checks); `npm run build:native` **passed** (448,512-byte x86 ASI); `npm run install:native -- -GameDirectory '..\Grand Theft Auto San Andreas'` **passed**, installed hash matched. User confirmed corrected female voice, hold-R recognition/spoken reply and real location/time/weather. Browser create/save/publish/delete verification passed with a temporary profile subsequently removed; protected Manu remained intact.
- **Dialogue follow-up verification:** Stable warmer personas, in-character/no-fourth-wall instructions, selective original game-reference summaries and optional advice-only classic-PC codes are covered by 105 offline tests. The default `gpt-5.4-mini` change was approved by the team. No new paid live turn was performed for this follow-up; restart the backend once and verify live tone. Previously confirmed native voice/PTT/world behavior and the 448,512-byte ASI remain unchanged.
- **Known limitations:** World sampling is verified for the installed 2014 layout, not every GTA build; missing/interior/stale observations are unavailable rather than guessed. Custom skins may have neutral gender fallback. Identity/memory is process/scene/pool scoped, not cross-game-restart resident identity. Mouse/gamepad suppression, all gameplay edges, every voice's pronunciation and other-mod compatibility are not exhaustively verified. Clips cap at 15 seconds; provider quotas/costs remain despite unlimited application turns. No CJ mission/cutscene dubbing, offline live runtime, public hosting or Favor + Trust feature is included. The deferred favor work is preserved only in a named local stash and is not part of this demo.
- **Libraries, APIs, and data sources used:** Node built-ins/SQLite, OpenAI Responses, ElevenLabs STT/TTS and Windows SDK Direct3D9/GDI+/WinMM. Public Plugin-SDK/GTA Reversed layout/behavior references are credited in the integration guide; installed model metadata is parsed locally without distributing game assets.
- **AI usage record:** [AI_USAGE.md](AI_USAGE.md)
- **Development change log:** [CHANGELOG.md](CHANGELOG.md)

## Submission checklist

- [x] Event development is recorded from scratch in the implementation/change history; external APIs/libraries/layout references are credited.
- [x] Four public participant credits are supplied and approved by the team.
- [x] Codex/assistant usage and actual contributions are documented in the AI usage record.
- [x] Repository URL, short description and reproducible local demo are provided.
- [x] README setup and demo instructions match the verified implementation.
- [x] Actual checks, failures/corrections and known limitations are documented.
- [x] Meaningful work is preserved in coherent commits without history rewriting.
- [x] Final implementation SHA recorded; push of `5f6d197` to GitHub main succeeded. The finalization revision changes documentation only.
- [x] Credentials/real voice IDs/private runtime files are excluded; only approved public participant credits are included.
- [x] Team action: submit the repository/demo through the organizers' announced channel before the deadline; organizer submission is not performed by this GitHub push.

Event reference: [DevDay Exchange Community: Kochi on Luma](https://luma.com/z10nz8gp). The event wraps up by **10:00 AM IST on 11 October 2026**.
