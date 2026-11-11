# Interruptores avancados da memoria: avaliacao de mecanismos

> **Versao consolidada:** [Resultados dos testes de memoria](../pt-BR/resultados-testes-memoria.md) · [English](../en/memory-test-results.md).

- Corpus: `memory-advanced-v1` (semente 20261111)
- Repeticoes por tarefa: 3
- Tarefas: 10

## Desenho

O harness usa o proprio CLI, sem chamada de modelo: `memory brief` para
caminhos e travessia, `memory bridges detect` para hipoteses e
`memory route` para a decisao local/fronteira. O banco e copiado do
template congelado antes de cada tarefa; nenhuma medicao altera o
conhecimento original.

## Tarefas

| Tarefa | Tipo | O que exige |
| --- | --- | --- |
| `path-contrast` | brief | recall de path-guia, claim-guia-contraste, claim-contraste-wcag |
| `path-expired` | brief | nao injetar itens proibidos |
| `traversal-chain` | brief | recall de claim-chain-1, claim-chain-2, claim-chain-3 |
| `traversal-no-anchor` | brief | nao injetar itens proibidos |
| `bridge-cross-lens` | bridges | hipotese para Guia |
| `bridge-same-lens` | bridges | nao propor Relatorio |
| `bridge-access-mismatch` | bridges | nao propor Chave |
| `routing-local` | route | tier local_large |
| `routing-frontier` | route | tier frontier |
| `routing-cold` | route | tier frontier |

## Resultados

| Variante | Recall positivo | Precisao | Negativos OK | Itens proibidos | Brief (chars) | Caminhos | Travessias |
| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: |
| baseline | 0.000 | 0.000 | True | 0 | 0.0 | 0 | 0 |
| path_serving | 0.500 | 0.500 | True | 0 | 105.5 | 1 | 0 |
| traversal | 0.500 | 0.250 | True | 0 | 184.0 | 0 | 1 |
| bridges | 0.000 | 0.000 | True | 0 | 0.0 | 0 | 0 |
| routing_shadow | 0.000 | 0.000 | True | 0 | 0.0 | 0 | 0 |
| routing_binary | 0.000 | 0.000 | True | 0 | 0.0 | 0 | 0 |
| all_on | 1.000 | 0.750 | True | 0 | 289.5 | 1 | 1 |

### Por tarefa (recall/precisao)

| Tarefa | baseline | path_serving | traversal | all_on |
| --- | ---: | ---: | ---: | ---: |
| `path-contrast` | 0.000/0.000 | 1.000/1.000 | 0.000/0.000 | 1.000/1.000 |
| `traversal-chain` | 0.000/0.000 | 0.000/0.000 | 1.000/0.500 | 1.000/0.500 |

## Pontes e roteamento

| Variante | Ponte esperada | ClaimIds | Entidades proibidas | Delta de arestas |
| --- | --- | --- | --- | ---: |
| baseline | False | False | False | 0 |
| path_serving | False | False | False | 0 |
| traversal | False | False | False | 0 |
| bridges | True | True | False | 0 |
| routing_shadow | False | False | False | 0 |
| routing_binary | False | False | False | 0 |
| all_on | True | True | False | 0 |

| Variante | routing-local | routing-frontier | routing-cold | shadow local |
| --- | --- | --- | --- | --- |
| baseline | ['current'] | ['current'] | ['current'] | [''] |
| path_serving | ['current'] | ['current'] | ['current'] | [''] |
| traversal | ['current'] | ['current'] | ['current'] | [''] |
| bridges | ['current'] | ['current'] | ['current'] | [''] |
| routing_shadow | ['current'] | ['current'] | ['current'] | ['local_large'] |
| routing_binary | ['local_large'] | ['frontier'] | ['frontier'] | [''] |
| all_on | ['local_large'] | ['frontier'] | ['frontier'] | [''] |

## Criterios de aceite

- OK `baselinePathRecallZero`
- OK `pathServingRecallOne`
- OK `pathServingPrecisionOne`
- OK `pathExpiredNotServed`
- OK `baselineTraversalRecallZero`
- OK `traversalRecallOne`
- OK `traversalPrecisionAtLeastHalf`
- OK `traversalNoAnchorClean`
- OK `traversalAccessClean`
- OK `bridgeProposalFound`
- OK `bridgeClaimsFound`
- OK `bridgeSameLensClean`
- OK `bridgeAccessClean`
- OK `bridgeNoEdgeCreation`
- OK `routingBinaryLocal`
- OK `routingBinaryFrontier`
- OK `routingBinaryCold`
- OK `routingShadowLocal`
- OK `routingShadowKeepsCurrent`
- OK `routingShadowColdKeepsCurrent`
- OK `allOnPath`
- OK `allOnTraversal`
- OK `allOnBridge`
- OK `allOnRouting`

## Decisao

A avaliacao de mecanismos passou. `pathServing` e `traversal` entregam o conteudo esperado sem violar acesso nem injetar itens expirados; `bridges` produz somente hipoteses e nao cria arestas; `routing.shadow` nao troca o tier executado e `routing.binary` escolhe local/fronteira conforme a confianca.

Esta medicao nao substitui um A/B de qualidade com modelo real. Ela
valida o mecanismo de recuperacao e roteamento; a promocao de
`routing.mode=binary` para padrao ainda exige custo e qualidade medidos
com o modelo escolhido.
