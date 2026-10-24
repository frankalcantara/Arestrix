# Exemplo 10 — Subflow chamável

Um coordenador chama um **subflow** declarado no grafo. O subflow tem seu próprio grafo aninhado
(`childGraph`) e roda nativamente no mesmo executável.

## Fluxo

```text
Coordenador_Subflow ──(fluxo)──► Entrega_da_Resposta
        │ call
        ▼
Subflow_Especialista (childGraph)
  Especialista_Interno ──(fluxo)──► Resultado_do_Subflow
```

1. O coordenador chama `subflow_especialista` por uma aresta tracejada.
2. O compilador gera o grafo filho como uma função nativa; o runtime registra o subflow e passa o
   input da chamada como `inputContext` aos agentes/interações internos.
3. O grafo interno grava seu próprio artefato e devolve o texto de saída.
4. O coordenador grava o texto devolvido em `workspace/resposta.txt`.
5. A saída do pai entrega `exports/resposta.txt`; o filho também deixa
   `exports/subflow_result.txt`.

O alvo do subflow é somente chamável (sem arestas de fluxo) e o grafo de chamadas continua fechado.
Arestas de chamada e subflows aninhados são rejeitados no v1.
