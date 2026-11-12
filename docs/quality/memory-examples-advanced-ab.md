# Interruptores avancados nos projetos de exemplo

> **Versao consolidada:** [Resultados dos testes de memoria](../pt-BR/resultados-testes-memoria.md) · [English](../en/memory-test-results.md).

- Corpus: `memory-examples-advanced-v1`
- Projetos: 14
- Repeticoes por tarefa: 3

## Desenho

Cada projeto de `examples/` e copiado para um diretorio temporario. Os
vertices e as arestas do pipeline viram nos e claims de memoria; a maior
cadeia simples vira o caminho canonico. O harness usa o proprio CLI
(`memory brief`, `memory bridges detect` e `memory route`), sem chamada de
modelo. O projeto original nao e alterado.

## Projetos

| Projeto | Grafo | Tarefa | Cadeia | Pontes |
| --- | --- | --- | ---: | --- |
| `01-svg-agente` | Exemplo 1 - Agente de SVG | `exemplo_exemplo_1_agente_de_svg` | 3 | nao se aplica |
| `02-triagem-de-suporte` | Triagem_de_Suporte | `exemplo_exemplo_2_triagem_de_suporte` | 3 | nao se aplica |
| `03-atendimento-ilustrado` | Atendimento_Ilustrado | `exemplo_exemplo_3_atendimento_ilustrado` | 3 | nao se aplica |
| `04-memoria` | Memoria_de_Projeto | `exemplo_exemplo_4_memoria_de_projeto` | 2 | nao se aplica |
| `05-matematica-com-memoria` | Matematica_Com_Memoria | `exemplo_exemplo_5_matematica_com_memoria` | 3 | nao se aplica |
| `06-chamada-agente` | Coordenador_e_Especialista | `exemplo_exemplo_6_chamada_entre_agentes` | 2 | sim: call, flow |
| `07-interacao` | Interacao_Humana | `exemplo_exemplo_7_interacao_humana` | 2 | nao se aplica |
| `08-formulario-gerado` | Formulario_Gerado | `exemplo_exemplo_8_formulario_gerado` | 2 | nao se aplica |
| `09-chamadas-paralelas` | Chamadas_Paralelas | `exemplo_exemplo_9_chamadas_paralelas` | 2 | sim: call, flow |
| `10-subflow-chamavel` | Exemplo 10 - Subflow Chamável | `exemplo_exemplo_10_subflow_chamavel` | 2 | sim: call, flow |
| `11-call-vertex` | Call_Vertex | `exemplo_exemplo_11_vertice_call` | 2 | sim: call, flow |
| `12-chamadas-assincronas` | Chamadas_Assincronas | `exemplo_exemplo_12_chamadas_assincronas` | 2 | sim: call, flow |
| `13-trilhas-independentes` | Trilhas_Independentes | `exemplo_exemplo_13_trilhas_independentes` | 2 | nao se aplica |
| `14-contexto-comprimido` | Contexto_Comprimido | `exemplo_exemplo_14_contexto_comprimido` | 2 | nao se aplica |

## Resultados

