# aisim — Architecture Design Notes

> **Status:** concept / design exploration. No code yet.
> **Audience:** a single-user **desktop application**, C++23/26 Core, heavy
> multithreading, modular so each piece evolves independently.
> **Explicit non-goal:** multi-tenant scale. We deliberately avoid
> "enterprise" architecture and pick the *least* machinery that satisfies the
> requirements.
>
> **Product north star (see [`use-cases.md`](./use-cases.md) §0):** aisim is a
> **personal coding-practice companion**. The goal is **code practice driven by
> the skill gap** — the skills the user's target job descriptions demand minus
> the skills they already have (from their CV), weighted by demand and decaying
> with mastery: `priority = demand × (1 − mastery)`. CV/JD analysis is a
> **feeder** that aims the practice engine; it is **not** interview or career
> coaching. This doc covers the *technical shape* (transport, modules,
> concurrency) — the shape is unchanged by the product framing, but the domain
> layer (`app`/`domain`) and module list below reflect it.
>
> **Companion docs:**
> - [`use-cases.md`](./use-cases.md) — product north star, use cases UC1–UC8,
>   consistency analysis, domain model (the skill-gap loop).
> - [`module-design.md`](./module-design.md) — per-module modern-C++ features,
>   challenges, class design for low coupling/testability, boundary entities +
>   interfaces, and design alternatives, with Mermaid UML diagrams.
> - [`roadmap.md`](./roadmap.md) — phased, Obsidian-ready task list: walking
>   skeleton → practice engine (first) → skill feeder → gap wiring.

---

## 1. Locked decisions

These were settled up front because they fork the whole design:

| Concern | Decision | Why |
|---|---|---|
| **UI ↔ Core transport** | **HTTP/REST** on localhost | Universal — every language has an HTTP client with zero codegen; mobile speaks it natively; trivially debuggable with `curl`. |
| **Live feedback channel** | **WebSocket** (full duplex) | Streaming model tokens + progress + UI-initiated control messages over one persistent connection. |
| **AI engine placement** | **In-Core module** on a thread pool, behind an **engine-agnostic `AiPort`** | Lowest latency, simplest. The engine (Ollama today) is *already* a separate process, so its crash domain is isolated. Agnosticism + capability discovery let per-task model routing and graceful degradation (no embeddings → fall back). |
| **Storage** | **SQLite + `sqlite-vec`** | One embedded engine, one file, no server. Covers settings, the **skill/CV/JD/task/mastery** domain data, conversation history, and vector search over skills/tasks. |
| **Mobile** | Same HTTP/WebSocket API as the desktop UI | No second interface to maintain. Assumed same-LAN reach (revisit if "from anywhere" is needed → relay + auth). |

Everything below builds on these.

---

## 2. The big picture

```
   ┌──────────────┐         ┌──────────────┐
   │  Desktop UI  │         │  Mobile app  │      clients — any language
   │ (any lang)   │         │  (Kotlin/    │
   └──────┬───────┘         │   Swift)     │
          │                 └──────┬───────┘
          │  REST (commands, CRUD) │
          │  WebSocket (live feed) │
          └───────────┬────────────┘
                      ▼
        ┌─────────────────────────────────┐
        │        aisim Core (C++23/26)     │   one long-lived local process
        │                                  │
        │  ┌────────────────────────────┐  │
        │  │  API Edge                  │  │  HTTP + WebSocket server,
        │  │  (REST handlers + WS hub)  │  │  (de)serialization, auth
        │  └─────────────┬──────────────┘  │
        │                │ in-process calls │
        │  ┌─────────────▼──────────────┐  │
        │  │  Application / Domain      │  │  use-cases, orchestration,
        │  │  (command handlers)        │  │  the "what the app does"
        │  └──┬───────────┬─────────┬───┘  │
        │     │           │         │      │
        │  ┌──▼────┐  ┌───▼────┐ ┌──▼────┐ │
        │  │  AI   │  │Storage │ │ Event │ │   domain: SkillGap / Mastery /
        │  │module │  │  (DB)  │ │  bus  │ │   Taxonomy / Extractor live in app
        │  └──┬────┘  └────────┘ └───────┘ │
        │     │ AiPort (engine-agnostic)    │
        └─────┼────────────────────────────┘
              │ HTTP (localhost:11434)
        ┌─────▼──────┐      loads & runs
        │  AI engine │ ───────────────────▶  qwen2.5-coder:7b (+ optional
        │ (Ollama /  │                        embedding model) — swappable
        │   …)       │
        └────────────┘
```

