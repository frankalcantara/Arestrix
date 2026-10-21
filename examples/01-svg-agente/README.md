# Example 1 — SVG agent (the tutorial)

[Português](README.pt-BR.md) · [All examples](../README.md)

The project of the [SVG agent tutorial](../../docs/pt-BR/tutorials/svg-agent.md) (in Portuguese): an agent that creates an animated
SVG and another that audits it.

```
Generator (agent) ─┬─► Typography linter ─┐
                   └─► SMIL timeline audit ┴─► Join ─► Auditor (agent) ─► Decision (C++) ─┬─► Delivery
        ▲                                                                               │
        └───────────────────────── feedback loop (rejected) ◄──────────────────────────┘
```

| Vertex | Type | Does |
| :--- | :--- | :--- |
| Agente_Gerador | agent | Writes `workspace/scene.svg` following `skills/gerador.md`. |
| Linter / Auditor SMIL | static (C++) | Two checks that run **in parallel**. |
| Consolidador | static | Joins both reports. |
| Agente_Auditor | agent | Reads the reports and the SVG, writes `workspace/audit_decision.txt` (`skills/jev.md`). |
| Decisor_Qualidade | condition (C++) | `STATUS: APROVADO` → delivery; anything else → back to the generator. |
| Entrega_SVG_Final | output | Copies `workspace/scene.svg` to `exports/scene.svg` and registers the artifact in the **Artifacts** tab. |

**Needs:** a model and its API key (see [how to run](../README.md#run-an-example)). Both agents inherit the project's model
(DeepSeek `deepseek-flash` by default). To use a different model for the auditor, open the vertex, press **Model**, uncheck
*inherit* and choose another provider (the tutorial used a visual-evaluation model for this step).

**Bring your own agents:** edit `skills/gerador.md` and `skills/jev.md`, or change the C++ of the two checks. Each agent runs at
most 10 times (`ARESTRIX_MAX_AGENT_RUNS`), so a loop that never converges stops instead of spending without limit.
