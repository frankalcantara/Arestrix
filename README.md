# TaskForge ⚡

**TaskForge** is a high-performance, next-generation visual node graph orchestrator and IDE built with **Three.js (WebGL)**, **TypeScript**, and a native **C++ Backend (WebUI)** for modeling, visualizing, and generating concurrent, heterogeneous agent pipelines powered by **Taskflow in C++20**.

---

## 💡 What is TaskForge?

Modern concurrent applications—ranging from multi-agent AI orchestrators to high-throughput data processing and physics engines—demand fine-grained task parallelism, dynamic subflow decomposition, stochastic conditional branching, and mutual exclusion. 

While C++20 and the **Taskflow** library provide a world-class parallel runtime, hand-crafting complex task graphs with feedback loops and nested subflows in raw code can be tedious and error-prone.

**TaskForge** bridges this gap by providing an intuitive, interactive 2.5D visual canvas with instant C++20 code generation, automated MSVC / CMake compilation, and live telemetry observability.

### Core Architectural Layers

TaskForge follows a strict, decoupled three-tier architecture:

1. **Pure Logical Layer (Frontend `src/core/`)**:
   - In-memory graph data structure (`Graph`).
   - Subflow graph nesting (hierarchical child graphs).
   - Reversible editing history powered by the **Command Pattern** (`CommandManager`, `ICommand`), with undo/redo support (`Ctrl+Z` / `Ctrl+Y`).
   - Headless C++20 Taskflow code generator (`TaskflowCodegen`).

2. **Visual & Interaction Layer (Frontend `src/view/`)**:
   - Hardware-accelerated Three.js WebGL canvas.
   - Smooth 2.5D orthographic camera with cursor-centered exponential zoom and drag panning (`CameraController`).
   - Procedural infinite background grid rendered with custom GPU shaders (`InfiniteGrid`).
   - High-contrast, Soft Neomorphic design: rounded node cards, crisp pill badges, and bold typography.
   - Smooth cubic Bézier connections using Three.js Fat Lines (`Line2`).

3. **Compiler & Native Toolchain Bridge (Backend `CppCore/`)**:
   - A lightweight, native C++ application wrapped with **WebUI** that statically serves the Vite-built frontend.
   - First-class support for **MSVC (`cl.exe`)** on Windows as the default C++20 compiler.
   - Decoupled real-time execution observer header (`include/TaskForgeObserver.hpp`) for live WebGL telemetry feedback via WebSocket.

---

## 🚀 How to Run TaskForge

The project is now unified under a fast C++ backend using WebUI, giving you a native Desktop App feel while leveraging the Three.js WebGL canvas.

### Prerequisites

1. **Node.js** (v18.0+) & **npm** (for building the frontend).
2. **Python 3** (for the automated build script).
3. **Windows environment** with Microsoft Visual C++ (MSVC) from Visual Studio 2022 or Build Tools installed (with C++20 support `/std:c++20` and `nmake`).

### 1. Build and Launch the Full App (Recommended)

To compile the TypeScript frontend, build the C++ WebUI backend, and launch the standalone Desktop App, simply run the Python dev script from the `CppCore` directory:

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

To verify the graph data structures, topological sorting, and cycle detection:
```bash
npm run test
```

---

## 🧩 Taskflow Node Mapping

TaskForge maps high-level visual graph nodes to native **Taskflow** primitives:

| Node Type | Color / Badge | Taskflow Semantic | Purpose in Concurrent Pipeline |
| :--- | :--- | :--- | :--- |
| **Static Task** | Blue (`#3B82F6`) | `tf::Task (taskflow.emplace([](){}))` | Deterministic compute tasks and agent workers. |
| **Condition Task** | Orange (`#FF7828`) | `tf::Task (returns int branch)` | Stochastic decisions, convergence checks, and feedback loops. |
| **Subflow Task** | Purple (`#8B5CF6`) | `tf::Task (accepts tf::Subflow&)` | Dynamic graph decomposition at runtime with child subgraphs. |
| **Module Task** | Green (`#10B981`) | `tf::Task (tf.composed_of(...))` | Reusable, modular task subgraphs. |
| **Critical Section**| Cyan (`#06B6D4`) | `tf::CriticalSection (acquire/release)` | Mutex-guarded atomic sections across concurrent threads. |

---

## 🎨 Design System Guidelines

TaskForge follows strict visual rules to ensure maximum clarity and ergonomics:
- **Zero Drop-Shadows or Glows on Typography**: Text elements never use `filter: drop-shadow`, `text-shadow`, or SVG blur filters.
- **High-Contrast Bold Emphasis**: Readability is achieved using bold weights (`font-weight: 700` and `800`), crisp color contrast (`#FFFFFF` on saturated badges, `#0F172A` on `#F1F5F9` cards), and solid rounded pill backgrounds.
- **Soft Neomorphic Canvas**: Elevated white cards, gentle Slate borders, and an infinite shader grid to reduce visual fatigue during long modeling sessions.

---

## 📜 License

MIT License. Designed and developed with passion and AI collaboration.
