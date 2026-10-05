# TaskForge ⚡

**TaskForge** is a high-performance, next-generation visual node graph orchestrator and IDE built with **Three.js (WebGL)** and **TypeScript** for modeling, visualizing, and generating concurrent, heterogeneous agent pipelines powered by **Taskflow in C++20**.

---

## 💡 What is TaskForge?

Modern concurrent applications—ranging from multi-agent AI orchestrators to high-throughput data processing and physics engines—demand fine-grained task parallelism, dynamic subflow decomposition, stochastic conditional branching, and mutual exclusion. 

While C++20 and the [**Taskflow**](https://github.com/taskflow/taskflow) library provide a world-class parallel runtime, hand-crafting complex task graphs with feedback loops and nested subflows in raw code can be tedious and error-prone.

**TaskForge** bridges this gap by providing an intuitive, interactive 2.5D visual canvas with instant C++20 code generation, automated MSVC / CMake compilation, and live telemetry observability.

### Core Architectural Layers

TaskForge follows a strict, decoupled three-tier architecture:

1. **Pure Logical Layer (`src/core/`)**:
   - In-memory graph data structure (`Graph`).
   - Directed Acyclic Graph (DAG) topological validation alongside managed conditional feedback loops.
   - Subflow graph nesting (hierarchical child graphs).
   - Reversible editing history powered by the **Command Pattern** (`CommandManager`, `ICommand`), with undo/redo support (`Ctrl+Z` / `Ctrl+Y`).
   - Headless C++20 Taskflow code generator (`TaskflowCodegen`).

2. **Visual & Interaction Layer (`src/view/`)**:
   - Hardware-accelerated Three.js WebGL canvas.
   - Smooth 2.5D orthographic camera with cursor-centered exponential zoom and drag panning (`CameraController`).
   - Procedural infinite background grid rendered with custom GPU shaders (`InfiniteGrid`).
   - High-contrast, Soft Neomorphic design: rounded node cards, crisp pill badges, and bold typography.
   - Smooth cubic Bézier connections using Three.js Fat Lines (`Line2`).
   - HiDPI canvas sprites with dynamic font auto-scaling to ensure long node titles remain clear and readable at all camera distances.

3. **Compiler & Native Toolchain Bridge (`src/core/codegen/`)**:
   - First-class support for **MSVC (`cl.exe`)** on Windows as the default C++20 compiler.
   - Modular build configuration emitting `build_msvc.ps1`, `CMakeLists.txt`, `Makefile`, and direct build scripts.
   - Decoupled real-time execution observer header (`include/TaskForgeObserver.hpp`) for live WebGL telemetry feedback via WebSocket.

---

## ⚡ Built via "Vibe Coding"

> *"Vibe coding is the art of steering high-level architecture, design aesthetics, and technical constraints in real-time collaboration with an AI coding partner—turning ideas into production-ready software at the speed of thought."*

TaskForge is being **vibe coded** from scratch using **Google Antigravity**. 

Rather than writing boilerplate line-by-line, the creator acts as the systems architect and design director:
- Establishing strict engineering constraints (such as C++20 MSVC standards, zero-shadow typography rules, and decoupled layers).
- Iterating rapidly on visual feel, usability, and edge cases.
- Driving features like subflow drill-down, live telemetry observers, and undo/redo histories through continuous interactive flow.

This project demonstrates that "vibe coding" is not just for trivial scripts—it can produce robust, strictly typed TypeScript codebases, complex WebGL vector graphics, comprehensive unit test suites (19/19 passing), and production-grade C++20 templates.

---

## 📍 Where We Are (Current Status & Roadmap)

TaskForge is in active, rapid development. Here is the current progress:

```
[Phase 1: Hierarchical Subflows]   ──▶ COMPLETED  (Nested graphs, Breadcrumbs, Recursive Codegen)
[Phase 2: Live Telemetry Observer] ──▶ COMPLETED  (TaskForgeObserver.hpp, WebSocket, Visual Node States)
[Phase 3: Command Pattern Undo/Redo]─▶ COMPLETED  (ICommand, History, Toolbar, Ctrl+Z / Ctrl+Y)
[HiDPI Typography & Port Labels]   ──▶ COMPLETED  (Zero-shadow, Bold 800, Auto-scaling, Socket badges)
[Phase 4: Dynamic C++ Plugin System] ──▶ IN PROGRESS (Hot-reloading runtime Taskflow tasks from DLLs)
[Phase 5: Execution Profiler & Dashboard] ──▶ PLANNED  (Gantt timeline, worker thread utilization)
```

### ✅ Implemented Features:
- **Interactive Three.js Canvas**: Smooth zoom/pan, node dragging, multi-selection, and rubberband port-to-port wire creation.
- **Strict Clean Typography**: Zero text-shadows or blur glows. High-contrast Slate 900 text on Slate 100 pill containers, bold 800 type weights, and crisp vector outlines.
- **Labeled Sockets**: Clear input (`IN`) and output (`OUT`) port indicators, plus dedicated conditional branch labels (`TRUE [0]` / `FALSE [1]`).
- **Phase 1 — Hierarchical Subflows & Drill-down**: Double-click subflow nodes to enter nested child graphs; navigate back using the floating Breadcrumbs bar; recursive C++20 code generation.
- **Phase 2 — Live Telemetry Observer**: Decoupled `TaskForge::LiveTelemetryObserver` in C++ with real-time WebGL node borders (`running` in emerald, `completed` in dark green).
- **Phase 3 — Full Undo/Redo Engine**: Non-destructive graph mutations with command encapsulation (`AddNodeCommand`, `MoveNodeCommand`, `AddEdgeCommand`, etc.), keyboard shortcuts, and toolbar buttons.
- **C++20 Taskflow Code Generator**: Generates complete, compilable C++20 programs with MSVC, CMake, or Makefile scripts.
- **Automated Test Suite**: 19 automated tests in Vitest verifying graph invariants, cycle handling, subflow codegen, and undo/redo stacks.

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

## 🎨 Design System Guidelines

TaskForge follows strict visual rules to ensure maximum clarity and ergonomics:
- **Zero Drop-Shadows or Glows on Typography**: Text elements never use `filter: drop-shadow`, `text-shadow`, or SVG blur filters.
- **High-Contrast Bold Emphasis**: Readability is achieved using bold weights (`font-weight: 700` and `800`), crisp color contrast (`#FFFFFF` on saturated badges, `#0F172A` on `#F1F5F9` cards), and solid rounded pill backgrounds.
- **Soft Neomorphic Canvas**: Elevated white cards, gentle Slate borders, and an infinite shader grid to reduce visual fatigue during long modeling sessions.

---

## 📄 License

MIT License. Designed and developed with passion and AI collaboration.