**Key idea:** the Core is a *process* that exposes an API, not a library the UI
links. That single choice is what makes "UI in any language" and "mobile sends
commands" fall out naturally — both are just HTTP/WebSocket clients.

---

## 3. The modules (independently maintainable pieces)

Each module is a separate CMake target (a static library) with a **header-only
public interface** and a hidden implementation. Modules depend on *interfaces*,
not on each other's concrete types — so any one can be rewritten without
touching the others. This is the whole "modular, maintained separately"
requirement, achieved with nothing more than disciplined layering.

```
aisim/
├─ core/
│  ├─ api/          # REST + WebSocket edge  (depends on app)
│  ├─ app/          # domain / use-cases     (depends on ports below)
│  │                #   skill-gap loop: Taxonomy, StructuredExtractor,
│  │                #   GapService, practice/feeder use-cases
│  ├─ ai/           # engine orchestration   (implements AiPort/EmbeddingPort)
│  ├─ storage/      # SQLite + sqlite-vec    (CvStore/JobStore/TaskStore/…)
│  ├─ events/       # in-process event bus / pub-sub
│  └─ common/       # Error, Result, concepts, logging, config
├─ apps/
│  └─ aisimd/       # the daemon: wires modules together + main()
└─ clients/
   ├─ desktop-ui/   # any language; lives in its own repo/dir
   └─ mobile/       # any language; talks the same API
```

### 3.1 `common` — shared vocabulary
- `Result<T>` = `std::expected<T, Error>` (you already use this — keep it).
- Core concepts (`AiPort`, `StoragePort`, …) expressed as C++20/23 `concept`s,
  the same pattern as your existing `Backend` concept.
- Structured logging, config loading, cancellation tokens (`std::stop_token`).

### 3.2 `ai` — the AI engine module
- Owns the `OllamaBackend` you already have, generalized behind an
  **engine-agnostic `AiPort`** so the rest of the Core never hardcodes Ollama.
  Adds **capability discovery** (`capabilities()`: streaming? embeddings? JSON
  mode?) and an optional **`EmbeddingPort`** (possibly a *different* model) for
  semantic skill/task search. See [`use-cases.md`](./use-cases.md) §3 ⚠️2/⚠️5.
- Runs requests on a **dedicated thread pool / async executor**, not raw
  detached `jthread`s (see §5 — your current `run_simulation` is fine for a
  demo but needs bounding for a long-lived service).
- Supports **streaming** (`"stream":true`); the module emits tokens as events so
  the API edge can forward over WebSocket — also how UC8's live hints arrive.
- Cancellable via `std::stop_token` (a user abandoning a task/chat).

### 3.3 `storage` — persistence
- **SQLite** single file (e.g. `~/.local/share/aisim/aisim.db`), WAL mode.
- One engine, several **narrow stores** (interface segregation), covering:
  - **Settings + user** — relational tables. Plain SQL.
  - **Domain data** — CV/experiences, job descriptions + skill demand, the
    canonical **skill/topic taxonomy**, tasks (with provenance), solutions, and
    **per-skill mastery** (`CvStore`/`JobStore`/`TaskStore`/`MasteryStore`/…).
  - **Conversation history** — turns keyed by session + timestamp.
  - **Vector search** — **`sqlite-vec`** extension: embeddings for semantic
    skill/task matching, nearest-neighbour query. *The one area that adds real
    complexity* (embeddings via the engine's `/api/embeddings`, dimension must
    match the model, index upkeep). Behind its own sub-interface so it can land
    later or degrade gracefully without disturbing the rest.
- A single writer thread + WAL handles the multithreaded reads cleanly for one
  user (see §5).

### 3.4 `app` — domain / use-cases (the skill-gap loop)
- The "what the app actually does" layer. Houses the **product domain**:
  `TaxonomyService` (canonical skill normalization), `StructuredExtractor<T>`
  (extract→validate→repair for CV/JD/task parsing), **`GapService`**
  (`priority = demand × (1 − mastery)`), and the use-cases — practice engine
  (`GenerateTasks`, `SolveTask`) and feeder (`IngestCv`, `IngestJob`,
  `ComputeDemand`), plus `SubmitPrompt`/`OpenSession`/`UpdateSettings`.
- Depends only on the **ports** (`AiPort`, the storage interfaces, event bus),
  never on concrete `ai`/`storage` types — and links **no I/O libraries**
  (curl/sqlite), so it physically cannot do I/O and stays trivially testable.

