# Architecture

[Português](../pt-BR/01-arquitetura.md) · [Index](../../README.md#documentation)

Arestrix turns a drawing of agents into a program. You edit a graph in a web canvas, the
C++ backend generates a **C++20 / Taskflow** program from it, compiles it, and runs it. This page
explains the pieces and how they fit.

## The big picture

```
 ┌────────────────────────────┐        WebUI RPC        ┌────────────────────────────────┐
 │ Frontend (TypeScript)      │ ◄─────────────────────► │ Backend (C++20, CppCore/)      │
 │  src/core  graph + history │                         │  project, build, sandbox,      │
 │  src/view  Three.js canvas │                         │  memory, MCP, code generator   │
 │  src/ui    ribbon + modals │                         └───────────────┬────────────────┘
 └────────────────────────────┘                                         │ generates + compiles
                                                                        ▼
                                                        ┌────────────────────────────────┐
                                                        │ Generated program (main.cpp)   │
                                                        │  Taskflow graph + arestrix_core│
                                                        │  runtime (agents, tools, HTTP) │
                                                        └────────────────────────────────┘
```

The window is a native **WebUI** window (no Electron). The interface is built by Vite into a single
`dist/index.html` that the executable serves from memory.

## Frontend layers (`src/`)

| Layer | Folder | Responsibility |
| :--- | :--- | :--- |
| Logical | `src/core/` | In-memory `Graph`, validation, subflow hierarchy, the **Command pattern** history (undo/redo), templates, skills, project and workspace logic, Git synchronization, and the Standalone Runner orchestrator (`RunnerManager`). No rendering. |
| Visual | `src/view/` | Three.js orthographic canvas, camera (pan/zoom), infinite grid, Bézier edges, and the SVG vertex layer that draws the node cards on top. |
| Interface | `src/ui/` | Office-style ribbon, Backstage view, inspector, modals (`StandaloneRunnerModal`, `GitAccountModal`), console, context menu. |
| i18n | `src/i18n/` | `pt-BR` and `en-US` catalogs, loaded through i18next. |

Every change to the graph goes through a command (`AddNodeCommand`, `RemoveNodeCommand`,
`MoveNodeCommand`, `AddEdgeCommand`, `UpdateNodeCommand`, `CompoundCommand`...), which is why undo and
redo work for everything, including paste, duplicate and delete.

## Backend modules (`CppCore/src/`)

| Module | What it does |
| :--- | :--- |
| `core/` | Graph and node types, JSON (de)serialization. |
| `compiler/` | `TaskflowGenerator`: graph → `main.cpp`. Detects loop (feedback) edges. |
| `build/` | `BuildService`: compiles the generated program with MSVC, g++ or clang++ and runs it. |
| `rt/` | The **runtime** every generated program links to: HTTPS clients, the agent tool-use loop, the switch decision (AI model or keywords), confined file tools, and the network guard against DNS rebinding. |
| `sandbox/` | Isolation drivers (host process, Docker, MicroVM), telemetry emitter, resume engine. |
| `native/` | The native tool servers (files, HTTP, data, memory, time, Git, SQLite): C++ that runs inside the program, with project confinement. |
| `mcp/` | Native Model Context Protocol client with persistent sessions (stdio and HTTP) and the server registry. |
| `memory/` | Blackboard and semantic-lessons memory. |
| `project/` | Projects and workspaces (`arestrix.project.json`), native Git synchronization service (`GitSyncService`), Standalone Runner export service (`RunnerService`) with fail-closed zero-secrets auditing, auto-commit/push, and private repository creation. |
| `search/` | Project search: lexical ranking (BM25 + character trigrams) over every pipeline, subflow and skill, with a content-hash index. |
| `pi/` | Scanner for importing existing Pi agent projects. |
| `cli/` | `arestrix-cli`, the headless runner. |
| `util/` | Own JSON layer on top of **simdjson**. |

Everything external lives in `third_party/` (see [Development](07-development.md)).

## Studio, Workspace and Project

Arestrix separates three scopes, in both the code and the interface. The separation exists so that
credentials, memory and files never leak between things that should be independent.

| | **Studio** | **Workspace** | **Project** |
| :--- | :--- | :--- | :--- |
| What | The application: canvas, compiler toolchain | The physical folder and its infrastructure | One business purpose and its pipelines |
| Holds | Global API keys, authorized GitHub skill repositories, recent projects | `.workspace/` skills and MCPs, SQLite database, sandboxes, limits | `arestrix.project.json`, `pipelines/*.json`, dedicated Git repository (local clone with remote sync), prompts, memory scoped by `projectId` |
| Analogy | The factory | The workshop | The product being built |

Interface elements are tagged with their scope (`Studio`, `Workspace`, `Projeto`) so you always know
what you are changing. More in [Workspaces, memory, skills and MCP](05-workspaces-memory-skills-mcp.md).

## From graph to running program

1. The graph is validated in the frontend (see [Graph and nodes](03-graph-and-nodes.md)).
2. It is sent to the backend as JSON.
3. `TaskflowGenerator` emits a `main.cpp`: each node becomes a Taskflow task, edges become
   `precede()` dependencies, condition nodes return the branch index, loop edges become Taskflow loops.
4. `BuildService` compiles it against `arestrix_core` and runs it in the project folder. An unchanged
   graph reuses the previous binary.
5. Logs and telemetry stream back to the console and to the badges on the canvas.

Details of step 4 and 5 are in [Agents and execution](04-agents-and-execution.md).

## Standalone Runner Architecture

The **Standalone Runner** decouples workflow execution from the visual Studio canvas:
1. **Frontend (`RunnerManager`):** Manages the execution lifecycle, relays `EventBus` telemetry to Progress, Diagnostics, and Operations tabs, and performs frontend pre-export security checks.
2. **Backend (`RunnerService`):**
   - Compiles native Taskflow pipelines into standalone binaries.
   - Generates distribution manifests (`runner_manifest.json`) supporting runtime parameter customization.
   - Bundles referenced workspace skills and launcher scripts.
   - Enforces a strict *fail-closed* zero-secrets policy by auditing manifest files, skill assets, and executable binaries before finalizing the bundle.
3. **Runtime Secrets Isolation:** The compiled executable contains zero baked-in secrets or keys; it resolves credentials dynamically from the target host OS environment variables (`platform::getEnv`), ensuring secure portability.

## Related

- [Getting started](02-getting-started.md)
- [Graph and nodes](03-graph-and-nodes.md)
