# How Arestrix memory works

> **Applies to:** Arestrix Beta 1.5 (`1.5.0-beta.1`) — last reviewed: 2026-10-25
>
> This guide describes the memory implemented in the C++20 + SQLite core. It covers the full
> cycle: capture, proposal, promotion, consolidation, retrieval, embeddings, playbooks,
> workspace and the local institution contract.

## 1. Principles

Arestrix memory is designed to be:

- **local-first:** project, workspace and institution use local SQLite; nothing requires cloud;
- **native:** the execution path is C++20 + SQLite, with no Python or JavaScript;
- **additive:** new schemas are created/migrated without deleting old data;
- **auditable:** promotions, conflicts, decay, policies and purges leave a trail;
- **opt-in:** memory and embeddings are disabled by default;
- **temporal:** facts carry validity, confidence and history; nothing is silently overwritten;
- **recoverable:** versioned snapshots restore the cognitive graph.

The operating principle is:

```text
capture → propose → prove → promote → consolidate → retrieve → measure
```

## 2. Where memory lives

| Scope | Database | Purpose |
| :--- | :--- | :--- |
| Project | `<project>/.arestrix/memory/memory.db` | private project memory |
| Workspace | `<project>/.workspace/memory/memory.db` | shared memory, promoted only |
| Institution | dedicated institution contract database | authenticated institution memory |

The project database path can be overridden by `ARESTRIX_MEMORY_DB`. The root used by the
generated binary comes from `ARESTRIX_PROJECT_ROOT`. The workspace is opened automatically only
when `.workspace/memory/memory.db` already exists; otherwise use `memory workspace init`.

## 3. Architecture

```text
Agent / Tools / Studio / CLI
            │
            ▼
     MemoryService  ──────────────►  MemoryInstitutionService
            │                              │
            ▼                              ▼
       MemoryStore  ◄──── MemoryConsolidator / MemoryPlaybookCompiler
            │
            ├── MemorySanitizer
            ├── MemoryExtractor
            ├── MemoryIndexer ──► MemoryVectorIndex / EmbeddingProvider
            └── MemoryRanker
```

### Components

- **`MemoryService`**: public facade for project, workspace, embeddings, consolidation,
  proposals, playbooks and status.
- **`MemoryStore`**: SQLite persistence, schema, FTS5, transactions, snapshot, purge, prune,
  aliases, policies and metrics.
- **`MemorySanitizer`**: removes secrets before any write, legacy read or export.
- **`MemoryExtractor`**: turns sanitized episodes into deterministic proposals.
- **`MemoryConsolidator`**: dedup, conflicts, expiration, decay and generalization.
- **`MemoryPlaybookCompiler`**: turns successful trajectories into playbooks.
- **`EmbeddingProvider`**: optional OpenAI-compatible provider.
- **`MemoryVectorIndex` / `MemoryIndexer` / `MemoryRanker`**: vector index, indexing and RRF.
- **`MemoryInstitutionService`**: local contract for tokens, roles and policies.
- **`DatabaseBackupService`**: snapshot, restore, quarantine and retention on shutdown.
- **`Agent` / `Tools`**: record episodes, lessons, hits and usage; expose `memory_search`.

## 4. Data model

The database is a temporal multigraph. Main tables:

| Table | Content |
| :--- | :--- |
| `memory_scope` | project, workspace or institution |
| `memory_scope_closure` | ancestry and depth between scopes |
| `memory_node` | entities, domains, rules, summaries and conflicts |
| `memory_edge` | facts, lessons (`HAS_LESSON`), conflicts and generalizations |
| `memory_episode` | sanitized run: outcome, tools, files, artifacts, evidence |
| `memory_working` | Blackboard: key/value per pipeline item |
| `memory_evidence` | structured evidence linked to edges/episodes |
| `memory_hit` | injected, consulted and used memory per run |
| `memory_usage` | tokens, calls, latency and memory context |
| `memory_audit` | writes, promotions, consolidation, decay and policy actions |
| `memory_proposal` | `pending/accepted/rejected/superseded` queue |
| `memory_embedding` | float32 little-endian vectors per model/object |
| `memory_playbook` | steps, counters, confidence, origin and evidence |
| `memory_alias` | entity resolution by normalized name |
| `memory_policy` | invariant policies per scope |

### States

- Edges: `candidate`, `active`, `rejected`, `superseded`.
- Proposals: `pending`, `accepted`, `rejected`, `superseded`.
- Playbooks: `candidate`, `active`; the schema accepts other states for evolution.
- **Dormant** is not an edge status: it is a populated `dormant_at`. The edge remains in the
  database but leaves retrieval.
