# Exemplo 8 — Formulário gerado

O formulário não é escrito campo a campo. O vértice de **interação** pede a um modelo que gere um
formulário seguro a partir do contexto do grafo e de um artefato de runtime.

## Fluxo

```text
Preparar_Dados ──(fluxo)──► Formulario_Gerado ──(fluxo)──► Entrega_da_Resposta
```

1. O vértice estático grava `workspace/dados.md`.
2. A interação envia o contexto e o conteúdo do artefato ao modelo gerador.
3. O runtime valida o schema devolvido (tipos, ids, rótulos, opções, limites). Se for inválido,
   aplica o fallback configurado (`fail` ou `free_text`).
4. O formulário seguro é exibido/respondido; `.arestrix/out/pergunta.txt` e `.json` são gravados.
5. A auditoria em `.arestrix/runs/<runId>/interactions/<requestId>.json` registra o formulário gerado
   e as respostas. O vértice de saída entrega `exports/resposta.txt`.

## Executar

```bash
arestrix-cli run pipelines/main.json --root . --answers answers.json
```

O modelo devolve um schema com `tipo` (escolha) e `obs` (textarea), compatível com `answers.json`.
No Studio, o mesmo schema gerado abre como modal de formulário.
