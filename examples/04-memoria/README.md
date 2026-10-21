# Example 4 — Project memory (lessons and Blackboard)

[Português](README.pt-BR.md) · [All examples](../README.md)

This example shows project memory working end to end:

1. `Semear_Licao` writes a **semantic lesson** and a **Blackboard** value.
2. `Agente_Com_Memoria` receives both in its system prompt, filtered by domain and allowed keys.
3. The agent's answer becomes an artifact at `exports/resposta.md`.
4. On the next run, the lesson and Blackboard value are loaded from disk, even without the seed node.

```
Semear_Licao (C++) ─► Agente_Com_Memoria (agent) ─► Entrega_Resposta (output)
       │                        ▲
       │                        └── memory injected into the prompt
       └── writes lesson + Blackboard summary
```

| Vertex | What it does |
| :--- | :--- |
| `Semear_Licao` | Records the lesson `A resposta deve citar a regra aprendida` in the `memoria` domain and writes `resumo` to the Blackboard. |
| `Agente_Com_Memoria` | Reads `semanticLessons` and `readBlackboard`, with `domains: ["memoria"]` and `allowedKeys: ["resumo"]`. |
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

- the first run creates the lesson and the Blackboard value;
- the second run loads them from `.arestrix/memory/memory.db` and injects them again. The seed node
  deduplicates the lesson, but the load happens before the graph starts.

The database lives at `.arestrix/memory/memory.db` and is ignored by Git. The `memory_usage` table
records input/output tokens, calls and memory context size for each agent.

## What to look for

- Console: `[MEMORIA] contexto injetado em Agente_Com_Memoria`.
- Agent prompt: the `[DADOS DE MEMORIA DO PROJETO ...]` block with the lesson and `resumo`.
- A lesson from another domain and a non-authorized key do **not** appear.
- The output vertex creates `exports/resposta.md`.

## Test without spending anything

```bash
python scripts/e2e_memory.py
```

The test runs two graphs in the same project: the first seeds, the second loads from the database.
It checks prompt injection, filters, API usage and persistence.

## Measure the effect on API consumption

The runtime records tokens, calls, latency and memory context size in `memory_usage`. To compare
with and without memory:

```bash
# memory off, metrics on
ARESTRIX_MEMORY_METRICS=1 arestrix-cli run pipelines/main.json --root <project>

# memory on (example configuration)
arestrix-cli run pipelines/main.json --root <project>

python scripts/memory_report.py <project>
```

## Limits of this phase

- The lesson is recorded by an explicit C++ node. The runtime already records agent failures
  automatically; a lesson derived from an audit failure is still explicit in the node that
  interprets the audit decision.
- The memory brief is bounded by `contextBudgetTokens` (400 in this example); when needed, the
  agent uses `memory_search` to fetch details.
- Storage is SQLite per project, with a simple temporal multigraph. Workspace/institution scopes
  and recursive learning arrive in the next phases; context budgeting is already enforced by
  `contextBudgetTokens`.

**Needs:** a model and its API key. Python is not required.
