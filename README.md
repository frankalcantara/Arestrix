# Arestrix

<p align="center">
  <b>English</b> &nbsp;|&nbsp; <a href="README.pt-BR.md">Português</a>
</p>

I am building this project because I like things that are graphical and simple to use. The problem I want to solve is that any accountant, lawyer, or nurse should be able to create their own swarm of agents, simply and directly, paying only the cost of the API they choose, for the duration of the work they do, and it has to be fast, very fast.

**Arestrix** is a visual node graph orchestrator and IDE built with **Three.js (WebGL)**, **TypeScript**, and a native **C++ Backend (WebUI)** for modeling, visualizing, and executing concurrent, heterogeneous agent pipelines powered by **Taskflow in C++20**.

---

## What is Arestrix?

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
   - First-class support for **MSVC (`cl.exe`)** on Windows and **g++ / clang++** on Linux as C++20 compilers.
   - Integrated execution drivers for host processes, Docker containers, and MicroVMs.
   - Thread-safe streaming telemetry emitter and checkpoint persistence engine.

---

## Autonomous Agents & Code Mode Orchestration

Arestrix features first-class support for autonomous AI agents based on the **Code Mode** paradigm, allowing each node in the DAG to act as an isolated worker with fine-grained permissions:

### 1. Isolated Sandbox & MicroVM Drivers
Each agent node can be configured to execute within its own secure environment:
- **Docker Containers**: Ephemeral execution containers with read-only (`RO`) system mounts, dedicated read-write (`RW`) working directories, strict RAM limits, and configurable network policies (`offline`, `egress_llm_only`, or `full`).
- **MicroVMs**: WSL2 / MicroVM lightweight virtualization for strict kernel-level isolation.
- **Host Process**: Native OS subprocess execution supervised with Win32 job controls, stdout/stderr stream piping, and timeout watchdogs.

### 2. Multi-Provider Model Routing & Per-Node Modals
Floating configuration modals directly on each canvas node allow tailoring execution parameters:
- **Model & Skill Configuration**: Route tasks across diverse providers (Anthropic Claude Opus/Sonnet, DeepSeek v4-pro/Flash, Google Gemini, Jev visual engine, or local models), customize prompt templates, and bind specialized `SKILL.md` skill files.
- **Granular Code Mode Tools**: Restrict permissions to specific tools (`write`, `edit`, `read`, `bash`, `validate_svg`) and authorized validation scripts.
- **Global Pipeline Defaults**: Configure workspace-wide defaults from the toolbar with optional cascade inheritance on every node (`[x] Herdar Padrão Global`).

### 3. Real-Time Streaming Telemetry
Native C++ streaming integration (`AgentTelemetryEmitter`) delivers live feedback from active agents:
- **Reasoning Accumulation (`thinking_delta`)**: Real-time token/byte counter and preview inspecting the agent's internal thought process.
- **Tool Execution Monitoring**: Instant notification on tool invocations (`tool_start` / `tool_end`) with arguments and success status.
- **Automated Rate-Limit Handling**: Regex-based detection of HTTP 429 quota exhaustion (`RESOURCE_EXHAUSTED`, `Rate limit reached`) with countdown timers for scheduled retries.
- **Live Canvas Badges**: Nodes visually reflect execution status (`thinking`, `tool call`, `429 retry`, `completed`).

### 4. Batch Pipelines & Fault-Tolerant Resume Engine
For complex multi-stage pipelines processing large collections of items (e.g. articles, data batches):
- **Atomic Checkpoint Persistence (`PipelineResumeEngine`)**: Tracks execution states per item and per stage in `pipeline-state.json` with atomic file swapping.
- **Physical Artifact Validation & Auto-Recovery**: Checks filesystem artifacts (file existence, minimum byte size, and regex content verification such as YAML frontmatter) or log exit codes. Stages with valid outputs are automatically marked as completed (`recoveredFromArtifact: true`), skipping redundant API calls.
- **Serialized Bottleneck Chaining**: Define heavy stages (e.g. redaction) to run sequentially across items to stay within provider limits, while downstream stages (diagram generation, frontmatter, animation) run concurrently.
- **Batch Orchestrator Modal**: Dedicated interface on the toolbar to set batch items, configure concurrency, toggle serialized stages, and monitor live progress across the checkpoint matrix.

