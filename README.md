<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/arestrix-logo-reverse.svg">
    <img src="docs/assets/arestrix-logo.svg" alt="Arestrix" width="440">
  </picture>
</p>

<p align="center">
  <b>English</b> &nbsp;|&nbsp; <a href="README.pt-BR.md">Português</a>
</p>

<p align="center">
  <a href="https://github.com/frankalcantara/Arestrix/releases"><img alt="Beta release" src="https://img.shields.io/github/v/release/frankalcantara/Arestrix?include_prereleases&label=beta&style=flat-square"></a>
  <a href="LICENSE"><img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-14151A?style=flat-square"></a>
  <img alt="Platform: Windows and Linux" src="https://img.shields.io/badge/platform-Windows%20%7C%20Linux-4B4F5C?style=flat-square">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=c%2B%2B">
</p>

<p align="center">
  <b>Visual orchestration and native C++20 compilation for AI agent pipelines.</b><br>
  <i>Design the graph, compile it to Taskflow, and ship a standalone runner.</i>
</p>

<p align="center">
  <b>2.4.0</b> &nbsp;·&nbsp; <a href="CHANGELOG.md">Changelog</a> &nbsp;·&nbsp; <a href="docs/README.md">Documentation</a>
</p>

<p align="center">
  <img src="docs/assets/screenshots/studio-main.webp" alt="Arestrix Studio: a multi-agent pipeline running on the canvas" width="960">
</p>

<p align="center">
  <img src="docs/assets/screenshots/properties-hub.webp" alt="Office-style Properties hub for Studio, Workspace and Project" width="470">
  <img src="docs/assets/screenshots/studio-dark.webp" alt="Arestrix Studio in the dark theme with a running multi-agent graph" width="470">
</p>

---

## Try it without building

