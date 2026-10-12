# Arestrix ⚡

**Arestrix** is a high-performance, next-generation visual node graph orchestrator and IDE built with **Three.js (WebGL)**, **TypeScript**, and a native **C++ Backend (WebUI)** for modeling, visualizing, and executing concurrent, heterogeneous agent pipelines powered by **Taskflow in C++20**.

---

## 💡 What is Arestrix?

Modern concurrent applications—ranging from autonomous multi-agent pipelines to high-throughput data processing and compute engines—demand fine-grained task parallelism, dynamic subflow decomposition, stochastic conditional branching, and strict execution isolation.

While C++20 and the **Taskflow** library provide a world-class parallel runtime, hand-crafting complex task graphs with feedback loops, nested subflows, and sandboxed AI workers in raw code can be tedious and error-prone.

**Arestrix** bridges this gap by providing an intuitive 2.5D visual canvas with instant C++20 code generation, automated MSVC compilation, native sandbox execution drivers, real-time streaming telemetry, and a persistent resume engine.

### Core Architectural Layers

Arestrix follows a decoupled three-tier architecture:

1. **Pure Logical Layer (Frontend `src/core/`)**:
   - In-memory graph data structure (`Graph`) with topological sorting and cycle detection.
   - Subflow hierarchy and nested child graphs.
   - Reversible editing history powered by the **Command Pattern** (`CommandManager`, `ICommand`), with undo/redo support (`Ctrl+Z` / `Ctrl+Y`).
   - Headless C++20 Taskflow code generator (`TaskflowCodegen` and `TaskflowGenerator`).

2. **Visual & Interaction Layer (Frontend `src/view/`)**:
   - Hardware-accelerated Three.js WebGL canvas.
   - Smooth 2.5D orthographic camera with cursor-centered exponential zoom and drag panning (`CameraController`).
   - Procedural infinite background grid rendered with custom GPU shaders (`InfiniteGrid`).
   - Interactive node graph cards, live execution telemetry badges, and hierarchical breadcrumb navigation.
   - Smooth cubic Bézier connections using Three.js Fat Lines (`Line2`).

3. **Compiler & Native Toolchain Bridge (Backend `CppCore/`)**:
   - A lightweight, native C++ application wrapped with **WebUI** that statically serves the Vite-built frontend.
   - First-class support for **MSVC (`cl.exe`)** on Windows as the default C++20 compiler.
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

Ensure the following prerequisites and packages are installed on your Windows environment:

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

3. **Visual Studio 2022 (Community or Build Tools) with MSVC C++20**:
   - Install Visual Studio Community or Visual Studio Build Tools:
     ```powershell
     winget install Microsoft.VisualStudio.2022.Community
     ```
   - **Required Workload**: Select **"Desktop development with C++"** (*Desenvolvimento para Desktop com C++*).
   - **Key Components**: MSVC v143 (x64/x86 C++20 build tools `/std:c++20`), Windows 10/11 SDK, and `nmake.exe`.

4. **Optional Enhancements**:
   - **Docker Desktop / Podman**: If you plan to isolate agent tasks inside container sandboxes (Phase 9/11).
   - **Graphviz (`dot`)**: If you wish to export standalone SVG architecture diagrams:
     ```powershell
     winget install Graphviz.Graphviz
     ```

---

### 🔧 First-Time Installation

Clone the repository and install the frontend dependencies:

```powershell
npm install
```

---

### 🏃 Execution Modes

#### Mode 1: Full Standalone Desktop Application (Native C++ + WebUI) — *Recommended*

This mode compiles the C++ native backend (`arestrix.exe`), starts a dedicated desktop window, and provides full access to native MSVC compilation, sandboxes, streaming telemetry, and MCP server transports.

To build everything automatically and launch:

```powershell
cd CppCore
python dev.py
```

**What `python dev.py` executes automatically under the hood:**
1. **Frontend Compilation**: Runs `npm run build` at the root folder, compiling TypeScript, Three.js, and CSS into a single bundled `dist/index.html` (via `vite-plugin-singlefile`).
2. **WebUI Static Library**: Verifies and extracts the pre-built `webui-2-static.lib` in `vendor/webui` if missing.
3. **MSVC Environment Auto-Detection**: Uses `vswhere.exe` to automatically locate Visual Studio, activates the `vcvars64.bat` 64-bit toolchain, and invokes `nmake -f Makefile`.
4. **C++ Compilation**: Compiles all engine sources into `arestrix.exe` (and synchronizes it both in `CppCore/` and at the project root).
5. **Desktop Studio Launch**: Starts `arestrix.exe`, opening the standalone Arestrix Studio desktop window with bidirectional RPC bindings.

💡 **Instant Quick Launch (When Already Built):**  
If `arestrix.exe` and `dist/index.html` are already compiled, you can launch the app directly from either the project root or `CppCore`:
```powershell
# From the project root:
.\arestrix.exe

# Or from CppCore:
cd CppCore
.\arestrix.exe
```
*(Or simply double-click `arestrix.exe` directly in Windows Explorer).*

