# Workspaces, memory, skills and MCP

> **Applies to:** Arestrix Beta 1.5 (`1.5.0-beta.1`) · Last reviewed: 2026-10-21

[Português](../pt-BR/workspaces-memoria-skills-mcp.md) · [Index](../README.md)

This page covers the parts of Arestrix that live outside a single graph. For the Studio / Workspace /
Project split itself, see [Architecture](architecture.md#studio-workspace-and-project).

## Projects and workspaces

A project is described by `arestrix.project.json` in its folder:

| Field | Meaning |
| :--- | :--- |
| `projectId`, `name`, `version`, `description` | Identity. The id is derived deterministically from the name (SHA-256). |
| `rootPath` | The physical workspace folder. |
| `pipelines` | The graphs of the project (`pipelines/main.json` is the primary one). |
| `database` | The SQLite file, by default `storage/database.sqlite`. |
| `git` | Dedicated Git repository synchronization (`provider`, `serverUrl`, `repositoryName`, `remoteUrl`, `isPrivate`, `autoCommitOnRun`, `autoCommitOnShutdown`, `lastSyncAt`). |
| `globalDefaults` | Default `model`, `sandbox`, `codeMode` (allowed tools) and `memory` for the vertices. |
| `environment` | Variables local to the project (`.env`). |

The Studio keeps a list of recent projects and restores the last one on start. *Save As* produces a
compressed `.arestrix` package.

### Per-Project Git Repository Synchronization (GitHub & Remotes)

Each project in Arestrix maps to an **independent Git repository** on a configurable Git server (default: a **GitHub** account):

1. **Auto-save inside the local clone:** Continuous real-time auto-save remains in effect inside the project folder on disk, which acts as the local Git clone/repository.
2. **Automated Commit & Push:**
   - **On every pipeline execution (`F5`):** A commit with contextual message is created and pushed immediately to the remote server prior to graph execution.
   - **On application shutdown:** Triggered natively by the C++ backend when closing the application or window, guaranteeing that no work remains unversioned on the remote server.
3. **Absolute Security (Zero-Secrets in Arestrix):** No credentials, passwords, or Personal Access Tokens (PAT) are stored in Arestrix files, manifests, or databases. Identity registration and authentication workflows are guided by Arestrix but managed **exclusively by the Operating System** via **Git Credential Manager (GCM)** / Windows Credential Store.
4. **Private Repositories by Default:** Repositories are created automatically with the project name and strictly enforced as **private** (`isPrivate: true`).
5. **Commercial Service Governance Warning:** When pointing to an external commercial Git host (e.g., `github.com` or `gitlab.com`), Arestrix displays a mandatory governance disclaimer reminding the user that project data (prompts, agent instructions, graphs, and metadata) will reside on third-party infrastructure outside Arestrix's custody, requiring explicit user consent.


## Global defaults and inheritance

**Global defaults** (toolbar) hold the model, sandbox, tools and memory settings. Each vertex can
inherit them (a checkbox on the vertex) or override them. Studio-level keys are never mixed with project `.env` variables.

## Memory

Memory is opt-in and disabled by default. In Beta 2 it is implemented by **AMG (Arestrix Memory
Graph)**, a temporal SQLite multigraph written natively in C++20.

| Kind | Scope | Purpose |
| :--- | :--- | :--- |
| **Blackboard** | Project | Shared key-value state between agents of the same project. |
| **Semantic lessons** | Project | Lessons agents record and recall later. |
| **Ephemeral vs. persistent** | Per vertex | RAM only, or stored in the project SQLite database. |
| **API consumption** | Project | Tokens, calls, latency and injected context size. |

Memory is partitioned by `projectId`, so two projects never read each other's data. Each vertex declares
which keys it may read or write.

### What exists today

- SQLite database at `.arestrix/memory/memory.db` per project.
- Schema: `memory_scope`/`memory_scope_closure`, `memory_node`, `memory_edge`, `memory_episode`,
  `memory_evidence`, `memory_audit`, `memory_working`, `memory_usage` and `memory_node_fts` (FTS5).
- Current scope: **project** (`project:<projectId>`). Workspace and institution arrive in later
  phases.
- Lessons and Blackboard can be read at the start of a run and written at the end.
- The agent can query memory with `memory_search` and, when `recordLessons` is on, record a lesson
  with `memory_record_lesson`.
- `contextBudgetTokens` limits the injected brief; the default is roughly 800 tokens (about 4
  characters per token).
- Legacy JSON (`arestrix-blackboard-*.json`, `arestrix-memory-*.json`) is imported once and marked
  `legacy_imported`.
- No LLM is used to extract or consolidate memory by default. The agent proposes; the runtime keeps
  the scope and evidence policy.

### How memory reaches the agent

In the generated pipeline, the runtime:

1. opens the project database when `persistent: true`;
2. builds a brief with the configured domains' lessons and the authorized key snapshot;
3. injects that brief into the system prompt as **untrusted historical data**;
4. saves the lesson and Blackboard at the end of the run;
5. automatically records a lesson when an agent fails and `recordLessons` is on.

Audit lessons are recorded by the node that interprets the decision (for example, a `condition`
that calls `recordLesson` on the rejection branch); the runtime does not guess the semantics of a
switch.

### Project memory vs. `arestrix.memoria`

These are different things:

- **Project memory** is the SQLite AMG used by agents and by `memory_search` /
  `memory_record_lesson`.
- **`arestrix.memoria`** is a small native key-value tool that writes `.arestrix/memory.json`. It
  remains available for simple manual cases, but it is not the agents' memory database.

### Examples

- [`04-memoria`](../../examples/04-memoria) — persistent lesson, Blackboard, prompt injection,
  `memory_search`, `memory_record_lesson` and artifact delivery.
- [`05-matematica-com-memoria`](../../examples/05-matematica-com-memoria) — web research through the
  Keenable MCP, memory brief and a mathematics text with references.

### Measuring API consumption

Each agent run records tokens, calls, latency and memory context size in the `memory_usage` table.
To compare consumption with memory on/off:

```bash
# 1) run without memory, still recording metrics
ARESTRIX_MEMORY_METRICS=1 arestrix-cli run pipelines/main.json --root <project>

# 2) run with memory (graph configuration)
arestrix-cli run pipelines/main.json --root <project>

# 3) report (optional development tool)
python scripts/memory_report.py <project>
```

`memory_report.py` is only a SQLite reader for development; the runtime does not depend on Python.
See also [`arestrix-cli`](arestrix-cli.md).

The **Standalone Runner** receives agent tokens through a single telemetry event emitted by the C++
runtime (`type: "usage"`), aggregated by `RunnerManager`. There is no stdout parsing and no auxiliary
Python/JS process during graph execution.

When a project uses the **AI Gateway**, memory impact is recorded there too. The runtime sends
`X-Arestrix-Memory-Enabled`, `X-Arestrix-Memory-Context-Chars`, `X-Arestrix-Agent` and
`X-Arestrix-Run-Id`; the gateway stores `memory_enabled`, `memory_context_chars` and `memory_tokens`
(estimated as characters / 4) in `usage_events`. **Global Defaults → AI Gateway** and `/v1/usage`
show the share of input tokens and the memory ON/OFF comparison. Collection is entirely C++: no
Python or JS process participates in graph execution.

### Current limitations

- A dedicated graph/conflict visualization is not in the UI yet.
- Full hybrid ranking (entities + graph + embeddings) is not implemented yet.
- Workspace/institution scopes, `memory_handoff` and LLM consolidation are future phases.

## Skills

A **skill** is a `SKILL.md` file (YAML front matter plus instructions) that becomes the agent's system prompt.

- **Workspace skills** live in `.workspace/skills`, pre-approved for that workspace.
- **Studio catalog:** authorized open-source GitHub repositories that supply skills you can import.
  Enable or disable repositories globally in **Agents → GitHub repositories**.
- The vertex **Skills** action binds skills to a vertex; the built-in editor edits `SKILL.md` as Markdown with preview.

### Governance

When a skill is imported, the **Workspace Skill Auditor** inspects its front matter, requested permissions,
script dependencies and sandbox safety before it can be used in a pipeline. The repository also vendors
NVIDIA's **SkillSpector** scanner in `third_party/`.

## Native tools (no install, no process)

Arestrix ships its own tool servers, written in C++ and executed **inside the program**: no Node, no Python, no child process, no
third-party code. They appear in the MCP vertex and in agents exactly like any MCP server (marked ⚡ in the list), but a call is a
function call.

| Server | Tools | Notes |
| :--- | :--- | :--- |
| `arestrix.arquivos` | `read_file`, `write_file`, `list_directory`, `file_info`, `make_directory`, `search_text` | Confined to the project. Text only, 1 MB to read, 2 MB to write. |
| `arestrix.http` | `http_get`, `http_post_json` | **Only hosts the project authorizes** (below). HTTPS only. |
| `arestrix.dados` | `json_query`, `csv_to_json`, `json_to_csv`, `regex_extract`, `base64_encode`, `base64_decode` | Pure data transformation. Regex patterns that could hang the program are refused. |
| `arestrix.memoria` | `memory_set`, `memory_get`, `memory_list`, `memory_delete` | Manual tool independent of AMG: small text values in `.arestrix/memory.json` (1000 keys, 64 KB each). |
| `arestrix.tempo` | `now`, `add_duration`, `diff` | ISO 8601, leap years, time zones. |
| `arestrix.git` | `git_status`, `git_log`, `git_diff`, `git_show_file` | **Read-only.** Needs Git installed. |
| `arestrix.sqlite` | `list_tables`, `describe_table`, `sql_query`, `sql_execute` | An embedded SQLite database in a project file (default `data/arestrix.db`). See below. |

**Safety rules, the same for all of them**
- Everything stays inside the project folder. `.git` cannot be read. **Pipelines, project configuration, `.workspace`, `.arestrix` and `.githooks`
  can never be written** by these tools, nor by the agents' own `write` and `edit` tools: writing there would inject C++ code or MCP
  servers that run on the next execution.
- **HTTP never decides by itself where data goes.** List the allowed sites in the Studio (**Global Defaults → Tools → Internet access**,
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

**Choosing the tools of an agent.** In the **Tools** window of an agent (and in Global Defaults) every server, native or not, appears with a
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