### 3.5 `api` — the edge
- Embedded HTTP + WebSocket server (candidate libs in §6).
- Translates HTTP requests / WS messages ⇄ `app` commands.
- Owns serialization (JSON), request validation, and auth (a local token; see
  §7).
- **REST** for request/response (CRUD on settings, sessions, history search).
- **WebSocket** for the live channel: token streaming, progress, server-pushed
  events.

### 3.6 `events` — in-process bus
- Decouples producers (AI module emitting tokens) from consumers (the WS hub
  fanning out to connected clients).
- A simple typed publish/subscribe over a concurrent queue — *not* a message
  broker. Keeps "core sends feedback to UI" clean without polling.

### 3.7 `aisimd` — the composition root
- The only place that knows concrete types. It constructs `storage`, `ai`,
  `events`, injects them into `app`, hands `app` to `api`, and starts the
  server. Classic dependency-injection-by-hand — no framework needed.

---

## 4. Candidate overall architectures (pros / cons / complexity)

The locked decisions narrow this, but *how* we structure the C++ inside the
Core still has options. Three realistic shapes, weakest-to-strongest coupling:

### Option A — **Layered monolith with ports** *(recommended)*
One process, modules as in §3, talking through in-process interfaces.

| | |
|---|---|
| **Complexity** | **Low–medium.** No IPC, one binary, one debugger session. |
| **Maintainability** | **High.** Ports/interfaces give you the module independence you asked for; you can rewrite `storage` or `ai` in isolation. |
| **Pros** | Simplest thing that meets every requirement. Fast (in-proc calls). Easy to test (mock a port). Matches "not overkill." |
| **Cons** | All modules share one crash domain & address space. A rogue module can corrupt others. Mitigated because Ollama (the risky native part) is already out-of-process. |
| **Verdict** | **Best fit for a single-user desktop app.** |

### Option B — **Multi-process (Core + AI worker + …)**
AI orchestration (and maybe storage) run as separate processes over local IPC.

| | |
|---|---|
| **Complexity** | **High.** Process supervision, IPC schemas, restart logic, more failure modes. |
| **Maintainability** | Medium. Strong isolation, but more plumbing to maintain than it removes. |
| **Pros** | Crash isolation; independent restart/upgrade of the AI worker; could pin it to specific cores. |
| **Cons** | Overkill for one user. IPC overhead and serialization between your own components. You'd be solving scaling problems you said you don't have. |
| **Verdict** | Only worth it if the AI code proves crash-prone in practice. Easy to migrate *into* later because of the `AiPort` seam — so don't pay for it now. |

### Option C — **Plugin architecture (dynamically loaded modules)**
Modules compiled as `.so`/`.dll`, loaded at runtime through a stable C ABI.

| | |
|---|---|
| **Complexity** | **High.** Stable ABI design, versioning, symbol management, plugin lifecycle. |
| **Maintainability** | Mixed. Great if you want *third parties* to extend aisim; otherwise pure overhead. |
| **Pros** | Hot-swap modules without recompiling the host; ecosystem extensibility. |
| **Cons** | C-ABI boundary means giving up your nice C++23 types between host and plugin. No real benefit for a closed single-user app. |
| **Verdict** | Not now. Revisit only if "user-installable extensions" becomes a goal. |

**Recommendation:** **Option A.** It delivers the modularity requirement
through compile-time interfaces, stays single-process (no overkill), and the
`AiPort`/`StoragePort` seams leave a clean migration path to Option B for any
module that later earns its own process.

---

## 5. Concurrency model (the "high multithreading" requirement)

Your current `run_simulation` spawns one **detached** `jthread` per prompt. Fine
for a one-shot CLI; for a long-lived daemon it has no back-pressure (1000
prompts = 1000 threads) and no cancellation. The service version should use:

- **A bounded thread pool / executor** for AI requests (size ~ hardware
  concurrency, since each request is mostly I/O-bound waiting on Ollama). Submit
  work, get a `std::future`/awaitable back. Keeps thread count bounded under
  load.
- **`std::jthread` + `std::stop_token`** for cancellation — a user abandoning a
  generation stops the underlying request instead of leaking it.
- **The API server's own thread(s)** for accepting connections — kept separate
  from the AI pool so a saturated model queue never blocks the UI from getting
  responses to cheap calls (settings, history).
- **Storage threading:** SQLite in **WAL mode** allows concurrent readers with a
  single writer. Simplest robust model for one user: a single dedicated
  **writer thread** (serialize writes through a queue) + a small pool of reader
  connections. Avoids `SQLITE_BUSY` headaches.
