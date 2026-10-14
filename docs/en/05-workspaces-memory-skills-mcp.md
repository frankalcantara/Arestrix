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
all threads: calls to the same server are serialized, different servers run in parallel. On a test server, 15 calls took 0.48 s
with the persistent session against 3.75 s opening a process per call (7.8x). A session is reopened if the server dies or its
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