- Expired facts keep `valid_to` and are marked `superseded` with `metadata.expired`.

## 5. Writes: what enters memory

### Lessons

`memory_record_lesson` (and promotion) goes through governance:

- domain required when the agent has `memoryDomains`;
- `writeKeys` limits the Blackboard;
- `maxLessonsPerRun` limits lessons per run;
- `maxLessonChars` limits rule and trigger;
- global cap of 5,000 active lessons per project for agent writes;
- exact dedup by domain + rule.

Agent failures also produce an automatic lesson when `recordLessons` is enabled.

### Episodes

At the end of each agent run, the runtime records an episode with:

- `task_type`, `outcome`, `error_class`;
- tool sequence (`toolCalls`);
- touched files (`filesTouched`);
- artifacts (`artifacts`);
- evidence and success signals;
- redaction count.

Raw prompts/responses and tool arguments are **not** stored.

### Blackboard

The Blackboard is a workspace per `item_id`. Reads and writes are controlled by
`allowedKeys`/`writeKeys`. `contextBudgetTokens = 0` disables the brief.

### Sanitization

Before persistence, `MemorySanitizer` removes patterns such as OpenAI/Stripe, Google, AWS,
GitHub and Slack keys, `Bearer` tokens, `api_key=...` assignments, URL parameters and PEM keys.
Sanitization is also applied to legacy reads and snapshot exports.

## 6. Proposals and promotion

### Deterministic extraction

`MemoryExtractor` reads a sanitized episode and produces proposals:

- **success:** tool facts (`USES_TOOL`), artifacts (`PRODUCES`), entities, a playbook and a
  summary;
- **failure:** only a procedural lesson; never a fact;
- confidence based on outcome, artifacts and evidence;
- limit of 20 proposals per episode.

Proposals stay `pending`. The extractor never promotes automatically.

### Review

- Studio → **Project Memory → Proposals**;
- CLI → `arestrix-cli memory proposals list|accept|reject`.

### Promotion

Promotion requires non-empty evidence. The destination depends on type:

- `lesson` → `recordLesson` with `source_kind = promotion`;
- `fact` → entity resolution + `active` edge with `source_kind = promotion`;
- `entity` → canonical node;
- `playbook` → `active` playbook, reusing the existing signature;
- `summary` → `summary` node.

Every promotion writes `memory_audit`. Duplicate facts reuse the active edge; the same fact does
not create a duplicate edge.

## 7. Retrieval

There are two main paths.

### `memory_search` (explicit search)

1. FTS5 over node names;
2. active graph edges;
3. lessons;
4. playbooks;
5. embeddings, when enabled;
6. RRF fusion of lexical + vector candidates;
7. `memoryDomains` filter and limit.

If the embedding provider fails, search falls back to lexical automatically. Results record
hits:

- `injected=1` when included in the brief;
- `used=1` when the agent consulted/used it;
- `consulted` for memory discovered through search.

### Injected brief

`buildBrief` assembles, within the token budget:

1. project lessons;
2. promoted workspace lessons;
3. relevant facts (hybrid, when enabled);
4. active playbooks by `task_type`;
5. authorized Blackboard.

Each section respects the budget. The brief never cuts partial JSON: it truncates by
line/section and marks `truncated`. `contextBudgetTokens = 0` injects nothing.

## 8. Embeddings: EmbeddingGemma 2

The provider is optional and off by default.

- Default model: **`embeddinggemma-2`**;
- native dimension: **768** (MRL: 512/256/128);
- context: up to **8K tokens**;
- license: Apache 2.0;
- endpoint: OpenAI-compatible `/v1/embeddings`;
- asymmetric prefixes:
  - query: `task: search result | query: ...`
  - document: `title: none | text: ...`
- fallback: without a provider, memory is 100% lexical.

### Enabling with Ollama

```powershell
ollama pull embeddinggemma-2
```

In the Studio:

- **Global Defaults → Memory**;
- enable semantic search;
- endpoint `http://127.0.0.1:11434/v1`;
- model `embeddinggemma-2`;
- dimensions `768`;
- **Test embeddings**;
- **Reindex**.

### Index

The index stores vectors in `memory_embedding` with `content_hash`, model and domain. Indexing
is incremental; `memory reindex` rebuilds everything. The cognitive snapshot does **not** carry
vectors: after a restore, run `memory reindex`.

## 9. Consolidation

Run on demand through `memory consolidate` or **Consolidate memory**.

- **Dedup:** normalization + Jaccard for lessons/facts; duplicates become `superseded` and
  evidence is merged into the canonical item.