| Projeto | baseline caminho | pathServing | travessia | pontes | binary local | binary frontier | binary frio |
| --- | --- | --- | --- | --- | --- | --- | --- |
| `01-svg-agente` | nao serviu | 1.000/1.000 | 1.000/0.375 | n/a | local_large | frontier | frontier |
| `02-triagem-de-suporte` | nao serviu | 1.000/1.000 | 1.000/0.300 | n/a | local_large | frontier | frontier |
| `03-atendimento-ilustrado` | nao serviu | 1.000/1.000 | 1.000/0.300 | n/a | local_large | frontier | frontier |
| `04-memoria` | nao serviu | 1.000/1.000 | 1.000/1.000 | n/a | local_large | frontier | frontier |
| `05-matematica-com-memoria` | nao serviu | 1.000/1.000 | 1.000/0.750 | n/a | local_large | frontier | frontier |
| `06-chamada-agente` | nao serviu | 1.000/1.000 | 1.000/1.000 | sim | local_large | frontier | frontier |
| `07-interacao` | nao serviu | 1.000/1.000 | 1.000/1.000 | n/a | local_large | frontier | frontier |
| `08-formulario-gerado` | nao serviu | 1.000/1.000 | 1.000/1.000 | n/a | local_large | frontier | frontier |
| `09-chamadas-paralelas` | nao serviu | 1.000/1.000 | 1.000/0.667 | sim | local_large | frontier | frontier |
| `10-subflow-chamavel` | nao serviu | 1.000/1.000 | 1.000/1.000 | sim | local_large | frontier | frontier |
| `11-call-vertex` | nao serviu | 1.000/1.000 | 1.000/0.667 | sim | local_large | frontier | frontier |
| `12-chamadas-assincronas` | nao serviu | 1.000/1.000 | 1.000/1.000 | sim | local_large | frontier | frontier |
| `13-trilhas-independentes` | nao serviu | 1.000/1.000 | 1.000/0.500 | n/a | local_large | frontier | frontier |
| `14-contexto-comprimido` | nao serviu | 1.000/1.000 | 1.000/1.000 | n/a | local_large | frontier | frontier |

## Criterios de aceite

