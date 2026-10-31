# Workspaces, memory, skills and MCP

> **Applies to:** Arestrix 2.5 (`2.5.0`) · Last reviewed: 2026-10-31

[Português](../pt-BR/workspaces-memoria-skills-mcp.md) · [Index](../README.md)

This page covers the parts of Arestrix that live outside a single graph. For the Studio / Workspace /
Project split itself, see [Architecture](architecture.md#studio-workspace-and-project).

For the full memory lifecycle — capture, proposals, embeddings, consolidation, playbooks and
workspace — see [How memory works](how-memory-works.md).

## Projects and workspaces

A project is described by `arestrix.project.json` in its folder:

| Field | Meaning |
| :--- | :--- |
| `projectId`, `name`, `version`, `description` | Identity. The id is derived deterministically from the name (SHA-256). |
| `rootPath` | The physical workspace folder. |
| `pipelines` | The graphs of the project (`pipelines/main.json` is the primary one). |
| `database` | The SQLite file, by default `storage/database.sqlite`. |
| `git` | Optional Git/GitHub repository synchronization (`enabled`, `provider`, `serverUrl`, `repositoryName`, `remoteUrl`, `isPrivate`, `autoCommitOnRun`, `autoCommitOnShutdown`, `timedPushEnabled`, `timedPushIntervalMinutes`, `lastSyncAt`). |
| `globalDefaults` | Default `model`, `sandbox`, `codeMode` (allowed tools) and `memory` for the vertices. |
| `environment` | Variables local to the project (`.env`). |

The Studio keeps a list of recent projects and restores the last one on start. *Save As* produces a
compressed `.arestrix` package.

### Per-Project Git Repository Synchronization (GitHub & Remotes)

Each project in Arestrix can be linked to an independent Git repository on a configurable Git server (such as GitHub, GitLab, or on-premise remotes):

1. **Strictly Optional Git Synchronization (Opt-In by Default & 100% Local Mode):**
   - When creating any new project in Arestrix, Git/GitHub synchronization is **disabled by default** (`enabled: false`).
   - If the user or enterprise chooses not to use Git, the project operates in 100% local mode: all files, pipelines, the SQLite database (`storage/database.sqlite`), and cognitive memory (`.arestrix/memory/memory.db`) remain exclusively on the local machine disk. Zero network requests, zero Git commands, and zero SQL dumps are executed. All governance, data retention, and backup redundancy against data loss or disk failure remain entirely the responsibility of the organization or user.
   - The user can enable Git synchronization at any time by clicking the Git button on the Quick Access Toolbar or in the Backstage File menu, checking *"Enable Git / GitHub Synchronization"*.
2. **Databases Kept Out of Git (Privacy & LGPD/GDPR Compliance):**
   - Arestrix never sends SQLite databases (`storage/database.sqlite` and `.arestrix/memory/memory.db`) to Git and never generates SQL/JSON backup dumps. The remote repository contains only project files (graphs, pipelines, skills and configuration).
   - The project's safe `.gitignore` blocks binary databases, SQL dumps, JSON snapshots and quarantine files.
3. **Smart Commit & On-Demand Push (GitHub Quota & Rate-Limit Protection):**
   - **On every pipeline execution (`F5`):** A **local** commit with contextual message is created inside the local clone without triggering an immediate remote push. This eliminates GitHub API rate-limit risks and bandwidth abuse during rapid, iterative debugging cycles.
   - **Automated Timed Push (default every 10 minutes):** If pending local modifications or unpushed commits exist, Arestrix automatically batches and pushes them periodically to the remote repository. Interval and toggle are configurable per project.
   - **Manual Synchronization Button (Immediate Push):** Located on the Quick Access Toolbar (QAT) in the title bar and within the automation modal, allowing instant pushes to GitHub anytime with one click.
   - **On application shutdown (`onShutdown`):** If Git is enabled, triggered natively by the C++ backend when closing the application or window, guaranteeing that no work remains unversioned on the remote server. If Git is disabled, shutdown finishes instantly without running external processes.
4. **No Automatic Database Dumps:**
   - Arestrix does not export, version or restore database dumps. Backup, redundancy and disaster recovery are the responsibility of the organization or user and must be executed outside the product with the tool of their choice.
   - On shutdown, Arestrix only removes previously committed database/dump files from Git tracking (`git rm --cached`); remote history is not rewritten.
5. **Native Pre-Commit Secret Scanning:**
   - `GitSyncService` automatically audits staged diffs before every commit for plain-text credential patterns (`sk-...`, `AIza...`, `AKIA...`, `ghp_...`). The commit is aborted (*fail-closed*) if any exposed key is detected.
6. **Robust `.gitignore` Exclusions (`ensureSafeGitignore`):**
   - Automated exclusion of binary SQLite files (`*.sqlite`, `*.db`), SQL dumps (`*.sql`), snapshots (`*.dump.json`), quarantines (`*.db.corrupt-*`), runtime caches (`.arestrix/`), local AI model weights (`*.gguf`, `*.safetensors`, `*.bin`, `*.onnx`, `models/`), columnar datasets (`*.parquet`, `*.arrow`, `*.feather`, `*.h5`), media, and temporary build artifacts.
7. **Absolute Security (Zero-Secrets in Arestrix):**
   - No credentials, passwords, or Personal Access Tokens (PAT) are stored in Arestrix files, manifests, or databases. Identity registration and authentication workflows are managed **exclusively by the Operating System** via **Git Credential Manager (GCM)** / Windows Credential Store.
8. **Private Repositories by Default:** Repositories created via GitHub CLI (`gh`) are strictly enforced as **private** (`isPrivate: true`).
9. **Commercial Service Governance Warning:** When pointing to an external commercial Git host (e.g., `github.com` or `gitlab.com`), Arestrix displays a mandatory governance disclaimer reminding the user that project data will reside on third-party infrastructure outside Arestrix's custody, requiring explicit user consent.

### Databases and Safe Versioning

Arestrix manages two SQLite database files in each project's scope:

1. **Operational / Analytical Project Database (`storage/database.sqlite`):** Stores structured business data manipulated by agents and pipeline nodes through the `workspace-sqlite` MCP or native tools (`sql_query`, `sql_execute`), as well as imported datasets (e.g., Excel workbooks via `ImportExcelModal`).
2. **AMG knowledge database (`.arestrix/memory/memory.db`):** stores the project's temporal multigraph (lessons, facts and evidence). Telemetry lives in `.arestrix/telemetry/telemetry.db`.

#### Why Databases are Excluded from Git
* **GitHub Repository Size Limits:** GitHub recommends keeping repositories below 1 GB (with a practical ceiling of 5 GB to 10 GB) and strictly blocks pushes containing individual files larger than 100 MB.
* **Packfile History Bloat:** SQLite files are mutable binaries. Every pipeline run mutates pages on disk; because Git cannot compute text-based line diffs on binaries, each commit would append a full binary blob, quickly causing repository bloat.
* **Acceptable Use Policies:** GitHub explicitly prohibits using repositories primarily as an active database backend or general cloud storage backup service.

#### Backup and Recovery Outside Arestrix

Arestrix does not export, version or restore database dumps. The product generates neither `storage/database.sql` nor `storage/memory.dump.json`. Backup policy, redundancy and disaster recovery are the responsibility of the organization or user and must be executed externally, with the backup tool of their choice.

On shutdown, if Git is enabled, Arestrix only removes previously committed database and dump files from tracking (`git rm --cached`) and keeps the safe `.gitignore` up to date. Remote history is not rewritten.

Locally, Arestrix still protects memory database integrity: a corrupt `.db` is quarantined (`memory.db.corrupt-*`) and telemetry retention is applied on shutdown.


## Global defaults and inheritance

**Global defaults** (toolbar) hold the model, sandbox, tools and memory settings. Each vertex can
inherit them (a checkbox on the vertex) or override them. Studio-level keys are never mixed with project `.env` variables.

## Memory

Memory is optional, local and owned by the project. The current model has three per-vertex modes:

| Mode | Effect |
| :--- | :--- |
| `off` | Reads and writes no knowledge. |
| `consult` | Injects the authorized brief into the prompt and records hits. Writes no knowledge. |
| `learn` | Consults, writes an episode and proposes lessons at the end of the run. |

Learning implies saving, but nothing enters on its own: lessons and playbooks start as proposals
or candidates and become active knowledge only after approval in the **Project Memory** panel.
Telemetry lives in `.arestrix/telemetry/telemetry.db`, separate from knowledge in
`.arestrix/memory/memory.db`; clearing a session or pruning telemetry never deletes knowledge.

The brief is built within `contextBudgetTokens` from active lessons, facts, evidence and legacy
working state when enabled. The query uses `task_type`, theme and the vertex prompt template; lessons
are filtered in SQL and the brief is cached with invalidation on mutations. Compiled playbooks remain
candidates and do not enter the brief until promoted. Every injected item is labeled `[MEMO:<id>]`;
only an ID cited in the final answer counts as used. Use changes utility, not confidence.

The agent has `memory_search` (read-only), `memory_propose_lesson` (pending proposal) and the state
tools `memory_state_get`, `memory_state_set`, `memory_state_list` and `memory_state_history`.
`memory_record_lesson`, `memoryWriteKeys` and the native `arestrix.memoria` server were removed.

Continuation state is declared by the project and stays outside the brief. Scopes: `project`,
`task_type` and `item`; TTL, CAS, JSON schema and `replace`/`versioned` are supported. The CLI exposes
`memory state declare|list|get|set`.

Search combines lexical FTS with optional vectors and RRF fusion. Search does not index during a
query; **Test** and **Reindex** in the panel are explicit actions. Ready profiles:
EmbeddingGemma 2 (768 dimensions) and Qwen3-Embedding-4B (2560). Without a provider, search stays
lexical. The index stores an endpoint/model/dimension/prefix fingerprint; search only uses the
current fingerprint. States: `ready` (hybrid), `stale`/`pending`/`partial`/`failed` (lexical).

The panel offers **Compare with and without memory (context)**: it creates a consistent copy with
`VACUUM INTO`, computes the SHA-256 fingerprint and measures brief tokens/characters per active
vertex. This is A/B v1 for context cost and content; validator approval belongs to the full
protocol.

Playbooks activate only with independent validation: 3 distinct `input_hash` values in the last 20
validated runs, a stable validator and 1 promotion per `task_type` in 24h. Infrastructure failures
do not become lessons; `task_max_turns` is a task failure. Facts use single-valued (functional) or
multi-valued (`USES`, `PRODUCES`, etc.) predicates, avoiding accidental overwrite and false conflicts.

Workspace and institution remain available for promoted and shared knowledge. Promotion is
explicit, evidence-backed and audited. The project reads authorized workspace knowledge; the
workspace does not read the project's private memory.

The complete guide, with configuration, retention, security and a quick reference, is in
[How memory works](how-memory-works.md). Deployment status and the only product gap — the advanced
semantic layer — are in [Pending items](pending.md).

### Examples

- [`04-memoria`](../../examples/04-memoria) — lesson proposal, approval, prompt injection,
  `memory_search` and artifact delivery.
- [`05-matematica-com-memoria`](../../examples/05-matematica-com-memoria) — web research through the
  Keenable MCP, memory brief and a math text with references.

### Measuring API consumption

Telemetry records prompt, output, cache-read (`cachedTokens`) and cache-write (`cacheWriteTokens`)
tokens, plus calls, retries and latency. Use the panel or `arestrix-cli memory report` for cost per
run.


Each agent run records tokens, calls, latency and memory context size in the `memory_usage` table.
To compare consumption with memory on/off:

In the **Studio**, open **File → Properties → Project → Model & tools → Manage advanced memory** and use **Measurement / A-B Mode**:

- **Normal**: use the vertices' memory configuration.
- **A/B: memory OFF in this project**: disables the brief and memory tools at runtime without
  editing vertices.
- **Record metrics even with memory OFF**: keeps `memory_usage`, outcomes, retries and hits.

Then run the same graph normally and check **Project Memory → Maintenance**. For automation/CI, the
same options exist as environment variables:

```bash
# 1) memory ON on the same graph, recording metrics
ARESTRIX_MEMORY_METRICS=1 arestrix-cli run pipelines/main.json --root <project>

# 2) memory OFF on the SAME graph (no JSON edit)
ARESTRIX_MEMORY_METRICS=1 ARESTRIX_MEMORY_DISABLE=1 \
  arestrix-cli run pipelines/main.json --root <project>

# 3) native report
arestrix-cli memory report --project <project>
```

The report shows success/failure, retries, injected memories, used memories and memories consulted
through search.
`memory_report.py` remains optional for development; the runtime does not depend on Python. See also
[`arestrix-cli`](arestrix-cli.md).

The **Standalone Runner** receives agent tokens through a single telemetry event emitted by the C++
runtime (`type: "usage"`), aggregated by `RunnerManager`. There is no stdout parsing and no auxiliary
Python/JS process during graph execution.

When a project uses the **AI Gateway**, memory impact is recorded there too. The runtime sends
`X-Arestrix-Memory-Enabled`, `X-Arestrix-Memory-Context-Chars`, `X-Arestrix-Agent` and
`X-Arestrix-Run-Id`; the gateway stores `memory_enabled`, `memory_context_chars` and `memory_tokens`
(estimated as characters / 4) in `usage_events`. **File → Properties → Studio → Credentials & Gateway** and `/v1/usage`
show the share of input tokens and the memory ON/OFF comparison. Collection is entirely C++: no
Python or JS process participates in graph execution.

### Current limitations

- Full cognitive graph visualization and conflict editing are not in the UI yet; the conflict list
  and consolidation state already appear in the panel.
- Deterministic extraction, proposals, optional hybrid search, consolidation (manual and
  background), playbooks, workspace memory and authenticated HTTP/MCP institution exposure already
  exist in the core.
- LLM generalization/consolidation and background coordination across multiple
  processes/instances are still pending.
- `memory_handoff` and LLM consolidation are future phases.

## Skills

A **skill** is a `SKILL.md` file (YAML front matter plus instructions) that becomes the agent's system prompt.

- **Workspace skills** live in `.workspace/skills`, pre-approved for that workspace.
- **Libraries:** the built-in **Arestrix Library** and local/curated libraries supply skills to import.
  Manage sources, create libraries and register repositories in
  **File → Properties → Studio/Workspace → Libraries**. At the top of the section, the
  **Repository sources (GitHub)** panel accepts `organization/project`, a ref and a license; click
  **Save**, then select the created library and use **Sync GitHub**. Synchronization reads only the
  manifest/tree; asset content is downloaded on demand, audited and only then promoted for use.
  `REJECT` discards the download.
- The vertex **Skills** action binds skills to a vertex; the built-in editor edits `SKILL.md` as Markdown with preview.

### Governance

When a skill is imported or changed, the **Workspace Skill Auditor** applies three layers before allowing it
into a pipeline:

1. **Integrity and provenance:** front matter, license, declared tools, SHA-256 hash and ledger.
2. **NVIDIA SkillSpector static evidence:** deterministic rules for injection, steganography, exfiltration,
   lethal trifecta and excessive permissions. The reference catalog lives in `third_party/SkillSpector/`.
3. **Cloudflare boundary methodology:** `third_party/security-audit-skill/` guides adversarial review of
   supply chain, persistence, data isolation, resource exhaustion and local IPC.

Arestrix keeps its own **`arestrix-skill-guard`** skill at
[`.agents/skills/arestrix-skill-guard/SKILL.md`](../../.agents/skills/arestrix-skill-guard/SKILL.md),
combining both references and defining the verdict contract. The effective policy is versioned by
`policyVersion`; the ledger cache is valid only while the skill, references and rules are the same.
When a reference or rule changes, the skill returns to `PENDING` and must be re-audited.

The local scanner sees the `SKILL.md`, the prompt and declared tools; scripts and attachments in a
skill repository still require manual review by `arestrix-skill-guard`.

The lock `src/core/skills/skill-validation-policy.json` pins each reference commit, tree and `SKILL.md`
hash. The gate is:

```bash
npm run check:skill-references
```

The review process is in
[`docs/quality/skill-reference-review.md`](../quality/skill-reference-review.md).

## Native tools (no install, no process)

Arestrix ships its own tool servers, written in C++ and executed **inside the program**: no Node, no Python, no child process, no
third-party code. They appear in the MCP vertex and in agents exactly like any MCP server (marked ⚡ in the list), but a call is a
function call.

| Server | Tools | Notes |
| :--- | :--- | :--- |
| `arestrix.arquivos` | `read_file`, `write_file`, `list_directory`, `file_info`, `make_directory`, `search_text` | Confined to the project. Text only, 1 MB to read, 2 MB to write. |
| `arestrix.http` | `http_get`, `http_post_json` | **Only hosts the project authorizes** (below). HTTPS only. |
| `arestrix.dados` | `json_query`, `csv_to_json`, `json_to_csv`, `regex_extract`, `base64_encode`, `base64_decode` | Pure data transformation. Regex patterns that could hang the program are refused. |
| `arestrix.tempo` | `now`, `add_duration`, `diff` | ISO 8601, leap years, time zones. |
| `arestrix.git` | `git_status`, `git_log`, `git_diff`, `git_show_file` | **Read-only.** Needs Git installed. |
| `arestrix.sqlite` | `list_tables`, `describe_table`, `sql_query`, `sql_execute` | An embedded SQLite database in a project file (default `data/arestrix.db`). See below. |

**Safety rules, the same for all of them**
- Everything stays inside the project folder. `.git` cannot be read. **Pipelines, project configuration, `.workspace`, `.arestrix` and `.githooks`
  can never be written** by these tools, nor by the agents' own `write` and `edit` tools: writing there would inject C++ code or MCP
  servers that run on the next execution.
- **HTTP never decides by itself where data goes.** List the allowed sites in the Studio (**File → Properties → Project → Model & tools → Manage HTTP allowlist**,
  one per line, like `api.example.com` or `*.example.org`), which saves them to `.workspace/native/http.json`; without any site nothing
  is sent, and the error message tells you what to do. Wildcards need a full domain (`*.com` is refused).
  Local and private addresses are refused even if listed, redirects are **not** followed (each destination must be authorized), and
  credentials only travel through `bearer_env`, the **name** of an environment variable, never the value.
- **A name can lie, so the address is checked too (DNS rebinding).** An authorized name such as `api.example.com` could later be pointed by
  whoever controls its DNS at `127.0.0.1`, at a machine on your network or at a cloud metadata address. Before connecting, Arestrix resolves
  the name and refuses the request if **any** returned address is not public (loopback, private, link-local, CGNAT, multicast, reserved,
  documentation, and the IPv6 forms that embed an IPv4 address such as `::ffff:10.0.0.1`, NAT64 and 6to4). Returning several addresses, one
  of them internal, is refused too. On Linux and macOS the connection is then **pinned** to the validated address (`curl --resolve`), so
  the name cannot change between the check and the connection. On Windows the system does not allow pinning, so the address that was
  **actually connected** is checked before any byte of the request body is sent. HTTPS already makes an attack hard (the internal machine
  would need a valid certificate for that name); this layer closes the gap anyway. Limit: if the system uses a **proxy** (Windows) the
  connected address is the proxy's, so only the check before connecting applies, and the proxy is responsible for the rest.
- Every result is limited to 64 KB, and a tool error stops the pipeline with a clear message.

**SQLite database.** `arestrix.sqlite` embeds SQLite (public domain, `third_party/sqlite`), with no server and no installation. Each call opens
the file, runs **one** command and closes it. The file must be inside the project and end in `.db`, `.sqlite` or `.sqlite3`, and never in
`.git` or in the protected folders. Values go through `params` (`?` in the command), never concatenated into the text. `sql_query` opens the file
**read-only** and only runs `SELECT`; `sql_execute` alters data and schema. The engine refuses `ATTACH`, `PRAGMA`, virtual tables, triggers,
transactions and extension loading, a command is interrupted after 5 seconds, and the database is limited to 200 MB.

**Choosing the tools of an agent.** In the **Tools** window of an agent (and in Properties → Model & tools) every server, native or not, appears with a
box, and "Choose tools" lets you switch its tools on and off one by one. Nothing is stored except server and tool names.

**Speed.** On the development machine a native call took about 4 microseconds; an external MCP server, with a persistent session,
about 56 microseconds for a trivial tool, and opening a process per call took roughly 190 milliseconds.

## MCP servers

Arestrix is an MCP client over stdio and HTTP/SSE (Streamable HTTP). There are two separate things:

- **The Studio registry** (`arestrix-mcp-servers.json`, *Workspace MCP servers* screen): to register and test servers.
- **Tools for agents at run time:** an agent can call the tools of the MCP servers **configured in the project**, in
  `.workspace/mcps/*.json` (one `McpServerConfig` object, or a list, per file). The agent lists the servers it may use in its
  code-mode settings (`codeMode.mcpServers`, and optionally `codeMode.allowedMcpTools` to restrict which tools). Each tool
  becomes `mcp__<server>__<tool>` for the model. If a server is missing, disabled, unreachable or references an unset
  variable, the agent **fails with a clear error** instead of running without the tool.

```json
{ "id": "clientes", "transport": "stdio", "command": "${ARESTRIX_PYTHON:-python}", "args": ["mcp/server.py"],
  "env": { "API_TOKEN": "${CRM_TOKEN}" }, "isEnabled": true }
```

Remote servers use `"transport": "http"` (or `"sse"`), `url` and optional `headers`. Keenable (web
search) example, with no key in the repository:

```json
{ "id": "keenable", "transport": "http", "url": "https://api.keenable.ai/mcp",
  "headers": { "X-API-Key": "${KEENABLE_API}" }, "isEnabled": true }
```

**Secrets never go in this file.** `command`, `args`, `url`, every `env` value and every `headers` value accept `${NAME}` (the value of an environment
variable of your user, resolved when the pipeline runs) and `${NAME:-default}`. The secret scanner rejects a literal value in `env`;
the UI and backend reject literal values in sensitive headers (key, token, secret, auth).
Stdio servers run as normal processes of your user, **outside** the agent sandbox: only configure servers you trust.

**Hardened transport.** Because these files can be written by third parties (imported projects), the MCP client refuses dangerous
input instead of trying to make it safe: on Windows, `npx`/`npm`/`uvx` arguments containing quotes or shell metacharacters
(`& | < > ^ % !`) are rejected before any process starts; the HTTP transport accepts only `https://` on port 443, a host that
resolves **only** to public addresses (DNS-rebinding protection, like the native HTTP tool), no redirects, no `user@` and no
literal IPv6 (use stdio for local servers); and MCP child processes do **not** inherit the API keys from your environment — they
receive only the variables declared in `env`.

**Translated errors.** Every MCP failure that reaches the Studio carries a stable `errorCode` (defined in
`CppCore/src/mcp/McpErrors.hpp`) plus the fallback text and a short dynamic `errorDetail`; the interface translates the code
(`mcp_errors.*` keys) and only falls back to the backend text for codes it does not know — so new codes never break older
interfaces.

**Two ways to use a server in a pipeline:** an [**MCP vertex**](graph-and-nodes.md#mcp-vertex) (a deterministic call you wire
into the graph) or as **tools of an agent** (the model decides when to call them).

**Performance.** The generated program talks to MCP servers with the **native C++ client** (no Node or Python in the middle on the
Arestrix side). Each server has **one persistent session** (one process and one handshake for the whole run), shared safely by
all threads: calls to the same server are serialized, different servers run in parallel. On a test server, 15 calls took 0.19 s
with the persistent session against 2.8 s opening a process per call (15x). A session is reopened if the server dies or its
configuration changes, and a call that failed is **never** retried automatically (it may already have had effect). Sessions are
closed when the program exits.

The **Tools** window of an *agent* vertex lists the project's servers (the native ones first) and their tools, as described above, so you no
longer need to edit `mcpServers` in the JSON (the [examples](../../examples/README.md) still declare them in the file, which keeps working).

## Importing existing work

The **Connectors** tab converts outside material into a graph:

| Source | Result |
| :--- | :--- |
| **Pi agent projects** | Scans `.pi/settings.json`, `.agents/*/SKILL.md`, `plano.md` and `pipeline-state.json`, then synthesizes a Taskflow pipeline with agents, tools and artifact validation. |
| **Claude, OpenAI, Hermes** | Imports prompts, assistants and scratchpad logic as agent vertices. |
| **Excel** | Splits a multi-sheet workbook into a SQLite database and a concurrent four-agent graph. |

## Related

- [Agents and execution](agents-and-execution.md)
- [Interface tour](interface.md)
