# Change log

Record each meaningful project change during the event, newest first. Update this file in the same commit as the change. Use a real timestamp with timezone, for example `YYYY-MM-DDTHH:MM:SS+05:30`.

## Event build log

## 2026-10-10T22:10:50+05:30 — Verify provider credentials and document game setup

- Changed: Added `docs/game-integration.md`, linked it from the README, and recorded the local provider-authentication result in project records.
- Why: Give the user a safe, ordered path from the current companion/backend scaffold to a future GTA plugin while distinguishing credential validity from a live conversation.
- Validation: Read-only `GET /v1/models` (OpenAI) and `GET /v1/user` (ElevenLabs) requests using `.env` returned HTTP 200. No key values were printed; no generation endpoint or game process was used. Reviewed Plugin-SDK and Ultimate ASI Loader primary documentation. Markdown local-link check and `git diff --check` passed.
- Limitations: The CLI remains mock-only; no live provider adapter, native plugin, executable compatibility record, or in-game demo exists yet.

## 2026-10-10T22:08:22+05:30 — Add the mock NPC backend slice

- Changed: Added a dependency-free Node.js CLI, one validated original NPC profile, reusable controller/registry/session management, configurable mock speech/dialogue providers, and a bounded in-memory scoped memory store. Updated README, plan status, and ignored local audio/database artifacts.
- Why: Start with the plan's provider-independent backend milestone while Windows game compatibility and live provider choices remain unverified.
- Validation: `npm run check` passed JavaScript syntax checks. `npm run demo` completed a two-turn Malayalam recall walkthrough using piped input. `git diff --check` passed.
- Limitations: Mock-only; no live microphone/TTS, paid providers, SQLite persistence, web companion, native plugin, or deterministic unit tests yet. Memory is lost when the CLI exits.

## Template setup — 10 October 2026

- Changed: Added the event README, rules, agent guide, change log, AI usage record, submission checklist, Git ignore rules, and pull request checklist.
- Why: Give participants a consistent starting point for event rules, basic coding patterns, documented changes, and regular commits.
- Validation: Checked local Markdown links, event detail consistency, required documents, and diff whitespace before publishing.
- Limitations: Documentation scaffolding only; no participant project, application, or working demo is included. GitHub publication and template status are verified separately after creation.
