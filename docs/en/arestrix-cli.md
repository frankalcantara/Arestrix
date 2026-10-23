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
arestrix-cli run <graph.json>|--project <folder> [options]
arestrix-cli gen <graph.json>
arestrix-cli check [--project <folder> | <graph.json>] [--require-env NAME]
arestrix-cli doctor [--project <folder>]
arestrix-cli env set|reset|list|check
arestrix-cli memory report [--project <folder>]
arestrix-cli mcp list|test|tools [id]
arestrix-cli project info|graphs|validate
arestrix-cli completion powershell|bash|zsh
arestrix-cli version
```

With no arguments it prints usage. `--help` and `version` return `0`.

Global option: `--no-user-env` (or `ARESTRIX_DISABLE_USER_ENV=1`) skips the persisted user store;
only the process environment is used. Useful in CI and hermetic tests.

### `env`

Environment variables use the same store as the Studio:

```powershell
# value from stdin (recommended for secrets)
"sk-..." | .\arestrix-cli.exe env set DEEPSEEK_API_KEY

# inspect names (never values)
.\arestrix-cli.exe env list

# check presence
.\arestrix-cli.exe env check DEEPSEEK_API_KEY KEENABLE_API

# remove
.\arestrix-cli.exe env reset DEEPSEEK_API_KEY
```

`env set` also accepts `--value`, but it may appear in shell history or the process list. Prefer
stdin. Storage is shared with the Studio: Windows user registry and `~/.config/arestrix/env` on
Linux.

By default the CLI loads those persisted variables into the process. In CI or hermetic tests, pass
`--no-user-env` (or set `ARESTRIX_DISABLE_USER_ENV=1`) so only the explicit process environment is
used. Explicit variables always win.

### `check`

```powershell
.\arestrix-cli.exe check --project .\examples\04-memoria `
  --require-env DEEPSEEK_API_KEY --json
```

Validates graph, codegen, project root, runtime, toolchain and required variables without compiling
or calling a model. Returns `5` when preflight fails.

### `doctor`

```powershell
.\arestrix-cli.exe doctor --json
```

Diagnoses runtime, toolchain, root and provider key presence. Keys are only `present`/`absent`;
values are never printed.

### `memory report`

```powershell
.\arestrix-cli.exe memory report --project .\examples\04-memoria
.\arestrix-cli.exe memory report --project .\examples\04-memoria --json
```

Reads SQLite natively and shows tokens, calls, retries, success/failure, context and
injected/used memories. It replaces `memory_report.py` in automation; the Python script remains
optional for development.

### `mcp`

```powershell
.\arestrix-cli.exe mcp list --project .\examples\02-triagem-de-suporte
.\arestrix-cli.exe mcp tools clientes --project .\examples\02-triagem-de-suporte
.\arestrix-cli.exe mcp test clientes --project .\examples\02-triagem-de-suporte --json
```

Lists servers, discovers tools and tests the connection before the pipeline.

### `project`

```powershell
.\arestrix-cli.exe project info --project .\examples\04-memoria
.\arestrix-cli.exe project graphs --project .\examples\04-memoria
.\arestrix-cli.exe project validate --project .\examples\04-memoria --json
```

Inspects the manifest, lists pipelines and validates graph + codegen without compiling.

### `completion`

```powershell
.\arestrix-cli.exe completion powershell | Out-String | Invoke-Expression
.\arestrix-cli.exe completion bash
.\arestrix-cli.exe completion zsh
```

Prints the shell completion script.

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

Automation options:

```powershell
.\arestrix-cli.exe run --project .\examples\04-memoria --json --timeout 300 --report .\run.json
```

- `--project` discovers `arestrix.project.json` and the primary pipeline;
- `--json` emits JSONL (`phase`, `log`, `telemetry`, `result`);
- `--timeout` cancels the pipeline and returns `124`;
- `--report` writes a JSON summary even on failure;
- `--quiet` suppresses phases/logs/telemetry;
- `--no-user-env` ignores persisted user credentials (only the process environment is used);
- `--ci` is `--json --quiet --no-color` with a default 900s timeout;
- `--batch <file.json>` runs sequential items;
- `--state <file>` sets the checkpoint (default `.arestrix/batch-state.json`);
- `--resume` skips completed items;
- `--answers <file.json>` feeds headless responses to `interaction` vertices.

### Headless human interaction

An `interaction` vertex asks the user a question with a safe form. In headless runs, provide the
answers as JSON keyed by the interaction node id:

```json
{
  "pergunta": { "tipo": "pj", "obs": "Enterprise customer" }
}
```

```powershell
.\arestrix-cli.exe run --project .\examples\07-interacao --answers .\examples\07-interacao\answers.json
```

The runtime validates required fields and choices, writes `.arestrix/out/<nodeId>.txt` and `.json`,
and returns the answers to the caller. A repeated interaction may use a list of answer objects; they
are consumed in order. Without `--answers` (and without `defaultAnswers` with `onTimeout: default`),
the pipeline fails closed. In the Studio, the same form opens as a modal and sends the answer through
the `answer_interaction` RPC.

Batch manifest:

```json
{
  "graph": "pipelines/main.json",
  "items": [
    { "id": "a", "env": { "ARESTRIX_EXAMPLE_TICKET": "ticket_a.txt" } },
    { "id": "b", "env": { "ARESTRIX_EXAMPLE_TICKET": "ticket_b.txt" } }
  ]
}
```

```powershell
.\arestrix-cli.exe run --batch .\batch.json --root .\my-project --json --resume
```

The batch is sequential; each item receives its environment variables and its result is stored in
the checkpoint. `completion powershell|bash|zsh` prints the shell completion script.

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
6. Exit code is `0` on success, `1` on build/run failure, `2` on usage, `3` on configuration, `4` on
   missing dependency, `5` on preflight and `124` on timeout.

The CLI does **not** call the model directly. The generated pipeline calls the provider using the
configured keys and endpoints.

| Code | Meaning |
| :---: | :--- |
| `0` | Success |
| `1` | Build/run failure |
| `2` | Invalid usage |
| `3` | Invalid configuration/project |
| `4` | Missing dependency (toolchain) |
| `5` | Preflight failed |
| `124` | Timeout |

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
# memory ON, recording metrics
$env:ARESTRIX_MEMORY_METRICS = "1"
.\arestrix-cli.exe run .\examples\04-memoria\pipelines\main.json `
  --root .\examples\04-memoria

# memory OFF on the SAME graph
$env:ARESTRIX_MEMORY_DISABLE = "1"
.\arestrix-cli.exe run .\examples\04-memoria\pipelines\main.json `
  --root .\examples\04-memoria

# native report
.\arestrix-cli.exe memory report --project .\examples\04-memoria
```

The report reads `.arestrix/memory/memory.db` and shows tokens, calls, retries, success/failure,
context and injected/used memories. See
[Workspaces, memory, skills and MCP](workspaces-memory-skills-mcp.md).

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
| `ARESTRIX_MEMORY_METRICS=1` | Records metrics even with memory off. In the Studio, use **Global Defaults → Memory → Record metrics**. |
| `ARESTRIX_MEMORY_DISABLE=1` | Disables the memory brief and tools at runtime. In the Studio, use **Measurement / A-B Mode**. |
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
