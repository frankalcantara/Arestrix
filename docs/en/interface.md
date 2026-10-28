# Interface tour

> **Applies to:** Arestrix Beta 2.3 (`2.3.0-beta.1`) · Last reviewed: 2026-10-27

[Português](../pt-BR/interface.md) · [Index](../README.md)

The Studio follows the Microsoft Office layout, because most people already know it.

## Ribbon

| Tab | What is there |
| :--- | :--- |
| **File** | Opens the **Backstage** view (below). |
| **Home** | Edit & view, *Graph vertices* (**+ Task**, type selector, **Catalog**), execution and governance. |
| **Insert** | Flow & concurrency (conditions, switch, MCP tool, subflows, fork/join, critical sections), GPU acceleration, cognitive agents. |
| **Agents** | Workspace security and skills, MCP servers and skill/MCP libraries. |
| **Connectors** | Import Pi projects, Claude, OpenAI, Hermes, Excel. |
| **Data** | *Project memory* and *Workspace storage*, kept visibly apart. |
| **Run** | Build & Run, the Taskflow engine, the Batch & Resume orchestrator, and Distribution (**Standalone Runner**). |

Other conveniences: the **Quick Access Toolbar** (Save, Undo, Redo, Run, and the **Git Sync** button with real-time status badge), an autosave switch, the
project search box (below), and `Ctrl+F1` to collapse the ribbon.

### Vertex contextual tab

Selecting a vertex adds a **contextual tab** with commands for that type (agent, decision, switch,
MCP, subflow, critical section, call, output, and so on). The ribbon returns to the last normal tab
when the vertex is deselected.

Commands write the fields that the C++ generator actually consumes: model/sandbox, memory,
`switchSettings`, `outputSettings`, `callSettings`, `customLambdaBody`, `criticalSectionId`, and
`parameters`. Edits participate in undo/redo and autosave. The Switch inserts new cases **before**
the `default`; critical sections generate one `tf::Semaphore` per key; and ribbon dialogs are native
to the Studio instead of relying on the WebView's `window.prompt`/`window.confirm`.

### Project search

The box at the top right searches the **open project**: every vertex (including those inside subflows and
modules), the skills of the workspace and the pipelines. Press `Ctrl+K` to focus it.

- Results are grouped as *Vertices*, *Skills*, *Pipelines* and *Commands*; `↑` `↓` choose, `Enter` opens, `Esc` closes.
- A vertex result selects the vertex, enters the subflows that contain it and centers the canvas on it.
  A skill result opens it read-only in the skill editor.
- It matches the label, the type, the C++ body, parameters, agent prompt and skill text, ignoring
  accents and plurals, tolerating typos and partial words, and knowing a few Portuguese/English synonyms
  (for example *decisão* finds a condition vertex, *banco* finds SQLite).
- The ranking is computed by the C++ backend with no AI model and no network. Autosave (about half a second
  after each edit) re-indexes only the files whose content hash changed; if the project hash is unchanged,
  nothing is redone. The index is cached in `.arestrix/search-index.json` inside the project.
