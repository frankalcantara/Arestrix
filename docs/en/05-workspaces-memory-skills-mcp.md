# Workspaces, memory, skills and MCP

[Português](../pt-BR/05-workspaces-memoria-skills-mcp.md) · [Index](../../README.md#documentation)

This page covers the parts of Arestrix that live outside a single graph. For the Studio / Workspace /
Project split itself, see [Architecture](01-architecture.md#studio-workspace-and-project).

## Projects and workspaces

A project is described by `arestrix.project.json` in its folder:

| Field | Meaning |
| :--- | :--- |
| `projectId`, `name`, `version`, `description` | Identity. The id is derived deterministically from the name (SHA-256). |
| `rootPath` | The physical workspace folder. |
| `pipelines` | The graphs of the project (`pipelines/main.json` is the primary one). |
| `database` | The SQLite file, by default `storage/database.sqlite`. |
| `globalDefaults` | Default `model`, `sandbox`, `codeMode` (allowed tools) and `memory` for the vertices. |
| `environment` | Variables local to the project (`.env`). |

The Studio keeps a list of recent projects and restores the last one on start. *Save As* produces a
compressed `.arestrix` package.

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

**Two ways to use a server in a pipeline:** an [**MCP vertex**](03-graph-and-nodes.md#mcp-vertex) (a deterministic call you wire
into the graph) or as **tools of an agent** (the model decides when to call them).

**Performance.** The generated program talks to MCP servers with the **native C++ client** (no Node or Python in the middle on the
Arestrix side). Each server has **one persistent session** (one process and one handshake for the whole run), shared safely by
all threads: calls to the same server are serialized, different servers run in parallel. On a test server, 15 calls took 0.19 s
with the persistent session against 2.8 s opening a process per call (15x). A session is reopened if the server dies or its
configuration changes, and a call that failed is **never** retried automatically (it may already have had effect). Sessions are
closed when the program exits.

Limitation: the **Tools** modal of an *agent* vertex does not list your servers yet (it shows a fixed example list); declare
`mcpServers` in the project's JSON, as the [examples](../../examples/README.md) do.

## Importing existing work

The **Connectors** tab converts outside material into a graph:

| Source | Result |
| :--- | :--- |
| **Pi agent projects** | Scans `.pi/settings.json`, `.agents/*/SKILL.md`, `plano.md` and `pipeline-state.json`, then synthesizes a Taskflow pipeline with agents, tools and artifact validation. |
| **Claude, OpenAI, Hermes** | Imports prompts, assistants and scratchpad logic as agent vertices. |
| **Excel** | Splits a multi-sheet workbook into a SQLite database and a concurrent four-agent graph. |

## Related

- [Agents and execution](04-agents-and-execution.md)
- [Interface tour](06-interface.md)
