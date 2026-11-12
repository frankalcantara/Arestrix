# Roteamento binario com modelos reais

> **Versao consolidada:** [Resultados dos testes de memoria](../pt-BR/resultados-testes-memoria.md) · [English](../en/memory-test-results.md).

- Provedor: `deepseek`
- Nivel local: `deepseek-flash`
- Nivel de fronteira: `deepseek-v4-pro`
- Repeticoes por tarefa: 2

## Desenho

O pipeline minimo tem um agente que grava a resposta em
`artefatos/resposta.txt`; o validador exige o texto exato. A telemetria
de cada tipo de tarefa e semeada com 10 episodios (9/10 nas faceis e
2/10 nas dificeis) para o roteador ter historico. Os tres bracos usam
o mesmo projeto e o mesmo conjunto de tarefas.

## Piloto local

O piloto roda o modelo local uma vez por tarefa e apenas define o
historico semeado; ele nao entra nas metricas finais.

| Tarefa | Dificuldade | Passou no piloto |
| --- | --- | --- |
| `easy-sum` | easy | sim |
| `easy-capital` | easy | sim |
| `easy-product` | easy | sim |
| `easy-sky` | easy | sim |
| `hard-product` | hard | sim |
| `hard-recurrence` | hard | sim |
| `hard-fib` | hard | sim |

## Resultados

| Braco | Aprovacao | Faceis | Dificeis | Tokens | Custo ponderado | Latencia media |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| local | 1.000 | 1.000 | 1.000 | 18819 | 3763.80 | 14963 ms |
| frontier | 1.000 | 1.000 | 1.000 | 27805 | 27805.00 | 25679 ms |
| binary | 1.000 | 1.000 | 1.000 | 28458 | 20444.40 | 20447 ms |

## Decisoes do roteador

| Braco | Tiers observados |
| --- | --- |
| local | - |
| frontier | - |
| binary | {'local_large': 8, 'frontier': 6} |

## Criterios de aceite

- OK `localApprovalOnEasy`
- OK `frontierApprovalOnHard`
- OK `binaryQualityNotBelowLocal`
- OK `binaryCostBelowFrontier`
- OK `binaryUsesBothTiers`
- OK `binaryUsesFrontierOnHard`
- OK `binaryUsesLocalOnEasy`
- OK `noInfraErrors`

## Decisao

O roteamento binario foi medido com chamadas reais. A qualidade e o
custo usam tokens e latencia observados; o custo ponderado aplica o
peso configurado de cada nivel (0,2 local e 1,0 fronteira) ao total de
tokens.

Neste conjunto, a aprovacao foi 1.000 no local, 1.000 na fronteira e 1.000 no binario. Nao houve ganho de qualidade: o valor medido esta no custo. O binario ficou 26.5% abaixo da fronteira e 5.43x o custo do local.

O resultado vale para este conjunto de tarefas e para os dois modelos
escolhidos; nao e uma garantia para todo trafego. A promocao do
`routing.mode=binary` para padrao continua exigindo um corpus maior e
telemetria de producao.
