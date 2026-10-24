# Example 12 — Asynchronous calls

A coordinator starts a call with `async: true`, receives a **pending** `callId`, keeps working and
checks the result with `call_result`.

## Flow

```text
Coordenador_Assincrono ──(flow)──► Entrega_Assincrona
        │ call (allowAsync)
        ▼
Especialista_Assincrono
```

1. `call_agent` with `async: true` starts the target in the background and returns
   `{"status":"pending","callId":"..."}`.
2. The coordinator asks `call_result` for that `callId`.
3. While the target is running, `call_result` returns `pending`; when it finishes, it returns the same
   result JSON as a synchronous call.
4. The coordinator writes the answer to `workspace/resposta.txt` and the output vertex exports it.

The call edge must explicitly set **Allow asynchronous call**. This keeps async opt-in per target.
Synchronous calls remain the default, and `ARESTRIX_ASYNC_CALLS=0` disables async globally.

The runtime waits for pending calls before the process exits, so completion notifications and audit
data are not lost.
