# Example 9 — Parallel calls

A coordinator asks two declared specialists **in the same model turn**. The runtime starts both
`call_agent` invocations concurrently and returns the results in the requested order.

## Flow

```text
Coordenador_Paralelo ──(flow)──► Entrega_da_Resposta
        │ call                         ▲
        ├────────► Especialista_A ─────┘ (no flow edge)
        └────────► Especialista_B
```

1. The coordinator skill asks for `especialista_a` and `especialista_b` in one turn.
2. The Agent loop runs all `call_agent` calls in that turn in parallel; each call keeps its declared
   limits and call depth.
3. The coordinator writes both opinions to `workspace/resposta.txt`.
4. The output vertex delivers `exports/resposta.txt`.

The call graph is still closed: only the two dashed targets are available. A mixed turn containing
non-call tools is executed sequentially to preserve side-effect order. Set
`ARESTRIX_PARALLEL_CALLS=0` to force sequential calls.