---

## How to Run Arestrix

The project is unified under a fast C++ backend using **WebUI**, giving you a native Windows Desktop experience while leveraging the Three.js WebGL canvas.

### Prerequisites & Toolchain Setup

Arestrix Studio runs on **Windows and Linux**. The programs generated from a graph target **Windows** (departmental apps are a later phase).

1. **Git, with submodules** — Taskflow 4.1.0, simdjson 5.0.4, WebUI 2.4.2, i18next, Monaco and SkillSpector live in [`third_party/`](third_party):
   ```bash
   git clone --recurse-submodules <repository-url>
   # or, in an existing clone:
   git submodule update --init
   ```

2. **Node.js (v18+) and npm** (frontend: Three.js, Vite, TypeScript, Vitest):
   ```powershell
   winget install OpenJS.NodeJS.LTS
   ```

3. **Python 3 (v3.10+)** — runs the build script (`CppCore/dev.py`) and the end-to-end test.

4. **A C++20 compiler and CMake (>= 3.20)**:
   - **Windows**: Visual Studio 2022 or newer (Community or Build Tools) with the **"Desktop development with C++"** workload. Its bundled CMake and Ninja are detected automatically.
   - **Linux**: `g++` >= 11 (or `clang++`), `cmake`, optionally `ninja`, and `curl` (used by the agent runtime for HTTPS).

5. **Optional**: Docker (container sandbox for agent `bash`), WSL2 (MicroVM driver), Graphviz.

---

### First-Time Installation

```powershell
git clone --recurse-submodules <repository-url>
cd NodeForge
npm install
```

---

### Execution Modes

#### Mode 1: Full Standalone Desktop Application (Native C++ + WebUI) — *Recommended*

```powershell
python CppCore/dev.py              # builds everything and opens the Studio
python CppCore/dev.py --no-run     # build only
python CppCore/dev.py --test       # build + C++ unit tests (ctest)
```

`dev.py` initializes the submodules, builds the frontend (`npm run build`, a single-file `dist/index.html`) and the C++ backend with **CMake** (WebUI and CivetWeb are compiled from `third_party/webui`; no binaries are downloaded). The build produces:

| Output | What it is |
| :--- | :--- |
| `arestrix(.exe)` | The Studio (WebUI window + engine). |
| `arestrix-cli(.exe)` | Headless runner: `arestrix-cli run graph.json --root <project>` / `arestrix-cli gen graph.json`. |
| `runtime/lib/arestrix_core.lib` (`libarestrix_core.a`) | The runtime that every program generated from a graph links against (agents, sandbox, tools, MCP, simdjson). |

#### Build & Run (F5): how a graph is executed

1. The graph is validated, then sent to the C++ backend.
2. The backend generates a **C++20 / Taskflow** `main.cpp` (agent nodes call the runtime), compiles it with MSVC (Windows) or g++/clang++ (Linux) against `arestrix_core`, and **runs it inside the project folder**. Unchanged graphs reuse the compiled binary.
3. Logs and agent telemetry (`thinking`, `tool_start/tool_end`, `429 retry`, node status) stream live to the console and to the canvas badges. `Shift+F5` stops the run.

**Agent nodes** run a real tool-use loop against the configured provider. Skills (`SKILL.md`) are the system prompt; the tools are `read`, `write`, `edit`, `bash` and `validate_svg`, all confined to the project folder.

