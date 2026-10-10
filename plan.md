# Standalone Implementation Plan: Malayalam AI NPCs in GTA San Andreas

**Delivery strategy:** integrate one NPC end to end, validate the demo, and only then enable additional NPC instances. Build reusable boundaries from the beginning without implementing city-scale behavior prematurely.

**Status (11 October 2026):** The reusable controller, local companion, OpenAI/ElevenLabs adapters, SQLite memory, GDI+ overlay, typed bridge flow, subtitle/audio display, and local bridge-token authentication are implemented. The backend requires provider credentials and stores memory in SQLite; it has no offline runtime. `npm run check` and the x86 native build pass. The rebuilt ASI and verified Ultimate ASI Loader x86 proxy are present in the game root. Nearby-pedestrian memory reads are gated to GTA SA 1.0 US entry points; the installed executable does not match, so the ASI's compiled fallback is manual F6/Enter chat. A game launch confirmed the loader initialized the ASI and the startup log reported overlay initialization, but Windows exposed no targetable game window (the process had no main window handle), so visible rendering and keys could not be checked. No bridge round trip, live voice turn, or in-game exchange has been verified. The old `--mock` backend process was stopped; no provider request was made. Phase 4 remains incomplete; this is not a validated in-game mod.

## 1. Goal and confirmed requirements

Build a local Windows hackathon prototype where the player approaches one designated GTA San Andreas NPC, speaks in Malayalam, receives spoken Malayalam replies, and returns later to find that the NPC remembers previous conversations.

**Core architectural requirement:** every NPC is an instance of the same reusable controller. Personality, voice, identity, and memories are data—not separate implementations.

| Decision | Status |
| --- | --- |
| Game | Classic GTA San Andreas PC, already installed |
| Reported version label | `Build 459558 \| Full Version` |
| Executable compatibility | Not yet established; the reported label is not a verified native executable version |
| Demo platform | Windows |
| Interaction | Push-to-talk voice input and voice output |
| Initial NPC count | Exactly one configured NPC |
| Later expansion | Multiple instances of the same controller, configured through data |
| Dialogue brain | OpenAI API; model selection deferred |
| Speech recognition and synthesis | ElevenLabs; speech model and voice selection deferred |
| Memory | Persistent, isolated per NPC and player |
| Distribution | Local hackathon demo; no public mod release planned |
| Deadline | Not specified; use milestone-based priorities |

This document is self-contained: it specifies a new project, its component boundaries, implementation order, interfaces, test criteria, and operational procedures. It does not depend on another game's implementation or an existing project structure.

### Scope boundary for provider details

Keep OpenAI and ElevenLabs behind provider interfaces, with credentials and SDK-specific types outside the NPC domain layer. Runtime conversations use the live adapters; tests can isolate provider requests with stubbed HTTP responses.

### Decisions still required

- Exact `gta_sa.exe` identity, installation source/storefront, and compatibility with the chosen mod loader/SDK. The supplied build label is recorded, but does not settle these questions.
- Available Windows development tools and microphone/audio devices.
- An original name, personality, and repeatable placement for the single demo NPC. Defaults can be used until these are chosen.
- The available implementation time and Windows test machine access.
- Country/jurisdiction and applicable game license before making a legal assessment or presenting publicly.

Provider-specific choices are intentionally deferred, not missing prerequisites for the core architecture.

### What additional version information is needed?

`Build 459558 | Full Version` may identify a distribution/build label, but must not be interpreted as GTA SA 1.0, a particular regional executable, or a supported SDK target without checking the binary.

Before native implementation, collect:

1. Installation source/storefront and whether the executable has been modified by existing mods.
2. `gta_sa.exe` file size, Windows file/product version if present, and SHA-256.
3. Windows version and whether an ASI loader, CLEO, Mod Loader, graphics wrapper, or other injected plugin is already present.

Read-only PowerShell commands; replace the example path with the actual installation:

```powershell
$exe = Get-Item -LiteralPath 'C:\Games\GTA San Andreas\gta_sa.exe'
$exe | Select-Object Name, Length
$exe.VersionInfo | Select-Object FileVersion, ProductVersion, OriginalFilename
Get-FileHash -Algorithm SHA256 -LiteralPath $exe.FullName
```

Version fields may be blank. In that case, preserve the hash and verify compatibility against documented supported executables. These commands do not establish licensing or game compatibility by themselves. Never request or bundle the executable as part of the project.

## 2. Demo experience

1. Approach a configured NPC and select them with a configurable interaction key.
2. Hold a push-to-talk key, speak Malayalam, and release it.
3. Show the recognized transcript and processing state.
4. The NPC answers in one or two short Malayalam sentences using its own personality and conversation history.
5. Show Malayalam subtitles and play the NPC's selected ElevenLabs voice.
6. Leave, return, and ask about something discussed earlier.
7. Restart the backend and demonstrate that the same NPC still remembers.
8. End the conversation or reset its memory using explicit controls.

Example exchange: tell one NPC your preferred nickname and favorite food, return later, and ask what they remember. Use fictional/demo information, not sensitive personal details.

**V1 deliverable:** one selected in-game NPC, live voice conversation, persistent memory, and cancellation/reset controls. No second NPC is required to ship V1. Multi-NPC isolation can be exercised with synthetic IDs in backend tests; actual second-character integration belongs to the later scaling milestone.

### Single-NPC user stories

| ID | Story | Observable result |
| --- | --- | --- |
| U1 | Approach the demo NPC | Interaction becomes available only within the configured range |
| U2 | Start a conversation | The companion identifies the selected NPC and reports readiness |
| U3 | Hold and release PTT | Exactly one recording is captured and one turn begins |
| U4 | Speak in Malayalam | The recognized transcript is shown without forced English translation |
| U5 | Receive a reply | The NPC speaks Malayalam and matching subtitles are visible |
| U6 | Ask a follow-up | The NPC uses the actual prior exchange, not an unrelated scripted answer |
| U7 | Return after leaving | The NPC recalls a harmless previously supplied detail |
| U8 | Restart the backend | Persistent application memory remains available under the same world/player/NPC IDs |
| U9 | Cancel or walk out of range | Recording, pending work, and playback stop; late results are discarded |
| U10 | Reset memory | Existing history cannot reappear through a stale task or cached prompt |

