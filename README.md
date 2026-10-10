# Arestrix ⚡

**Arestrix** is a high-performance, next-generation visual node graph orchestrator and IDE built with **Three.js (WebGL)**, **TypeScript**, and a native **C++ Backend (WebUI)** for modeling, visualizing, and executing concurrent, heterogeneous agent pipelines powered by **Taskflow in C++20**.

---

## 💡 What is Arestrix?

Modern concurrent applications—ranging from autonomous multi-agent pipelines to high-throughput data processing and compute engines—demand fine-grained task parallelism, dynamic subflow decomposition, stochastic conditional branching, and strict execution isolation.

While C++20 and the **Taskflow** library provide a world-class parallel runtime, hand-crafting complex task graphs with feedback loops, nested subflows, and sandboxed AI workers in raw code can be tedious and error-prone.

**Arestrix** bridges this gap by providing an intuitive 2.5D visual canvas with instant C++20 code generation, automated MSVC/GCC compilation, native sandbox execution drivers, real-time streaming telemetry, and a persistent resume engine.

### Core Architectural Layers

Arestrix follows a decoupled three-tier architecture:

1. **Pure Logical Layer (Frontend `src/core/`)**:
   - In-memory graph data structure (`Graph`) with topological sorting and cycle detection.
   - Subflow hierarchy and nested child graphs.
   - Reversible editing history powered by the **Command Pattern** (`CommandManager`, `ICommand`), with undo/redo support (`Ctrl+Z` / `Ctrl+Y`).
   - Headless C++20 Taskflow code generator (`TaskflowGenerator`, in `CppCore/src/compiler/`).

2. **Visual & Interaction Layer (Frontend `src/view/`)**:
   - Hardware-accelerated Three.js WebGL canvas.
   - Smooth 2.5D orthographic camera with cursor-centered exponential zoom and drag panning (`CameraController`).
   - Procedural infinite background grid rendered with custom GPU shaders (`InfiniteGrid`).
   - Interactive node graph cards, live execution telemetry badges, and hierarchical breadcrumb navigation.
   - Smooth cubic Bézier connections using Three.js Fat Lines (`Line2`).

