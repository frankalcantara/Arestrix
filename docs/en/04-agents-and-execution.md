# Agents and execution

[Português](../pt-BR/04-agentes-e-execucao.md) · [Index](../../README.md#documentation)

## Build & Run (F5)

1. The graph is validated and sent to the C++ backend.
2. The backend generates a Taskflow `main.cpp`, compiles it with MSVC (Windows) or g++/clang++ (Linux)
   against `arestrix_core`, and **runs it inside the project folder**. An unchanged graph reuses the
   compiled binary.
3. Logs and agent telemetry stream live to the console and to badges on the vertices.

`F5` or `Ctrl+Enter` runs, `Shift+F5` stops. The same engine is available without the interface:

```bash
arestrix-cli gen graph.json                    # print the generated C++
arestrix-cli run graph.json --root <project>   # generate, compile and run
```

## What an agent vertex does

An agent vertex runs a real **tool-use loop** against its configured provider:

- The **skill** (`SKILL.md`) is the system prompt; the vertex prompt template is the task.
- The model can call tools: `read`, `write`, `edit`, `bash`, `validate_svg`, plus the tools of the MCP servers the agent declares (including the [native Arestrix tools](05-workspaces-memory-skills-mcp.md#native-tools-no-install-no-process))
  (see [MCP servers](05-workspaces-memory-skills-mcp.md#mcp-servers)). The built-in ones are confined
  to the project folder.
- The loop continues until the model stops calling tools. Skills can add more tools and MCP servers.

**Agent language.** At run time the agent is configured in the language of its own constitution: the skill and the prompt
template (the texts that define it). The runtime detects the language and emits everything the agent sees — the system
context block, the default prompt, the built-in and native tool descriptions and schemas, and the error messages returned
to it — in that language (Portuguese or English; Portuguese is the default when the texts give no clear signal). External
MCP tool descriptions are third-party data and pass through unchanged. Console logs and telemetry stay in the interface's
language.

Providers are listed in [Getting started](02-getting-started.md#api-keys-and-providers).

### The five quick actions

Every vertex has a bar of five buttons (also in the right-click menu):

| Action | Scope | Configures |
| :--- | :--- | :--- |
| Sandbox | Workspace | Isolation level, network policy, memory, timeout, mounts. |
| Model | Project | Provider, model, skill file, prompt template. |
| Tools | Project | Allowed tools and MCP servers. |
| Memory | Project | Blackboard, semantic lessons, persistence. |
| Skills | Workspace / Project | Skills bound to the vertex. |

Settings can be inherited from the **global defaults** (toolbar) or overridden per vertex.

## Sandboxes

| Level | Isolation | Notes |
| :--- | :--- | :--- |
| `host_direct` | Job Object / process group, timeout, memory limit | **Not a security boundary.** Strips `*_API_KEY`, `*_TOKEN`, `*_SECRET` from the child environment. |
| `container` | Docker | `--cap-drop=ALL`, read-only root, no network by default, workspace-only mounts. |
| `microvm` | WSL2 | Shares the host file system; refuses `offline` policies it cannot enforce. |

Network policy per vertex: `offline`, `egress_llm_only` or `full`.

## Safety properties

- A sandbox level that is unavailable **fails closed**. It does not silently fall back to the host unless
  `allowFallbackToHost` is set.
- File tools and the file endpoints exposed to the interface reject paths outside the project: `..`,
  absolute paths, symlinks and NTFS streams.
- No shell is used to start processes (`std::system` is not used).
- A feedback loop cannot spend unlimited money: each agent vertex is capped at 10 runs
  (`ARESTRIX_MAX_AGENT_RUNS`).

## Telemetry

The runtime streams events that the canvas shows as badges on each vertex:

| Event | Meaning |
| :--- | :--- |
| `thinking` | Reasoning in progress, with a running token/byte count. |
| `tool_start` / `tool_end` | A tool call began or finished, with arguments and status. |
| `429 retry` | Rate limit detected (`RESOURCE_EXHAUSTED`, "Rate limit reached"); a countdown shows the scheduled retry. |
| `completed` | The vertex finished. |

## Batch pipelines and resume

For pipelines that process many items (articles, data batches), the **Resume engine** keeps a checkpoint
per item and per stage in `pipeline-state.json`, written atomically.

- **Artifact validation:** a stage is marked complete without calling the API again if its output
  exists, has a minimum size, and matches an expected pattern (for example YAML front matter). This
  is recorded as `recoveredFromArtifact: true`.
- **Serialized bottlenecks:** heavy stages can run one item at a time to respect provider limits while
  later stages run concurrently.
- The **Run** tab has the batch orchestrator: items, concurrency, serialized stages, and a live checkpoint matrix.

## Deliverables and Output Nodes (Artifacts)

Any pipeline designed to generate deliverable products (vector graphics, reports, spreadsheets, models, or datasets) can declare one or more **`output` (Output / Artifact)** nodes.

- **Source and Destination Configuration:** The vertex specifies a `sourcePath` (e.g., `workspace/scene.svg` or `workspace/report.md`) produced by preceding agents, and optionally a `destinationPath` (e.g., `exports/scene.svg`). At execution completion, the deliverable is atomically copied to the final destination folder.
- **Built-in Multi-Format Support:**
  - **SVG:** Interactive vector viewer with zoom controls (+, -, 100%, fit to screen), source code inspection, and crisp typography rendering (no blur/drop-shadow on text).
  - **Spreadsheets (CSV, TSV, XLSX):** Tabular data grid with instant cell search and raw data toggling.
  - **Text / Markdown:** Formatted viewer for technical documents and reports with syntax highlighting and one-click copy.
  - **PDF and JSON:** Structured view with clean indentation and direct system viewer launch.
- **Artifacts Tab in the Bottom Drawer:** Positioned alongside the *Terminal* and *Telemetry* tabs, the *Artifacts* tab displays the real-time count of generated deliverables, lets users inspect visual graphics or code, copy content, and open the `exports/` folder in Windows Explorer with one click.
- **Local Chat Assistant Operation:** The Studio's native chat assistant integrates with the artifact lifecycle through the `list_artifacts` and `get_artifact` tools, allowing the assistant to examine generated deliverables and provide analytical feedback directly to the user.

## Standalone Runner (Phase 1: Autonomous Executable)

The **Standalone Runner** transforms any agent graph into an independent application suitable for standalone distribution or streamlined operation, without requiring operators to launch the full visual graph editor.

### Activation Triggers
1. **Ribbon Button:** In the **Run** tab, the **Distribution** group exposes the **Standalone Runner** button (icon 🚀).
2. **Backstage (File):** In the **Standalone Runner** card on the active project overview page.
3. **Project Manifest Property:** Configured directly in `arestrix.project.json` under `"runner": { "enabled": true, ... }`.

### The Three Operational Views
The Runner provides a modern, high-contrast, clean UI with 3 essential screens for monitoring and operational control:

1. **Progress Screen:**
   - **Real-Time Status Indicator:** Status pill (`Waiting`, `Compiling`, `Running`, `Success`, `Error`, `Stopped`).
   - **Linear Progress Bar:** Smooth visual progress bar tracking workflow completion percentage.
   - **Elapsed Timer:** Real-time run duration timer with millisecond accuracy.
   - **Step Timeline:** Granular tracking of each Taskflow pipeline stage with status badges, start timestamps, and stage execution duration.

2. **Diagnostics Screen:**
   - **Telemetry KPIs:** Live cards monitoring token consumption (Prompt, Completion, Total), tool calls count, 429 rate limit retries, and average latency.
   - **Agent Thinking Stream:** Live streaming view of agent reasoning and cognitive deliberations during execution loops.
   - **Tool Call Inspection:** Detailed table of invoked tools, arguments passed, execution duration, and success/failure status.
   - **Diagnostic Export:** One-click button to copy complete diagnostic logs in structured JSON format.

3. **Operations Screen:**
   - **Execution Controls:** Start Execution (`F5`), Stop (`Shift+F5`), and Restart.
   - **Configurable Input Parameters:** Dynamically rendered form based on manifest parameters (strings, numbers, booleans), allowing runtime customization without modifying graph definitions.
   - **Artifacts Access:** Direct button to open the generated outputs directory in Windows Explorer.

### Standalone Packaging
The **Export** tab in the Standalone Runner modal coordinates native distribution packaging:
- Target distribution directory (defaults to `dist/runner`).
- Copies the native C++ executable and referenced workspace skills.
- Generates `runner_manifest.json` with metadata, schema, and parameter definitions.
- Generates single-click launcher batch scripts (`run.bat`).

## Related

- [Workspaces, memory, skills and MCP](05-workspaces-memory-skills-mcp.md)
- [Architecture](01-architecture.md)
