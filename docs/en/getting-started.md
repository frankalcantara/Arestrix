# Getting started

> **Applies to:** Arestrix Beta 1.0 (`1.0.0-beta.1`) · Last reviewed: 2026-10-19

[Português](../pt-BR/primeiros-passos.md) · [Index](../README.md)

Arestrix Studio runs on **Windows and Linux**. The programs generated from a graph target Windows for now.

## Requirements

1. **Git with submodules.** Taskflow, simdjson, WebUI, i18next, Monaco and SkillSpector live in `third_party/`.
2. **Node.js 20+ and npm** (Vite, TypeScript, Three.js, Vitest).
3. **Python 3.10+**, which drives the build script (`CppCore/dev.py`) and the end-to-end test.
4. **A C++20 compiler and CMake 3.20+.**
   - Windows: Visual Studio 2022 or newer (Community or Build Tools) with the *Desktop development with C++* workload. Its bundled CMake and Ninja are detected automatically.
   - Linux: `g++` 11+ (or `clang++`), `cmake`, optionally `ninja`, and `curl` (used by the agent runtime for HTTPS).
5. Optional: Docker (container sandbox), WSL2 (MicroVM driver), Graphviz.

## Install and run

```bash
git clone --recurse-submodules https://github.com/frankalcantara/Arestrix.git
cd Arestrix
npm install
python CppCore/dev.py
```

If you cloned without `--recurse-submodules`, run `git submodule update --init`.

`dev.py` initializes the submodules, builds the frontend (`npm run build`) and the backend (CMake) and
then opens the Studio.

| Command | Effect |
| :--- | :--- |
| `python CppCore/dev.py` | Build everything and open the Studio. |
| `python CppCore/dev.py --no-run` | Build only. |
| `python CppCore/dev.py --test` | Build and run the C++ unit tests (`ctest`). |
| `python CppCore/dev.py --no-frontend` | Skip the frontend build. |

### What the build produces

| Output | What it is |
| :--- | :--- |
| `arestrix` / `arestrix.exe` | The Studio: the WebUI window and the engine. |
| `arestrix-cli` / `arestrix-cli.exe` | Headless runner: `arestrix-cli run graph.json --root <project>` and `arestrix-cli gen graph.json`. |
| `runtime/lib/arestrix_core.lib` (`libarestrix_core.a`) | The runtime that every generated program links against. |

## Other ways to work

| Mode | Command | Use it for |
| :--- | :--- | :--- |
| Browser dev server | `npm run dev` (http://localhost:5173) | Editing the canvas, styling and modals with hot reload. Execution, compilation and MCP need the native app. |
| Frontend tests | `npm test` | Vitest suite (see [Development](development.md)). |
| Frontend build only | `npm run build` | Produces the single-file `dist/index.html` and copies it to `CppCore/dist/`. |

## Your first run

1. Open the Studio. The first screen is the Backstage view; create or open a project.
2. On the **Home** tab, click **Catalog** (the node library) and choose *Instantiate full agent architecture*,
   or insert a single vertex.
3. Configure the model and the API key (see below).
4. Press **F5**. Press **Shift+F5** to stop.

### API keys and providers

Keys are read from environment variables. The Studio stores them as user variables (Windows Registry,
or `~/.config/arestrix/env` on Linux) and never passes them through a shell.

| Provider | Key variable | Endpoint |
| :--- | :--- | :--- |
| `deepseek` | `DEEPSEEK_API_KEY` | `https://api.deepseek.com` |
| `openai` | `OPENAI_API_KEY` | `https://api.openai.com/v1` |
| `anthropic` | `ANTHROPIC_API_KEY` | `https://api.anthropic.com` |
| `google` | `GEMINI_API_KEY` | Gemini OpenAI-compatible endpoint |
| `jev` | `JEV_API_KEY` (optional) | `JEV_BASE_URL` is **required** (any OpenAI-compatible server) |
| `local` | `LOCAL_LLM_API_KEY` (optional) | `LOCAL_LLM_BASE_URL`, default `http://localhost:11434/v1` (Ollama, llama.cpp, vLLM) |

Any endpoint can be overridden with `<PROVIDER>_BASE_URL`.

### Studio default model

Besides the per-vertex model, the Studio has its own **default model** for small tasks of its own, such as
writing the search-synonym dictionary of a language. It lives **only on your machine**, in user environment
variables, and is never written to a project:

| Variable | Meaning |
| :--- | :--- |
| `ARESTRIX_DEFAULT_PROVIDER` | `deepseek`, `openai`, `anthropic`, `google`, `groq`, `jev` or `local`. |
| `ARESTRIX_DEFAULT_MODEL` | Model name. Optional for the hosted providers (each has a default); **required** for `jev` and `local`. |

You do not need to set these by hand: **Global Defaults → Provider & Model → Studio default model** (Studio scope)
saves them for you, shows which model is in use, and also asks for the server address of `local` and `jev`.
Choosing *Automatic* stores the value `auto`, which means "not set".

If `ARESTRIX_DEFAULT_PROVIDER` is not set (or is `auto`), the Studio uses the first provider that has an API key on this
machine (DeepSeek, OpenAI, Anthropic, Google, Groq, in that order). The keys and endpoints are the same
variables listed above, so a local model (Ollama, llama.cpp, vLLM) needs `ARESTRIX_DEFAULT_PROVIDER=local`,
`ARESTRIX_DEFAULT_MODEL=<name>` and `LOCAL_LLM_BASE_URL`.

### Try the tutorial without spending money

```bash
python scripts/e2e_tutorial.py
```

This builds the graph from the [SVG agent tutorial](../pt-BR/tutorials/svg-agent.md) (in Portuguese)
against a scripted fake LLM server, compiles the program, and runs the fork/join, the 429 retry and the
auto-correction loop.

## Related

- [Architecture](architecture.md)
- [Agents and execution](agents-and-execution.md)