- **Conflicts:** same subject + predicate with a different object; the loser becomes
  `candidate`, both receive `conflictGroup`, and a `conflict` node + `CONTRADICTS` edges are
  created.
- **Expiration:** past `valid_to` becomes `superseded` with `metadata.expired`.
- **Decay:** unused facts get `dormant_at`; they are not deleted.
- **Generalization:** domains with N lessons get a `summary` node + `GENERALIZES` edges.
- **Reinforcement:** a hit used in success increments `success_count`, clears `dormant_at` and
  raises confidence; failure increments `failure_count` and lowers confidence.

Consolidation is idempotent and audited. It does not run in the graph hot path.

## 10. Playbooks

`MemoryPlaybookCompiler` observes episodes when `recordLessons` is enabled.

- signature = `task_type` + tool sequence;
- 1st success → `candidate`;
- 2nd success → `active`;
- failure increments `failure_count`, lowers confidence and can demote to `candidate`;
- failure without a matching playbook creates nothing.

Active playbooks enter the brief by `task_type`, with compact steps, within budget.

## 11. Workspace

- dedicated database at `.workspace/memory/memory.db`;
- the project **reads** the workspace;
- the workspace **does not read** the project's private memory;
- explicit project → workspace promotion for `lesson`, `fact` and `playbook`;
- promotion requires evidence and writes audit in both databases;
- promotion never deletes the original;
- siblings cannot see each other.

CLI:

```powershell
arestrix-cli memory workspace init --project .\my-project --workspace-id team
arestrix-cli memory workspace status --project .\my-project --workspace-id team
arestrix-cli memory promote --project .\my-project --workspace-id team `
  --kind lesson --id <lesson-id> --evidence "reviewed by two people"
```

## 12. Institution

The core provides `MemoryInstitutionService` and the `arestrix-memory` executable, which exposes
the institution contract over HTTP and MCP:

- tokens kept in memory only;
- `viewer`, `editor`, `admin` roles;
- authenticated reads;
- writes require `editor`;
- policies require `admin`;
- `memory_policy` allows an immutable ancestor policy; children cannot override it.

### Start the service

```powershell
arestrix-memory token --principal ana --role viewer
arestrix-memory init --db C:\memory\institution.db --institution acme
arestrix-memory serve --db C:\memory\institution.db --institution acme `
  --host 127.0.0.1 --port 8790 --tokens-file C:\memory\tokens.txt
```

The token file has one line per principal:

```text
# token principal role
<ana-token> ana viewer
<bruno-token> bruno editor
<carla-token> carla admin
```

You can also use `--tokens-json` or `ARESTRIX_INSTITUTION_TOKENS` with an array:

```json
[{"token":"<token>","principal":"ana","role":"viewer"}]
```

Without tokens the service starts fail-closed: every authenticated route returns `401`. Tokens are
never persisted; restarting requires reloading the file or environment variable. Keep the token file
outside the repository and restrict its operating-system permissions.

### HTTP API

| Method | Route | Role | Effect |
| :--- | :--- | :--- | :--- |
| `GET` | `/health` | — | liveness, no authentication |
| `GET` | `/v1/status` | `viewer` | institution, principal, role and lessons |
| `POST` | `/v1/context` | `viewer` | search `{ "query", "limit", "domains" }` |
| `POST` | `/v1/lessons` | `editor` | write `{ "domain", "triggerError", "lessonRule" }` |
| `POST` | `/v1/policies` | `admin` | write `{ "key", "value", "immutable" }` |
| `POST` | `/mcp` | bearer | MCP JSON-RPC 2.0 |

Example:

```powershell
curl.exe -H "Authorization: Bearer $env:ARESTRIX_INSTITUTION_TOKEN" `
  -H "Content-Type: application/json" `
  -d '{"query":"style convention","limit":5}' `
  http://127.0.0.1:8790/v1/context
