<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/arestrix-logo-reverse.svg">
    <img src="docs/assets/arestrix-logo.svg" alt="Arestrix" width="420">
  </picture>
</p>

<p align="center">
  <b>English</b> &nbsp;|&nbsp; <a href="README.pt-BR.md">Português</a>
</p>

<p align="center">
  <img alt="Grafite" src="https://img.shields.io/badge/Grafite-14151A?style=flat-square">
  <img alt="Papel" src="https://img.shields.io/badge/Papel-F4F2EE?style=flat-square">
  <img alt="Brasa" src="https://img.shields.io/badge/Brasa-E8590C?style=flat-square">
  <img alt="Ardósia" src="https://img.shields.io/badge/Ard%C3%B3sia-4B4F5C?style=flat-square">
  <img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-14151A?style=flat-square">
</p>

## Why this exists

I am building this project because I like things that are graphical and simple to use. The problem I want to solve is that any accountant, lawyer, or nurse should be able to create their own swarm of agents, simply and directly, paying only the cost of the API they choose, for the duration of the work they do, and it has to be fast, very fast.

## What it is

**Arestrix Studio** is a visual editor for graphs of AI agents. You draw the pipeline on a canvas, and Arestrix turns the drawing into a **C++20 program built on [Taskflow](https://github.com/taskflow/taskflow)**, compiles it and runs it, with live feedback on every vertex.

- **Draw, don't code.** Vertices are tasks, conditions, sub-pipelines, parallel forks and joins, GPU kernels and autonomous agents. Edges are the order of execution, including feedback loops.
- **Agents that do real work.** Each agent vertex runs a tool-use loop against DeepSeek, OpenAI, Anthropic, Google, a local model, or any OpenAI-compatible server, inside a sandbox you choose.
- **Fast and parallel.** The generated program is native C++20; independent branches run concurrently.
- **Safe by default.** Sandboxes fail closed, file access is confined to the project, and feedback loops have a spending cap.
- **Reusable.** A library of ready-made vertices, your own saved templates, skills, and MCP servers.
- **Windows and Linux**, bilingual interface (English and Portuguese), MIT licensed.

## Quick start

```bash
git clone --recurse-submodules https://github.com/frankalcantara/Arestrix.git
cd Arestrix
npm install
python CppCore/dev.py
```

You need Git, Node.js 18+, Python 3.10+, CMake 3.20+ and a C++20 compiler. Details in
[Getting started](docs/en/02-getting-started.md). Then build a graph and press **F5**.

## Documentation

| | |
| :--- | :--- |
| [Architecture](docs/en/01-architecture.md) | Layers, backend modules, Studio / Workspace / Project, from graph to program. |
| [Getting started](docs/en/02-getting-started.md) | Requirements, build, API keys, first run. |
| [Graph and nodes](docs/en/03-graph-and-nodes.md) | Vertex types, solid and feedback edges, validation, context menu, node library. |
| [Agents and execution](docs/en/04-agents-and-execution.md) | Build & Run, tool use, sandboxes, safety, telemetry, batch and resume. |
| [Workspaces, memory, skills and MCP](docs/en/05-workspaces-memory-skills-mcp.md) | Projects, memory, skills, governance, MCP, importing existing work. |
| [Interface tour](docs/en/06-interface.md) | Ribbon, Backstage, canvas, inspector, modals, shortcuts. |
| [Development](docs/en/07-development.md) | Tests, adding a language, third-party policy, conventions. |
| [Brand](docs/en/08-brand.md) | Icon, colors, typography. |
| [Examples](examples/README.md) | Three ready-to-run projects, from the tutorial to a multi-agent support triage with MCP, and how secrets stay out of Git. |

A complete worked example: [`TUTORIAL_AGENTE_SVG_ARESTRIX.md`](TUTORIAL_AGENTE_SVG_ARESTRIX.md) (in Portuguese).

## License

[MIT](LICENSE). Designed and developed with passion and AI collaboration.

## Acknowledgements

Arestrix stands on the shoulders of outstanding open-source work. Our sincere thanks to the authors and communities behind the projects vendored in [`third_party/`](third_party):

| Project | Role in Arestrix | License |
| :--- | :--- | :--- |
| [**WebUI**](https://github.com/webui-dev/webui) (Hassan Draga) and the [**CivetWeb**](https://github.com/civetweb/civetweb) server it embeds | The lightweight native window that hosts the Studio interface and the RPC bridge between the web UI and the C++ backend, without Electron. | MIT |
| [**Taskflow**](https://github.com/taskflow/taskflow) (Dr. Tsung-Wei Huang and contributors) | The C++20 parallel and heterogeneous task-programming engine that runs every graph. | MIT |
| [**simdjson**](https://github.com/simdjson/simdjson) (Daniel Lemire, Geoff Langdale and contributors) | Parses every JSON document the engine reads, at gigabytes per second. | Apache-2.0 / MIT |
| [**SQLite**](https://sqlite.org) (D. Richard Hipp and contributors) | The embedded database behind the native `arestrix.sqlite` tools. | Public domain |
| [**i18next**](https://github.com/i18next/i18next) | Internationalization behind every translated string. | MIT |
| [**Monaco Editor**](https://github.com/microsoft/monaco-editor) (Microsoft) | The code editor that shows the generated C++. | MIT |
| [**SkillSpector**](https://github.com/NVIDIA/SkillSpector) (NVIDIA) | Security scanner for AI agent skills. | Apache-2.0 |
| [**Inter**](https://github.com/rsms/inter) (The Inter Project Authors) | Typeface of the canvas text. | OFL-1.1 |

Also: [Three.js](https://threejs.org), [Lucide](https://lucide.dev), [troika-three-text](https://github.com/protectwise/troika), [Vite](https://vitejs.dev) and [Vitest](https://vitest.dev).

Each project remains the property of its authors and is distributed under its own license, available in its folder under `third_party/`. The policy for external code is in [Development](docs/en/07-development.md#third-party-code-policy).
