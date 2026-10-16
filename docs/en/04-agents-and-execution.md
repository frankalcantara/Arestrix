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

## Related

- [Workspaces, memory, skills and MCP](05-workspaces-memory-skills-mcp.md)
- [Architecture](01-architecture.md)