### Example original character

Working profile: **Manu**, a friendly, mildly sarcastic passerby who enjoys Malayalam conversation. His biography is original, short, and unrelated to GTA's canonical plot. The player can tell him a fictional nickname or favorite snack, then ask him to recall it after reconnecting.

The profile is a replaceable configuration choice, not a reason to hard-code a `ManuController` or maintain a second dialogue implementation.

### Explicitly out of scope for the first demo

- Replacing every pedestrian's native AI or making everyone talk simultaneously.
- New missions, maps, a total conversion, or multiplayer integration.
- Cloning GTA actors or celebrities, or translating/distributing the original game script.
- Combat control, arbitrary model-generated game commands, or model-selected native function calls.
- Lip synchronization, positional audio, and native in-game Malayalam typography.
- Vector databases, autonomous NPC-to-NPC conversations, and a realtime full-duplex voice pipeline.
- Social graphs, gossip propagation, autonomous schedules, and scaling benchmarks involving hundreds of NPCs.
- Replacing GTA's pedestrian population manager or automatically assigning durable identities to all ambient pedestrians.

## 3. Architecture

```text
GTA SA process                         Local companion UI
  Thin C++ ASI adapter                   Microphone + Unicode subtitles
  NPC selection + PTT events             Audio playback + reset/cancel
           |                                      |
           | localhost TCP / NDJSON                | HTTP / WebSocket
           +------------------+-------------------+
                              |
                   Node.js / TypeScript backend
                     GameBridge + SessionManager
                              |
                         NpcRegistry
                              |
                  NpcController: one V1 instance
                 |          |          |          |
              STT adapter  Brain     Memory       TTS adapter
             ElevenLabs   OpenAI     SQLite       ElevenLabs
```

### Recommended stack

- **Game adapter:** a small Windows x86 C++ ASI plugin using Plugin-SDK, subject to an executable compatibility check. Select an ASI loader only after confirming support for the installed game.
- **Backend:** a supported Node.js LTS release, TypeScript, schema validation, and a small local HTTP/WebSocket server.
- **Storage:** SQLite with migrations; choose a driver compatible with the Windows Node version.
- **Speech integration:** an ElevenLabs adapter behind application-owned recognition/synthesis interfaces. Implementation/model/voice specifics are deferred.
- **Brain:** an OpenAI adapter behind an application-owned dialogue interface. Model selection is deferred.
- **UI/audio:** a localhost browser companion for the first milestone. It handles Unicode shaping, microphone capture, playback, and subtitles without changing the game's renderer.

Open the companion before playing, explicitly grant microphone access, and unlock audio playback through a user gesture. Test recording/playback while GTA has focus. If browser background capture is unreliable, wrap the companion in a small Electron shell; do not assume either approach works before testing.

**Presentation tradeoff:** the first version speaks through the computer's audio output and shows subtitles in the companion, not inside GTA. Use a second display or a combined recording layout. An in-game overlay is a later integration task.

### Dependency rules

1. The domain layer must not import GTA memory layouts, provider SDKs, HTTP framework objects, or browser APIs.
2. The native plugin never talks directly to ElevenLabs or OpenAI.
3. Provider adapters translate vendor responses into application-owned types.
4. The SQLite repository owns persistence queries; controllers do not issue SQL directly.
5. The companion does not create NPC identities, select provider credentials, or decide what should become long-term memory.
6. The backend session manager is the authority for selected NPC, active turn, cancellation generation, and delivery state.
7. Modules receive dependencies through constructors or factories, not mutable global singletons.

### Startup flow

1. Backend validates local configuration, opens storage, applies migrations, and registers one NPC profile.
2. Companion connects, authenticates, and performs an explicit microphone/audio readiness check.
3. Native adapter verifies its supported executable before installing hooks and connects to the backend.
4. The backend accepts a versioned native handshake and registers the current game session.
5. The native adapter binds the designated pedestrian to the application's NPC ID for this scene.
6. Interaction becomes enabled only after a valid game binding, backend connection, and companion readiness are present.

These components may start in another order. Missing components should produce a visible disconnected state and reconnect safely, not crash or deadlock.

### Keep the native adapter thin

The ASI plugin only selects/binds NPCs, detects input, reports lifecycle events, and displays a simple interaction indicator where supported. API calls, memory, microphone handling, and voice generation belong outside the game process.

- No network waits, audio processing, or database operations on the game thread.
- Networking runs on a worker with bounded queues; game APIs and entity checks run on the game thread.
- Initialize outside the Windows loader lock according to the selected loader's documented lifecycle.
- Backend failure must leave the game playable.
- Never persist raw pedestrian pointers or assume a recycled entity handle identifies the same NPC.

## 4. Shared NPC object model

Use composition and dependency injection rather than one subclass per character.

```ts
type NpcProfile = {
  id: string;
  displayName: string;
  personality: string;
  originalBackstory: string;
  language: "ml";
  voiceRef: string; // Application reference resolved by the speech adapter.
  memoryPolicy: "private";
};

interface NpcDependencies {
  stt: SpeechRecognizer;
  brain: DialogueBrain;
  tts: SpeechSynthesizer;
  memory: MemoryStore;
  events: ConversationEventSink;
}

// Illustrative design, not an implemented API.
class NpcController {
  constructor(
    readonly profile: NpcProfile,
    private readonly services: NpcDependencies,
  ) {}

  // handleUtterance(context, audio, signal): transcription -> memory
  // -> dialogue -> persistence -> TTS -> playback events.
}

const npcA = new NpcController(profiles.npcA, sharedServices);
// V1 registers only npcA. Later, the same factory creates more instances.
// const npcB = new NpcController(profiles.npcB, sharedServices);
```

### Module responsibilities

