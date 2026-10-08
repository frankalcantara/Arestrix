# TaskForge ⚡

**TaskForge** is a high-performance, next-generation visual node graph orchestrator and IDE built with **Three.js (WebGL)**, **TypeScript**, and a native **C++ Backend (WebUI)** for modeling, visualizing, and executing concurrent, heterogeneous agent pipelines powered by **Taskflow in C++20**.

---

## 💡 What is TaskForge?

Modern concurrent applications—ranging from autonomous multi-agent pipelines to high-throughput data processing and compute engines—demand fine-grained task parallelism, dynamic subflow decomposition, stochastic conditional branching, and strict execution isolation.

While C++20 and the **Taskflow** library provide a world-class parallel runtime, hand-crafting complex task graphs with feedback loops, nested subflows, and sandboxed AI workers in raw code can be tedious and error-prone.

**TaskForge** bridges this gap by providing an intuitive 2.5D visual canvas with instant C++20 code generation, automated MSVC compilation, native sandbox execution drivers, real-time streaming telemetry, and a persistent resume engine.

### Core Architectural Layers

TaskForge follows a decoupled three-tier architecture:

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

TaskForge features first-class support for autonomous AI agents based on the **Code Mode** paradigm, allowing each node in the DAG to act as an isolated worker with fine-grained permissions:

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

## 🚀 How to Run TaskForge

The project is unified under a fast C++ backend using WebUI, giving you a native Desktop App experience while leveraging the Three.js WebGL canvas.

### Prerequisites

1. **Node.js** (v18.0+) & **npm** (for building the frontend).
2. **Python 3** (for the automated build script).
3. **Windows environment** with Microsoft Visual C++ (MSVC) from Visual Studio 2022 or Build Tools installed (with C++20 support `/std:c++20` and `nmake`).

### 1. Build and Launch the Full App (Recommended)

To compile the TypeScript frontend, build the C++ WebUI backend, and launch the standalone Desktop App:

```bash
# From the root of the project:
cd CppCore
python dev.py
```

*What this does:*
1. Runs `npm run build` in the root folder to compile the TS/Three.js frontend into `dist/`.
2. Downloads and extracts the `webui` static library if it's missing.
3. Automatically locates your MSVC installation and runs `nmake -f Makefile` to compile `app.exe`.
4. Launches `app.exe`, which opens the TaskForge Visual Studio window.

### 2. Run the Web Studio Only (Dev Mode)

If you are only editing the TypeScript/Three.js visual layer and want Hot-Module Replacement (HMR) without the C++ backend:

```bash
# From the root of the project:
npm install
npm run dev
```

*Note: In web-only dev mode, native C++ compilation/execution features won't work, but you can still design graphs and view generated code.*

### 3. Run Automated Tests

To run the complete test suite (graph validation, history undo/redo, agent inheritance, telemetry, and batch resume engine):
```bash
npm run test
```

---

## 🧩 Taskflow Node Mapping

TaskForge maps visual graph nodes directly to native **Taskflow** primitives:

| Node Type | Color / Badge | Taskflow Semantic | Purpose in Concurrent Pipeline |
| :--- | :--- | :--- | :--- |
| **Static Task** | Blue (`#3B82F6`) | `tf::Task (taskflow.emplace([](){}))` | Deterministic compute tasks and background workers. |
| **Condition Task** | Orange (`#FF7828`) | `tf::Task (returns int branch)` | Stochastic decisions, convergence checks, and feedback loops. |
| **Subflow Task** | Purple (`#8B5CF6`) | `tf::Task (accepts tf::Subflow&)` | Dynamic graph decomposition at runtime with child subgraphs. |
| **Module Task** | Green (`#10B981`) | `tf::Task (tf.composed_of(...))` | Reusable, modular task subgraphs. |
| **Critical Section**| Cyan (`#06B6D4`) | `tf::CriticalSection (acquire/release)` | Mutex-guarded atomic sections across concurrent threads. |
| **Agent Task** | Indigo (`#6366F1`) | `tf::Task (Sandbox / MicroVM)` | Autonomous Code Mode agent with isolated VM, multi-provider model routing, and artifact validation. |

---

## 📜 License

MIT License. Designed and developed with passion and AI collaboration.