| Provider | API key variable | Endpoint (override with `<PROVIDER>_BASE_URL`) |
| :--- | :--- | :--- |
| `deepseek` | `DEEPSEEK_API_KEY` | `https://api.deepseek.com` |
| `openai` | `OPENAI_API_KEY` | `https://api.openai.com/v1` |
| `anthropic` | `ANTHROPIC_API_KEY` | `https://api.anthropic.com` (Messages API) |
| `google` | `GEMINI_API_KEY` | Gemini OpenAI-compatible endpoint |
| `jev` | `JEV_API_KEY` (optional) | **`JEV_BASE_URL` is required** — any OpenAI-compatible server |
| `local` | `LOCAL_LLM_API_KEY` (optional) | `LOCAL_LLM_BASE_URL`, default `http://localhost:11434/v1` (Ollama, llama.cpp, vLLM...) |

Keys are stored as user environment variables (Windows Registry / `~/.config/arestrix/env` on Linux) — never passed through a shell.

**Safety properties** (all covered by tests):
- A sandbox level that is not available **fails closed**; it never silently falls back to the host unless `allowFallbackToHost` is set.
- `host_direct` runs the command in a Job Object / process group with timeout and memory limit and **removes `*_API_KEY`, `*_TOKEN`, `*_SECRET` variables** from the child environment. It is *not* a security boundary. `container` (Docker) adds `--cap-drop=ALL`, `--read-only`, no network and workspace-only mounts. `microvm` (WSL2) shares the host file system and refuses `offline` policies it cannot enforce.
- File tools and the file endpoints exposed to the UI reject paths outside the project (`..`, absolute, symlinks, NTFS streams).
- A feedback loop cannot spend unlimited money: each agent node is capped at 10 runs (`ARESTRIX_MAX_AGENT_RUNS`).

**Run the tutorial headless** (`TUTORIAL_AGENTE_SVG_ARESTRIX.md`), against a scripted fake LLM server:
```powershell
python scripts/e2e_tutorial.py
```
It builds the tutorial graph, generates and compiles the Taskflow program, runs the fork/join, the 429 retry and the auto-correction loop, and checks the final SVG.

**Quick launch (already built):** run `./arestrix` (or double-click `arestrix.exe`).

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

**Test Coverage Summary (192/192 frontend tests in 29 suites; 92 C++ checks via `ctest`; plus the end-to-end tutorial run `python scripts/e2e_tutorial.py`):**
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
- `tests/project-creation-hash.spec.ts`: Deterministic project ID hashing (`hashProjectName`), sanitized filesystem paths, and UI creation flow.
- `tests/workspace-governance.spec.ts`: Pre-approved workspace skills & MCPs catalog, and automated `WorkspaceSkillAuditorAgent` validation.
- `tests/github-skills-separation.spec.ts`: Global Studio open-source catalog segregation vs. per-node skill binding and import.
- `tests/node-view-typography.spec.ts` & `tests/node-action-tooltips.spec.ts`: High-DPI SDF vector typography, clean badge pills without text-shadows, and interactive action tooltips.
- `tests/modal-window-manager.spec.ts`: Multi-window modal stacking, drag header handles, and responsive dialog viewports.
- `tests/office-ribbon-interface.spec.ts` & `tests/office-workspace-interface.spec.ts`: Microsoft Office Fluent Ribbon interface, Backstage View, QAT shortcuts, and connector ingestion.
- `tests/pi-project-importer.spec.ts` & `tests/generic-pi-import.spec.ts`: Native Pi project scanner, CLI settings ingestion, and automated Taskflow C++20 DAG synthesis.

---

#### Mode 4: Standalone Frontend Build

To compile only the frontend assets into production distribution files:

```powershell
npm run build
```
- Compiles TypeScript and packages all CSS and assets into a singlefile inlined `dist/index.html` consumed in-memory by `arestrix.exe`.

---

## Taskflow Node Mapping

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

## Modern Architecture: Office Ribbon, Workspaces & Ingestion

Arestrix includes enterprise-grade desktop UX and model ingestion capabilities:

1. **Microsoft Office Ribbon UI Standard (Phase Complete)**:
   - Modern Ribbon bar with functional tabs (*Arquivo / Backstage*, *Página Inicial*, *Inserir*, *Agentes & Tools*, *Conectores & Importação*, *Dados & Memória*, *Execução & Lote*).
   - Chunked tool groups with bottom labels, Quick Access Toolbar (Save, Undo, Redo, Run F5), and clean soft-neomorphic badges strictly respecting the zero text-shadow rule.
   - Quick search / Tell-Me box (*"O que você deseja fazer?"*) and Ribbon auto-collapse shortcut (`Ctrl+F1`).

2. **Project Governance & Workspace Encapsulation (Phase Complete)**:
   - First-class Project entities with native C++20 backend management (`arestrix.project.json`).
   - Strict isolation of memory namespaces (Blackboard / Semantic Lessons), sandboxes, working directories, and pipelines per workspace.
   - Recent projects MRU management and 3-tab Project Manager Hub.

### Tripartite Architecture & UI Scope Separation: Studio vs. Workspace vs. Project

Arestrix strictly enforces a **tripartite separation of concerns** across both its architectural backend and its user interface. This guarantees clean modularity, multi-tenant isolation, enterprise security governance, and prevents accidental cross-contamination of credentials, cognitive memory, and filesystem boundaries.

```
+-----------------------------------------------------------------------------------+
| STUDIO (Desktop Application & Global Engine)                                      |
| • WebGL 2.5D Canvas & Camera     • C++20 Taskflow Code Generator & MSVC Compiler  |
| • Global LLM API Credentials     • Authorized GitHub Open-Source Repositories     |
+-----------------------------------------------------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
| WORKSPACE (Physical Host Environment & Local Storage Infrastructure)              |
| • Filesystem Root (.workspace/)  • Relational SQLite Storage (database.sqlite)    |
| • Docker / MicroVM Sandboxes     • Pre-approved MCP Servers & Workspace Skills    |
| • WorkspaceSkillAuditorAgent     • Local Execution Limits & Directory Mounts      |
+-----------------------------------------------------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
| PROJECT (Logical Business Purpose & Concurrent DAG Pipelines)                     |
| • arestrix.project.json          • DAG Pipeline Graphs (pipelines/main.json)      |
| • Agent Node Prompts & Overrides • Local Project .env Variables                   |
| • Scoped Blackboard Memory       • Isolated Semantic Lessons by projectId         |
+-----------------------------------------------------------------------------------+
```

#### Comparative Matrix: The Three Operational Environments

| Dimension | Studio (Desktop App / Global) | Workspace (Physical Infrastructure) | Project (Logical Business Unit) |
| :--- | :--- | :--- | :--- |
| **Core Concept** | The desktop application host, rendering engine, and universal compiler toolchain. | The physical filesystem sandbox, OS isolation container, and local storage runtime. | The purposeful business solution, workflow identity, and concurrent DAG execution graph. |
| **Manifest / Core Files** | `arestrix.exe`, global configuration, and window layouts. | Physical directory (`rootPath`) containing `.workspace/`, `storage/`, and `output/`. | `arestrix.project.json` (`projectId`, `name`, `version`, `description`) and `pipelines/main.json`. |
| **Persistence & Storage** | Application settings and MRU recent projects index. | Local SQLite relational database (`storage/database.sqlite`), checkpoint states (`pipeline-state.json`). | Pipeline definitions, subflow graph schemas, and scoped output artifacts. |
| **Credentials & Execution** | Global LLM API keys securely stored via `EnvironmentManager` (Anthropic, OpenAI, DeepSeek, Google). | Execution sandboxes (Docker, MicroVM, Host Job Objects), memory/CPU quotas, directory mounts. | Node-specific model overrides, prompt templates, and local `.env` pipeline variables. |
| **Skill & MCP Governance** | Global catalog of authorized open-source GitHub repositories (`GitHubSkillsRegistry`). | Pre-approved workspace skills (`.workspace/skills`), approved MCP server daemons, and `WorkspaceSkillAuditorAgent`. | Nodes bind curated skills and MCP tools directly to agent task execution. |
| **Cognitive Memory** | None (application-level state only). | Physical SQLite tables, local vector embedding indexes, and thread-safe lock primitives. | Scoped Blackboard and Semantic Lessons strictly isolated per `projectId`, preventing data leakage. |
| **Conceptual Analogy** | **The Factory / Industrial Park** (The building, electricity, universal machine tools). | **The Workshop / Assembly Bay** (The workbench, storage bins, safety gear, local toolbox). | **The Product Being Built** (Blueprints, bill of materials, assembly steps, quality checks). |

