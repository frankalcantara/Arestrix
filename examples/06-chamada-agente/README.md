# Example 6 — Agent-to-agent call

A coordinator agent declares a **call** to a specialist agent and receives the specialist's answer
synchronously. The call is a dashed edge in the graph; the specialist is a callable vertex, not a
scheduled step.

## Flow

```text
Coordenador_de_Chamadas ──(flow)──► Entrega_da_Resposta
        │
        └──(call, max 2)──► Especialista_de_Chamadas
```

1. The coordinator runs its tool loop.
2. The runtime exposes `call_agent` with only the declared target (`especialista`).
3. The coordinator calls it with a short input.
4. The runtime executes the specialist synchronously and returns a JSON result.
5. The coordinator writes the result to `workspace/resposta.txt`.
6. The output vertex copies it to `exports/resposta.txt`.

## Run

```bash
arestrix-cli run pipelines/main.json --root .
```

Or open the folder in the Studio, choose a model, and press **F5**. The example uses the project's
global model (DeepSeek by default); no key is stored in the project.

## What to look for

- The dashed violet edge between the two agents.
- `call_agent` in the coordinator's tool list; the specialist does **not** receive it (its own call
  table is empty).
- The call result in `workspace/resposta.txt` with `"status":"ok"`.
- `ARESTRIX_MAX_CALL_DEPTH` (default 3) and `ARESTRIX_MAX_TOTAL_CALLS` (default 50) bound nested
  calls; `maxCalls` on the edge bounds repeated calls by the same caller.
