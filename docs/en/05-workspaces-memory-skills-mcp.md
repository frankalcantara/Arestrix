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

Arestrix is an MCP client over stdio and SSE. Servers are registered per workspace
(`arestrix-mcp-servers.json`), shown in the interface as *Workspace MCP servers*, and assigned to vertices
in the **Tools** action. Pre-approved MCP configurations are provisioned in `.workspace/mcps`.

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