---

#### UI Segregation Across Screens and Modals

To ensure users always know which scope they are configuring, every screen, ribbon group, inspector panel, and modal in Arestrix features explicit scope badges and separated sections:

1. **Office Ribbon Bar (`OfficeRibbon.ts`)**:
   - **Data & Memory Tab**: Explicitly partitioned into **Memória do Projeto** (scoped Blackboard and Semantic Lessons) and **Armazenamento do Workspace** (SQLite database file and physical workspace directory).
   - **Tools Tab**: Highlights **MCP Servers** as workspace infrastructure tools available to project nodes.

2. **Backstage View (`BackstageView.ts`)**:
   - **Info Section (`info`)**: Renders two distinct cards: *Informações do Projeto Ativo* (manifest details, version, and registered pipelines) and *Infraestrutura do Workspace Físico* (physical path, database location, and workspace auditor status).
   - **Open Section (`open`)**: Displays *Grafos Recentes do Workspace Ativo `(Pipelines do Projeto)`* followed by *Projetos Recentes*.
   - **Options Section (`options`)**: Formally labeled as *Opções Globais do Studio & Governança do Workspace*, providing access to Studio defaults and workspace security auditing.

3. **Node Inspector Panel (`Inspector.ts`)**:
   - **Empty State**: Displays separate overview cards for the *Grafo do Projeto Ativo* and the *Ambiente do Workspace*.
   - **Node Action Bar**: Every modal action button is tagged with its operational scope:
     - `[Projeto]`: Model Configuration (`NodeModelModal`), Cognitive Memory (`NodeMemoryModal`), and Tool Permissions (`NodeToolsModal`).
     - `[Workspace]`: Sandbox Isolation & Host Limits (`NodeSandboxModal`).
     - `[Workspace / Projeto]`: Node Skills (`NodeSkillsModal` — binds project agent nodes to verified workspace skills).

4. **Dedicated Dialogs & Modals**:
   - **Global Defaults (`GlobalDefaultsModal.ts`)**: Structured into distinct scope badges: `Studio` for universal LLM API keys and defaults, `Workspace` for sandbox container isolation and approved tools, and `Projeto` for default node models and memory retention.
   - **Project Manager (`ProjectManagerModal.ts`)**: Clearly separates the logical project metadata (`arestrix.project.json`) from the physical directory (`Diretório Raiz do Workspace`). Local project environment variables (`.env`) are strictly separated from Studio global keys.
   - **GitHub Repositories (`GitHubReposModal.ts`)**: Identified as `Catálogo Global do Studio`, managing authorized open-source repositories across the entire application.
   - **MCP Servers (`McpServersModal.ts`)**: Identified as `Servidores MCP do Workspace`, providing physical tool daemons for the current host environment.
   - **Cognitive Memory Modals (`ProjectMemoryModal.ts` & `NodeMemoryModal.ts`)**: Labeled as `Escopo do Projeto`, guaranteeing that memory queries and blackboard key-values remain isolated to the active `projectId`.

---

#### Workspace Skill & MCP Security Governance