3. **Compiler & Native Toolchain Bridge (Backend `CppCore/`)**:
   - A lightweight, native C++ application wrapped with **WebUI** that statically serves the Vite-built frontend.
   - Cross-platform: **MSVC (`cl.exe`)** on Windows and **GCC (`g++`)** on Linux (Ubuntu). The backend is written in **C++23** and stays compatible with **C++20** (see [Language Standard](#-language-standard-c23--c20)); the generated Taskflow pipelines target C++20.
   - Integrated execution drivers for host processes, Docker containers, and MicroVMs.
   - Thread-safe streaming telemetry emitter and checkpoint persistence engine.

---

## 🤖 Autonomous Agents & Code Mode Orchestration

Arestrix features first-class support for autonomous AI agents based on the **Code Mode** paradigm, allowing each node in the DAG to act as an isolated worker with fine-grained permissions:

### 1. Isolated Sandbox & MicroVM Drivers
Each agent node can be configured to execute within its own secure environment:
- **Docker Containers**: Ephemeral execution containers with read-only (`RO`) system mounts, dedicated read-write (`RW`) working directories, strict RAM limits, and configurable network policies (`offline`, `egress_llm_only`, or `full`).
- **MicroVMs**: WSL2 / MicroVM lightweight virtualization for strict kernel-level isolation.
- **Host Process**: Native OS subprocess execution supervised with Win32 job controls, stdout/stderr stream piping, and timeout watchdogs.

### 2. Multi-Provider Model Routing & Per-Node Modals
Floating configuration modals directly on each canvas node allow tailoring execution parameters:
- **Model & Skill Configuration (🧠)**: Route tasks across diverse providers (Anthropic Claude Opus/Sonnet, DeepSeek v4-pro/Flash, Google Gemini, Jev visual engine, or local models), customize prompt templates, and bind specialized `SKILL.md` skill files.
- **Granular Code Mode Tools (🔧)**: Restrict permissions to specific tools (`write`, `edit`, `read`, `bash`, `validate_svg`) and authorized validation scripts.
- **Global Pipeline Defaults (⚙️)**: Configure workspace-wide defaults from the toolbar with optional cascade inheritance on every node (`[x] Herdar Padrão Global`).

### 3. Real-Time Streaming Telemetry
Native C++ streaming integration (`AgentTelemetryEmitter`) delivers live feedback from active agents:
- **Reasoning Accumulation (`thinking_delta`)**: Real-time token/byte counter and preview inspecting the agent's internal thought process.
- **Tool Execution Monitoring**: Instant notification on tool invocations (`tool_start` / `tool_end`) with arguments and success status.
- **Automated Rate-Limit Handling**: Regex-based detection of HTTP 429 quota exhaustion (`RESOURCE_EXHAUSTED`, `Rate limit reached`) with countdown timers for scheduled retries.
- **Live Canvas Badges**: Nodes visually reflect execution status (`🧠 thinking`, `🔧 tool call`, `⏳ 429 retry`, `✔ completed`).

### 4. Batch Pipelines & Fault-Tolerant Resume Engine
For complex multi-stage pipelines processing large collections of items (e.g. articles, data batches):
- **Atomic Checkpoint Persistence (`PipelineResumeEngine`)**: Tracks execution states per item and per stage in `pipeline-state.json` with atomic file swapping.
- **Physical Artifact Validation & Auto-Recovery**: Checks filesystem artifacts (file existence, minimum byte size, and regex content verification such as YAML frontmatter) or log exit codes. Stages with valid outputs are automatically marked as completed (`recoveredFromArtifact: true`), skipping redundant API calls.
- **Serialized Bottleneck Chaining**: Define heavy stages (e.g. redaction) to run sequentially across items to stay within provider limits, while downstream stages (diagram generation, frontmatter, animation) run concurrently.
- **Batch Orchestrator Modal (📦)**: Dedicated interface on the toolbar to set batch items, configure concurrency, toggle serialized stages, and monitor live progress across the checkpoint matrix.

---

## 🚀 How to Run Arestrix

The project is unified under a fast C++ backend using **WebUI**, giving you a native Windows Desktop experience while leveraging the Three.js WebGL canvas.

### 📦 Prerequisites & Toolchain Setup

Arestrix runs on **Windows (MSVC)** and **Linux/Ubuntu (GCC)**. Install the common requirements plus the toolchain for your platform.

1. **Node.js (v18.0+) & npm**:
   - Download from [nodejs.org](https://nodejs.org/) or install via Windows Package Manager:
     ```powershell
     winget install OpenJS.NodeJS.LTS
     ```
   - Installs frontend packages: Three.js, Vite, TypeScript, Vitest, and `vite-plugin-singlefile`.

2. **Python 3 (v3.10+)**:
   - Download from [python.org](https://www.python.org/) or install via winget:
     ```powershell
     winget install Python.Python.3.12
     ```
   - Powers the automated build script (`CppCore/dev.py`) using standard library utilities.

3. **Windows — Visual Studio 2022 (Community or Build Tools) with MSVC C++20/C++23**:
   - Install Visual Studio Community or Visual Studio Build Tools:
     ```powershell
     winget install Microsoft.VisualStudio.2022.Community
     ```
   - **Required Workload**: Select **"Desktop development with C++"** (*Desenvolvimento para Desktop com C++*).
   - **Key Components**: MSVC v143 (x64/x86 C++ build tools), Windows 10/11 SDK, and `nmake.exe`.

   **Linux (Ubuntu 22.04+) / WSL2 — GCC 13+ recommended** (GCC 11/12 work through the `-std=c++2b` alias, handled automatically). See [Building on WSL2 / Ubuntu](#-building-on-wsl2--ubuntu) for the full step-by-step guide:
   ```bash
   sudo apt install build-essential curl unzip git python3 xdg-utils
   # Node.js 18+ is installed separately (see the WSL2 / Ubuntu section)
   ```
   The Linux static WebUI library is vendored in `CppCore/vendor/webui/linux/`; `make -f Makefile.linux setup` re-downloads it if missing. The WebUI window needs a Chromium-based browser (or Firefox) installed.

4. **Optional Enhancements**:
   - **Docker Desktop / Podman**: If you plan to isolate agent tasks inside container sandboxes (Phase 9/11).
   - **Graphviz (`dot`)**: If you wish to export standalone SVG architecture diagrams:
     ```powershell
     winget install Graphviz.Graphviz      # Windows
     ```
     ```bash
     sudo apt install graphviz             # Ubuntu
     ```

---

### 🔧 First-Time Installation

Clone the repository and install the frontend dependencies:

```powershell
npm install
```

---

### 🐧 Building on WSL2 / Ubuntu

Everything needed to compile and run Arestrix on **Windows with WSL2 (Ubuntu 22.04 / 24.04)**. The same steps work on a native Ubuntu machine, skipping step 1.

#### 1. Install WSL2 with Ubuntu (Windows side, once)

In an elevated PowerShell:

```powershell
wsl --install -d Ubuntu-24.04
wsl --set-default-version 2
wsl -l -v          # confirm VERSION = 2
```

Reboot if requested, then open the Ubuntu terminal and create your Linux user.
GUI/browser windows require **WSLg** (included in Windows 11 and recent Windows 10 builds; keep WSL updated with `wsl --update`).

#### 2. Install the system dependencies

| Package | Why it is needed |
| :--- | :--- |
| `build-essential` | `g++`, `make`, libc headers — compiles the C++23 backend (GCC 13+ recommended; GCC 11/12 fall back to `-std=c++2b`) |
| `curl`, `unzip` | `make -f Makefile.linux setup` uses them to fetch the WebUI static library if it is missing |
| `git` | Clone the repository |
| `python3` (3.10+) | Runs the build script `CppCore/dev.py` |
| `xdg-utils`, `wslu` | Let WebUI open the default browser from inside WSL (`xdg-open` / `wslview`) |
| Node.js 18+ and `npm` | Builds the Three.js/TypeScript frontend (Vite, `tsc`, Vitest) |

```bash
sudo apt update
sudo apt install -y build-essential curl unzip git python3 xdg-utils wslu
```

> **Do not use `sudo apt install nodejs npm`** on Ubuntu 22.04: it ships Node 12, which is too old for Vite 5. Install a current LTS with `nvm` instead:
> ```bash
> curl -o- https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.1/install.sh | bash
> source ~/.bashrc
> nvm install --lts
> node -v            # must be v18 or newer
> ```

Optional extras: `graphviz` (SVG architecture export), `docker.io` or Docker Desktop with WSL integration (Docker sandbox driver), and a Linux browser such as Chromium/Firefox if you prefer not to use the Windows browser through `wslview`.

#### 3. Get the code (keep it inside the Linux filesystem)

Clone into your Linux home (e.g. `~/projetos`), **not** under `/mnt/c/...`: compilation and `npm install` are dramatically slower across the Windows mount, and file permissions behave differently.

```bash
mkdir -p ~/projetos && cd ~/projetos
git clone https://github.com/frankalcantara/arestrix.git
cd arestrix
npm install
```

#### 4. Build and run

One command (builds the frontend, compiles the backend, launches the app):

```bash
cd CppCore
python3 dev.py
```

Or step by step:

```bash
npm run build                          # from the repository root -> dist/index.html
cd CppCore
make -f Makefile.linux                 # C++23; produces CppCore/arestrix and ../arestrix
make -f Makefile.linux STD=c++20       # C++20 compatibility build
make -f Makefile.linux info            # show the compiler and -std flag in use
cd .. && ./arestrix                    # launch (must run from the repository root)
```

The first build also downloads `libwebui-2-static.a` into `CppCore/vendor/webui/linux/` when it is absent (`make -f Makefile.linux setup`).

#### 5. Troubleshooting

| Symptom | Fix |
| :--- | :--- |
| `[C++ Aviso] Nao foi possivel abrir um navegador automaticamente` | The server is still running: open the printed `http://localhost:<port>` URL in your Windows browser, or install `wslu xdg-utils`. |
| `'make'/'g++' nao encontrados` | `sudo apt install build-essential` |
| `unrecognized command-line option '-std=c++23'` | Old GCC. The Makefile already falls back to `c++2b`; otherwise install `g++-13` and run `make -f Makefile.linux CXX=g++-13`. |
| `curl`/`unzip: command not found` during `setup` | `sudo apt install curl unzip` |
| `tsc`/`vite` errors or `npm` syntax errors | Node is too old; reinstall via `nvm` (see above) and run `npm install` again. |
| Very slow builds | The repository is under `/mnt/c/...`; move it into `~/`. |
| No window / GUI errors | Run `wsl --update` in PowerShell and `wsl --shutdown`, then reopen Ubuntu (WSLg). |

> The host-process sandbox driver and the stdio MCP transport run natively on Linux/WSL. The native folder picker needs `zenity` or `kdialog` (`sudo apt install zenity`); without them the UI falls back to the browser's directory input.

---

### 🏃 Execution Modes

#### Mode 1: Full Standalone Desktop Application (Native C++ + WebUI) — *Recommended*

This mode compiles the C++ native backend (`arestrix.exe` on Windows, `arestrix` on Linux), starts a dedicated desktop window, and provides full access to native compilation, sandboxes, streaming telemetry, and MCP server transports.

To build everything automatically and launch (same command on both platforms):

```bash
cd CppCore
python dev.py          # Windows (python) / Linux (python3 dev.py)
```

**What `python dev.py` executes automatically under the hood:**
1. **Frontend Compilation**: Runs `npm run build` at the root folder, compiling TypeScript, Three.js, and CSS into a single bundled `dist/index.html` (via `vite-plugin-singlefile`).
2. **WebUI Static Library**: On Windows, verifies/downloads the pre-built `webui-2-static.lib` in `vendor/webui`; on Linux, uses the vendored `libwebui-2-static.a`.
3. **Toolchain**:
   - *Windows*: uses `vswhere.exe` to locate Visual Studio, activates the `vcvars64.bat` 64-bit toolchain, and invokes `nmake -f Makefile`.
   - *Linux*: invokes `make -f Makefile.linux` with `g++`.
4. **C++ Compilation**: Compiles all engine sources into the executable (and synchronizes it both in `CppCore/` and at the project root).
5. **Desktop Studio Launch**: Starts `arestrix`, opening the standalone Arestrix Studio window with bidirectional RPC bindings.

You can also build directly, without Python:

```powershell
# Windows (Developer Command Prompt for VS)
cd CppCore
nmake -f Makefile                    # C++23 (/std:c++latest)
nmake -f Makefile STD=/std:c++20     # C++20 compatibility build
```
```bash
# Linux / Ubuntu
cd CppCore
make -f Makefile.linux               # C++23 (-std=c++23)
make -f Makefile.linux STD=c++20     # C++20 compatibility build
make -f Makefile.linux run           # build + launch
```

> Environment overrides for `dev.py`: `ARESTRIX_STD` (Linux, e.g. `c++20`) and `ARESTRIX_STD_MSVC` (Windows, e.g. `/std:c++20`).

💡 **Instant Quick Launch (When Already Built):**  
If the executable and `dist/index.html` are already compiled, you can launch the app directly from either the project root or `CppCore`:
```bash
# Windows (PowerShell)              # Linux
.\arestrix.exe                      ./arestrix
```
*(Or simply double-click `arestrix.exe` directly in Windows Explorer).*

> **Linux status:** the build, the WebUI window, the host-process sandbox driver, the stdio MCP transport and the folder picker (`zenity`/`kdialog`) are supported. Windows with MSVC remains the primary target platform.
>
> **Working directory:** Arestrix starts every folder browser (Pi import, subdirectory listing, known Pi projects) in the folder where it is running; there are no hard-coded paths.

---

#### Mode 2: Web Studio Development Mode (Vite Hot-Module Replacement)

If you are editing the Three.js 2.5D canvas, shaders, UI modais, or styling and want instantaneous Hot-Module Replacement (HMR) inside your browser:

```powershell
npm run dev
```

- Opens the Vite development server at `http://localhost:5173/`.
- **Supported in Web Mode**: Visual DAG editing, 2.5D camera pan/zoom, subflow breadcrumbs, C++ Taskflow code preview, and JSON graph export/import.
- *Note:* Native Windows OS subprocess execution, direct MSVC compilation (`cl.exe`), and live stdio MCP process spawning require the native C++ backend (Mode 1).

---

#### Mode 3: Automated Unit Test Suite (Vitest)

To run the complete test suite verifying DAG validation, reversible editing history, agent settings inheritance, live streaming telemetry, batch resume engine, memory architecture, and MCP protocol integration:

```powershell
npm test
```

**Test Coverage Summary (16 test suites, 77 tests, all passing):**
- `tests/graph-validation.spec.ts`: Topological sorting, DFS cycle checks, feedback loops, conditional branches.
- `tests/history-undo-redo.spec.ts`: Command Pattern undo/redo stack (`Ctrl+Z` / `Ctrl+Y`) and parameter mutations.
- `tests/subflow-hierarchy.spec.ts`: Nested subflow drill-down, parent graph isolation, and breadcrumb navigation.
- `tests/agent-templates.spec.ts`: Template catalogue, dynamic parameter binding, and custom lambda generation.
- `tests/agent-settings-inheritance.spec.ts`: Pipeline global defaults cascading, per-node overrides, and tool configuration.
- `tests/agent-telemetry.spec.ts`: Real-time streaming thinking accumulation, tool execution events, and 429 rate-limit backoff.
- `tests/batch-resume-engine.spec.ts`: Atomic checkpoint state files, physical artifact filesystem validation, and auto-recovery.
- `tests/agent-memory-config.spec.ts`: Heterogeneous memory architecture, opt-in rules (default disabled), ephemeral RAM vs. persistent disk, and project-level scoping.
- `tests/mcp-protocol.spec.ts`: Model Context Protocol server configuration, stdio/sse transports, tool assignment, and undo/redo support.
- `tests/project-management.spec.ts`: Multi-project workspace isolation, manifest schema validation, MRU history, and memory scope separation.
- `tests/office-ribbon-interface.spec.ts`: Microsoft Office Fluent Ribbon interface, Backstage View, QAT shortcuts, and connector ingestion (Pi, Claude, OpenAI, Hermes, Excel).
- `tests/pi-project-importer.spec.ts`: Scanner nativo de projetos Pi, importação dinâmica de agentes, habilidades e orquestração Taskflow C++20.
- `tests/generic-pi-import.spec.ts`: Importação genérica de projetos Pi e Modelo Padrão de Inteligência.
- `tests/pi-modal-tabs.spec.ts`: Navegação em abas do modal de importação Pi.
- `tests/skills-and-concurrency.spec.ts`: Topologia concorrente (fork/join) e ingestão de skills.
- `tests/office-workspace-interface.spec.ts`: Design system Office/Visio do workspace.

---

#### Mode 4: Standalone Frontend Build

To compile only the frontend assets into production distribution files:

```powershell
npm run build
```
- Compiles TypeScript and packages all CSS and assets into a singlefile inlined `dist/index.html` consumed by the native Arestrix executable.

---

## 🔧 Language Standard: C++23 / C++20

| Component | Standard | Notes |
| :--- | :--- | :--- |
| **Arestrix backend** (`CppCore/`) | **C++23** by default, **C++20 compatible** | Code must compile under both. Do not rely on C++23-only features without a fallback. |
| **Generated pipelines** (`Output/`) | **C++20** | Taskflow requires C++20; generated `main.cpp`, CMake, Makefile and MSVC scripts target C++20. |

Select the standard at build time: `STD=c++20` (GCC/`Makefile.linux`) or `STD=/std:c++20` (MSVC/`Makefile`). MSVC has no stable `/std:c++23` yet, so the default is `/std:c++latest` (`/std:c++23preview` on VS 2022 17.13+).

---

## 🧩 Taskflow Node Mapping

Arestrix maps visual graph nodes directly to native **Taskflow** primitives:

| Node Type | Color / Badge | Taskflow Semantic | Purpose in Concurrent Pipeline |
| :--- | :--- | :--- | :--- |
| **Static Task** | Blue (`#3B82F6`) | `tf::Task (taskflow.emplace([](){}))` | Deterministic compute tasks and background workers. |
| **Condition Task** | Orange (`#FF7828`) | `tf::Task (returns int branch)` | Stochastic decisions, convergence checks, and feedback loops. |
| **Subflow Task** | Purple (`#8B5CF6`) | `tf::Task (accepts tf::Subflow&)` | Dynamic graph decomposition at runtime with child subgraphs. |
| **Module Task** | Green (`#10B981`) | `tf::Task (tf.composed_of(...))` | Reusable, modular task subgraphs. |
| **Critical Section**| Cyan (`#06B6D4`) | `tf::CriticalSection (acquire/release)` | Mutex-guarded atomic sections across concurrent threads. |
| **Agent Task** | Indigo (`#6366F1`) | `tf::Task (Sandbox / MicroVM)` | Autonomous Code Mode agent with isolated VM, multi-provider model routing, and artifact validation. |
| **Fork / Join** | — | `tf::Task` (dispatcher / barrier) | Concurrent dispatch and synchronization barrier between parallel branches (the generator emits a dispatcher / barrier task that reports the real number of parallel branches; ordering comes from `precede()` edges). |

---

## 🏛️ Modern Architecture: Office Ribbon, Workspaces & Ingestion

Arestrix includes enterprise-grade desktop UX and model ingestion capabilities:

1. **Microsoft Office Ribbon UI Standard (Fase Concluída ✅)**:
   - Modern Ribbon bar with functional tabs (*Arquivo / Backstage*, *Página Inicial*, *Inserir*, *Agentes & Tools*, *Conectores & Importação*, *Dados & Memória*, *Execução & Lote*).
   - Chunked tool groups with bottom labels, Quick Access Toolbar (Save, Undo, Redo, Run F5), and clean soft-neomorphic badges strictly respecting the zero text-shadow rule.
   - Quick search / Tell-Me box (*"💡 O que você deseja fazer?"*) and Ribbon auto-collapse shortcut (`Ctrl+F1`).

2. **Project Governance & Workspace Encapsulation (Fase Concluída ✅)**:
   - First-class Project entities with native C++20 backend management (`arestrix.project.json`).
   - Strict isolation of memory namespaces (Blackboard / Semantic Lessons), sandboxes, working directories, and pipelines per workspace.
   - Recent projects MRU management and 3-tab Project Manager Hub.

3. **External AI Assistant & Spreadsheet Ingestion Engine (Phases 15 to 20)**:
   - **Pi Agent Real Project & CLI Ingestion (Fase Concluída ✅ 🎯)**:
     - 1-Clique para importar projetos locais do Pi CLI com varredura dinâmica de qualquer repositório.
     - Scanner C++ nativo e TS que inspeciona `.pi/settings.json`, habilidades em `.agents/*/SKILL.md`, `plano.md` e checkpoints em `pipeline-state.json`.
     - Síntese visual imediata de pipeline Taskflow C++20: Agentes autônomos, ferramentas MCP e validação física de artefatos.
   - **Claude (Anthropic)**: XML structured prompt and artifact conversion.
   - **OpenAI (ChatGPT / Assistants)**: Export threads and tools mapping.
   - **Hermes (Nous Research)**: Scratchpad logic for local offline microVMs.
   - **Excel Modernization Engine** *(em desenvolvimento 🚧)*: the ribbon/modal entry point exists, but today it only synthesizes a fixed 4-agent demo graph. Real `.xlsx` parsing (sheets/ranges as graph nodes) is the next planned feature.

---

## 📜 License

MIT License. Designed and developed with passion and AI collaboration.
