# Example 13 - Independent paths

Two chains that share no vertex. The Studio badges each one with **START** and **END**, counts two
paths and tells the Console they run in parallel.

## Flow

```text
Levantar fontes → Resumir fontes → Entregar resumo das fontes
Levantar anexos → Resumir anexos → Entregar resumo dos anexos
```

1. Each path writes its own file under `workspace/` and delivers under `exports/`.
2. With no edge between the paths, the generator wires both entries to `Pipeline_Bootstrap`: they
   start together, run in parallel and the pipeline finishes when the last one does.
3. The Console prints `[RUN] 2 trilhas independentes: Levantar fontes, Levantar anexos…` before
   compiling, and the final `[TRACE]` line reports `concurrency=2`.

## What to look for in the Studio

- **Badges**: `INÍCIO` on the two first vertices and `FIM` on both deliveries (the labels follow the
  UI language).
- **Status bar**: `6 vértices, 4 conexões · 2 trilhas`.
- **Paths button**: colors each path and tints its `INÍCIO` badge.
- **Context menu on an END vertex → Sugerir junção (análise local)**: both deliveries share the folder
  and the format, so the Studio proposes a junction and explains the criterion. One Ctrl+Z undoes it.
- **Unir trilhas com o Assistente…**: the same decision, made semantically by a model (needs a
  configured AI).

## Write conflict

The two deliveries write different files, so there is no warning. Point both to the same
`destinationPath` and **Validate DAG** starts showing a `resource_conflict` warning (it never blocks
execution): the paths may write at the same time and the last write wins.

## Run it

No model needed: the vertices are static C++ tasks.

```bash
python scripts/copy_example.py 13-trilhas-independentes C:/my-projects/trails
```

In the Studio: **File → Open** the copied folder and press **F5**. Without the Studio:
`arestrix-cli run pipelines/main.json --root <the copied folder>`.

See also [`docs/en/paths-and-junction.md`](../../docs/en/paths-and-junction.md).
