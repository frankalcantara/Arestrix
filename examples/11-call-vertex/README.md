# Example 11 — Call vertex

A **Call** vertex executes a declared callable target **inside the flow** and hands the answer to
the next vertex.

## Flow

```text
Preparador_da_Chamada ──(flow)──► Chamada_em_Fluxo ──(flow)──► Entrega_da_Resposta
                                         │ call
                                         ▼
                                  Especialista_Chamado
```

1. The preparer agent writes its final text to `.arestrix/out/preparador.txt`.
2. The Call vertex reads that file as its input (`source: previous`) and invokes the target
   declared by its dashed call edge.
3. The target is callable-only: it has no flow edges and runs inside the Call vertex.
4. The Call vertex writes the answer to `.arestrix/out/chamada.txt`.
5. The output vertex exports it to `exports/resposta.txt`.

The call graph is still closed: the Call vertex can only invoke targets declared by its call edge,
and the target cannot be scheduled by the flow.

## Notes

- Set a fixed input in the Inspector to skip gathering from the previous vertex.
- `source: file` reads a project file; `source: none` sends an empty payload.
- A Call vertex needs exactly one outgoing call edge.
