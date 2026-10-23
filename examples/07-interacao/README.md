# Example 7 — Human interaction

A human **interaction** vertex in the flow. In the Studio it opens a safe form modal; in CI/headless
runs the answers come from `--answers`.

## Flow

```text
Inicio ──(flow)──► Pergunta_ao_Usuario ──(flow)──► Entrega_da_Resposta
```

1. The interaction runtime loads the form from the vertex settings.
2. In headless mode it reads `answers.json` (keyed by the interaction node id).
3. It validates the required fields and choices, writes `.arestrix/out/pergunta.txt` and `.json`.
4. It records the request/answer at `.arestrix/runs/<runId>/interactions/<requestId>.json`.
5. The output vertex copies the answer to `exports/resposta.txt`.

## Run

```bash
arestrix-cli run pipelines/main.json --root . --answers answers.json
```

Without `--answers` the pipeline fails closed with a clear message. In the Studio, run the same graph
and the interaction opens as a safe form modal; the headless channel is what this example scripts.

## Answer file

```json
{
  "pergunta": { "tipo": "pj", "obs": "Cliente empresarial" }
}
```

A value may also be an array, so repeated interactions can consume answers in order.
