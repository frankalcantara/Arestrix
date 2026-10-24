# Graph and nodes

> **Applies to:** Arestrix Beta 1.5 (`1.5.0-beta.1`) · Last reviewed: 2026-10-19

[Português](../pt-BR/grafo-e-vertices.md) · [Index](../README.md)

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
| **Agent** | Indigo | A task that runs an AI agent | An autonomous agent in a sandbox. See [Agents and execution](agents-and-execution.md). |
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
  program. See [Native tools](workspaces-memory-skills-mcp.md#native-tools-no-install-no-process).
- It runs on the **native C++ MCP client** with a **persistent session** per server (see [MCP servers](workspaces-memory-skills-mcp.md#mcp-servers)).

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

## Call edges (delegation)

A **call edge** is a dashed violet edge that declares a delegation, not a scheduling dependency. It
means:

> The source agent **may invoke** the target agent synchronously, at most N times, and receives its
> answer.

In v1:

- the source is an **agent**; the target is an **agent**, an **interaction** or a **subflow**;
- the target must be **callable-only**: it has no flow edges (in or out). If it also needs to run as a
  normal step, duplicate the vertex;
- the target is **not** a top-level Taskflow task; it runs inside the caller's task. A callable
  subflow's `childGraph` is compiled as a native function in the same executable.

To create a call, drag from the agent's **`call`** output port (violet) to the target's `in` port.
Select the source agent and use the **Delegation** section of the Inspector to set `maxCalls` and a
description that the model sees when choosing the target.

At run time the caller receives the `call_agent` tool with an **enum of declared targets**. The
skill guides the agent; the graph authorizes; the runtime enforces:

| Limit | Default | Environment override |
| :--- | :--- | :--- |
| `maxCalls` per edge, per caller run | 1 | — |
| Nesting depth | 3 | `ARESTRIX_MAX_CALL_DEPTH` |
| Total calls per process | 50 | `ARESTRIX_MAX_TOTAL_CALLS` |

The result is a JSON tool message with `callId`, `target`, `status`, `output` and `depth`; an
interaction target also carries `answers`. The target's final text is also written to
`.arestrix/out/<target>.txt`.

See [`examples/06-chamada-agente`](../examples/06-chamada-agente) for a coordinator calling a
specialist, [`examples/09-chamadas-paralelas`](../examples/09-chamadas-paralelas) for two
delegations in one turn, and [`examples/10-subflow-chamavel`](../examples/10-subflow-chamavel) for a
native callable subflow.

## Interaction vertices

An **interaction** vertex asks a human a question. It accepts a safe form schema (text, textarea,
number, boolean, choice, multi-choice, date), a channel (`studio`, `runner`, `cli`, `any`), a timeout,
who answers and a sensitive-answer flag. It can run in the flow or be called by an agent through a
call edge.

The form is **static** (fields authored in the Inspector) or **generated by the vertex**
(`schemaSource: "vertex"`). In the generated mode the runtime sends the markdown context and the
configured artifacts to the generator model, validates the returned JSON schema against the allowed
types/limits, and never executes HTML or JavaScript. An invalid schema or model failure applies
`generator.fallback` (`fail` or `free_text`). Artifacts are listed in the modal and their text is
included in the generator context.

Headless runs use `--answers` (see [`arestrix-cli`](arestrix-cli.md#headless-human-interaction)); the
answers are validated and written to `.arestrix/out/<nodeId>.txt`/`.json`. In the Studio, the same
safe form opens as a modal and returns the validated answers to the flow or to the calling agent.
Every attempt is audited at `.arestrix/runs/<runId>/interactions/<requestId>.json`. Without a channel
the vertex fails closed. Exported standalone runners warn that interactions need an interactive host
or `ARESTRIX_INTERACTION_ANSWERS`.

## Ports and edges

Each vertex has **input ports** (blue) and **output ports** (orange). A port carries either
**flow** (execution order) or **data**. Drag from an output port to an input port to connect them.

### The two kinds of edge you will see

| Edge | Look | Meaning |
| :--- | :--- | :--- |
| **Flow edge** | Solid, slate gray | Normal dependency: the target runs after the source finishes. |
| **Feedback edge** | Dashed, amber | A return edge that closes a loop. The app marks it automatically. |
| **Call edge** | Dashed, violet | Delegation: the source may invoke the target synchronously and receive its answer. It does not schedule the target. |

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

- [Architecture](architecture.md)
- [Interface tour](interface.md)