To protect users against untrusted tool execution, Arestrix includes an automated governance workflow:
1. **Pre-Approved Workspace Repositories**: Approved skills and MCP configurations are provisioned inside `.workspace/skills` and `.workspace/mcps` upon workspace initialization.
2. **Automated Skill Auditor (`WorkspaceSkillAuditorAgent`)**: Whenever a user or an agent imports an external skill, the workspace auditor automatically analyzes its YAML frontmatter, permission requirements, script dependencies, and sandbox safety before authorizing execution within project pipelines.


3. **External AI Assistant & Spreadsheet Ingestion Engine (Phases 15 to 20)**:
   - **Pi Agent Real Project & CLI Ingestion (Phase Complete)**:
     - One-click import of local Pi CLI projects, with dynamic scanning of any repository.
     - Native C++ and TS scanner that inspects `.pi/settings.json`, skills in `.agents/*/SKILL.md`, `plano.md`, and checkpoints in `pipeline-state.json`.
     - Immediate visual synthesis of a C++20 Taskflow pipeline: autonomous agents, MCP tools, and physical artifact validation.
   - **Claude (Anthropic)**: XML structured prompt and artifact conversion.
   - **OpenAI (ChatGPT / Assistants)**: Export threads and tools mapping.
   - **Hermes (Nous Research)**: Scratchpad logic for local offline microVMs.
   - **Excel Modernization Engine**: Deconstruction of complex multi-sheet spreadsheets into relational SQLite databases and concurrent 4-agent DAGs.

---

## Internationalization (i18n) & How to Add New Languages

Arestrix Studio is a fully multilingual application. Every title, Ribbon menu, button, modal, tooltip, and message is loaded dynamically from JSON translation catalogs.