| Module | Responsibility |
| --- | --- |
| `NpcRegistry` | Load validated profiles and create/reuse controller instances |
| `NpcController` | Orchestrate a turn without depending on GTA-specific APIs |
| `SessionManager` | Own target selection, PTT state, cancellation, and single-speaker arbitration |
| `GameBridge` | Translate validated native messages into application events |
| `SpeechRecognizer` | Transcribe audio and expose bounded transcript results |
| `DialogueBrain` | Produce short character-consistent Malayalam responses |
| `SpeechSynthesizer` | Generate voice audio and report failures for subtitle fallback |
| `MemoryStore` | Read/write history and summaries with enforced NPC/player isolation |
| `CompanionUI` | Capture audio, display states/subtitles, play speech, acknowledge playback |

### Ownership and object lifetimes

- **Application lifetime:** provider adapters, SQLite connection, validated configuration, and the registry.
- **NPC lifetime:** immutable profile and a reusable `NpcController` instance. Do not permanently load all history into the instance.
- **Conversation lifetime:** selected entity binding, world/player/NPC key, dialogue state, and last activity timestamp.
- **Turn lifetime:** turn ID, cancellation signal, input artifact, context snapshot, generated response, and delivery status.
- **Scene lifetime:** native entity handles and binding generation. These become invalid on save load or game-world recreation.

Use a registry map even when V1 contains a single entry. Do not introduce a global `currentNpcMemory` variable or create provider credentials separately for each NPC.

### Provider-independent interfaces

These are design contracts, not complete SDK code:

```ts
type ConversationKey = {
  worldId: string;
  playerId: string;
  npcId: string;
};

type TurnContext = ConversationKey & {
  turnId: string;
  gameSessionId: string;
  sceneGeneration: number;
  memoryGeneration: number;
};

interface SpeechRecognizer {
  transcribe(input: AudioInput, signal: AbortSignal): Promise<Transcript>;
}

interface DialogueBrain {
  respond(input: DialogueInput, signal: AbortSignal): Promise<DialogueReply>;
}

interface SpeechSynthesizer {
  synthesize(input: SpeechRequest, signal: AbortSignal): Promise<AudioArtifact>;
}

interface MemoryStore {
  loadContext(key: ConversationKey): Promise<ConversationHistory>;
  recordAcceptedInput(context: TurnContext, text: string): Promise<void>;
  recordGeneratedReply(context: TurnContext, reply: DialogueReply): Promise<void>;
  markDelivery(context: TurnContext, receipt: DeliveryReceipt): Promise<void>;
  reset(key: ConversationKey): Promise<number>; // Returns new generation.
}
```

Validate inputs/outputs at the application boundary. Interfaces use bounded UTF-8 text and explicit audio metadata, not vendor-specific response objects. Provider test doubles should cover success, delay, cancellation, malformed responses, and failures without adding an offline runtime mode.

Provider clients and the database connection may be shared. Mutable dialogue state must be scoped to `(worldId, playerId, npcId)`; shared services must not contain a global conversation history.

**Adding an NPC:** add a profile and an explicit game-entity binding. No changes to the dialogue, voice, or memory pipeline should be necessary.

### Stable identity and lifecycle

- Give demo NPCs stable application IDs such as `demo-npc-001`.
- Bind those IDs to validated game entities for the current scene only. Prefer explicitly configured demo characters over randomly choosing a new pedestrian on every launch.
- Include a scene/session generation in messages so replies for a previous save/load cannot affect the current scene.
- On despawn, death, save load, or target loss: invalidate the binding and cancel the active turn; retain the persistent application memory.
- Rebinding must be explicit. Do not transfer memories to a pedestrian just because its recycled handle matches.
- Cross-NPC knowledge sharing is disabled by default; introduce deliberate, sourced sharing only as a later feature.

### Binding the first in-game NPC

Implement one explicit binding strategy after the native compatibility smoke test:

1. Configure a repeatable demo location and a stable application ID.
2. During scene setup, either bind an explicitly chosen existing pedestrian or create one using a verified game API. Choose one strategy; do not silently mix them.
3. If using an existing pedestrian, require explicit user selection and validation rather than treating a nearest-pedestrian search as durable identity.
4. If creating a demo pedestrian, record plugin ownership, avoid mission-critical characters, and clean it up using safe game APIs when the scene ends.
5. Track the runtime handle, scene generation, and validity separately from the application NPC ID.
6. Check range/alive/entity-valid state before interaction and before applying UI-related updates.
7. On invalidation, disable interaction until an explicit scene rebind succeeds.

**Do not promise that the NPC remains stationary or survives indefinitely.** Idle behavior, location, and ownership must be tested against the actual executable. In-place save/load, mission transitions, and pedestrian pool recycling need dedicated validation.

## 5. Conversation and voice pipeline

State machine:

```text
IDLE -> SELECTED -> RECORDING -> TRANSCRIBING -> THINKING
     -> SYNTHESIZING -> SPEAKING -> SELECTED
Any active state -> CANCELLED / ERROR -> SELECTED or IDLE
```

1. Lock the selected NPC, player, and scene generation when recording begins.
2. Capture microphone input only while PTT is active. Initially cap utterances at 15 seconds and reject empty/oversized audio. The configured capture limit must also obey the chosen provider's actual upload/format limits.
3. Send supported audio to STT; validate the transcript and display it. Preserve Malayalam and code-switching rather than forcing English.
4. Load the NPC profile, a bounded recent history, and an optional stored summary.
5. Ask the brain for a short spoken response. Require Malayalam script; permit natural English loanwords where appropriate.
6. Validate a minimal response schema such as `{ "replyMl": "..." }`. Avoid game actions in the first milestone.
7. Persist the accepted player utterance and generated response with a turn ID and delivery status.
8. Synthesize through the ElevenLabs speech adapter using the profile's speech reference. Display subtitles as soon as reply text is available.
9. Play audio and record playback acknowledgement. Distinguish generated, displayed, spoken, partially played, and failed replies.
10. Future context must not describe an unplayed/undisplayed reply as something the player heard. Use delivered replies in normal dialogue history.

