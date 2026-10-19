# Workspaces, memory, skills and MCP

> **Applies to:** Arestrix Beta 1.5 (`1.5.0-beta.1`) · Last reviewed: 2026-10-19

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

Memory is opt-in and disabled by default.

| Kind | Scope | Purpose |
| :--- | :--- | :--- |
| **Blackboard** | Project | Shared key-value state between agents of the same project. |
| **Semantic lessons** | Project | Lessons agents record and recall later. |
| **Ephemeral vs. persistent** | Per vertex | RAM only, or stored on disk in the workspace database. |

Memory is partitioned by `projectId`, so two projects never read each other's data. Each vertex declares
which keys it may read or write.

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
| `arestrix.memoria` | `memory_set`, `memory_get`, `memory_list`, `memory_delete` | Small text values kept in `.arestrix/memory.json` (1000 keys, 64 KB each). |
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

Arestrix is an MCP client over stdio and SSE. There are two separate things:

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

**Secrets never go in this file.** `command`, `args`, `url` and every `env` value accept `${NAME}` (the value of an environment
variable of your user, resolved when the pipeline runs) and `${NAME:-default}`. The secret scanner rejects a literal value in `env`.
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
