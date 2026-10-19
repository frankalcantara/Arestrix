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
  <img alt="Grafite" src="https://img.shields.io/badge/Grafite-14151A?style=flat-square">
  <img alt="Papel" src="https://img.shields.io/badge/Papel-F4F2EE?style=flat-square">
  <img alt="Brasa" src="https://img.shields.io/badge/Brasa-E8590C?style=flat-square">
  <img alt="Ardósia" src="https://img.shields.io/badge/Ard%C3%B3sia-4B4F5C?style=flat-square">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=c%2B%2B">
  <img alt="Taskflow" src="https://img.shields.io/badge/Taskflow-Parallel_DAG-blue?style=flat-square">
  <img alt="License: MIT" src="https://img.shields.io/badge/license-MIT-14151A?style=flat-square">
</p>

<p align="center">
  <b>Enterprise Visual Orchestration and Native C++20 Compiler for Intelligent AI Agent Swarms</b><br>
  <i>From interactive graph to high-performance standalone C++20 binaries with end-to-end security and zero runtime dependencies.</i>
</p>
<p align="center">
  <b>Version: Beta 1.0</b> &nbsp;·&nbsp; <a href="CHANGELOG.md">Changelog</a>
</p>
<p align="center">
  <img src="docs/assets/screenshots/studio-main.png" alt="Arestrix Studio - Main Interface" width="960">
</p>

---

## Table of Contents