### State transition rules

| Event | Preconditions | Result |
| --- | --- | --- |
| Select NPC | Native binding valid, within range, no active turn | Enter `SELECTED`; companion shows original profile name |
| PTT down | Selected NPC valid, speech capture ready | Freeze target/context and enter `RECORDING` |
| PTT up | Recording active | Finish one audio artifact and enter `TRANSCRIBING` |
| Transcript accepted | Same turn/scene/memory generation still active | Persist input, load context, enter `THINKING` |
| Reply validated | Turn still active, reply within bounds | Persist generated reply, show subtitle, enter `SYNTHESIZING` |
| Audio ready | Turn still active and playback permitted | Start playback and enter `SPEAKING` |
| Playback completes | Receipt matches current turn | Mark delivered; return to `SELECTED` |
| Cancel/target loss | Any active stage | Abort work, stop capture/playback, invalidate turn |
| Scene changes | Any stage | Cancel, discard runtime binding, enter `IDLE` |
| Backend reconnects | No valid active turn | Perform handshake; require target validity before readiness |

Repeated key-down events, duplicate messages, and out-of-order provider results must not create additional turns. On game pause or loss of game focus, end/cancel recording to avoid an indefinitely held PTT state. Preserve the memory of already accepted player input, but do not present an incomplete reply as delivered.

### Dialogue context policy

Construct each brain request from a small, deterministic set of sections:

1. Language/length rules and the requirement not to invent remembered facts.
2. The original NPC profile and its version.
3. A bounded, provenance-aware conversation summary if enabled.
4. Recent accepted player turns and delivered NPC replies.
5. The current user transcript, added exactly once.

Store the accepted transcript before generating a reply, but exclude that same turn from the historical context query to prevent duplication. Treat remembered content as quoted data, not privileged instructions. Do not provide GTA story scripts or actor dialogue as personality prompts.

### Audio behavior

- Exactly one player conversation and one NPC speaker at a time in the MVP.
- Stop microphone capture during NPC playback to prevent feedback; no full-duplex barge-in initially.
- Provide an explicit cancel action; discard late STT/LLM/TTS results using turn and scene IDs.
- Stop playback if the NPC becomes invalid or the conversation ends.
- Preserve generated text when TTS fails and clearly label subtitle-only fallback.
- Do not blindly retry a whole turn: provider retries can duplicate costs and replies. Use bounded per-stage retries and idempotent local turn handling.
- Default audio playback is non-positional. Positional playback and lip sync are stretch goals.

### Latency and quality

- Instrument recording end, STT completion, LLM completion, first playable audio, and playback completion.
- Aim for first audible reply within approximately eight seconds of PTT release; this is an initial benchmark, not a guarantee.
- Keep answers to one or two sentences and avoid full history in every request.
- Use an initial configurable 30-second total turn deadline, then tune after measurements.
- Evaluate at least ten Malayalam samples, including code-switching, two speaker styles, and background game audio.
- Check Malayalam recognition and pronunciation at live-integration time. Provider selection is fixed at the service level, but successful language quality is still something to measure.
- Keep provider configuration behind adapters. Model/voice selection is deliberately deferred and is not part of the current architecture decision.

## 6. Persistent memory

Use ordinary application storage, not model training.

### Initial schema

- `npc_profiles`: profile ID and profile version, or load these from validated configuration files.
- `conversations`: conversation ID, world ID, player ID, NPC ID.
- `turns`: unique turn ID, conversation ID, transcript, response, status, delivery metadata, memory generation, timestamps.
- `memory_summaries`: conversation ID, summary, last covered turn ID, version.
- `memory_scopes`: unique world/player/NPC key and reset generation for guarding concurrent writes.

SQLite operations should use parameterized statements and transactions. Store data outside the game installation and source tree, for example under `%LOCALAPPDATA%\MalayalamNpcDemo`.

### Storage invariants

- Enforce a unique constraint on `(world_id, player_id, npc_id)` for each conversation scope.
- A unique turn ID prevents duplicate persistence when messages are retried.
- Generated reply, subtitle delivery, and audio playback are separate facts; track them separately.
- Reset increments the scope generation transactionally. Pending writes with an older generation are rejected, even if their API call finishes after reset.
- Prompt context is loaded afresh for each turn, so deleted history cannot survive in a controller's cached prompt.
- A backend restart restores history, not an in-flight recording or partially played response.
- A game save rollback does not automatically rewind SQLite. V1 uses one explicit demo `worldId`; document that memory is external to game saves. A future save-slot/world-linking feature must specify rollback behavior deliberately.
- Profile updates retain conversation identity. Record profile versions in turns; offer an explicit reset if a major character change makes old memories inconsistent.

### Memory tiers

| Tier | V1 status | Purpose |
| --- | --- | --- |
| Recent turn history | Required | Maintain continuity and recall details within the bounded demo |
| Persistent storage | Required | Recover conversation after backend restart |
| Rolling summary | Optional after V1 works | Keep longer histories within a context budget |
| Structured facts | Later | Track explicit preferences with source turn and correction handling |
| Cross-NPC knowledge | Out of scope | Requires deliberate visibility and provenance rules |

Do not advertise unlimited recall. V1 guarantees recall only for retained, included demo history. If a fact falls outside the context window and no summary/fact store preserves it, the NPC should acknowledge uncertainty.

### Recall policy

- Begin with the most recent delivered turns, bounded by a token/character budget.
- Add rolling summaries only after the first persistent-memory milestone works; no vector store is needed for the demo.
- Preserve user-stated details with provenance. Do not convert NPC guesses into confirmed player facts.
- Treat user messages and recalled text as untrusted data, not system instructions.
- If a detail is unknown, have the NPC admit it rather than invent a memory.
- Keep memory private to each NPC/player/world key and test isolation explicitly.
- Store memories about the demo interaction; avoid collecting sensitive personal information.
- Provide reset-current-NPC and reset-demo-memory controls with confirmation. Reset also cancels pending writes and clears in-memory context and associated temporary audio.
- A new game/world must not silently inherit another world's memory.

