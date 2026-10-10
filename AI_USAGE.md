# AI usage record

Use Codex during the hackathon and briefly document its actual contribution here. Add a row after each meaningful task or milestone. Keep summaries concise and omit secrets, private data, and full chat transcripts.

## Event build log

Record actual assistance after each meaningful task. Mark human review as pending until someone has reviewed the contribution.

| Date and time with timezone | Tool / model, if known | Task | Assistance and affected files | Human review / corrections | Validation and result |
| --- | --- | --- | --- | --- | --- |
| 2026-10-10 22:08 IST | Codex | Start Phase 1 backend | Implemented profile validation, reusable NPC controller/registry, turn/session cancellation, mock providers, in-memory scoped memory, CLI demo, and project docs. | Pending human review. Changed the CLI to iterate input lines after the first piped run showed an unsettled prompt at EOF. | `npm run check`, two-turn mock Malayalam recall walkthrough, and `git diff --check` passed. No automated tests were added. |
| 2026-10-10 22:10 IST | Codex | Check configured provider credentials and document game integration | Confirmed `.env` contains the expected variable names, made read-only OpenAI/ElevenLabs authentication requests, and added `docs/game-integration.md` with a staged no-game-first integration procedure. | Pending human review; secret values were not printed. | OpenAI model-list and ElevenLabs user-info endpoints both returned HTTP 200; Markdown local-link check and `git diff --check` passed. No text/audio generation or game launch was performed. |
| 2026-10-10 22:33 IST | Codex | Build NPC personality admin dashboard | Added `admin/index.html`, `admin/styles.css`, and `admin/app.js` with editable identity, trait sliders, dialogue parameters, behavior toggles, live mock preview, local draft saving, reset, and JSON profile export. Updated README and package syntax check. | Pending human review. | `npm run check` and `git diff --check` passed. No automated tests were added. |
| 2026-10-11 00:40 IST | Codex | Expand dashboard profile workflows | Added multi-NPC local roster actions, import/export, versioned local publish/restore, reusable behavior presets, scenario simulation, voice preference controls, and honest offline/backend status labels. Updated README. | Pending human review. | `npm run check` and `git diff --check` passed. No automated tests were added. |

## Template preparation

On 10 October 2026, Codex prepared this documentation template from the organizer's supplied rules and checked the linked Luma page for the event name and dates. This covered `README.md`, `RULES.md`, `AGENTS.md`, `CHANGELOG.md`, `AI_USAGE.md`, `SUBMISSION.md`, `.gitignore`, and the pull request checklist. Agent validation covered local links, event consistency, required documents, and diff whitespace; organizer review is pending.

This preparation is separate from participant project implementation and participant AI usage during the event.
