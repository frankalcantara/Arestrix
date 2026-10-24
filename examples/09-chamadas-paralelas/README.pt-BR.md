# Exemplo 9 — Chamadas paralelas

Um coordenador consulta dois especialistas declarados **no mesmo turno**. O runtime inicia as duas
chamadas `call_agent` em paralelo e devolve os resultados na ordem pedida.

## Fluxo

```text
Coordenador_Paralelo ──(fluxo)──► Entrega_da_Resposta
        │ call                         ▲
        ├────────► Especialista_A ─────┘ (sem aresta de fluxo)
        └────────► Especialista_B
```

1. A skill do coordenador pede `especialista_a` e `especialista_b` no mesmo turno.
2. O loop do agente executa todas as chamadas `call_agent` daquele turno em paralelo; cada chamada
   mantém seus limites declarados e a profundidade de chamada.
3. O coordenador grava os dois pareceres em `workspace/resposta.txt`.
4. O vértice de saída entrega `exports/resposta.txt`.

O grafo de chamadas continua fechado: só os dois alvos tracejados estão disponíveis. Um turno misto,
com ferramentas que não são `call_agent`, roda em sequência para preservar a ordem dos efeitos. Use
`ARESTRIX_PARALLEL_CALLS=0` para forçar chamadas sequenciais.