### Corrections and contradiction handling

- New player statements may correct earlier ones; preserve their source turns rather than deleting evidence arbitrarily.
- Prefer the latest explicit correction when answering about a player preference.
- Do not turn a generated joke, hallucination, or uncertain STT transcript into an authoritative player fact.
- If extraction/summarization is added later, keep it outside the critical voice path and allow inspection/reset of the result.
- Do not add autonomous relationship scores or inferred sensitive traits to the first version.

## 7. Local bridge and security

Suggested native protocol: UTF-8 newline-delimited JSON over loopback TCP, with a versioned handshake, local session token, message-size limits, and bounded buffering. The backend also serves the companion via localhost HTTP/WebSocket.

Envelope fields: `protocolVersion`, `messageId`, `type`, `gameSessionId`, `sceneGeneration`, `npcId`, `turnId` where applicable, and a schema-validated payload.

Initial events:

- Native -> backend: `hello`, `npc.selected`, `npc.invalidated`, `ptt.started`, `ptt.stopped`, `conversation.cancelled`, `scene.changed`.
- Backend -> companion: `capture.start`, `capture.stop`, `conversation.state`, `transcript.ready`, `reply.ready`, `audio.ready`, `playback.stop`.
- Companion -> backend: audio upload, `playback.started`, `playback.completed`, cancellation/reset requests.
- Backend -> native: connection health and simple conversation status; no arbitrary native calls.

### Example protocol message

```json
{
  "protocolVersion": 1,
  "messageId": "local-message-001",
  "type": "ptt.started",
  "gameSessionId": "game-session-001",
  "sceneGeneration": 3,
  "npcId": "demo-npc-001",
  "payload": {}
}
```

The backend allocates the authoritative turn ID after validating readiness; do not trust a client-supplied ID to establish a new conversation. Authentication belongs in the connection handshake, not in every logged event. Binary microphone/audio payloads travel through bounded companion upload/download endpoints, not as base64 inside native NDJSON messages.

### Transport behavior

- Parse partial/coalesced TCP reads correctly; one socket read is not one message.
- Use an initial 64 KiB limit for a control-message frame, rejecting invalid UTF-8/JSON and oversized buffered frames.
- Use a bounded event queue and coalesce replaceable health/state updates rather than allowing unbounded growth.
- A version mismatch fails closed with an actionable error, not best-effort native execution.
- Reconnect with capped backoff. A new connection must not resume old turn ownership implicitly.
- Require one active native adapter and one active companion capture/playback owner for V1. A second tab must explicitly take over or remain read-only.
- Stop capture/playback on connection loss and perform a fresh readiness check after reconnect.

Security rules:

- Bind all services to `127.0.0.1`, not public interfaces.
- Authenticate local clients and restrict allowed browser origins; loopback alone is not sufficient protection against a malicious web page.
- Keep provider API keys in backend environment variables only—not in the ASI plugin, browser, profiles, screenshots, or Git.
- Never evaluate generated scripts, commands, addresses, or filesystem paths.
- Enforce audio/text/message-size limits, request concurrency, timeouts, and a configurable cost ceiling.
- Audio endpoints use opaque identifiers with expiry, not caller-supplied file paths.
- Log IDs, state transitions, errors, and timing by default, not raw conversations or microphone recordings.
- Treat save/load, disconnect, and reset as cancellation boundaries; stale responses must never resume an old conversation.

## 8. Proposed project layout

```text
gta-sa-malayalam-npcs/
  native/
    src/                     # ASI bootstrap, entity binding, PTT, bridge
    tests/                   # Protocol/frame parsing and lifecycle tests
  backend/
    src/
      npc/                   # Shared controller, registry, profile validation
      sessions/              # State machine, turn ownership, cancellation
      providers/             # OpenAI brain and ElevenLabs speech adapters
      memory/                # SQLite repository, migrations, summaries
      bridge/                # Native protocol and companion transport
      config/                # Environment/schema validation
    tests/                   # Unit/integration tests with stubbed provider responses
  companion/
    src/                     # Microphone, subtitles, playback, controls
  profiles/                  # Original NPC profiles; no credentials
  fixtures/                  # Synthetic protocol/dialogue fixtures
  docs/                      # Windows setup, compatibility, demo runbook
  .env.example               # Placeholder credentials and local settings
  .gitignore                 # Secrets, recordings, databases, build output
```

Initial local configuration keys: `BRIDGE_HOST`, `BRIDGE_PORT`, `UI_PORT`, `DATA_DIR`, `WORLD_ID`, `PLAYER_ID`, `NPC_PROFILE_PATH`, `INTERACTION_KEY`, `PTT_KEY`, `INTERACTION_RADIUS`, `TURN_TIMEOUT_MS`, `MAX_AUDIO_SECONDS`, and `DEMO_COST_LIMIT`.

Profiles refer to speech through an application-owned `voiceRef`. Resolve that reference inside the ElevenLabs integration configuration later. Provider credentials are server-side secrets populated at live integration; model/voice specifics are not required to scaffold the project.

Obtain the local bridge token at startup or from a protected local configuration file. Local state, API keys, and game installations must not be committed.

### Configuration organization

- `config/app.json`: public local behavior, time/range limits, world/player IDs, and profile paths.
- `config/native.json`: game executable compatibility record, binding strategy, and input choices; no runtime pointers.
- `profiles/demo-npc-001.json`: the single original NPC definition.
- Backend environment/secrets: provider credentials and bridge authentication.
- Integration-specific settings: deferred OpenAI/ElevenLabs choices, kept separate from profile semantics.

Validate configuration at startup and report field-specific errors. Do not silently choose a random NPC or voice when a referenced profile/configuration is missing.

## 9. Implementation milestones

### Phase 0: Windows compatibility and integration preflight

