# Graph and nodes

[Português](../pt-BR/03-grafo-e-vertices.md) · [Index](../../README.md#documentation)

A pipeline is a directed graph. **Vertices** (nodes) are tasks, **edges** are the order in which they run.

## Vertex types

Each type maps to a Taskflow primitive when the program is generated.

| Type | Header color | Taskflow meaning | Use it for |
| :--- | :--- | :--- | :--- |
| **Static** | Blue | `taskflow.emplace(...)` | Deterministic work. |
| **Condition** | Orange | A task that returns an `int` | Decisions, convergence checks, feedback loops. Its output ports are numbered branches. |
| **Switch** | Gold | A task that returns an `int` (the case index) | Choosing one of several paths: one output port per case, plus a `default`. |
| **MCP** | Cyan | A task that calls the native MCP client | Calling ONE tool of an MCP server (a CRM, a database, a search) with no AI in between. |
| **Subflow** | Purple | A task that receives `tf::Subflow&` | Decomposing work into a child graph at run time. |
| **Module** | Green | A reusable composed graph | Packaging a sub-pipeline for reuse. |
| **Critical** | Teal | `tf::CriticalSection` | Mutually exclusive access to a shared resource. |
| **CUDA** | Blue | CUDA graph task (Taskflow 4.1) | GPU kernels. |
| **Agent** | Indigo | A task that runs an AI agent | An autonomous agent in a sandbox. See [Agents and execution](04-agents-and-execution.md). |
| **Fork** | Teal | Concurrent dispatcher | Starting several branches in parallel. |
| **Join** | Purple | Synchronization barrier | Waiting for the parallel branches to finish. |

Double-click a **subflow** or **module** vertex to open its child graph. `Esc` goes back up; the
breadcrumb bar above the canvas shows where you are.

### MCP vertex

An **MCP** vertex calls **one tool** of an MCP server configured in the project (`.workspace/mcps/`), with no AI model in between:
you pick the **server** and the **tool** from lists (the Studio asks the server which tools it offers), write the **arguments** as
JSON and say where the **input** comes from (the previous vertex, a project file, or nothing).

- Arguments accept `{{input}}` (the input text) and `{{input.field}}` (a field, when the input is JSON). A string made only of a
  reference keeps the value's type: `{"id": "{{input.id}}"}` sends a number if the field is a number.
- The result is kept in `.arestrix/out/<id>.txt`, which is exactly where **switches and agents read "the previous vertex"**, so an
  MCP vertex can feed a decision or an agent directly. You can also save it to a project file.
- If the server is missing or unreachable, a referenced field is absent, or the tool reports an error, the pipeline **stops with a
  clear message**.
- Servers marked ⚡ are **native Arestrix tools** (files, HTTP, data, memory, time, Git, SQLite): they need no installation and run inside the
  program. See [Native tools](05-workspaces-memory-skills-mcp.md#native-tools-no-install-no-process).
- It runs on the **native C++ MCP client** with a **persistent session** per server (see [MCP servers](05-workspaces-memory-skills-mcp.md#mcp-servers)).

### Switch

A **switch** sends the flow down one of several paths. Each **case** is an output port; the last port is always the
**default**, used when nothing else fits. You decide *how* the path is chosen in the Inspector (**General** tab),
with no code needed:

| Mode | What it does | Needs |
| :--- | :--- | :--- |
| **An AI model decides** *(default)* | The model reads the text and picks one case **by its name**. You can add a guidance sentence ("choose *high* if the patient is at risk"). | A model: the vertex's own (button **Choose this vertex model**), or the pipeline default. |
| **Look for words in the text** | Goes to the first case whose words appear in the text. Accents and capitals do not matter. | Nothing: no AI, no internet. |
| **C++ code** *(advanced)* | You write the rule in the **Lambda** tab and return the case number (`0`, `1`, `2`...). | C++ knowledge. |

**What text is analyzed:** the final answer of the previous vertex (an agent), or a file of the project
(for example `report/audit.txt`).

How it behaves:
- If the model answers with something that is not one of the case names, the flow follows the **default** (and the console says so).
- If there is nothing to analyze (the previous vertex wrote no text, the file does not exist) or the model cannot be
  reached, the pipeline **stops with a clear error**. It never picks a path silently.
- In code mode, any number outside the range also goes to the **default**.
- Add, rename and remove cases in the same panel. Removing a case also removes its connections, and one undo brings
  both back. The default is fixed.
- An unconnected case is a validation warning; an unconnected default is not (the flow simply ends there).
- Like a condition, a switch may close a loop (the return edge becomes a feedback edge).
- To convert a vertex to or from switch, first remove its output connections, because the ports change.

## Ports and edges

Each vertex has **input ports** (blue) and **output ports** (orange). A port carries either
**flow** (execution order) or **data**. Drag from an output port to an input port to connect them.

### The two kinds of edge you will see

| Edge | Look | Meaning |
| :--- | :--- | :--- |
| **Flow edge** | Solid, slate gray | Normal dependency: the target runs after the source finishes. |
| **Feedback edge** | Dashed, amber | A return edge that closes a loop. The app marks it automatically. |

An edge becomes a **feedback edge** when it leaves a **condition** vertex and points back to that
vertex or to one that leads back to it. In generated code it becomes a Taskflow loop instead of a
plain dependency. At run time every agent is limited to 10 executions
(`ARESTRIX_MAX_AGENT_RUNS`), so a loop that never converges stops with an error instead of spending
money forever.

Example: in the *full agent architecture* preset the LLM decision vertex sends branch 0 to storage and
consensus, and branch 1 back to the queue. That return is the dashed edge.

## Validation

Before running, the graph is checked:

| Finding | Severity |
| :--- | :--- |
| A vertex with no connections (when the graph has more than one vertex) | Warning |
| A condition vertex with an unconnected branch | Warning |
| A cycle that contains **no** condition vertex | **Error**: it would deadlock or loop forever |

Cycles are allowed only when a condition vertex can break them.

## Editing

| Action | How |
| :--- | :--- |
| Add a vertex | **Home → + Task**, the type selector, or the node library (**Catalog**). |
| Move | Drag. Snaps to a 10 px grid; hold `Shift` for free movement. |
| Connect | Drag from an output port to an input port. |
| Pan / zoom | Drag the empty canvas / mouse wheel. |
| Undo / redo | `Ctrl+Z` / `Ctrl+Y` (or `Ctrl+Shift+Z`). |
| Delete | `Del` or `Backspace` on the selected vertex or edge. |

### Context menu (right-click a vertex)

| Item | Shortcut | Notes |
| :--- | :--- | :--- |
| Sandbox, Model, Tools, Memory, Skills | | The same five quick actions as the toolbar above the vertex. |
| Duplicate | `Ctrl+D` | New id, new port ids, label gets a "(copy)" suffix, offset by 40 px. Edges are not copied. |
| Copy / Cut / Paste | `Ctrl+C` / `Ctrl+X` / `Ctrl+V` | Internal clipboard; each paste shifts further. |
| Save as template | | Adds the vertex to the node library (below). |
| Enter subflow | | Only for subflow and module vertices. |
| Remove all connections | | One undo step restores them. |
| Delete | `Del` | |

## Node library

**Home → Catalog** opens the *Node Library*: ready-made vertices with ports, C++ body, required headers
and agent settings already filled in.

- Filter by category; **Insert** adds the vertex at the center of the view and selects it.
- **View C++20** opens the generated body in a read-only window with a Copy button, so you can audit
  it before inserting.
- **Instantiate full agent architecture** replaces the current graph with a complete multi-agent
  pipeline (queue → vision subflow → LLM decision → SQLite → consensus, with a feedback loop).
- **Import / Export** exchange templates as JSON, so a team can share them.
- **Save as template** (context menu) stores a vertex under *Custom plugins*. The library refuses it when an
  equivalent template already exists: same type, same ports, same parameter names and same C++ body.
  Vertices that contain a child graph cannot be saved yet.

Built-in templates: LLM decision agent, thread-safe queue worker, SQLite persistence, parallel vision
pipeline (subflow), CUDA kernel, and consensus module.

## Related

- [Architecture](01-architecture.md)
- [Interface tour](06-interface.md)