- [Overview & Enterprise Positioning](#overview--enterprise-positioning)
- [Competitive Comparison: Arestrix vs. Alternatives](#competitive-comparison-arestrix-vs-alternatives)
- [Enterprise Security & Governance Pillars](#enterprise-security--governance-pillars)
- [High-Resolution Feature Showcase](#high-resolution-feature-showcase)
- [Architecture: From Canvas to C++20 Taskflow](#architecture-from-canvas-to-c20-taskflow)
- [Quick Start](#quick-start)
- [Engineered in Collaboration with Intelligent AI Agents](#engineered-in-collaboration-with-intelligent-ai-agents)
- [Code Integrity & Test Audit Log](TEST_REPORT.md)
- [Complete Documentation](#complete-documentation)
- [Acknowledgements](#acknowledgements)
- [License](#license)

---

## Overview & Enterprise Positioning

Enterprises of all sizes face a critical obstacle when moving autonomous AI agent workflows into production: **the chasm between prototype scripts and production-grade enterprise software**.

Most modern agent frameworks impose severe runtime liabilities: heavy Node.js or Python interpreter overhead, fragile virtual environments (`venv`/`conda`), unencrypted plaintext `.env` secrets, or vendor lock-in with multi-tenant SaaS clouds that compromise data sovereignty.

**Arestrix** was architected to bridge this gap:
1. **Visual Accessibility, Native Speed**: Anyone — software engineers, financial analysts, legal counsel, or operations specialists — can design complex agent swarms on an intuitive canvas inspired by Microsoft Office 365.
2. **C++20 Compilation with Taskflow**: The visual graph is not interpreted. It compiles into clean, modern C++20 source code powered by the heterogeneous parallel programming engine [Taskflow](https://github.com/taskflow/taskflow).
3. **Frictionless Standalone Distribution**: 1-click export into self-contained binaries (`.exe`), WiX MSI installers, and MSIX packages fully certifiable for **Microsoft Intune and SCCM** enterprise deployment — without requiring Node, Python, or Docker on the end-user machine.
4. **Fail-Closed Security & Governance**: Seamless integration with Windows Credential Vault (DPAPI) and Azure Key Vault (via Managed Identity/IMDS), tamper-evident SHA-256 HMAC digital signatures, and strict sandbox confinement.

---

## Competitive Comparison: Arestrix vs. Alternatives

The table below contrasts the architectural, operational, and governance differences between **Arestrix**, server-based automation platforms like **n8n**, and script-based agent wrappers such as **Hermes Agent** and **Pi Agent**:

| Strategic Dimension | Arestrix | n8n | Hermes Agent (Nous) | Pi Agent (Inflection) |
| :--- | :--- | :--- | :--- | :--- |
| **Execution Paradigm** | **Native C++20 Machine Code** via Taskflow parallel task DAGs. | Interpreted **Node.js** with asynchronous JavaScript event loops. | Interpreted **Python** script constrained by package dependencies & GIL. | Proprietary **Closed Cloud SaaS** running on remote vendor servers. |
| **End-User Distribution** | **1-Click Standalone (`.exe`), MSI & MSIX** with zero runtime prerequisites. | Requires dedicated Linux server, Docker, or Cloud SaaS subscription. | Requires terminal, Python 3.10+, `pip`, and virtual environment setup. | Web/Mobile application only; completely unexportable. |
| **Memory & CPU Footprint** | **Minimal (< 30 MB)**; native multi-core task scheduling. | **High (> 400 MB)** per Node.js worker process. | **Medium/High** due to Python interpreter and ML dependencies. | Unknown (remote compute running on third-party cloud). |
| **Secrets & Key Governance** | **Windows DPAPI & Azure Key Vault**; zero secrets on disk, memory wiping. | Stored in server database or plaintext environment variables. | Plaintext `.env` files or OS environment variables. | Managed on third-party cloud; credentials & data leave perimeter. |
| **Cryptographic Integrity** | **SHA-256 HMAC Digital Signature** verified at boot (fail-closed). | Not applicable to distributed workflows. | No package integrity or tamper-evident checks. | Not applicable (proprietary hosted SaaS). |
| **Visual Graph Orchestration** | **Yes**: Interactive canvas, Office 365 Ribbon, feedback cycles & spending caps. | **Yes**: Visual workflow canvas primarily designed for ETL/Webhooks. | **No**: Terminal CLI interface only. | **No**: Standard single-prompt chat interface. |
| **Enterprise IT Packaging** | **Silent MSI/MSIX** (`msiexec /qn`) certified for **Intune & SCCM**. | Requires container orchestration or web server infrastructure. | Not viable for locked-down corporate endpoint deployment. | Incompatible with on-premises offline security baselines. |
| **Model Context Protocol (MCP)** | **Native**: Full MCP client support and extensible tool registry. | Partial via community nodes or custom HTTP requests. | Focused on bespoke LLM tool calls. | Closed ecosystem with no arbitrary tool integration. |
| **Licensing & Cost** | **Open Source (MIT)**; pay only for your chosen model API usage. | Fair-Code / Commercial with usage limits on advanced workflows. | Open Source (Apache 2.0). | Commercial subscription / closed proprietary platform. |

---

## Enterprise Security & Governance Pillars

Arestrix adopts a defense-in-depth security architecture designed to meet rigorous Enterprise Information Security and compliance audits:

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    Arestrix Enterprise Security Model                   │
├──────────────────────────┬──────────────────────────┬───────────────────┤
│    1. Zero-Secret Core   │ 2. Cryptographic Trust   │ 3. IT Deployment  │
│  • DPAPI / Azure Vault   │  • SHA-256 HMAC Sign     │  • MSI & MSIX     │
│  • IMDS Managed Identity │  • Tamper-evident check  │  • Silent Intune  │
│  • Secure memory purge   │  • Fail-closed policies  │  • Audit logging  │
└──────────────────────────┴──────────────────────────┴───────────────────┘
```

1. **Zero-Secret Architecture**: No passwords, API keys, or access tokens are ever baked into exported executables, source files, or configuration trees. Secrets are resolved at runtime via the **Windows Credential Manager (DPAPI)** or **Azure Key Vault** (leveraging transparent **Azure Managed Identity / IMDS** in corporate clouds). Upon task completion, sensitive buffers are actively scrubbed (`SecureZeroMemory`).
2. **Cryptographic Package Verification**: Every packaged pipeline exports a cryptographic integrity manifest (`arestrix.manifest.sig`). On startup, the runner verifies the SHA-256 HMAC digital signature; if any configuration, prompt, or binary artifact was modified out-of-band, the process terminates immediately (*fail-closed*).
3. **Enterprise Silent Deployment for Intune & SCCM**: With built-in WiX Toolset MSI and MSIX packagers, corporate IT administrators can push silent unattended updates (`msiexec /i arestrix-runner.msi /qn`) aligned with enterprise group policies.
4. **Sandbox Confinement & Cost Caps**: Tool-assisted file operations are strictly confined within the project directory workspace. Autonomous feedback loops and self-repairing agent vertices feature strict token budgets and iteration caps to prevent runaway API expenditures.

---

## High-Resolution Feature Showcase

### 1. Studio Canvas & Agent Swarm Workflow
Clean, responsive interface inspired by Microsoft Office 365 showcasing an active multi-agent pipeline in **real-time concurrent execution (F5)**: completed and active vertices, live thinking telemetry badges, docked MSVC C++20 Taskflow build terminal, and agent properties inspector.

<p align="center">
  <img src="docs/assets/screenshots/studio-main.png" alt="Arestrix Studio Main Canvas" width="960">
</p>

### 2. Native C++20 Taskflow Generation & Compilation
Inspect generated, production-ready C++20 Taskflow source code in the embedded Monaco Editor. Parallel branches automatically map into concurrent task DAGs without thread safety hazards.

<p align="center">
  <img src="docs/assets/screenshots/cpp20-taskflow-generator.png" alt="C++20 Taskflow Code Generator in Monaco Editor" width="960">
</p>

### 3. Standalone Runner: Execution & Timeline
End-user execution screen providing a real-time progress timeline, node execution indicators, and detailed iteration history for non-technical operators.

<p align="center">
  <img src="docs/assets/screenshots/standalone-runner.png" alt="Standalone Runner - Progress Tab" width="960">
</p>

### 4. Real-Time Telemetry & Diagnostics
Monitor execution KPIs: node latencies, token throughput per second, live model reasoning and thinking streams, and tool argument inspections.

<p align="center">
  <img src="docs/assets/screenshots/runner-diagnostics.png" alt="Standalone Runner - Diagnostics and Telemetry" width="960">
</p>

### 5. Corporate Governance & Enterprise Export
Configure enterprise credential vaults (Windows DPAPI / Azure Key Vault), generate tamper-evident HMAC signatures, build WiX MSI / MSIX packages for Intune/SCCM, and produce production standalone binaries.

<p align="center">
  <img src="docs/assets/screenshots/runner-export-governance.png" alt="Standalone Runner - Export and Governance" width="960">
</p>

---

## Architecture: From Canvas to C++20 Taskflow

Arestrix utilizes a lightweight, multi-tiered architecture that bypasses Electron's memory overhead while maintaining high fidelity:

```
┌────────────────────────────────────────────────────────┐
│  Arestrix Studio UI (TypeScript + Three.js + Monaco)    │
└───────────────────────────┬────────────────────────────┘
                            │ WebUI IPC / Bidirectional RPC
┌───────────────────────────▼────────────────────────────┐
│  CppCore Backend (C++20, CivetWeb, simdjson, SQLite)   │
├────────────────────────────────────────────────────────┤
│  Taskflow Engine (Heterogeneous C++20 Task DAGs)       │
├────────────────────────────────────────────────────────┤
│  Agent Tool Loop (OpenAI, Anthropic, DeepSeek, MCP)    │
├────────────────────────────────────────────────────────┤
│  Enterprise Vaults (Windows DPAPI & Azure Key Vault)   │
└────────────────────────────────────────────────────────┘
```

- **Electron-Free Frontend**: Hosted via [WebUI](https://github.com/webui-dev/webui) and an embedded [CivetWeb](https://github.com/civetweb/civetweb) HTTP/WebSocket server, yielding native responsiveness and a sub-30 MB baseline memory footprint.
- **Hardware-Speed Parsing**: All inter-process communication and manifest serialization leverage [simdjson](https://github.com/simdjson/simdjson) for gigabytes-per-second JSON parsing.
- **Embedded Persistence**: Session history, long-term vector/key memories, and audit logs are managed locally with embedded [SQLite](https://sqlite.org).

---

## Quick Start

### Prerequisites
- Git 2.30+
- Node.js 20+
- Python 3.10+
- CMake 3.20+
- C++20 compliant compiler (MSVC v143+ on Windows, GCC 11+ or Clang 14+ on Linux)

### Installation & Launch

```bash
# Clone the repository including all submodules
git clone --recurse-submodules https://github.com/frankalcantara/Arestrix.git
cd Arestrix

# Install web UI and build dependencies
npm install

# Launch the integrated development environment (WebUI + C++ Backend)
python CppCore/dev.py
```

Inside the Studio:
1. Construct your agent graph or load the concurrent multi-agent demo project from the **Run** ribbon tab.
2. Press **F5** to compile and run instantly with live vertex highlights.
3. Click **Export Standalone Runner** to package the pipeline into a standalone binary with cryptographic integrity and MSI/MSIX installers.

---

## Engineered in Collaboration with Intelligent AI Agents

The architecture, implementation, refactoring, and security verification of Arestrix were co-engineered through a continuous partnership between human software engineering and multiple **state-of-the-art intelligent AI agents**.

We gratefully acknowledge the specialized contributions of frontier reasoning and coding models from leading global research labs:

- **Anthropic**: **Claude 3.5 Sonnet** and **Claude 3.7 Sonnet**, instrumental in large-scale codebase refactoring, architectural consistency, and UI precision.
- **OpenAI**: Reasoning models **o1**, **o3-mini**, and **GPT-4o**, leveraged for multi-threaded task decomposition, Taskflow DAG scheduling, and security hardening.
- **Google DeepMind**: **Gemini 2.0 Flash** and **Gemini 2.0 Pro**, central to automated testing, workflow generation, and tool-use orchestration.
- **DeepSeek**: **DeepSeek-R1** and **DeepSeek-V3**, crucial for memory efficiency analysis, low-level data structures, and mathematical validation of graph execution algorithms.

This collaborative engineering process stands as a living testament to what modern agent swarms can create when coupled with a native, high-performance compiler.

---

## Complete Documentation

| Guide | Description |
| :--- | :--- |
| [Architecture](docs/en/01-architecture.md) | Architectural layers, C++20 modules, WebUI lifecycle, from graph to native binary. |
| [Getting Started](docs/en/02-getting-started.md) | System requirements, build process, API key setup, and first execution. |
| [Graph & Nodes](docs/en/03-graph-and-nodes.md) | Vertex types, control edges, feedback loops, graph validation, and node library. |
| [Agents & Execution](docs/en/04-agents-and-execution.md) | Agent tool-use loop, sandboxes, safety boundaries, telemetry, and batch processing. |
| [Workspaces, Memory & MCP](docs/en/05-workspaces-memory-skills-mcp.md) | Persistent agent memory, skill catalogs, and Model Context Protocol (MCP) servers. |
| [Interface Tour](docs/en/06-interface.md) | Office 365 Ribbon, Backstage view, canvas controls, node inspector, and shortcuts. |
| [Development & Testing](docs/en/07-development.md) | Unit tests, security checks, third-party policies, and coding standards. |
| [Brand Guidelines](docs/en/08-brand.md) | Visual identity, color palette, and crisp shadow-free typography standards. |
| [Worked Examples](examples/README.md) | Pre-built pipelines from multi-agent triage with MCP to concurrent workflows. |
| [Test Audit Log](TEST_REPORT.md) | Chronological test execution ledger, category breakdown, and code integrity audits. |

---

## Acknowledgements

Arestrix stands on the shoulders of world-class open-source projects vendored under [`third_party/`](third_party):

| Project | Role in Arestrix | License |
| :--- | :--- | :--- |
| [**WebUI**](https://github.com/webui-dev/webui) | Ultralight native window and bidirectional C++/Web RPC bridge without Electron. | MIT |
| [**Taskflow**](https://github.com/taskflow/taskflow) | C++20 parallel and heterogeneous task-programming DAG engine. | MIT |
| [**simdjson**](https://github.com/simdjson/simdjson) | SIMD-accelerated JSON parser handling gigabytes of data per second. | Apache-2.0 / MIT |
| [**SQLite**](https://sqlite.org) | Embedded database for persistent session storage and `arestrix.sqlite` tools. | Public Domain |
| [**Monaco Editor**](https://github.com/microsoft/monaco-editor) | Code editor with native C++20 syntax highlighting and inspection. | MIT |
| [**SkillSpector**](https://github.com/NVIDIA/SkillSpector) | NVIDIA static security analyzer for AI agent skills. | Apache-2.0 |
| [**i18next**](https://github.com/i18next/i18next) | Seamless internationalization engine for English and Portuguese. | MIT |
| [**Inter**](https://github.com/rsms/inter) | Clean, legible typography for canvas and UI controls. | OFL-1.1 |

Special thanks also to the creators and maintainers of [Three.js](https://threejs.org), [Lucide Icons](https://lucide.dev), [troika-three-text](https://github.com/protectwise/troika), [Vite](https://vitejs.dev), and [Vitest](https://vitest.dev).

---

## License

Released under the [MIT License](LICENSE). Free for commercial and academic use with full code sovereignty.