- [ ] Record the supplied `Build 459558 | Full Version` label, collect executable metadata/hash, and establish a supported native target.
- [ ] Check the applicable game terms and AI restrictions; do not infer permission from being free or demo-only.
- [ ] Confirm the proposed demo scope and presentation requirements; defer provider/model specifics until live integration.
- [ ] Compile/load a harmless ASI smoke test using the actual installed executable.
- [ ] Validate safe entity selection and a simple input event without model calls.
- [ ] Verify microphone capture while GTA has focus and audio playback with the companion in the background.
- [ ] Document a repeatable single-NPC binding/location strategy and an uninstall/restore procedure.

**Gate:** if native compatibility fails, investigate a documented compatible approach without cracked binaries or DRM bypasses. A companion-only showcase must be labeled as such, not represented as an integrated mod. Do not build gameplay hooks against an assumed executable version. Legal permission and technical compatibility remain distinct checks.

Deliverables: compatibility record, harmless native smoke-test log, and a description of how the first pedestrian is bound and invalidated.

### Phase 1: reusable backend, no game dependency

- [x] Implement provider interfaces and live adapters; isolate tests with stubbed responses.
- [x] Implement `NpcController`, registry, and session state machine.
- [x] Register exactly one profile through the reusable factory.
- [x] Implement validated, short Malayalam responses and error/cancellation handling.
- [x] Add deterministic unit and adapter tests.

**Repository progress (10 October 2026):** The backend and local companion use the same profile registry/controller. Deterministic checks cover live-provider request construction, SQLite restart persistence, private scope isolation, delivery status, and stale reset writes. A previous CLI and provider-free companion were removed; the current app requires live provider credentials. Live microphone/provider quality still needs an opt-in real-device round trip.

**Exit:** the single controller is covered by deterministic provider stubs outside GTA; the running app uses live providers. A synthetic second ID can be used in tests, but a second in-game NPC is not implemented here.

Deliverables: shared interfaces, controller, registry, session state machine, live provider adapters, and deterministic provider-stub tests.

### Phase 2: persistence

- [x] Add SQLite schema setup and parameterized memory storage.
- [x] Persist delivered turns and load bounded history after restart.
- [x] Add NPC/world/player isolation and confirmed reset controls.
- [x] Check that undelivered replies are excluded from history; summaries are not implemented in V1.

**Exit:** the single NPC recalls prior information after restart, and synthetic-scope tests prove that reads/writes do not leak across IDs.

Deliverables: schema migrations, context-loading policy, generation-guarded reset, and restart/isolation tests.

### Phase 3: voice companion

- [x] Implement PTT capture, bounded audio upload, and transcription display.
- [x] Connect the ElevenLabs speech adapter for recognition and generation; add environment configuration.
- [x] Connect the OpenAI dialogue adapter without changing the controller contract.
- [ ] Validate a complete Malayalam voice exchange outside GTA before coupling it to input hooks.
- [x] Add playback acknowledgement and delivery-state handling.
- [x] Add processing indicators, subtitles, cancel, mute, and text-input fallback.
- [x] Add a live-turn cap, total turn timeout, and stage-level latency measurements.

**Status:** implementation is present, but the exit condition is still pending a live microphone/provider/device round trip. Provider request shapes are checked with stubbed responses only.

Deliverables: local companion UI, live provider adapters, stage timing, and an unverified microphone/playback path. Audio behavior with GTA focused remains untested.

### Phase 4: in-game integration

- [ ] Bind a configured NPC to a stable application identity.
- [ ] Forward interaction/PTT/lifecycle events through the native bridge.
- [ ] Test target lock, entity invalidation, save/load cancellation, and reconnection.
- [ ] Keep the scene and registry restricted to the single configured NPC.

**Exit:** live conversations are demonstrably tied to the selected in-game NPC, not a manually selected disconnected chatbot.

Deliverables: native adapter, validated protocol, scene lifecycle integration, and the first complete in-game voice/memory exchange.

### Phase 5: demo hardening

- [ ] Prepare a repeatable save/location and the single original NPC profile.
- [ ] Rehearse memory recall, isolation, restart, and reset.
- [ ] Test network/provider failures and expired/late audio results.
- [ ] Check recording layout, microphone clarity, and game/music volume.
- [ ] Record a backup video; label any cached or prerecorded fallback explicitly.
- [ ] Document the tested game/SDK/loader/provider versions and launch instructions.

**Exit:** all V1 acceptance criteria below pass on the actual Windows demo machine. Freeze the working single-NPC demo before starting multi-NPC integration.

### Phase 6: later multi-NPC scaling

- [ ] Add a second profile and a second explicit game binding through the same factory.
- [ ] Introduce deterministic target selection when interaction ranges overlap.
- [ ] Preserve one active player conversation/voice at a time; cancel cleanly before switching targets.
- [ ] Verify independent personalities, speech references, and persistent memories.
- [ ] Add per-NPC cooldowns, bounded registry lifecycle, and aggregate provider limits.
- [ ] Test one NPC's failure/despawn without affecting another's stored state.

**Exit:** additional NPCs are data plus bindings, not copied controllers. Only expand beyond a small handful after measuring actual load and memory behavior.

## 10. Single-NPC V1 acceptance tests

1. Exactly one configured profile is instantiated through the reusable controller/registry path; no character-specific controller subclass is required.
2. The NPC accepts Malayalam speech and produces intelligible spoken Malayalam replies.
3. The NPC remembers a player-stated demo detail after backend restart; synthetic IDs cannot access its history.
4. Selection changes cannot redirect an in-progress utterance or response to the wrong NPC.
5. Save load, despawn, cancellation, and reset suppress stale replies and audio.
6. Generated-but-undelivered speech is distinguishable from delivered dialogue in memory.
7. Disconnecting the backend does not freeze or crash GTA.
8. TTS failure leaves usable subtitles; STT failure permits an explicitly selected text fallback.
9. Malayalam subtitles render correctly in the companion; native glyph rendering is not assumed.
10. Reset removes associated application memory and temporary artifacts, not arbitrary game files.
11. No credentials, personal recordings, database files, or proprietary game assets appear in the repository/package.
12. The demo accurately labels live, cached, and prerecorded behavior.

### Test layers

