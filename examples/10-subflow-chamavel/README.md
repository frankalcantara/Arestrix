# Example 10 — Callable subflow

A coordinator calls a **subflow** declared in the graph. The subflow has its own nested graph
(`childGraph`) and runs natively inside the same executable.

## Flow

```text
Coordenador_Subflow ──(flow)──► Entrega_da_Resposta
        │ call
        ▼
Subflow_Especialista (childGraph)
  Especialista_Interno ──(flow)──► Resultado_do_Subflow
```

1. The coordinator calls `subflow_especialista` through a dashed call edge.
2. The compiler emits the child graph as a native function; the runtime registers the subflow and
   passes the call input as `inputContext` to the child agents/interactions.
3. The child graph writes its own artifact and returns its output text.
4. The coordinator writes the returned text to `workspace/resposta.txt`.
5. The parent output delivers `exports/resposta.txt`; the child also leaves
   `exports/subflow_result.txt`.

The subflow target is callable-only (no flow edges) and the call graph remains closed. Nested call
edges and nested subflows are rejected in v1.
