# Exemplo 6 — Chamada entre agentes

Um agente coordenador declara uma **chamada** a um agente especialista e recebe a resposta de forma
síncrona. A chamada é uma aresta tracejada no grafo; o especialista é um vértice chamável, não uma
etapa agendada.

## Fluxo

```text
Coordenador_de_Chamadas ──(fluxo)──► Entrega_da_Resposta
        │
        └──(chamada, máx. 2)──► Especialista_de_Chamadas
```

1. O coordenador executa seu loop de ferramentas.
2. O runtime expõe `call_agent` apenas com o alvo declarado (`especialista`).
3. O coordenador chama com um input curto.
4. O runtime executa o especialista de forma síncrona e devolve um resultado JSON.
5. O coordenador grava o resultado em `workspace/resposta.txt`.
6. O vértice de saída copia para `exports/resposta.txt`.

## Executar

```bash
arestrix-cli run pipelines/main.json --root .
```

Ou abra a pasta no Studio, escolha um modelo e pressione **F5**. O exemplo usa o modelo global do
projeto (DeepSeek por padrão); nenhuma chave fica no projeto.

## O que observar

- A aresta tracejada violeta entre os dois agentes.
- `call_agent` na lista de ferramentas do coordenador; o especialista **não** a recebe (a tabela de
  chamadas dele é vazia).
- O resultado da chamada em `workspace/resposta.txt`, com `"status":"ok"`.
- `ARESTRIX_MAX_CALL_DEPTH` (padrão 3) e `ARESTRIX_MAX_TOTAL_CALLS` (padrão 50) limitam chamadas
  aninhadas; `maxCalls` na aresta limita chamadas repetidas do mesmo chamador.