| Layer | Required checks | Tools/approach |
| --- | --- | --- |
| Domain unit tests | Profile validation, turn ownership, transitions, context construction | Provider stubs and fake clock |
| Memory integration tests | Persistence, isolation, duplicates, reset during pending turn | Temporary SQLite database |
| Protocol tests | Split/coalesced frames, invalid JSON, oversized messages, stale generations | Native/backend fixtures |
| Companion tests | PTT down/up, focus loss, empty recording, permission denial, duplicate playback | Browser automation plus device smoke tests |
| Provider integration | One Malayalam round trip, failures, cancellation, latency | Explicit opt-in live tests; no credentials in fixtures |
| Game integration | Binding, range, despawn, save load, backend disconnect | Manual Windows runbook on the verified executable |

### Important failure scenarios

- PTT is released after the game loses focus.
- The NPC despawns while the brain is generating its reply.
- Memory is reset immediately before a delayed reply returns.
- The backend crashes after storing a generated reply but before playback acknowledgement.
- A reconnect resends an already accepted input or completed playback receipt.
- The companion closes mid-recording or mid-playback.
- Recognition produces an empty or obviously unusable transcript.
- TTS fails after subtitles are displayed; the stored delivery state reflects the actual fallback.
- Two companion tabs attempt to capture/play audio simultaneously.
- A game save reload reuses a pedestrian handle from the previous scene.

## 11. Legal, licensing, and privacy checklist

This plan is technical guidance, not legal clearance. A public hackathon presentation is not automatically covered by private-use or non-commercial exceptions.

- Confirm the agreement governing the exact San Andreas edition. Rockstar's single-player mod statement is a discretionary non-enforcement policy, not a license, and contains exclusions.
- Review applicable Take-Two generative-AI restrictions before providing game content to models or using GTA to promote an AI integration. Independent characters or an external backend do not automatically resolve those restrictions.
- Do not upload original GTA scripts, actor recordings, cutscenes, or extracted assets to providers without the necessary rights.
- Use original character biographies/dialogue and authorized ElevenLabs voices; verify the voice-specific and service-specific terms.
- ElevenLabs free and paid plans have different publication/commercial-use conditions. Do not assume a prize-bearing or sponsored event qualifies as non-commercial; verify the proposed use.
- OpenAI/provider output permissions do not grant rights to Rockstar's game or third-party voices/content.
- Use a legitimate game installation; do not distribute executables, cracks, proprietary archives, save content containing extracted assets, or DRM workarounds.
- Keep the project single-player and do not imply Rockstar endorsement.
- Obtain participant permission before recording demo speech and explain which providers receive it.
- Capture only on explicit PTT. Keep raw audio ephemeral, clean it up on every success/failure/cancellation path, and provide deletion controls for retained transcripts.
- Verify provider retention/data-use settings. Deleting local memory does not necessarily delete provider-held records; disclose that distinction.
- Retain only necessary demo history, use fictional details, and set a retention policy before inviting others to play.
- If applicable permissions remain uncertain, obtain clarification/legal advice or demonstrate the reusable NPC system in a scene using assets you own. Do not describe uncertainty as approval.

## 12. Demo runbook

1. Validate provider readiness, applicable permissions, available credits, and microphone/audio devices.
2. Start the backend, unlock companion audio, and grant microphone access.
3. Launch GTA and verify native bridge health and NPC identity bindings.
4. Load the prepared save; approach NPC A and have a live Malayalam exchange.
5. Share a harmless fictional preference and ask a follow-up.
6. End the conversation and walk away from the single NPC.
7. Return to the same bound NPC and demonstrate recall; optionally restart the backend to prove persistence.
8. Explain that future characters use the same controller with new profile/binding data. Do not imply multiple NPCs are already integrated.
9. Explain the actual scope: external AI services, application memory, non-positional companion audio, and no claim of model training or an AI running inside GTA.
10. Reset demo data after presentation. If services fail, switch to a visibly labeled fallback or the backup recording.

## 13. Windows environment, build, and launch plan

### Development prerequisites

- The installed classic PC game and access to its executable metadata—not a bundled copy of the game.
- Windows C++ build tools compatible with the selected SDK/loader and the game's measured executable architecture; classic x86 is the expected target, but verify rather than infer from the build label.
- A supported Node.js LTS installation, package manager, and working SQLite driver.
- A browser/companion environment with microphone permission and an available output device.
- A writable application data directory and loopback ports that are not occupied.

### Installation safety

1. Inventory existing injected plugins, graphics wrappers, and loaders before adding anything.
2. Back up only files that the installation process will actually modify. Avoid overwriting existing loader DLLs without investigating conflicts.
3. Choose the loader variant/name according to documented support for the actual executable.
4. Place only the project's own ASI/configuration artifacts in the documented plugin location.
5. Keep provider credentials and memory data outside the game directory.
6. Maintain an installation manifest so uninstall removes only files this project installed and restores backed-up files deliberately.
7. Test with a clean save/location and minimal unrelated mods where possible; do not remove the user's existing mods automatically.

### Proposed developer commands

These are script names to implement, not existing executable commands:

- `dev`: start the backend and companion with configured live providers.
- `test`: run domain, memory, and protocol tests without paid API calls.
- `test:live`: explicit opt-in provider smoke tests.
- `build`: build the TypeScript applications and static companion assets.
- `build:native`: documented Windows native build for the verified target.
- `doctor`: print non-secret configuration, port readiness, storage readiness, game compatibility record, and component connection state.

### Startup checklist

1. Start the backend and check configuration/migrations succeeded.
2. Open the companion, authenticate, grant microphone access, and run the audio readiness check.
3. Launch GTA with the verified plugin setup.
4. Confirm the versioned handshake and the single NPC's current binding.
5. Verify that PTT is unavailable until the whole chain is ready.
6. Perform one harmless exchange before recording or presenting the demo.

### Shutdown checklist

1. End/cancel the active turn and stop capture/playback.
2. Close the game; invalidate runtime handles and stop native networking.
3. Stop the companion/backend cleanly and flush committed database operations.
4. Remove expired temporary audio and inspect whether demo history should be retained or reset.
5. Do not restart an interrupted conversation automatically on the next launch.