Download the ready-to-run **Windows x64** build from
[**Releases → latest**](https://github.com/frankalcantara/Arestrix/releases/tag/latest):

- **Installer (recommended):** run `arestrix-<version>-windows-x64-setup.exe`. It installs per user
  in `%LOCALAPPDATA%\Programs\Arestrix` without administrator rights and checks WebView2, the
  Visual C++ Redistributable, Git/GitHub credentials, MSVC Build Tools and provider API keys without
  exposing their values.
- **Portable:** extract the ZIP to any folder and run `start-arestrix.bat`. The extracted folder
  becomes `ARESTRIX_HOME`.

The CLI is installed with the Studio (`arestrix-cli.exe`). Compiling pipelines additionally requires
the MSVC Build Tools ("Desktop development with C++"); the package does not bundle a compiler.
Every build ships `SHA256SUMS`, `SBOM.json` and a per-file `package-manifest.json`. `latest` is a
rolling pre-release built from `main`; version tags publish their own release.

## What is Arestrix?

Arestrix is a desktop studio and native compiler for AI agent workflows. You draw a pipeline on a
graph canvas; Arestrix validates it, generates C++20 source and compiles it with
[Taskflow](https://github.com/taskflow/taskflow) into parallel task DAGs.

- **Visual design, native execution.** The graph is not interpreted. It becomes a compiled program.
- **Local-first by default.** Projects, memory, logs and databases stay on the machine; only model
  and MCP calls leave it.
- **Built for delivery.** Export a standalone runner with optional MSI/MSIX packaging for managed
  Windows deployment.
- **Secrets stay out of the project.** Credentials are resolved at runtime through Windows DPAPI or
  Azure Key Vault / Managed Identity.
- **Reviewable artifacts.** Graphs, generated C++ and packaging metadata are text files that can be
  versioned and reviewed.

## Why Arestrix?

| Concern | How Arestrix addresses it |
| :--- | :--- |
| **Prototype-to-production gap** | The same graph can be executed in the Studio or compiled into a distributable runner. |
| **Runtime footprint** | The Studio avoids Electron and the generated program runs as a native binary. |
| **Credential exposure** | Project files hold variable names or `${NAME}` references, never secret values. |
| **Operational trust** | Exported packages carry an integrity manifest; the runner can verify it before execution. |
| **Vendor lock-in** | MIT-licensed, local project format and support for multiple model providers and MCP servers. |

## Feature highlights

- **Visual graph editor** - Office-inspired ribbon, templates, decision vertices, feedback loops,
  undo/redo and graph validation.
- **Office Properties hub** - Studio, Workspace and Project settings in one Office-style surface,
  with explicit inheritance, origin badges and specialist editors for credentials, Git and memory.
- **Office File menu** - Info, New, Open, Save, Export and Options with Office tables, action cards,
  vector icons and a Backstage that stays behind Properties.
- **Light and dark themes** - Light, dark or follow-system appearance across the ribbon, task pane,
  console, Backstage and all Office dialogs. The canvas remains an independent preference.
- **Native C++20 compiler** — inspect generated Taskflow code in Monaco, compile it with MSVC or
  GCC/Clang, and reuse unchanged builds.
- **Agent runtime** - tool loop, sandboxes, per-vertex model settings, live telemetry and batch
  resume.
- **Local AI models** - managed llama.cpp runtime, pre-approved GGUF catalog, hardware-aware
  recommendation and CPU fallback. No inference runtime is bundled by default.
- **Enterprise AI Gateway** - on-prem proxy with virtual keys, budgets and rate limits by
  key/project/user/provider/model, usage ledger and fail-closed routing. Local models are exempt.
- **Project memory (AMG)** - opt-in SQLite temporal multigraph with per-vertex `off`/`consult`/`learn`
  modes, `memory_search` and pending `memory_propose_lesson` proposals, success/failure episodes,
  evidence-backed promotion, project-declared continuation state (`project`/`task_type`/`item` scopes,
  schema, TTL and CAS), optional hybrid retrieval (EmbeddingGemma 2 or Qwen3-Embedding-4B) with a
  full index fingerprint, deterministic consolidation, validator-gated playbooks, workspace
  promotion, injected/consulted/used tracking, a token-budgeted brief and context A/B in the Studio.
  Telemetry lives in a separate database. See [how memory works](docs/en/how-memory-works.md).
- **Arestrix Library** - audited skill, MCP and template assets with Studio, Workspace and Project
  scopes, project pins, built-in/workspace imports and a native GitHub source with commit pin,
  license check and on-demand downloads.
- **Workspaces, skills and MCP** - local skill catalogs and MCP servers with a persistent session
  per server, including HTTP/Streamable HTTP and per-server headers.
- **Delivery and governance** — standalone runner, MSI/MSIX packaging, DPAPI/Azure Key Vault
  integration and HMAC integrity verification.
- **Bilingual** — interface and documentation in English and Portuguese.

See the [feature showcase](docs/product/showcase.md) for screenshots of each area.

## Quick start

### Requirements

- Git 2.30+
- Node.js 20+
- Python 3.10+
- CMake 3.20+
- A C++20 compiler: MSVC v143+ on Windows, GCC 11+ or Clang 14+ on Linux

### Build and launch

```bash
# Clone with the pinned third-party submodules
git clone --recurse-submodules https://github.com/frankalcantara/Arestrix.git
cd Arestrix

# Install frontend dependencies
npm ci

# Build the frontend and the C++ backend, then open the Studio
python CppCore/dev.py
```

Optional: enable the secret-scanning pre-commit hook once per clone with `npm run hooks:install`.

Inside the Studio:

1. Open one of the [examples](examples/README.md) or create a project.
2. Choose a provider and model in **File > Properties > Project > Model & tools**; API keys are read
   from environment variables or your credential vault.
3. Press **F5** to compile and run the graph with live telemetry.
4. Use **Export Standalone Runner** to package the pipeline for delivery.

For CI, servers or a terminal-only workflow, use the headless runner generated by the same build:

```powershell
.\arestrix-cli.exe run .\examples\04-memoria\pipelines\main.json --root .\examples\04-memoria
```

See [`arestrix-cli`](docs/en/arestrix-cli.md) for `gen`, `env`, `check`, `doctor`, native
`memory report`, `mcp`, `project`, `--ci`, batch mode, exit codes and troubleshooting.

No API key is required to run the scripted end-to-end examples:

```bash
python scripts/e2e_examples.py
```

## Documentation

| Start here | What you will find |
| :--- | :--- |
| [Documentation hub](docs/README.md) | The complete guide, organized by language and topic. |
| [Getting started](docs/en/getting-started.md) | Requirements, build, model configuration and first execution. |
| [Architecture](docs/en/architecture.md) | Studio, engine, WebUI and the graph-to-C++ pipeline. |
| [`arestrix-cli`](docs/en/arestrix-cli.md) | Headless execution, CI usage, exit codes and troubleshooting. |
| [Institution memory](docs/en/how-memory-works.md#12-institution) | `arestrix-memory`: authenticated HTTP API and MCP (stdio and `/mcp`). |
| [Skill validation](docs/quality/skill-reference-review.md) | Arestrix Skill Guard: NVIDIA SkillSpector + Cloudflare security-audit, lock and reference review. |
| [Examples](examples/README.md) | Fourteen runnable projects, from a two-agent pipeline to callable subflows, flow Call vertices, asynchronous and parallel calls, generated forms, project memory, web research and reversible context compression. |
| [Test audit log](docs/quality/test-audit-log.md) | Chronological test rounds and integrity audits. |
| [Release gate](docs/en/release.md) | Reproducible validation, SHA-256 manifest and SBOM before distribution. |
| [Beta 2.3 test plan](docs/quality/beta2.3-test-plan.md) | Registered Beta 2.3 scope and remaining acceptance tests (in Portuguese). |
| [Changelog](CHANGELOG.md) | Release history, changes and known limitations. |

## Project status

**Arestrix 2.4.0** is the current release (2026-10-29). The Studio runs on Windows and Linux;
programs generated from a graph currently target Windows. APIs, file formats and command-line flags
may still change between minor releases. See the [changelog](CHANGELOG.md) for the current
limitations. The Windows installer and code signing are included; clean-machine validation, the
Linux package and the full test matrix are still pending. Track them in the
[test audit log](docs/quality/test-audit-log.md) and the
[Beta 2.3 test plan](docs/quality/beta2.3-test-plan.md). Windows artifacts are signed by
[SignPath Foundation](https://signpath.org) when signing is enabled; see the
[code signing policy](CODE_SIGNING.md).

## Security

- Report vulnerabilities privately according to [SECURITY.md](SECURITY.md).
- Read the [security and governance model](docs/security/governance.md).
- Read the [code signing policy](CODE_SIGNING.md) and [privacy policy](PRIVACY.md).
- Check [dependency advisories](docs/security/dependency-advisories.md) before upgrading npm
  packages.
- Third-party components and licenses are listed in [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Contributing

Contributions are welcome. Read [CONTRIBUTING.md](CONTRIBUTING.md) for setup, tests, commit
conventions and the pull request checklist, and follow the [Code of Conduct](CODE_OF_CONDUCT.md).

- Use [GitHub Discussions](https://github.com/frankalcantara/Arestrix/discussions) for questions and
  ideas.
- Use the issue templates for reproducible bugs and feature requests.
- Do not open a public issue for a security vulnerability.

## Acknowledgements

Arestrix builds on the work of many open-source projects:

- **Runtime and rendering:** [Taskflow](https://github.com/taskflow/taskflow) (task DAG engine),
  [simdjson](https://github.com/simdjson/simdjson) (SIMD JSON), [FastWebUI](https://github.com/frankalcantara/FastWebUI)
  (a fork of [WebUI](https://github.com/webui-dev/webui); native window and web/RPC bridge),
  [SQLite](https://sqlite.org) (embedded persistence) and [cpp-httplib](https://github.com/yhirose/cpp-httplib)
  (gateway HTTP server).
- **Studio:** [Monaco Editor](https://github.com/microsoft/monaco-editor) (code editor),
  [i18next](https://github.com/i18next/i18next) (internationalization), [Inter](https://github.com/rsms/inter)
  (typography), [lucide](https://lucide.dev) (icons), [three.js](https://threejs.org),
  [troika-three-text](https://github.com/protectwise/troika) and
  [@hpcc-js/wasm](https://github.com/hpcc-systems/hpcc-js-wasm) (3D canvas and Graphviz/WASM).
- **AI and compression:** [ONNX Runtime](https://github.com/microsoft/onnxruntime) and
  [DirectML](https://github.com/microsoft/DirectML) power optional prose compression on CPU and GPU;
  [Kompress-v2-base](https://huggingface.co/chopratejas/kompress-v2-base) and
  [ModernBERT-base](https://huggingface.co/answerdotai/ModernBERT-base) provide the compressor and its
  encoder; [llama.cpp / ggml](https://github.com/ggml-org/llama.cpp) is the optional local runtime.
- **Security:** [NVIDIA SkillSpector](https://github.com/NVIDIA/SkillSpector) and
  [Cloudflare security-audit-skill](https://github.com/cloudflare/security-audit-skill) inform the
  Arestrix Skill Guard.

The official Windows installer bundles ONNX Runtime and DirectML. The Kompress weights and the local
runtime are downloaded on demand after the user accepts their licenses; Arestrix does not redistribute
the weights. See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) and
[`third_party/`](third_party/README.md) for versions, roles and licenses.

## Contact

- General, commercial and conduct matters: [contact@arestrix.com](mailto:contact@arestrix.com)
- Security reports: follow [SECURITY.md](SECURITY.md) and use GitHub private reporting first.
- Technical questions: [GitHub Discussions](https://github.com/frankalcantara/Arestrix/discussions)

## License

Released under the [MIT License](LICENSE). Free for commercial and academic use.