- OK `01-svg-agente:baseline_no_path`
- OK `01-svg-agente:baseline_no_traversal`
- OK `01-svg-agente:path_served`
- OK `01-svg-agente:traversal_recall`
- OK `01-svg-agente:routing_binary:routing-local`
- OK `01-svg-agente:routing_binary:routing-frontier`
- OK `01-svg-agente:routing_binary:routing-cold`
- OK `01-svg-agente:shadow_keeps_current`
- OK `01-svg-agente:all_on_path`
- OK `01-svg-agente:all_on_traversal`
- OK `01-svg-agente:all_on_routing`
- OK `02-triagem-de-suporte:baseline_no_path`
- OK `02-triagem-de-suporte:baseline_no_traversal`
- OK `02-triagem-de-suporte:path_served`
- OK `02-triagem-de-suporte:traversal_recall`
- OK `02-triagem-de-suporte:routing_binary:routing-local`
- OK `02-triagem-de-suporte:routing_binary:routing-frontier`
- OK `02-triagem-de-suporte:routing_binary:routing-cold`
- OK `02-triagem-de-suporte:shadow_keeps_current`
- OK `02-triagem-de-suporte:all_on_path`
- OK `02-triagem-de-suporte:all_on_traversal`
- OK `02-triagem-de-suporte:all_on_routing`
- OK `03-atendimento-ilustrado:baseline_no_path`
- OK `03-atendimento-ilustrado:baseline_no_traversal`
- OK `03-atendimento-ilustrado:path_served`
- OK `03-atendimento-ilustrado:traversal_recall`
- OK `03-atendimento-ilustrado:routing_binary:routing-local`
- OK `03-atendimento-ilustrado:routing_binary:routing-frontier`
- OK `03-atendimento-ilustrado:routing_binary:routing-cold`
- OK `03-atendimento-ilustrado:shadow_keeps_current`
- OK `03-atendimento-ilustrado:all_on_path`
- OK `03-atendimento-ilustrado:all_on_traversal`
- OK `03-atendimento-ilustrado:all_on_routing`
- OK `04-memoria:baseline_no_path`
- OK `04-memoria:baseline_no_traversal`
- OK `04-memoria:path_served`
- OK `04-memoria:traversal_recall`
- OK `04-memoria:routing_binary:routing-local`
- OK `04-memoria:routing_binary:routing-frontier`
- OK `04-memoria:routing_binary:routing-cold`
- OK `04-memoria:shadow_keeps_current`
- OK `04-memoria:all_on_path`
- OK `04-memoria:all_on_traversal`
- OK `04-memoria:all_on_routing`
- OK `05-matematica-com-memoria:baseline_no_path`
- OK `05-matematica-com-memoria:baseline_no_traversal`
- OK `05-matematica-com-memoria:path_served`
- OK `05-matematica-com-memoria:traversal_recall`
- OK `05-matematica-com-memoria:routing_binary:routing-local`
- OK `05-matematica-com-memoria:routing_binary:routing-frontier`
- OK `05-matematica-com-memoria:routing_binary:routing-cold`
- OK `05-matematica-com-memoria:shadow_keeps_current`
- OK `05-matematica-com-memoria:all_on_path`
- OK `05-matematica-com-memoria:all_on_traversal`
- OK `05-matematica-com-memoria:all_on_routing`
- OK `06-chamada-agente:baseline_no_path`
- OK `06-chamada-agente:baseline_no_traversal`
- OK `06-chamada-agente:path_served`
- OK `06-chamada-agente:traversal_recall`
- OK `06-chamada-agente:bridge_proposed`
- OK `06-chamada-agente:bridge_claims`
- OK `06-chamada-agente:bridge_no_edges`
- OK `06-chamada-agente:routing_binary:routing-local`
- OK `06-chamada-agente:routing_binary:routing-frontier`
- OK `06-chamada-agente:routing_binary:routing-cold`
- OK `06-chamada-agente:shadow_keeps_current`
- OK `06-chamada-agente:all_on_path`
- OK `06-chamada-agente:all_on_traversal`
- OK `06-chamada-agente:all_on_routing`
- OK `06-chamada-agente:all_on_bridge`
- OK `07-interacao:baseline_no_path`
- OK `07-interacao:baseline_no_traversal`
- OK `07-interacao:path_served`
- OK `07-interacao:traversal_recall`
- OK `07-interacao:routing_binary:routing-local`
- OK `07-interacao:routing_binary:routing-frontier`
- OK `07-interacao:routing_binary:routing-cold`
- OK `07-interacao:shadow_keeps_current`
- OK `07-interacao:all_on_path`
- OK `07-interacao:all_on_traversal`
- OK `07-interacao:all_on_routing`
- OK `08-formulario-gerado:baseline_no_path`
- OK `08-formulario-gerado:baseline_no_traversal`
- OK `08-formulario-gerado:path_served`
- OK `08-formulario-gerado:traversal_recall`
- OK `08-formulario-gerado:routing_binary:routing-local`
- OK `08-formulario-gerado:routing_binary:routing-frontier`
- OK `08-formulario-gerado:routing_binary:routing-cold`
- OK `08-formulario-gerado:shadow_keeps_current`
- OK `08-formulario-gerado:all_on_path`
- OK `08-formulario-gerado:all_on_traversal`
- OK `08-formulario-gerado:all_on_routing`
- OK `09-chamadas-paralelas:baseline_no_path`
- OK `09-chamadas-paralelas:baseline_no_traversal`
- OK `09-chamadas-paralelas:path_served`
- OK `09-chamadas-paralelas:traversal_recall`
- OK `09-chamadas-paralelas:bridge_proposed`
- OK `09-chamadas-paralelas:bridge_claims`
- OK `09-chamadas-paralelas:bridge_no_edges`
- OK `09-chamadas-paralelas:routing_binary:routing-local`
- OK `09-chamadas-paralelas:routing_binary:routing-frontier`
- OK `09-chamadas-paralelas:routing_binary:routing-cold`
- OK `09-chamadas-paralelas:shadow_keeps_current`
- OK `09-chamadas-paralelas:all_on_path`
- OK `09-chamadas-paralelas:all_on_traversal`
- OK `09-chamadas-paralelas:all_on_routing`
- OK `09-chamadas-paralelas:all_on_bridge`
- OK `10-subflow-chamavel:baseline_no_path`
- OK `10-subflow-chamavel:baseline_no_traversal`
- OK `10-subflow-chamavel:path_served`
- OK `10-subflow-chamavel:traversal_recall`
- OK `10-subflow-chamavel:bridge_proposed`
- OK `10-subflow-chamavel:bridge_claims`
- OK `10-subflow-chamavel:bridge_no_edges`
- OK `10-subflow-chamavel:routing_binary:routing-local`
- OK `10-subflow-chamavel:routing_binary:routing-frontier`
- OK `10-subflow-chamavel:routing_binary:routing-cold`
- OK `10-subflow-chamavel:shadow_keeps_current`
- OK `10-subflow-chamavel:all_on_path`
- OK `10-subflow-chamavel:all_on_traversal`
- OK `10-subflow-chamavel:all_on_routing`
- OK `10-subflow-chamavel:all_on_bridge`
- OK `11-call-vertex:baseline_no_path`
- OK `11-call-vertex:baseline_no_traversal`
- OK `11-call-vertex:path_served`
- OK `11-call-vertex:traversal_recall`
- OK `11-call-vertex:bridge_proposed`
- OK `11-call-vertex:bridge_claims`
- OK `11-call-vertex:bridge_no_edges`
- OK `11-call-vertex:routing_binary:routing-local`
- OK `11-call-vertex:routing_binary:routing-frontier`
- OK `11-call-vertex:routing_binary:routing-cold`
- OK `11-call-vertex:shadow_keeps_current`
- OK `11-call-vertex:all_on_path`
- OK `11-call-vertex:all_on_traversal`
- OK `11-call-vertex:all_on_routing`
- OK `11-call-vertex:all_on_bridge`
- OK `12-chamadas-assincronas:baseline_no_path`
- OK `12-chamadas-assincronas:baseline_no_traversal`
- OK `12-chamadas-assincronas:path_served`
- OK `12-chamadas-assincronas:traversal_recall`
- OK `12-chamadas-assincronas:bridge_proposed`
- OK `12-chamadas-assincronas:bridge_claims`
- OK `12-chamadas-assincronas:bridge_no_edges`
- OK `12-chamadas-assincronas:routing_binary:routing-local`
- OK `12-chamadas-assincronas:routing_binary:routing-frontier`
- OK `12-chamadas-assincronas:routing_binary:routing-cold`
- OK `12-chamadas-assincronas:shadow_keeps_current`
- OK `12-chamadas-assincronas:all_on_path`
- OK `12-chamadas-assincronas:all_on_traversal`
- OK `12-chamadas-assincronas:all_on_routing`
- OK `12-chamadas-assincronas:all_on_bridge`
- OK `13-trilhas-independentes:baseline_no_path`
- OK `13-trilhas-independentes:baseline_no_traversal`
- OK `13-trilhas-independentes:path_served`
- OK `13-trilhas-independentes:traversal_recall`
- OK `13-trilhas-independentes:routing_binary:routing-local`
- OK `13-trilhas-independentes:routing_binary:routing-frontier`
- OK `13-trilhas-independentes:routing_binary:routing-cold`
- OK `13-trilhas-independentes:shadow_keeps_current`
- OK `13-trilhas-independentes:all_on_path`
- OK `13-trilhas-independentes:all_on_traversal`
- OK `13-trilhas-independentes:all_on_routing`
- OK `14-contexto-comprimido:baseline_no_path`
- OK `14-contexto-comprimido:baseline_no_traversal`
- OK `14-contexto-comprimido:path_served`
- OK `14-contexto-comprimido:traversal_recall`
- OK `14-contexto-comprimido:routing_binary:routing-local`
- OK `14-contexto-comprimido:routing_binary:routing-frontier`
- OK `14-contexto-comprimido:routing_binary:routing-cold`
- OK `14-contexto-comprimido:shadow_keeps_current`
- OK `14-contexto-comprimido:all_on_path`
- OK `14-contexto-comprimido:all_on_traversal`
- OK `14-contexto-comprimido:all_on_routing`

## Decisao

A medicao nos exemplos passou. Os interruptores de caminho, travessia,
pontes e roteamento funcionam sobre a topologia real dos 14 projetos,
sem alterar o conhecimento original. Os padroes continuam `off`.

Esta medicao usa projetos de exemplo, nao projetos de cliente. Ela
valida a generalidade do mecanismo entre grafos e manifestos distintos.
O A/B real de roteamento binario foi medido separadamente; ele teve
paridade de qualidade e custo abaixo da fronteira, mas ainda exige
projetos reais e um corpus maior antes de virar padrao.