```

### MCP

`mcp` mode uses newline-delimited JSON-RPC on stdin/stdout:

```powershell
$env:ARESTRIX_INSTITUTION_TOKEN = "<token>"
arestrix-memory mcp --db C:\memory\institution.db --institution acme --role editor
```

Tools:

- `memory_institution_context` (`viewer`);
- `memory_institution_write_lesson` (`editor`);
- `memory_institution_status` (`viewer`);
- `memory_institution_set_policy` (`admin`).

In the Studio, configure a stdio MCP server pointing to the binary:

```json
{
  "id": "arestrix-memory",
  "transport": "stdio",
  "command": "C:\\Arestrix\\arestrix-memory.exe",
  "args": ["mcp", "--db", "C:\\memory\\institution.db", "--institution", "acme", "--role", "editor"],
  "env": { "ARESTRIX_INSTITUTION_TOKEN": "${ARESTRIX_INSTITUTION_TOKEN}" }
}
```

The HTTP `/mcp` endpoint accepts the same JSON-RPC with `Authorization: Bearer ...`.

### Operations

- listen on `127.0.0.1` and put a **TLS reverse proxy** in front for network exposure;
- the database is single-node SQLite; there is no multi-machine sync in this version;
- the service has no built-in rate limit; use the proxy/gateway for that;
- logs do not contain tokens, lesson bodies or memory content.

## 13. Snapshot, retention and purge

- Snapshot v2 includes scopes, closure, nodes, edges, working rows, lessons, aliases and
  playbooks.
- Snapshot v1 remains importable.
- Atomic writes and protection against an empty snapshot overwriting existing memory.
- A corrupt database is quarantined as `memory.db.corrupt-<stamp>`.
- `memory purge` removes graph, proposals, playbooks, embeddings, aliases, hits, usage,
  episodes and (optionally) audit; then runs `VACUUM`.
- `memory prune --days N` removes old telemetry without touching the cognitive graph.
- Retention also runs on shutdown, regardless of Git being enabled.

## 14. Configuration

Fields in **Global Defaults → Memory**:

| Field | Default |
| :--- | :--- |
| `enabled` | `false` |
| `persistent` | `false` |
| `readBlackboard` / `writeBlackboard` | `false` |
| `semanticLessons` / `recordLessons` | `false` |
| `writeKeys` | `[]` |
| `maxLessonsPerRun` | `20` |
| `maxLessonChars` | `4000` |
| `retentionDays` | `180` |
| `contextBudgetTokens` | `800` |
| `embeddingEnabled` | `false` |
| `embeddingModel` | `embeddinggemma-2` |
| `embeddingDimensions` | `768` |
| `embeddingBatchSize` | `8` |
| `embeddingMaxChars` | `8000` |
| `embeddingTimeoutSeconds` | `60` |

Embedding environment variables:

```text
ARESTRIX_EMBEDDINGS_ENABLED
ARESTRIX_EMBEDDING_BASE_URL
ARESTRIX_EMBEDDING_MODEL
ARESTRIX_EMBEDDING_BATCH
ARESTRIX_EMBEDDING_TIMEOUT
ARESTRIX_EMBEDDING_MAX_CHARS
ARESTRIX_EMBEDDING_DIM
ARESTRIX_EMBEDDING_API_KEY
```

## 15. Operational CLI

```powershell
arestrix-cli memory report --project .\my-project
arestrix-cli memory proposals list --project .\my-project
arestrix-cli memory consolidate --project .\my-project
arestrix-cli memory embeddings status --project .\my-project
arestrix-cli memory embeddings test --project .\my-project `
  --base-url http://127.0.0.1:11434/v1 --model embeddinggemma-2
arestrix-cli memory reindex --project .\my-project
arestrix-cli memory workspace init --project .\my-project --workspace-id team
arestrix-cli memory prune --project .\my-project --days 180
arestrix-cli memory purge --project .\my-project

# Institution service (separate binary)
arestrix-memory token --principal ana --role viewer
arestrix-memory init --db C:\memory\institution.db --institution acme
arestrix-memory serve --db C:\memory\institution.db --institution acme `
  --port 8790 --tokens-file C:\memory\tokens.txt
arestrix-memory mcp --db C:\memory\institution.db --institution acme --role editor
```

## 16. Metrics and health

`memory report` shows tokens, calls, retries, latency, context, hits and embeddings.
`memory embeddings status` shows model, effective endpoint, state, indexed vectors and pending
objects. `memory embeddings test` performs a real call; if Ollama is down, the message includes
the pull command.

## 17. Known limitations

- the institution server has no built-in TLS or rate limiting; use a reverse proxy;
- institution tokens do not persist across restarts;
- background consolidation is still manual;
- LLM generalization is not part of this core;
- snapshots do not carry vectors; reindex after restore;
- institution policies are local and tokens are not persisted;
- Studio ribbon debt is independent from memory.

## 18. Checklist for local embedding production

1. `ollama pull embeddinggemma-2`;
2. endpoint `http://127.0.0.1:11434/v1`;
3. model `embeddinggemma-2`, 768 dimensions;
4. **Test embeddings**;
5. **Reindex**;
6. confirm `indexed > 0` and `pending = 0`;
7. run a semantic search and compare with lexical mode;
8. keep lexical fallback enabled (it is automatic).