## 14. Observability and debugging

### Correlation fields

Attach `gameSessionId`, `sceneGeneration`, `npcId`, `turnId`, and `memoryGeneration` to structured events. Add a monotonic sequence or explicit transition counter to identify out-of-order messages.

### Safe default logging

Log connection changes, readiness, selected NPC ID, state transitions, error codes, durations, queue sizes, and provider usage metadata where available. Do not log raw audio, API keys, full transcripts, or complete prompts by default.

A temporary debug mode may expose additional local information only after explicit consent. It must be clearly visible and easy to disable; exclude its outputs from version control and demo uploads.

### Companion diagnostics panel

Display:

- Native bridge, backend, and speech/dialogue adapter readiness.
- Current NPC name/application ID and scene-binding validity.
- Current state and turn ID.
- Last-stage timing and a concise actionable error.
- Whether the displayed result is live, cached, or a fallback.
- Reset, cancel, mute, and test-audio controls.

### Debugging order

1. Reproduce a domain/memory issue with stubbed provider clients, outside GTA.
2. Reproduce microphone/playback behavior with the companion, outside GTA.
3. Validate the native handshake and PTT events without live providers.
4. Validate one provider turn without native events.
5. Connect the verified pieces and inspect correlation IDs rather than changing several layers simultaneously.

## 15. Scaling design: prepared now, implemented later

### What must exist in V1

- A profile-driven constructor/factory and registry keyed by stable NPC ID.
- Storage keys containing world, player, and NPC identifiers, despite there being only one visible character.
- Shared service adapters with no shared mutable conversation history.
- Explicit turn ownership, scene generation, and memory reset generation.
- A speech reference on each profile rather than one globally hard-coded voice.

### What is deliberately not built in V1

- Multi-target proximity search and ambiguous-target UI.
- Background inference for inactive NPCs.
- Simultaneous speakers, autonomous schedules, social graphs, or shared memories.
- Pooling or distributed services merely to support hypothetical scale.

### Expansion sequence

1. Add one second character through profile and binding configuration.
2. Prove target selection, memory isolation, and restart behavior.
3. Add a small handful of characters with bounded registration and lifecycle cleanup.
4. Measure game-thread overhead, storage growth, token/context usage, and total inference spend.
5. Add per-NPC idle eviction and memory compaction only if measurements justify them.
6. Introduce multi-party conversation or controlled knowledge sharing only as separate features with explicit rules.

At each step, keep one speaker active unless a new arbitration model has been implemented and tested. Scaling should not create one timer/API request stream per idle pedestrian.

## 16. Risk register and fallbacks

| Risk | Consequence | Mitigation / fallback |
| --- | --- | --- |
| Build label does not identify a supported executable | Hooks may fail or crash | Hash/version preflight; reject unsupported target before hooking |
| Existing loader/graphics mods conflict | Instability or failed plugin loading | Inventory and test a minimal setup without discarding user files |
| Native development takes longer than expected | No in-game integration | Finish the browser companion independently; label the unintegrated showcase honestly |
| Background microphone/audio restrictions | Voice stops when GTA is focused | Preflight with the actual machine; optional companion desktop shell |
| Pedestrian lifecycle changes | Wrong entity receives a response | Stable app identity plus scene-generation binding and cancellation |
| Late provider result after reset | Deleted memory reappears | Transactional reset generation checked on every write |
| Recognition quality is poor | NPC answers the wrong question | Show transcript; offer explicit text correction/input fallback |
| Speech synthesis is slow/unavailable | Long silence or missing audio | Immediate subtitles and a clearly identified text-only fallback |
| Long/unbounded history | High cost and slow replies | Bounded context; optional later summaries; no unlimited-recall claims |
| Live service outage | Demo interruption | Prepared, clearly labeled recording or documented offline fallback |
| Licensing/privacy uncertainty | Public presentation or distribution risk | Review actual agreements and proposed use; do not treat technical success as permission |

## 17. Immediate next actions

1. Identify a GTA executable whose verified memory layout is recognized by the proximity scanner, or choose a documented API/interface for this installation.
2. Run the backend and validate token-authenticated bridge health, selection, typed response, delivery, and cancellation; the attempted local smoke process could not start because desktop policy blocked process creation.
3. Load the ASI with the installed compatible loader and verify the overlay/key path on a supported executable before relying on proximity behavior.
4. Perform an opt-in live voice exchange outside GTA and check Malayalam transcription and pronunciation.
5. Run the in-game proximity, subtitle, audio, cancellation, memory, restart, and reset flow; add native PTT only if the demo requires it.
6. Rehearse the tested demo; add another NPC only after V1 is stable.

**Automatic proximity remains gated on:** a verified executable layout and a working loader on that executable. The current provider defaults are `gpt-4.1-mini`, ElevenLabs Scribe v2, and Eleven v4. The current local `.env` selects a Malayalam-labeled voice available in the account; no account-specific voice ID or API key is committed. Voice quality has not been checked.

## 18. External technical and policy references

- Plugin-SDK: https://github.com/DK22Pac/plugin-sdk
- Ultimate ASI Loader: https://github.com/ThirteenAG/Ultimate-ASI-Loader
- OpenAI developer documentation: https://developers.openai.com/
- OpenAI API/business agreement: https://openai.com/policies/business-terms/
- ElevenLabs documentation: https://elevenlabs.io/docs
- ElevenLabs output publication guidance: https://help.elevenlabs.io/hc/en-us/articles/13313564601361-Can-I-publish-the-content-I-generate-on-the-platform
- ElevenLabs terms and voice-library conditions: https://elevenlabs.io/terms-of-use
- Rockstar single-player mod statement: https://support.rockstargames.com/articles/5NVOAYjcTomO8v6SX2k76k/pc-single-player-mods
- Rockstar legal documents: https://www.rockstargames.com/legal

Recheck versions, language support, licenses, and policies against the actual accounts/game before implementation and presentation.
