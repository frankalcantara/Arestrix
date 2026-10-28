# Paths and junction

> **Applies to:** Arestrix Beta 2.3 (`2.3.0-beta.2`)

A graph can have more than one execution path. When two paths share no vertex, the Studio treats them
as **independent paths**: they start together, run in parallel, and the pipeline only finishes when
the last one does. This page covers how that shows up in the Studio, the write-conflict warning and
the two ways to propose a junction.

## What a path is

A path starts at a vertex **with no incoming flow** and takes everything it reaches through flow
edges. The projection is the same one used by the C++ generator:

- **call** edges are delegation, not precedence — they create no path and order nothing;
- **feedback** edges close a loop and do not count as an entry;
- a **pure call target** never becomes a flow task and stays out of the paths.

In the generated code every entry is wired to `Pipeline_Bootstrap`; there is no “start vertex” to
draw.

## What the Studio shows

| Signal | Where | When it appears |
| :--- | :--- | :--- |
| **START** / **END** badges | Outside the card, at header height | Only when the graph has more than one entry **or** more than one exit |
| Counter `6 vertices, 4 connections · 2 paths` | Status bar | Always |
| `[RUN] N trilhas independentes: …` | Console, before compiling | Only with more than one path |
| `concurrency=` in the `[TRACE]` summary | Console, at the end | Whenever the trace carries timings |

With a single entry and a single exit the drawing is already unambiguous and no badge appears — a
fork with a join included.

The **Paths** button in the status bar colors each path with a stripe at the bottom of the cards and
tints the matching **START** badge, tying the color to the entry. It is read-only: it changes neither
the topology nor execution.

## Write conflict

Two vertices **with no flow path between them** may run at the same time. If both declare the same
output file — `destinationPath` of an `output` vertex or `output.file` of an `mcp` vertex — the last
write wins with no runtime notice. The Studio turns that into a `resource_conflict` warning, shown by
**Validate DAG** and in the Console before each run.

- **Never blocks**: `isValid` still counts only errors.
- **Only declared settings**: free-form C++ bodies are opaque on purpose.
- **Ordered pairs stay quiet**: if a flow path exists between them, one runs after the other.
- The Assistant validator (`validate_graph`) runs the same check with the same path normalization.

## Merging paths

### With the Assistant

The context menu of a vertex with an **END** badge offers **Merge paths with the Assistant…**. The
Studio enumerates the paths and sends the model: labels, types, active skill, MCP server, declared
artifacts and the **first comment you wrote** in the C++ body (template boilerplate that repeats the
label is dropped). The request tells the model to:

1. decide semantically whether the paths should converge and where;
2. create **one** `join` vertex at the best point, connecting the output of each path's last vertex;
3. neither remove nor rewire existing connections;
4. leave the graph untouched if the paths are independent on purpose.

Every operation in that turn becomes **a single Ctrl+Z step**. Inside a subflow the request carries
the `parent_path` and the Assistant edits the right subgraph; the Studio moves up to the root before
applying, because the operations arrive with an absolute path.

Without a configured model (or without tool support) the action changes nothing and explains why.

### Local analysis

**Suggest a junction (local analysis)** is the no-AI alternative. It scores declared signals:

| Signal | Weight |
| :--- | :--- |
| Same delivery folder | +2 |
| Same artifact type (declared or by extension) | +2 |
| Same active skill | +2 |
| Same MCP server | +2 |
| Term shared by the names (accent-insensitive, stop words removed) | +1 |
| All folders **and** types distinct | −2 |

With **2 points or more**, the junction is created after the last vertex of each path — the only
position that does not rewrite connections — and the notification names the signals that counted.
Below that, nothing changes and the notification explains why.

## Known limits

- The automatic junction is always **after** the paths: consolidating *before* the deliveries would
  require rewiring edges and is a manual decision (the Assistant can do it when you ask).
- The local analysis is a heuristic: two artifacts of the same type in different folders may still be
  independent pipelines. It is a suggestion, not a verdict.
- Conflicts inside a subflow's `childGraph` are not part of the root graph validation.

## Runnable example

[`examples/13-trilhas-independentes`](../../examples/13-trilhas-independentes) has two paths of three
vertices each, no model required, and a walkthrough to observe badges, counter, colors and both ways
of merging.

## Related

- [Graph and nodes](graph-and-nodes.md)
- [Interface](interface.md)
- [Examples](../../examples/README.md)
