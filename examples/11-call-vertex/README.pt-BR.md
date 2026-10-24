# Exemplo 11 — Vértice Call

Um vértice **Call** executa um alvo chamável declarado **dentro do fluxo** e entrega a resposta ao
próximo vértice.

## Fluxo

```text
Preparador_da_Chamada ──(fluxo)──► Chamada_em_Fluxo ──(fluxo)──► Entrega_da_Resposta
                                          │ call
                                          ▼
                                   Especialista_Chamado
```

1. O agente preparador grava seu texto final em `.arestrix/out/preparador.txt`.
2. O vértice Call lê esse arquivo como entrada (`source: previous`) e invoca o alvo declarado pela
   aresta tracejada.
3. O alvo é somente chamável: não tem arestas de fluxo e roda dentro do vértice Call.
4. O vértice Call grava a resposta em `.arestrix/out/chamada.txt`.
5. O vértice de saída exporta para `exports/resposta.txt`.

O grafo de chamadas continua fechado: o vértice Call só invoca alvos declarados pela sua aresta de
chamada, e o alvo não pode ser agendado pelo fluxo.

## Observações

- Defina um input fixo no Inspector para não coletar do vértice anterior.
- `source: file` lê um arquivo do projeto; `source: none` envia um payload vazio.
- Um vértice Call precisa de exatamente uma aresta de chamada de saída.