- **Event bus** as the thread-safe hand-off: AI pool threads *publish* tokens;
  the WebSocket hub thread *consumes* and fans out. No shared mutable state
  across modules beyond the queue.

Modern C++ features this naturally showcases: `std::jthread`/`std::stop_token`,
`std::expected`, `std::generator`/`std::ranges` for token streams, atomics, and
(if the toolchain is ready) `std::execution`/senders-receivers for the executor.

---

## 6. Notable third-party dependencies (candidates)

| Need | Candidates | Notes |
|---|---|---|
| HTTP+WS server | **Crow**, **Drogon**, **cpp-httplib** (+ a WS lib) | Crow/Drongon are header-friendly and do both HTTP and WebSocket. cpp-httplib is the simplest but WS is weaker. |
| HTTP client (to Ollama) | **libcurl** (already used) | Keep it; supports streaming via write callbacks. |
| JSON | **nlohmann/json** or **glaze** | glaze is faster + compile-time reflection; nlohmann is ubiquitous and simplest. |
| SQLite wrapper | **sqlite3** (C API) + thin C++ wrapper, or **SQLiteCpp** | Plus the **`sqlite-vec`** extension for vectors. |

Deliberately small list — each is a single, well-scoped library.

---

## 7. Cross-cutting concerns

- **Security / auth.** Even single-user + localhost: once mobile connects over
  the LAN, the API is reachable by other devices. Bind to a chosen interface and
  require a **shared token** (generated on first run, shown in the desktop UI /
  paired to mobile via QR). Use TLS if traffic leaves localhost. Cheap, and it
  closes the obvious hole.
- **Config.** Single TOML/JSON file + env overrides (you already read
  `OLLAMA_HOST`). Layer: defaults → file → env.
- **Versioning the API.** Prefix routes `/v1/...` from day one so the UI and
  Core can evolve independently — central to "maintained separately."
- **Observability.** Structured logs + a `/health` and `/version` endpoint so
  the UI can show Core/Ollama status (you already probe Ollama in
  `ollama-service.sh status`).
- **Lifecycle.** The daemon should start/stop Ollama awareness gracefully and
  report when the model isn't pulled/loaded (mirrors current error handling).

---

## 8. What changes from today's code

Today: a single `main.cpp` CLI that fan-outs prompts to Ollama and prints
replies. The good parts to **keep**: the `Backend` concept, `std::expected`
error model, and the HTTP-to-engine transport. The path forward (the
[`roadmap.md`](./roadmap.md) sequences these into testable phases):

1. Extract `OllamaBackend` + the `Backend` concept into the **`ai`** module
   behind an engine-agnostic `AiPort` (+ `capabilities()`/`EmbeddingPort`).
2. Replace the detached-thread fan-out with a **bounded executor** + streaming.
3. Add **`storage`** (SQLite) and the **`events`** bus.
4. Add the **`api`** edge (HTTP + WebSocket) and the **`aisimd`** daemon as the
   composition root.
5. The CLI becomes either a thin client of the daemon or a `--oneshot` debug
   mode.
6. Build the **domain** (§3.4): taxonomy + extraction foundation, then the
   **practice engine first** (on manual skills), then the **feeder**, then wire
   **`GapService`** so the skill gap drives practice. See the roadmap.

None of these steps require throwing away existing code — they relocate it
behind interfaces.

---

## 9. Open questions / assumptions to confirm

- **Mobile reach** assumed **same-LAN**. If you need "control from anywhere over
  the internet," that's a materially bigger design (relay/tunnel + real auth +
  TLS) — flag it and we'll add a section.
- **Embeddings source**: assumed the engine's `/api/embeddings` with a local
  embedding model (possibly different from the chat model). Confirm, or name a
  preferred model. *Resolved-ish:* degrades gracefully if absent (⚠️2).
- **Desktop UI language/toolkit** is intentionally unconstrained by this design.
  If you already have one in mind (Qt/C++, Tauri, Electron, Flutter…), it
  doesn't change the Core but does change the client examples I'd sketch.
  *Note:* UC8 needs a code editor with debounced snapshots — the heaviest UI
  piece, so the toolkit choice matters most there.
- **Single binary vs. background service**: is `aisimd` launched by the UI on
  demand, or installed as a system service that's always up? Affects lifecycle
  (§7).
- **Practice scope**: the use cases are C++-centric (UC2/UC8), but skills/JDs may
  span many languages/topics. Confirm whether guided solving (UC8) targets C++
  only at first, or is language-agnostic from the start.
```
