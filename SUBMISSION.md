# Project submission

Complete this file during the hackathon and use it to prepare your submission through the method announced by the organizers. Do not submit your project to the template repository.

## Project

- **Project name:** Malayalam NPC Demo
- **Short description:** A profile-driven local Malayalam NPC companion with OpenAI dialogue, ElevenLabs speech, and private SQLite conversation memory. A typed GTA bridge is implemented, but in-game behavior is unverified.
- **Code repository URL:** TODO
- **Final submitted implementation commit SHA:** TODO
- **Working demo URL or local demo steps:** With provider keys in local `.env`, run `npm start`, open the printed local URL, select Manu, and use push-to-talk. This demo requires OpenAI and ElevenLabs provider access.
- **Demo access requirements:** Windows, Node.js 22.18 or newer, a browser and microphone for live speech, plus OpenAI and ElevenLabs API access. The game is not required for the current companion demo.

## Participants

Participate solo or in a team of up to 4. Remove unused rows.

| Participant name | GitHub handle | Contribution |
| --- | --- | --- |
| TODO | TODO | TODO |

## Run and demonstrate

- **Prerequisites and versions:** Windows and Node.js 22.18 or newer, OpenAI and ElevenLabs credentials, and browser microphone permission.
- **Install command:** None; there are no npm package dependencies.
- **Required environment variable names:** `OPENAI_API_KEY`, `ELEVENLABS_API_KEY`, and `ELEVENLABS_VOICE_ID`; optional model, local port, data directory, timeout, and audio-limit settings are documented in `.env.example`.
- **Start command:** `npm start` for provider-backed speech and dialogue.
- **Demo walkthrough:** Select Manu, speak Malayalam with F7 held in the focused companion window, play or read his reply, then ask what you told him. Restart the server to demonstrate SQLite memory; use **Reset memory** to clear it.
- **Checks performed and results:** `npm run check` passed; five deterministic adapter/SQLite tests passed in an earlier check. Earlier provider-backed configuration and page startup returned HTTP 200 without generation calls, and read-only OpenAI and ElevenLabs authentication requests returned HTTP 200. No live audio round trip or GTA interaction has been demonstrated.
- **Known limitations or incomplete features:** The typed game bridge is implemented but unverified; automatic NPC proximity is disabled for the installed executable, and loader/game behavior has not been tested. Live microphone recognition, Malayalam voice quality, and audio playback still need device/provider validation. The app requires provider credentials and has no offline runtime. See [README.md](README.md).
- **Libraries, APIs, and public datasets used:** Node.js built-ins including `node:sqlite`; OpenAI Responses API; ElevenLabs speech-to-text and text-to-speech APIs. No public dataset is used.
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
