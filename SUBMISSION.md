# Project submission

Complete this file during the hackathon and use it to prepare your submission through the method announced by the organizers. Do not submit your project to the template repository.

## Project

- **Project name:** Malayalam NPC Demo
- **Short description:** A profile-driven backend foundation for a Malayalam-speaking game NPC with scoped conversation memory. Current demo uses labeled mock providers.
- **Code repository URL:** TODO
- **Final submitted implementation commit SHA:** TODO
- **Working demo URL or local demo steps:** From the repository root, run `npm run demo` with Node.js 22.18 or newer.
- **Demo access requirements:** Local terminal only; no credentials, microphone, or game installation required for the mock demo.

## Participants

Participate solo or in a team of up to 4. Remove unused rows.

| Participant name | GitHub handle | Contribution |
| --- | --- | --- |
| TODO | TODO | TODO |

## Run and demonstrate

- **Prerequisites and versions:** Node.js 22.18 or newer.
- **Install command:** None; this milestone has no third-party dependencies.
- **Required environment variable names:** None for the mock demo.
- **Start command:** `npm run demo`.
- **Demo walkthrough:** Type a Malayalam utterance, observe the mock transcript/reply, then ask `മുമ്പ് ഞാൻ എന്താണ് പറഞ്ഞത്?`; use `/reset` to clear in-process memory.
- **Checks performed and results:** `npm run check` passed; manual mock CLI recall walkthrough passed; `git diff --check` passed. No automated tests yet.
- **Known limitations or incomplete features:** Mock speech and dialogue only; no actual audio, live provider, durable memory, companion UI, or in-game bridge. See [README.md](README.md).
- **Libraries, APIs, and public datasets used:** Node.js built-ins only; no external API or dataset used in the current demo.
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
