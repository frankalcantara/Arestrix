# Exemplo 7 — Interação humana

Um vértice de **interação** humana no fluxo. No Studio ele abre um modal de formulário seguro; em
execuções CI/headless as respostas vêm de `--answers`.

## Fluxo

```text
Inicio ──(fluxo)──► Pergunta_ao_Usuario ──(fluxo)──► Entrega_da_Resposta
```

1. O runtime de interação carrega o formulário das configurações do vértice.
2. Em modo headless ele lê `answers.json` (chaveado pelo id do vértice de interação).
3. Valida os campos obrigatórios e as escolhas, grava `.arestrix/out/pergunta.txt` e `.json`.
4. Registra a auditoria em `.arestrix/runs/<runId>/interactions/<requestId>.json`.
5. O vértice de saída copia a resposta para `exports/resposta.txt`.

## Executar

```bash
arestrix-cli run pipelines/main.json --root . --answers answers.json
```

Sem `--answers` o pipeline falha fechado com uma mensagem clara. No Studio, execute o mesmo grafo e
a interação abre como modal de formulário seguro; o canal headless é o que este exemplo roteiriza.

## Arquivo de respostas

```json
{
  "pergunta": { "tipo": "pj", "obs": "Cliente empresarial" }
}
```

Um valor também pode ser uma lista, para interações repetidas consumirem as respostas em ordem.