---

#### Mode 2: Web Studio Development Mode (Vite Hot-Module Replacement)

If you are editing the Three.js 2.5D canvas, shaders, UI modais, or styling and want instantaneous Hot-Module Replacement (HMR) inside your browser:

```powershell
cd c:\Projetos\NodeForge
npm run dev
```

- Opens the Vite development server at `http://localhost:5173/`.
- **Supported in Web Mode**: Visual DAG editing, 2.5D camera pan/zoom, subflow breadcrumbs, C++ Taskflow code preview, and JSON graph export/import.
- *Note:* Native Windows OS subprocess execution, direct MSVC compilation (`cl.exe`), and live stdio MCP process spawning require the native C++ backend (Mode 1).

---

#### Mode 3: Automated Unit Test Suite (Vitest)

To run the complete test suite verifying DAG validation, reversible editing history, agent settings inheritance, live streaming telemetry, batch resume engine, memory architecture, and MCP protocol integration:

```powershell
cd c:\Projetos\NodeForge
npm test
```

**Test Coverage Summary (59/59 tests passing in 12 test suites):**
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

---

#### Mode 4: Standalone Frontend Build

To compile only the frontend assets into production distribution files:

```powershell
npm run build
```
- Compiles TypeScript and packages all CSS and assets into a singlefile inlined `dist/index.html` consumed in-memory by `arestrix.exe`.

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

### 📁 Conceptual Architecture: Workspace vs. Project

Arestrix strictly delineates between the **Workspace** (physical infrastructure container & OS isolation) and the **Project** (logical business entity & governance).

| Dimension | Workspace (Physical Container & Environment) | Project (Logical Governance & Purpose) |
| :--- | :--- | :--- |
| **Core Concept** | The physical filesystem sandbox, OS isolation boundary, and local storage runtime. | The purposeful business unit, DAG orchestration workflow, and identity. |
| **Manifest / Core File** | Physical directory (`rootPath`) housing `pipelines/`, `storage/`, and `output/`. | `arestrix.project.json` (`projectId`, `name`, `version`, `description`). |
| **Persistence & Storage** | Local SQLite database (`storage/database.sqlite`), checkpoint state files. | Pipeline schemas (`pipelines/main.json`), child subflow definitions. |
| **Execution Boundary** | Sandbox isolation level (`container`, `microvm`, `local`), CPU/RAM constraints (e.g., 512MB), directory mounts, local `.env` variables. | Model routing (Claude, DeepSeek, Gemini), master prompt templates, Code Mode tool permissions (`write`, `edit`, `read`, `bash`). |
| **Cognitive Memory Scope** | Local cache, physical vector index stores, and thread-safe lock mechanisms. | Scoped Blackboard and Semantic Lessons isolated strictly per `projectId` to prevent cross-contamination. |
| **Analogy** | The **Workshop / IDE Workspace** (filesystem root, tools, terminals, environment). | The **Product / Solution** being engineered inside the workshop. |

#### Architectural Isolation Model
1. **Physical Decoupling**: Each workspace encapsulates its own filesystem sandbox (`rootPath`), database, and execution environment. Workspaces can reside anywhere across local and mapped drives.
2. **Logical Governance**: Projects are first-class citizens identified by deterministic hashes (`hashProjectName`). When switching projects, Arestrix seamlessly rebinds the memory namespaces (clearing Blackboard sessions and scoping semantic retrieval) so distinct projects never leak context or credentials.
3. **Multi-Pipeline Composition**: A single project workspace can host multiple pipelines (`pipelines/main.json`, specialized subflows, and batch evaluation DAGs), all sharing the workspace's environment configuration while maintaining independent execution graphs.


3. **External AI Assistant & Spreadsheet Ingestion Engine (Phases 15 to 20)**:
   - **Pi Agent Real Project & CLI Ingestion (Fase Concluída ✅ 🎯)**:
     - 1-Clique para importar projetos locais do Pi CLI com varredura dinâmica de qualquer repositório.
     - Scanner C++ nativo e TS que inspeciona `.pi/settings.json`, habilidades em `.agents/*/SKILL.md`, `plano.md` e checkpoints em `pipeline-state.json`.
     - Síntese visual imediata de pipeline Taskflow C++20: Agentes autônomos, ferramentas MCP e validação física de artefatos.
   - **Claude (Anthropic)**: XML structured prompt and artifact conversion.
   - **OpenAI (ChatGPT / Assistants)**: Export threads and tools mapping.
   - **Hermes (Nous Research)**: Scratchpad logic for local offline microVMs.
   - **Excel Modernization Engine**: Deconstruction of complex multi-sheet spreadsheets into relational SQLite databases and concurrent 4-agent DAGs.

---

## 📜 License

MIT License. Designed and developed with passion and AI collaboration.
