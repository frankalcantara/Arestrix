# Example 4 — Project memory (proposal, approval and consultation)

[Português](README.pt-BR.md) · [All examples](../README.md)

This example shows the current project-memory cycle:

1. `Semear_Licao` proposes a **lesson**; nothing becomes active on its own.
2. Open **Project Memory**, review and approve the pending proposal.
3. `Agente_Com_Memoria` receives the approved lesson in its system prompt, filtered by theme.
4. The agent's answer becomes an artifact at `exports/resposta.md`.
5. On the next run, the lesson is loaded from disk, even without the seed node.

```
Semear_Licao (C++) ─► Agente_Com_Memoria (agent) ─► Entrega_Resposta (output)
       │                        ▲
       │                        └── memory injected into the prompt
       └── proposes a lesson for approval
```

| Vertex | What it does |
| :--- | :--- |
| `Semear_Licao` | Proposes the lesson `A resposta deve citar a regra aprendida` in the `memoria` domain. Approve it in **Project Memory** before the next run. |
| `Agente_Com_Memoria` | Runs in `learn` mode with theme `memoria`; after approval, the lesson enters the brief. |
| `Entrega_Resposta` | Copies `workspace/resposta.txt` to `exports/resposta.md`. |

## Run it

```bash
python scripts/copy_example.py 04-memoria C:/my-projects/memory
```

Open the folder in the Studio, choose a model and press **F5**. Without the Studio:

```bash
arestrix-cli run pipelines/main.json --root <the copied folder>
```

Run it **twice**:

- the first run creates a **pending lesson proposal**;
- open **Project Memory**, review the proposal and approve it;
- the second run loads the approved lesson from `.arestrix/memory/memory.db` and injects it at the
  start of the agent node.

Knowledge lives at `.arestrix/memory/memory.db`; telemetry lives at
`.arestrix/telemetry/telemetry.db`. Both are ignored by Git. Telemetry records input/output tokens,
calls and memory context size for each agent.

## What to look for

- Console: `[MEMORIA] contexto injetado em Agente_Com_Memoria`.
- Agent prompt: the `[DADOS DE MEMORIA DO PROJETO ...]` block with the approved lesson.
- A lesson from another theme does **not** appear.
- The output vertex creates `exports/resposta.md`.

## Test without spending anything

```bash
python scripts/e2e_memory.py
```

The test runs two graphs in the same project: the first seeds, the second loads from the database.
It checks prompt injection, filters, API usage and persistence.

## Measure the effect on API consumption

The runtime records tokens, calls, retries, success/failure, context and injected/used memories. In
the Studio, use **Global Defaults → Memory → Measurement / A-B**: run with **Normal** and then with
**A/B: memory OFF** + **Record metrics**, on the same graph. For automation:

```bash
# memory on, metrics on
ARESTRIX_MEMORY_METRICS=1 arestrix-cli run pipelines/main.json --root <project>

# memory OFF on the same graph
ARESTRIX_MEMORY_METRICS=1 ARESTRIX_MEMORY_DISABLE=1 \
  arestrix-cli run pipelines/main.json --root <project>

# native report
arestrix-cli memory report --project <project>
```

## Limits of this phase

- The lesson is recorded by an explicit C++ node. The runtime already records agent failures
  automatically; a lesson derived from an audit failure is still explicit in the node that
  interprets the audit decision.
- The memory brief is bounded by `contextBudgetTokens` (400 in this example); when needed, the
  agent uses `memory_search` to fetch details.
- Storage is SQLite per project, with a simple temporal multigraph. There is no semantic
  extraction, embeddings, dedup, contradiction or playbooks yet; those items and
  workspace/institution scopes arrive in the next phases. Context budgeting is already enforced by
  `contextBudgetTokens`.

**Needs:** a model and its API key. Python is not required.
