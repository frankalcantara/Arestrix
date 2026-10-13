# Example 3 — Illustrated support (a project of projects)

[Português](README.pt-BR.md) · [All examples](../README.md)

Example 2 and example 1 working together: the support triage runs as before, and on the **FAQ branch** the written answer
is handed to the SVG agent, which draws an animated 3-step explanation and has it audited.

```
Receive ─► Reader ─► Triage ─┬─ urgente ─► Escalator (MCP) ─► Delivery
 (example 2)                 ├─ normal ──► Writer ⇄ Reviewer ⇄ Quality ─► Delivery
                             ├─ duvida ──► FAQ ─► [ example 1: Generator ─► checks ─► Auditor ─► Decision ] ─► Delivery (answer + SVG)
                             └─ revisao_humana ─► Human review
```

- Vertices from example 2 are prefixed `atd_`, those of example 1 `svg_`; the only new edge is **FAQ → SVG generator**, and the
  generator receives a different prompt ("explain `workspace/resposta.md` in 3 animated steps").
- The pipeline is **generated** from the two sources (`python scripts/build_examples.py`). Nested subflows are not executed yet,
  so the two flows are merged into one graph instead of being embedded.
- Everything from examples 1 and 2 applies: same model inheritance, same MCP server, same `samples/` tickets.

**Needs:** a model + its API key, and Python. Try `ARESTRIX_EXAMPLE_TICKET=chamado_duvida.txt` to see both flows chained, then
`chamado_urgente.txt` to see that the SVG flow stays idle on the other branches.