The [`i18next`](https://github.com/i18next/i18next) library is managed directly as a **Git submodule** in `third_party/i18next`, which allows it to be verified and updated automatically without depending on external packages in `node_modules`.

Languages shipped out of the box:
- **Português do Brasil (`pt-BR`)** (Default)
- **English - United States (`en-US`)**

---

### Step by Step: Installing and Adding a New Language

To add a new language (for example, Spanish `es-ES`, French `fr-FR`, or German `de-DE`), follow the steps below:

#### 1. Create the JSON Dictionary File
Duplicate one of the existing dictionaries in `src/i18n/locales/` using the corresponding BCP-47 code:
```bash
# Example for Spanish (Spain / International):
cp src/i18n/locales/en-US.json src/i18n/locales/es-ES.json
```
Edit the new `src/i18n/locales/es-ES.json` file and translate the values, keeping the same hierarchical structure (keys identical to those in `pt-BR.json` and `en-US.json`).

#### 2. Register the New Language in `src/i18n/types.ts`
Add the new code to the `SupportedLocale` type union and declare its metadata in the `SUPPORTED_LOCALES` list:

```typescript
// src/i18n/types.ts
export type SupportedLocale = 'pt-BR' | 'en-US' | 'es-ES';

export const SUPPORTED_LOCALES: readonly ILocaleOption[] = [
  {
    code: 'pt-BR',
    name: 'Português (Brasil)',
    nativeName: 'Português do Brasil',
    flag: '🇧🇷'
  },
  {
    code: 'en-US',
    name: 'English (US)',
    nativeName: 'US English',
    flag: '🇺🇸'
  },
  {
    code: 'es-ES',
    name: 'Español (España)',
    nativeName: 'Español',
    flag: '🇪🇸'
  }
];
```

#### 3. Import and Register the Catalog in `src/i18n/index.ts`
Import the JSON file and include it in the `resources` object of the `i18next` initialization:

```typescript
// src/i18n/index.ts
import esES from './locales/es-ES.json';

// In i18next.init:
resources: {
  'pt-BR': { translation: ptBR },
  'en-US': { translation: enUS },
  'es-ES': { translation: esES }
}
```

Also update the sanitization check in `setLocale`:
```typescript
if (locale !== 'pt-BR' && locale !== 'en-US' && locale !== 'es-ES') {
  locale = 'pt-BR';
}
```

#### 4. Add the Option to the Backstage View Dropdown
In `src/ui/ribbon/BackstageView.ts`, add the new `<option>` inside the `<select id="backstage-language-select">` element:

```html
<select id="backstage-language-select" class="backstage-language-dropdown">
  <option value="pt-BR" ${currentLocale === 'pt-BR' ? 'selected' : ''}>BR Português (Brasil)</option>
  <option value="en-US" ${currentLocale === 'en-US' ? 'selected' : ''}>US English (United States)</option>
  <option value="es-ES" ${currentLocale === 'es-ES' ? 'selected' : ''}>ES Español (España)</option>
</select>
```

#### 5. Run the Automated Parity Tests
Arestrix has strict Vitest unit tests that verify the consistency and parity of every key across languages:

```powershell
npm test
```
*If a key was forgotten or misnamed, the test will point out the mismatch immediately.*

#### 6. Build and Sync with the C++ Desktop Binary
Run the production build:

```powershell
npm run build
```
*The script compiles the assets with Vite and automatically syncs the `dist/` folder with `CppCore/dist/`, making the new language immediately available in the `arestrix.exe` desktop executable.*

---

### Managing the `third_party/i18next` Submodule

To audit or update the official `i18next` library code:

```powershell
# 1. Check whether the remote i18next repository has new updates (without changing files)
npm run submodule:check

# 2. Update the submodule to the latest version of the remote master branch
npm run submodule:update
```

---

## License

MIT License. Designed and developed with passion and AI collaboration.

---

## Acknowledgements

Arestrix stands on the shoulders of outstanding open-source work. Our sincere thanks to the authors and communities behind the projects vendored as Git submodules in [`third_party/`](third_party):

| Project | Role in Arestrix | License |
| :--- | :--- | :--- |
| [**WebUI**](https://github.com/webui-dev/webui) (Hassan Draga) and the [**CivetWeb**](https://github.com/civetweb/civetweb) server it embeds | The lightweight native window that hosts the Studio interface and the RPC bridge between the web UI and the C++ backend, without Electron. Built from `third_party/webui` (v2.4.2). | MIT |
| [**Inter**](https://github.com/rsms/inter) (The Inter Project Authors) | Typeface used by the canvas text renderer. Files and license in `third_party/fonts/inter`. | OFL-1.1 |
| [**Taskflow**](https://github.com/taskflow/taskflow) (Dr. Tsung-Wei Huang and contributors) | The C++20 parallel and heterogeneous task-programming engine that runs every graph: static, condition, subflow, module and agent nodes all become Taskflow tasks. Served from `third_party/taskflow` (v4.1.0). | MIT |
| [**simdjson**](https://github.com/simdjson/simdjson) (Daniel Lemire, Geoff Langdale and contributors) | Parses every JSON document the engine reads — graphs, projects, LLM responses, MCP messages — at gigabytes per second. Served from `third_party/simdjson` (v5.0.4). | Apache-2.0 / MIT |
| [**i18next**](https://github.com/i18next/i18next) | Internationalization framework behind every translated string in the Studio. | MIT |
| [**Monaco Editor**](https://github.com/microsoft/monaco-editor) (Microsoft) | Code editor that powers the generated C++ code view. | MIT |
| [**SkillSpector**](https://github.com/NVIDIA/SkillSpector) (NVIDIA) | Security scanner for AI agent skills. | Apache-2.0 |

Each project remains the property of its respective authors and is distributed under its own license, available in its folder under `third_party/`.

### Third-party code policy

All external code lives in [`third_party/`](third_party) (Git submodules, or vendored files shipped with their license) — there is no `vendor/` folder — and every license must be compatible with Taskflow's MIT license: permissive licenses only (MIT, BSD, ISC, Apache-2.0, ...). The single documented exception is the Inter font under the SIL OFL 1.1, which allows embedding the font in software under any license. JavaScript dependencies are declared in `package.json` and checked against the same list. The policy is enforced by:

```powershell
python scripts/check_third_party_licenses.py
```
