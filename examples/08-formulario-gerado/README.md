# Example 8 — Generated form

The form is not authored field by field. The **interaction** vertex asks a model to generate a
safe form from the graph context and a runtime artifact.

## Flow

```text
Preparar_Dados ──(flow)──► Formulario_Gerado ──(flow)──► Entrega_da_Resposta
```

1. The static vertex writes `workspace/dados.md`.
2. The interaction sends the context and artifact content to the generator model.
3. The runtime validates the returned schema (types, ids, labels, options, limits). If it is
   invalid, the configured fallback (`fail` or `free_text`) applies.
4. The safe form is shown/answered; `.arestrix/out/pergunta.txt` and `.json` are written.
5. The audit at `.arestrix/runs/<runId>/interactions/<requestId>.json` records the generated form
   and the answers. The output vertex delivers `exports/resposta.txt`.

## Run

```bash
arestrix-cli run pipelines/main.json --root . --answers answers.json
```

The model returns a schema with `tipo` (choice) and `obs` (textarea), matching `answers.json`.
In the Studio, the same generated schema opens as the form modal.