- **Synonyms per language.** Portuguese and English ship built in. When you select any other language, the
  Studio asks its **default model** to write the synonym dictionary for that language in the background
  (see [Getting started](getting-started.md#studio-default-model)). The result is stored on your machine in
  `%APPDATA%\Arestrix\synonyms\<locale>.json` (or `~/.config/arestrix/synonyms/`), not in the project.
  Without a configured model nothing is generated and the search still works without that language's synonyms.
- Limitation: only the pipeline open on the canvas (`pipelines/main.json`) can be navigated. Results from
  other pipelines of the project are listed but cannot be opened yet.
- Semantic (embedding) search is planned as a later layer.

### Keyboard shortcuts

| Keys | Action |
| :--- | :--- |
| `F5` / `Ctrl+Enter` | Build and run |
| `Shift+F5` | Stop the run |
| `Ctrl+S` / `Ctrl+Shift+S` | Save / Save As |
| `Ctrl+Z`, `Ctrl+Y` | Undo, redo |
| `Ctrl+C`, `Ctrl+X`, `Ctrl+V`, `Ctrl+D` | Copy, cut, paste, duplicate the selected vertex |
| `Del` / `Backspace` | Delete the selected vertex or edge |
| `Esc` | Leave a subflow |
| `Ctrl+K` | Focus the project search |
| `Ctrl+F1` | Collapse or expand the ribbon |

## Backstage

Opened by **File**. It has the project information (four cards: the active project, Git repository synchronization, the physical
workspace, and the **Standalone Runner**), the **Properties** hub, the **Open** screen, the unified **Save** screen, and the **Options**
(workspace execution and governance shortcuts). Language, theme and the editable settings now live in **Properties**.

The **Open** screen follows Office's Open. Locations sit on the left and the file table uses
**Name / Date modified / Type / Size** columns, with a search box aligned to the right.

- **Recent**: two tabs, **Projects** (grouped by Today, Yesterday, Previous 7 days, Previous 30 days
  and Older) and **Graphs** (the pipelines of the current project).
- **This PC**: browses drives and folders. It shows subfolders, project folders (with
  `arestrix.project.json`), `.arestrix` packages and `.json` files. Clicking a project folder opens the
  project; the button next to it enters the folder without opening it.
- **Browse**: opens the system file picker.

![Office-style Open screen](../assets/screenshots/open-screen.webp)

An `.arestrix` package is extracted into a new folder next to it (`name`, `name (2)`, ...) and opened as a
project. A `.json` graph inside `<project>/pipelines` opens the whole project; a loose graph replaces the
current project's canvas only after confirmation.

The **Save** screen unifies quick saving and *Save As* in the Office style:

- On the left, you can **Save Current Project** directly in place or choose a destination via **This PC**,
  **Recent Locations**, or **Browse Folder**.
- On the right, a prominent scope selector at the top lets you choose between **Complete Project**
  (`.arestrix` package or structured project folder with `arestrix.project.json` + `pipelines/main.json`) and
  **Single Project Graph** (`.json` pipeline file). It provides full drive, folder, and breadcrumb navigation,
  quick shortcuts to the active project's root and `pipelines/` folder, and automatic registration in the
  project manifest when a graph is saved inside the open project.

## Properties

**File > Properties** opens the shared Office-style properties hub for the three scopes. Each scope
uses the same title bar, left navigation, content cards and Cancel/Save footer.

![Properties hub for Studio, Workspace and Project](../assets/screenshots/properties-hub.webp)

| Scope | Owns | Examples |
| :--- | :--- | :--- |
| **Studio** | Installation-wide preferences | Language, theme, canvas, default model, provider keys, AI Gateway, integrations, config-file reset |
| **Workspace** | The physical workspace of the active project | Sandbox default, audit/security policy, `.workspace` rebuild, restore defaults |
| **Project** | The active project manifest | Name, version, description, output, SQLite path, model/tools defaults, environment, Git/Runner summaries, pipelines |

### Studio Properties

Language, theme (Light / Dark / Follow system) and canvas are applied on save. The model and gateway
sections write only to the protected environment; provider keys are write-only status fields.

![Studio Properties](../assets/screenshots/studio-properties.webp)

![Studio Properties in dark mode](../assets/screenshots/studio-properties-dark.webp)

### Workspace Properties

The Workspace owns the default sandbox policy and audit settings. Rebuilding `.workspace` rewrites
the approved skills and MCPs while preserving the edited properties.

![Workspace sandbox properties](../assets/screenshots/workspace-properties.webp)

### Project Properties

The Project screen edits the manifest, model/tools defaults, sanitized environment references and
the SQLite path. Git and Runner remain specialist dialogs opened from their summary sections.

![Project Properties](../assets/screenshots/project-properties.webp)

![Project model and tools defaults](../assets/screenshots/project-defaults.webp)

### Explicit inheritance

Resolved values follow **Studio → Workspace → Project → Vertex**. The screen shows the current
origin, and saving a Project value creates an explicit Project override. Vertex dialogs keep their
own inheritance toggle.

![Vertex sandbox dialog using the shared Office shell](../assets/screenshots/vertex-sandbox.webp)

![Vertex model dialog using the shared Office shell](../assets/screenshots/vertex-model.webp)

## Assistant

The side panel has two tabs: **Properties** and **Assistant**. The Assistant is a chat with the Studio's
default AI (File → Properties > Studio default model) that helps you **operate** and **learn** the Studio:

- It answers questions by reading this documentation (in the Studio language).
- It creates projects and builds or changes graphs: adds, connects, changes, moves and removes vertices, validates the graph.
- It saves the project and runs the pipeline.

Graph edits show up on the canvas right away. Everything the Assistant does in one reply can be undone at
once with **Ctrl+Z**. Creating, opening or clearing a graph, saving and running ask for confirmation first
(**Allow** and **Decline** buttons in the chat).

Each conversation is a **session** saved in the project, under `.arestrix/assistant/sessions/`; the selector
at the top of the tab reopens earlier ones. The Assistant answers in the Studio language.

The Assistant needs a provider with **tool calling**. Providers without it (Jev and local model) are marked
"no Assistant support" in the default model selector, and the tab states that it cannot be used. The whole
conversation loop, the tools and session storage run in the C++ backend; the tab only draws the conversation
and applies the changes on the canvas.

## Canvas and Viewport

The workspace is organized into three integrated areas:
1. **Side Area:** Task Pane, Inspector, and Assistant with a freely resizable vertical splitter.
2. **Central Area (Diagram):** Infinite canvas powered by WebGL/Three.js with crisp SVG nodes and high-contrast vector typography.
3. **Bottom Area (Console):** Docked drawer for observability and deliverables with a horizontal splitter.

### Resizing and Zoom Invariance

The Viewport's orthographic projection operates on a 1:1 metric scale anchored strictly to physical pixel resolution:
- **Scale Preservation:** Resizing the side pane (horizontally) or the bottom console (vertically) **never changes diagram zoom**. The canvas merely expands or contracts its visible clipping window, without compressing or distorting graph elements.
- **Crisp Typography:** Strict prohibition of shadows, blurs, or glow effects (`text-shadow`, `drop-shadow`) on fonts, numbers, and labels. Text outlines remain 100% sharp across any display scale. Hovering ports and buttons displays contextual tooltips.
- **Live Run Feedback:** Clean badges and pills indicate `thinking`, active tool calls, `429 retry` rate limit retries, and step completion.
- **Canvas Appearance:** The floating controls include a background toggle that switches between the light canvas and a pure black canvas. The choice is stored locally and reapplied on startup. The floating controls are hidden while a pipeline is compiling or running and return when it finishes.

## Inspector

The right panel shows the selected vertex: name, type, C++ body and agent settings. With nothing selected it
shows two overview cards, one for the active project graph and one for the workspace environment.
Each action button carries its scope tag: `[Project]` for Model, Memory and Tools, `[Workspace]` for Sandbox,
`[Workspace / Project]` for Skills.

## Modals and Office 365 Tab Architecture

All Studio dialogs adhere to the **Microsoft Office 365 (Fluent 2)** corporate design system:
- **Floating Windows:** Draggable by title bar, resizable, and stackable with automated z-index and focus management.
- **Shared Modal Shell:** Every dialog uses the same neutral 46px title bar, elevated surface, SVG window controls, 10px corner radius and Arestrix mark instead of ad-hoc emoji icons.
- **Tabbed Structure:** Complex forms are partitioned into structured 34px-high tabs with a `#0f6cbd` active indicator.
- **State Preservation:** Switching between tabs keeps all entered form values intact without unmounting data.
- **Standardized Footer:** 58px action bar with Fluent primary buttons (`#0f6cbd`), cancel, and contextual help shortcuts.
- **Light and Dark Tokens:** Modal chrome, surfaces, text, borders, inputs, badges, callouts and footers share light/dark tokens. The global **Studio > Properties > General > Theme** switch applies `html[data-theme="dark"]` across the ribbon, task pane, console, Backstage and dialogs. The default remains light, and the canvas background is an independent preference.

| Modal | Office 365 Tabs | Scope |
| :--- | :--- | :--- |
| **Properties (Studio / Workspace / Project)** | Scope-specific categories with shared Office shell | Studio / Workspace / Project |
| **Node Library** | Template Catalog \| Pipeline Presets \| Import / Export Plugins | Studio |
| **Node Model** | Provider & Model \| Instructions & Skills \| Parameters | Per vertex |
| **Node Tools** | Native Tools \| MCP Servers \| Add from Library \| Validation & Batch | Per vertex |
| **Node Sandbox** | Isolation & Resources \| Network & Confinement \| Folders & Volumes | Per vertex |
| **Node Memory** | Blackboard \| Cognitive Lessons \| Regime & Scope | Per vertex |
| **Node Skills** | Enabled Skills \| Studio/Workspace Library \| Skill Editor | Per vertex |
| **Skill Editor** | Markdown Editor \| Rendered Preview \| Metadata & Testing | Per vertex / Workspace |
| **Libraries** | Repository sources (GitHub) \| Create Library | Studio / Workspace |
| **MCP Servers** | Active Servers \| Connect New Server \| Inspection & Logs | Workspace |
| **Project Manager** | Recent Projects \| New Project \| Settings summary (links to Properties) | Project |
| **Git Synchronization** | OS Identity & Account \| Remote Repository \| Automation & Governance | Project / OS |
| **Standalone Runner** | Progress \| Diagnostics \| Operations \| Export | Project / Distribution |
| **Batch Pipeline** | Input Data \| Concurrency & Bottlenecks \| Checkpoint Matrix | Run |
| **External Importers** | File / Connection \| Agent Mapping \| Options & Destination | Connectors |
| **Graphviz Export** | Graphic Preview \| DOT Code \| Export Options | Tools |

### Standalone Runner Modal

The **Standalone Runner** modal (opened via the **Run** tab > **Distribution** group or from **Backstage**) offers a high-contrast, clean execution environment independent of the full visual graph editor. It is organized into 4 tabs:

1. **Progress:**
   - **Status Badge:** Live status pill (`Waiting`, `Compiling`, `Running`, `Success`, `Error`, `Stopped`).
   - **Linear Progress Bar & Elapsed Timer:** Continuous percentage progress tracking with real-time millisecond elapsed timer.
   - **Step Timeline:** Sequence of pipeline stages showing individual vertex duration, execution status, and timestamp.
2. **Diagnostics:**
   - **Telemetry KPIs:** Real-time KPI cards for token counts (Prompt, Completion, Total), tool invocations, 429 rate limit retries, and average latency.
   - **Thinking Stream:** Live streaming view of the cognitive deliberations and reasoning emitted by AI models.
   - **Tool Call Inspection:** Detailed table of invoked tools, input arguments, execution time, and output status.
   - **Export Diagnostics:** Copies structured JSON logs and metrics to clipboard.
3. **Operations:**
   - **Action Controls:** Buttons to Start (`F5`), Stop (`Shift+F5`), and Restart workflow execution.
   - **Dynamic Parameters Form:** Form fields generated automatically from the project's parameter schema, allowing runtime tweaks without touching the graph.
   - **Artifacts Access:** Direct button to open the deliverables directory (`exports/`) in Windows Explorer.
4. **Export:**
   - Output directory selector (defaults to `dist/runner`).
   - One-click native compilation and packaging with `runner_manifest.json` and `run.bat`.
   - Automated **Zero-Secrets** fail-closed audit preventing any embedded keys or sensitive files.

## Console and Artifacts Drawer

A docked drawer at the bottom of the screen (resizable with a splitter and collapsible) centralizes observability and pipeline deliverables. Its tab strip follows the **Microsoft Office / Excel** corporate design:
- **Fluent Tab Strip:** Connected 34px-high tabs, 'Segoe UI' 12px / 600 typography, `#ffffff` active background, and `#0f6cbd` blue accent line.
- **Vector Icons and Sharp Badges:** Clean SVG iconography without loose emojis or text shadows; high-contrast artifact count badge and compact actions for copy, clear, and minimize/maximize.
- **Stable Collapse State:** Running a pipeline never expands a collapsed console; open it explicitly when you need the logs.
- **Three Specialized Tabs:**
  1. **MSVC C++20 Terminal:** Displays generated C++20 source code, native compiler invocations, and raw execution logs from the Taskflow engine.
  2. **Real-Time Telemetry:** Monitors cognitive agent execution steps (thinking reasoning progress, tool calls, parameters, and 429 rate limit retries).
  3. **Artifacts (Deliverables):** Dedicated multi-format panel for inspecting and exporting pipeline deliverables:
   - **Vector SVG:** Interactive native vector preview with pan/zoom controls and one-click toggle between graphic scene and clean XML/SVG source code.
   - **Spreadsheets & Tabular Data (CSV/TSV):** Paginated data grid with instant cell search and raw data inspection.
   - **Technical Documents & Markdown:** Formatted reader with code block syntax highlighting and one-click copy.
   - **PDFs & JSONs:** Clean structured display with a button to launch the system's native viewer.
   - **Quick Delivery Actions:** **"In Explorer"** button to open the `exports/` folder in Windows Explorer, **"Copy"**, and **"Download"** for direct local saving.

## Language

Choose the language in **File → Properties → Studio → General**: Portuguese (Brazil) or English
(US). To add another, see [Development](development.md#adding-a-language).

## Related

- [Graph and nodes](graph-and-nodes.md)
- [Workspaces, memory, skills and MCP](workspaces-memory-skills-mcp.md)
