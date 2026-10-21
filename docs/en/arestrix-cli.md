# `arestrix-cli` — headless execution

> **Applies to:** Arestrix Beta 1.5 (`1.5.0-beta.1`) · Last reviewed: 2026-10-21

[Português](../pt-BR/arestrix-cli.md) · [Index](README.md)

`arestrix-cli` is the **headless** Arestrix runner: it reads a graph, generates C++20 Taskflow
code, compiles it and runs the pipeline without opening the Studio or the WebUI.

It uses the same `TaskflowGenerator`, runtime and memory database as the Studio. The difference is
that there is no window, canvas, inspector or visual editor.

```text
graph JSON
   ↓
TaskflowGenerator
   ↓
C++20 + Taskflow
   ↓
local compiler (MSVC, GCC or Clang)
   ↓
pipeline binary
   ↓
agents, MCPs, memory, outputs and telemetry in the terminal
```

## Where it lives

CMake produces two executables:

| Binary | What it is |
| :--- | :--- |
| `arestrix` / `arestrix.exe` | The full Studio: WebUI, canvas, editors, memory and MCP. |
| `arestrix-cli` / `arestrix-cli.exe` | The headless runner for CI, servers and scripts. |

On Windows the build copies the CLI to the repository root:

```text
build\arestrix-cli.exe
arestrix-cli.exe
```

The `.exe` is not versioned. To build it:

```powershell
python CppCore/dev.py --no-run
```

or build the CMake target `arestrix-cli` directly.

## Usage

```text
arestrix-cli run <graph.json> [--root <project-folder>]
arestrix-cli gen <graph.json> [--root <project-folder>]
```

With no arguments it prints usage and returns code `2`.

### `run`

Generates, compiles and runs the pipeline:

```powershell
.\arestrix-cli.exe run .\examples\04-memoria\pipelines\main.json `
  --root .\examples\04-memoria
```

`--root` is the project root. The runtime resolves from it:

- `skills/`;
- `.workspace/mcps/`;
- `workspace/` and `output/`;
- `exports/`;
- `.arestrix/memory/memory.db`;
- relative paths used by agents and vertices.

If `--root` is omitted, the root is the current directory. In automation, always pass the explicit
path.

### `gen`

Does not compile or run: it prints the generated C++ to stdout.

```powershell
.\arestrix-cli.exe gen .\examples\05-matematica-com-memoria\pipelines\main.json
```

Use it to debug codegen, review a vertex body or compare two graph configurations.

## What `run` does

1. Reads and validates the graph JSON with the same `core::Graph` model as the Studio.
2. `TaskflowGenerator` generates the pipeline's `main.cpp`.
3. `BuildService` compiles it with the configured compiler.
4. The generated binary runs.
5. Logs, agent telemetry and `[OUTPUT_DELIVERY]` lines appear in the terminal.
6. Exit code is `0` on success, `1` on build/run failure and `2` on usage or graph read errors.

The CLI does **not** call the model directly. The generated pipeline calls the provider using the
configured keys and endpoints.

## Requirements

- C++20 compiler: MSVC on Windows; GCC or Clang on Linux.
- CMake and the Arestrix runtime.
- Taskflow (already in `third_party/`).
- Model provider key when the pipeline has agents.
- No Python or Node in the pipeline runtime.

The CLI is a native binary. Python appears only in optional development tools (e2e scripts and
`memory_report.py`) and in the demo MCPs of examples 2 and 3; see [Development](development.md).

## Examples

### Run an example project

```powershell
.\arestrix-cli.exe run .\examples\05-matematica-com-memoria\pipelines\main.json `
  --root .\examples\05-matematica-com-memoria
```

Example 05 uses the remote Keenable MCP. Set the key first:

```powershell
$env:KEENABLE_API = "your-key"
```

### Compare memory on and off

```powershell
# run without memory, still recording metrics
$env:ARESTRIX_MEMORY_METRICS = "1"
.\arestrix-cli.exe run .\examples\04-memoria\pipelines\main.json `
  --root .\examples\04-memoria

# run with memory (graph configuration)
Remove-Item Env:ARESTRIX_MEMORY_METRICS
.\arestrix-cli.exe run .\examples\04-memoria\pipelines\main.json `
  --root .\examples\04-memoria

# consumption report (development tool)
python scripts\memory_report.py .\examples\04-memoria
```

The report reads `.arestrix/memory/memory.db` and shows tokens, calls, latency and memory context
per agent and mode. See [Workspaces, memory, skills and MCP](workspaces-memory-skills-mcp.md).

### CI

```yaml
- run: cmake --build build --target arestrix-cli
- run: ./arestrix-cli run pipelines/main.json --root .
  env:
    DEEPSEEK_API_KEY: ${{ secrets.DEEPSEEK_API_KEY }}
```

For tests without real keys, the e2e scripts use fake LLMs and test MCPs; see
[Development](development.md).

## Environment variables

| Variable | Effect |
| :--- | :--- |
| `DEEPSEEK_API_KEY`, `OPENAI_API_KEY`, etc. | Provider key used by agents. |
| `KEENABLE_API` | Keenable MCP key in example 05. |
| `ARESTRIX_MEMORY_METRICS=1` | Records metrics even with memory off, for comparison. |
| `ARESTRIX_PYTHON` | Used only by examples that declare a Python stdio MCP. |

Never put keys in the graph, `arestrix.project.json` or `.workspace/mcps`. Use environment variables
and `${NAME}` references.

## When to use the CLI

- CI that builds and runs a pipeline without the Studio.
- A server or machine without a graphical environment.
- Reproducing a runtime bug with clean logs.
- Running examples and tutorials in automation.
- Comparing API consumption between memory configurations.
- Inspecting generated C++ with `gen`.

## When to use the Studio

- Building and editing the graph visually.
- Configuring vertices, skills, memory and MCPs.
- Viewing telemetry, artifacts and the memory graph.
- Testing MCPs and providers through the panels.

## Troubleshooting

| Symptom | Likely cause | Fix |
| :--- | :--- | :--- |
| `arestrix-cli` is not recognized | The binary is not on `PATH`. | Use `.\arestrix-cli.exe` or add the repository root to `PATH`. |
| `Erro ao ler o grafo` | `arestrix.project.json` was passed instead of the graph. | Pass `pipelines/main.json`. |
| Compilation failure | Missing or incompatible C++20 toolchain/CMake. | Run `python CppCore/dev.py --no-run` and check the compiler. |
| API key failure | Provider variable not set. | Set the key in the user environment. |
| Files do not appear in `exports/` | `--root` points to another folder. | Pass the real project root. |
| MCP not found | Server is not in `.workspace/mcps`. | Register the MCP in the project or use the UI preset. |

## Related

- [Getting started](getting-started.md)
- [Agents and execution](agents-and-execution.md)
- [Workspaces, memory, skills and MCP](workspaces-memory-skills-mcp.md)
- [Development](development.md)
- [Examples](../../examples/README.md)
