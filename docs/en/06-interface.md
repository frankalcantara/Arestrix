# Interface tour

[Português](../pt-BR/06-interface.md) · [Index](../../README.md#documentation)

The Studio follows the Microsoft Office layout, because most people already know it.

## Ribbon

| Tab | What is there |
| :--- | :--- |
| **File** | Opens the **Backstage** view (below). |
| **Home** | Edit & view, *Graph vertices* (**+ Task**, type selector, **Catalog**), execution and governance. |
| **Insert** | Flow & concurrency (conditions, subflows, fork/join, critical sections), GPU acceleration, cognitive agents. |
| **Agents** | Workspace security and skills, MCP servers, the global GitHub skill catalog. |
| **Connectors** | Import Pi projects, Claude, OpenAI, Hermes, Excel. |
| **Data** | *Project memory* and *Workspace storage*, kept visibly apart. |
| **Run** | Build & Run, the Taskflow engine, and the Batch & Resume orchestrator. |

Other conveniences: the **Quick Access Toolbar** (Save, Undo, Redo, Run), an autosave switch, the
*Tell me what you want to do* search box, and `Ctrl+F1` to collapse the ribbon.

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
| `Ctrl+F1` | Collapse or expand the ribbon |

## Backstage

Opened by **File**. It has the project information (two cards: the active project and the physical
workspace), the open/recent screen, *Save As* with folder and drive navigation, and the **Options**
(Studio defaults, workspace governance and the **language** selector).

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
