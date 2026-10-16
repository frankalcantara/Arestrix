# Interface tour

[Português](../pt-BR/06-interface.md) · [Index](../../README.md#documentation)

The Studio follows the Microsoft Office layout, because most people already know it.

## Ribbon

| Tab | What is there |
| :--- | :--- |
| **File** | Opens the **Backstage** view (below). |
| **Home** | Edit & view, *Graph vertices* (**+ Task**, type selector, **Catalog**), execution and governance. |
| **Insert** | Flow & concurrency (conditions, switch, MCP tool, subflows, fork/join, critical sections), GPU acceleration, cognitive agents. |
| **Agents** | Workspace security and skills, MCP servers, the global GitHub skill catalog. |
| **Connectors** | Import Pi projects, Claude, OpenAI, Hermes, Excel. |
| **Data** | *Project memory* and *Workspace storage*, kept visibly apart. |
| **Run** | Build & Run, the Taskflow engine, and the Batch & Resume orchestrator. |

Other conveniences: the **Quick Access Toolbar** (Save, Undo, Redo, Run), an autosave switch, the
project search box (below), and `Ctrl+F1` to collapse the ribbon.

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
  (see [Getting started](02-getting-started.md#studio-default-model)). The result is stored on your machine in
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

Opened by **File**. It has the project information (two cards: the active project and the physical
workspace), the **Open** screen, *Save As* with folder and drive navigation, and the **Options**
(Studio defaults, workspace governance and the **language** selector).

The **Open** screen follows Office's Open. The places are on the left:

- **Recent**: a search box and two tabs, **Projects** (grouped by Today, Yesterday, Previous 7 days,
  Previous 30 days and Older) and **Graphs** (the pipelines of the current project).
- **This PC**: browses drives and folders. It shows subfolders, project folders (with
  `arestrix.project.json`), `.arestrix` packages and `.json` files. Clicking a project folder opens the
  project; the button next to it enters the folder without opening it.
- **Browse**: opens the system file picker.

An `.arestrix` package is extracted into a new folder next to it (`name`, `name (2)`, ...) and opened as a
project. A `.json` graph inside `<project>/pipelines` opens the whole project; a loose graph replaces the
current project's canvas only after confirmation.

## Canvas

The canvas is infinite. The cards are SVG, drawn over the WebGL scene, with sharp vector text and no
text shadows. When you zoom out the cards drop detail. Hovering ports and buttons shows tooltips.

Each card shows a header with the type icon and color, the title, ports, and, when selected, the
quick action bar. During a run, badges show `thinking`, tool calls, `429 retry` and completion.

## Inspector

The right panel shows the selected vertex: name, type, C++ body and agent settings. With nothing selected it
shows two overview cards, one for the active project graph and one for the workspace environment.
Each action button carries its scope tag: `[Project]` for Model, Memory and Tools, `[Workspace]` for Sandbox,
`[Workspace / Project]` for Skills.

## Modals

Modals are floating windows: draggable by the title bar, resizable, and stackable.

| Modal | Scope |
| :--- | :--- |
| Global defaults | Studio keys, Workspace sandbox and tools, Project model and memory |
| Node library | Templates, preset, import/export |
| Sandbox, Model, Tools, Memory, Skills | Per vertex |
| Skill editor | Markdown editor with preview |
| GitHub repositories | Studio-wide skill catalog |
| MCP servers | Workspace |
| Project manager | Project metadata, workspace path, project `.env` |
| Batch & Resume | Run |

## Console

A docked drawer at the bottom shows build output and the live execution log. The status bar below it
shows the graph and project state.

## Language

Choose the language in **Backstage → Options**: Portuguese (Brazil) or English (US). To add another,
see [Development](07-development.md#adding-a-language).

## Related

- [Graph and nodes](03-graph-and-nodes.md)
- [Workspaces, memory, skills and MCP](05-workspaces-memory-skills-mcp.md)
