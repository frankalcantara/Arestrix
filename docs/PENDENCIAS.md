# Pendências do projeto Arestrix

> **Versão:** 2.8.0 · **Atualizado em:** 2026-11-12
>
> Documento único de pendências do projeto. O plano de memória (Fases A–C) e o
> multigrafo avançado (Fase D / E11–E20) estão concluídos. As pendências
> restantes são operacionais, de performance e de release.
>
> Resultados consolidados: [Resultados dos testes de memória](pt-BR/resultados-testes-memoria.md).

## 1. Resumo

| Área | Estado |
| :--- | :--- |
| Fases A–C do plano de memória | **Concluídas** |
| Modos, propostas, telemetria, validação, brief, estado, embeddings e A/B | **Concluídos** |
| Fase D — multigrafo avançado | **Implementada; interruptores opt-in** |
| Harness de execução do A/B v2 | **Executado com provedor real em 2026-11-11 (paridade/custo e valor)** |
| Porteiro de política da memória (`memory.gate`) | **Implementado; A/B de 3 repetições concluído** |
| Cenários de memória do projeto (Consultar / Consultar e Decidir / Aprender) | **Implementados; memória ligada por padrão** |
| Interruptores experimentais (`pathServing`, `traversal`, `bridges`, `routing.mode`) | **Mecanismos medidos em corpus curado e nos 14 exemplos; roteamento binário com A/B real; padrões seguem `off`** |
| Nível `institution` no `memory_inspect` do assistente | **Implementado; leitura do escopo institucional do workspace** |
| Validação com provedores reais de embeddings | **Concluída com EmbeddingGemma 2 e Qwen3-Embedding-4B em endpoint local; falta endpoint de produção** |
| Interface gráfica para configurar o agente de memória | **Implementada e validada no Studio** |
| Laço de profiling (Tracy) e correção automática do código gerado via modelo padrão do Studio | Build opcional pronto; loop pendente |
| Instrumentação do próprio Arestrix com Tracy e laço de auto-otimização | Build opcional pronto; instrumentação pendente |
| Ciclo completo de testes e otimização de performance (C++, TypeScript, etc.) a cada release | Pendente de release |
| Validação de release em máquina limpa | Pendente de release |

## 2. O que está concluído na memória

- Modos por vértice `off`, `consult` e `learn`, com configuração de projeto.
- Lições e playbooks como propostas/candidatos; nada ativa sozinho na Fase A.
- Telemetria separada do conhecimento.
- Estado de continuação declarado pelo projeto, com escopos `project`, `task_type` e `item`,
  schema JSON, TTL, CAS, `replace`/`versioned` e histórico.
- Validação de memória, `task_type`, hash canônico, `[MEMO:<id>]`, uso real vs. injetado e
  utilidade separada da confiança.
- `errorClass` enumerado e falhas de infraestrutura fora das propostas de lição.
- Gate de playbooks com 3 entradas distintas, validador estável, limite de 24h, unpromote e
  auto-demoção.
- Busca híbrida com fingerprint completo, estados `ready/stale/pending/partial/failed` e fallback
  lexical.
- Brief guiado por `task_type`, tema e prompt-template; filtro SQL por tema e cache invalidável.
- Tokens de prompt/cache em `memory_usage`.
- Predicados de fato single-valued e multi-valued.
- A/B v2 com conjunto versionado 12/4/4, ordem sorteada, repetições, deltas pareados e IC 95%.
- Ferramentas do assistente do Studio: `memory_inspect`, `memory_search`,
  `memory_review_proposal`, `memory_declare_state_key`, `memory_unpromote_playbook` e
  `memory_retire_path`.
- CLI para memória, estado, embeddings, propostas, workspace, caminhos, pontes, unpromote e A/B.

## 3. Fase D — multigrafo avançado implementado

A Fase D foi implementada nas etapas E11–E20, com interruptores opt-in e
medição reproduzível. Já existem no núcleo e no Studio:

1. **Fontes e afirmações** — ingestão local versionada, extração validada e
   propostas de claim.
2. **Lentes e facetas** — facetas determinísticas por entidade/lente e filtro de
   busca.
3. **Caminhos e esquemas** — consolidação idempotente de traços aprovados,
   utilidade Beta, invalidação e atalhos reconstruíveis.
4. **Serviço e travessia** — caminhos elegíveis antes do grafo, exploração
   Thompson, travessia em feixe e vetor de cobertura.
5. **Roteamento e pontes** — decisão local/fronteira por `task_type` e
   hipóteses interdomínio.

Os padrões continuam conservadores: `memory.pathServing`, `memory.traversal`,
`memory.bridges` e `routing.mode` ficam desligados. A medição em corpus curado,
o corpus derivado dos 14 exemplos e o A/B real do roteamento binário estão
concluídos, mas ainda faltam projetos reais de cliente. Relatórios:
`docs/quality/memory-advanced-ab.md`, `docs/quality/memory-examples-advanced-ab.md`
e `docs/quality/routing-binary-ab.md`.

### Fora de escopo (não é pendência)

- A extração de afirmações por modelo local já existe atrás de `memory.claimExtraction`;
  consolidação/generalização por LLM continua fora do escopo.
- Sincronização multi-máquina e coordenação de múltiplos workers (fora do escopo desta release).
- Checkpoint/retomada do grafo.
- Mudança do protocolo institucional HTTP/MCP.
- Migração automática de projetos antigos; não há importador legado.

## 4. Pendências operacionais

### 4.1 Harness de execução do A/B v2

O harness está implementado em `memory ab run` e coberto por
`scripts/e2e_memory_ab.py`: 20 tarefas × 3 repetições × 2 braços, banco
congelado, validador e um arquivo `observations-<timestamp>.json` com `runId`,
`episodeId`, `arm`, `ok`, tokens, turnos e latência. Custo monetário não é
calculado.

Executado com provedor real em **2026-11-11** por
`scripts/e2e_memory_ab_live.py`: DeepSeek (`deepseek-chat`), 120 execuções e
banco congelado. Relatório em `tests/fixtures/memory-ab-live-report.json`;
observações brutas em `tests/fixtures/memory-ab-live/`.

| Métrica | OFF | ON |
| :--- | ---: | ---: |
| Aprovação | 60/60 (1,000) | 60/60 (1,000) |
| Prompt tokens | 99.428 | 298.588 |
| Prompt em cache | 38.400 | 204.288 |
| Completion tokens | 46.792 | 78.790 |
| Total tokens | 146.220 | 377.378 |
| Turnos médios | 2,00 | 3,52 |
| Latência média | 6,46 s | 10,95 s |

O conhecimento congelado ficou inalterado e não houve falha de infraestrutura
nem tarefa inválida. O delta de aprovação foi 0 (IC95 [0,0]) porque o validador
do piloto não depende da memória. O custo maior do braço ON vem de chamadas
`memory_search` feitas pelo próprio agente — 20/20 traces no ensaio de 20
execuções, média de 1,8 busca por trace —, não do brief: o contexto injetado tem
~451 caracteres em média.

A variante com validador dependente de memória, executada em 2026-11-11 com
`--memory-dependent`, exige o token `AB-7731`, que só existe em uma lição
curada. O resultado mostra o sinal de valor: aprovação 60/60 (1,000) no ON contra
0/60 (0,000) no OFF, delta 1,000 (IC95 [1,000, 1,000]), sem falha de
infraestrutura nem tarefa inválida. O custo continua maior no ON (2,67× tokens
totais e 1,62× latência), porque o agente consulta a memória por conta própria; a
lição é efetivamente usada, mas o efeito é binário por desenho do validador.
Relatório: `tests/fixtures/memory-ab-live-value-report.json`.

Isso fecha a lacuna de valor da entrega da memória. Os interruptores
experimentais têm medição de mecanismos própria na seção 4.1.2; o A/B de valor
continua medindo a entrega da lição, não a qualidade da recuperação avançada.

### 4.1.1 Porteiro de política da memória (`memory.gate`)

Implementado com os modos `off`, `shadow` e `enforce`, decisão determinística e
modelo opcional (`provider`/`model`; Jev, local ou API). O gate escolhe entre
`none`, `brief`, `search` e `learn`, grava um passo `memory_gate` no trace e tem
um piso de validação: se o validador exige um valor presente no brief, `none` é
promovido a `brief`. O piloto histórico de 1 repetição (2026-11-11) permanece
como referência; a confirmação de 3 repetições foi concluída em 2026-11-12 com
Jev.

| Cenário | Aprovação ON | ON/OFF tokens | ON/OFF latência |
| :--- | ---: | ---: | ---: |
| Baseline, paridade | 1,000 | 2,69× | 1,60× |
| Gate determinístico, paridade | 1,000 | 1,79× | 1,25× |
| Gate com modelo, paridade | 1,000 | 1,34× | 1,16× |
| Gate com modelo, valor | 1,000 | 1,47× | 1,19× |
| Jev, paridade | 1,000 | 1,39× | 1,01× |
| Jev, valor | 1,000 | 1,51× | 1,03× |

Os números do gate com DeepSeek incluem os tokens e a latência do modelo do gate
(~9 mil tokens e ~1,3 s por execução); só o agente ficaria em 1,17×/0,96× na
paridade e 1,28×/0,99× no valor. O Jev gasta ~14 mil tokens e ~270 ms por
execução.

No piloto histórico, o gate com DeepSeek tomou 17 decisões `brief`, 2 `none` e 1
`search` na paridade, e 19 `brief` + 1 `search` no valor; com **Jev**
(`jev-latest`, TypeSafe `/v1/systemone`) foram 13 `brief`, 6 `none` e 1 `search`
na paridade, e 19 `brief` + 1 `search` no valor.

**Confirmação (3 repetições, 2026-11-12)**

20 tarefas × 2 braços × 3 repetições = 120 execuções por suíte; 0 falhas de
infraestrutura, 0 tarefas inválidas e conhecimento congelado inalterado.

| Cenário | Aprovação ON | ON/OFF tokens | ON/OFF latência | ON total | OFF total |
| :--- | ---: | ---: | ---: | ---: | ---: |
| Jev, paridade | 1,000 | 1,48× | 1,08× | 215.381 (173.453 agente + 41.928 gate) | 145.623 |
| Jev, valor | **1,000 × 0,000** | 1,58× | 1,09× | 238.059 (193.311 agente + 44.748 gate) | 151.129 |

Decisões: paridade 39 `brief`, 18 `none` e 3 `search`; valor 57 `brief` e 3
`search`. Latência média do gate: 271,6 ms na paridade e 420,3 ms no valor. O
delta de aprovação na paridade foi 0 (IC95 [0,0]); no valor, 1,000
(IC95 [1,000,1,000]). A confirmação fecha a lacuna do piloto; os padrões do gate
continuam opt-in e a promoção para produção ainda exige o A/B de qualidade/custo
com o modelo escolhido. Detalhes em `docs/quality/memory-gate-ab.md`.

### 4.1.2 Interruptores avançados: medição de mecanismos

O corpus `tests/fixtures/memory-advanced-v1.json` e o harness
`scripts/memory_advanced_eval.py` medem os quatro interruptores sem chamada de
modelo, sobre banco congelado e 10 tarefas (caminho, caminho expirado, cadeia de
travessia, consulta sem âncora, três casos de ponte e três de roteamento). Cada
tarefa roda em uma cópia do template, em 3 repetições.

| Cenário | Resultado |
| :--- | :--- |
| `pathServing` | tarefa de caminho: recall 1,000 e precisão 1,000; baseline 0,000 |
| `traversal` | cadeia de 3 saltos: recall 1,000 e precisão 0,500; baseline 0,000 |
| `bridges` | 1 hipótese correta (`Guia`, lentes `contraste`/`tipografia`), 0 arestas criadas; casos de mesma lente e acesso incompatível não propostos |
| `routing.mode=shadow` | decisão registrada, tier executado permanece `current` |
| `routing.mode=binary` | `local_large` com confiança alta, `frontier` com confiança baixa e `frontier` sem histórico |
| `all_on` | todas as tarefas positivas aprovadas, nenhum item proibido injetado |

A medição revelou e corrigiu dois defeitos: a travessia não resolvia âncoras de
prompts em linguagem natural (agora tokeniza a consulta quando a frase inteira
não é um alias) e o modo `shadow` trocava o tier executado quando não havia
histórico (agora mantém o tier atual e apenas registra a sugestão).

#### Corpus derivado dos 14 exemplos

`scripts/memory_examples_eval.py` copia cada projeto de `examples/`, semeia o
banco a partir dos pipelines e mede os interruptores sem chamada de modelo, em 3
repetições. O original não é alterado; o corpus é derivado dos exemplos, não é
dado de cliente.

| Cenário | Resultado nos 14 projetos |
| :--- | :--- |
| `pathServing` | recall 1,000 e precisão 1,000 em todos |
| `traversal` | recall 1,000 em todos; precisão entre 0,300 e 1,000 |
| `bridges` | 5 projetos com estrutura natural (`06`, `09`, `10`, `11`, `12`), 5 propostas corretas e 0 arestas criadas |
| `routing.mode=shadow` | decisão registrada e tier `current` mantido nos 14 |
| `routing.mode=binary` | `local_large` com confiança alta, `frontier` com confiança baixa e `frontier` sem histórico nos 14 |

Relatório: `docs/quality/memory-examples-advanced-ab.md`.

#### Roteamento binário real

O A/B usou 7 tarefas (4 fáceis e 3 difíceis), 2 repetições e chamadas reais ao
provedor `deepseek`: `deepseek-flash` no nível local e `deepseek-v4-pro` na
fronteira. O custo ponderado aplica 0,2 ao token local e 1,0 ao de fronteira.

| Braço | Aprovação | Tokens | Custo ponderado | Latência média | Decisões |
| :--- | ---: | ---: | ---: | ---: | :--- |
| local | 1,000 | 18.819 | 3.763,80 | 14.963 ms | — |
| frontier | 1,000 | 27.805 | 27.805,00 | 25.679 ms | — |
| binary | 1,000 | 28.458 | 20.444,40 | 20.447 ms | 8 `local_large`, 6 `frontier` |

O binário ficou 26,5% abaixo da fronteira em custo ponderado, mas custou 5,43×
o local. Não houve ganho de qualidade: o modelo local acertou também as tarefas
difíceis. Relatório: `docs/quality/routing-binary-ab.md`.

Decisão: os mecanismos estão validados, mas os padrões continuam `off`. A
promoção ainda exige projetos reais de cliente e um corpus maior. O A/B do
roteamento binário mediu custo, não ganho de qualidade; o local foi suficiente
para todas as tarefas deste conjunto.

### 4.2 Nível institucional no assistente

`memory_inspect` agora cobre `project`, `workspace`, `institution`, `vertex` e `all`. O nível
`institution` lê o escopo `institution:<id>` já presente no banco do workspace, quando o workspace
foi configurado com um pai institucional. Sem esse escopo, a resposta é `configured=false` com
`reason=institution_not_configured`; o serviço institucional remoto (`arestrix-memory`) continua
separado e autenticado por token. A leitura é somente de status e contagens: lições, playbooks,
arestas ativas, aliases e caminho do banco.

### 4.3 Provedores reais de embeddings

A validação de qualidade e latência foi concluída em 2026-11-12 com os dois
modelos reais em um endpoint local OpenAI-compatible. O corpus tem 16 documentos
em pt-BR e 16 consultas mistas pt-BR/en, com 3 repetições.

| Modelo | Dimensões | Recall@1 | Recall@3 | Consulta p50 |
| :--- | ---: | ---: | ---: | ---: |
| EmbeddingGemma 2 | 768 | 0,875 | 1,000 | 134 ms |
| Qwen3-Embedding-4B (768) | 2560 → 768 | 0,813 | 1,000 | 8.474 ms |
| Qwen3-Embedding-4B (2560) | 2560 | 0,875 | 1,000 | 8.411 ms |
| Lexical BM25 | — | 0,813 | 0,938 | — |

O EmbeddingGemma 2 teve o melhor top-1 e a menor latência. O Qwen3 completo
empatou no recall@1, mas o truncamento para 768 caiu ao nível lexical neste
corpus e o modelo de 4B rodou em CPU (8,5 s por consulta). A integração passou
nos dois: `embeddings test` e reindexação 4/4, sem falhas. Com o endpoint
desligado, o teste retornou `ok=false` e a busca lexical permaneceu como fallback
seguro. Relatório: `docs/quality/memory-embeddings-live.md`; evidência:
`tests/fixtures/memory-embeddings-live-report.json`. Ainda falta validar um
endpoint de produção e dados de cliente.

### 4.4 Interface gráfica do agente de memória

O alvo `memory.claimExtraction.agent` agora é registrado a partir dos agentes do
pipeline ativo no `AssistantSession`, exposto pela listagem
`memory_claim_agents` e editável em Arquivo → Propriedades → Projeto → Memória,
com fallback explícito para o modelo padrão e aviso quando o alvo salvo não
existe no pipeline atual. O CLI `memory claims extract` também registra o
pipeline do projeto antes de executar.

A validação manual foi concluída no Studio: a tela lista os três agentes do
pipeline, habilita o seletor ao ligar a extração, salva `{ id, graph, node }` e
restaura a seleção ao reabrir o projeto. A comparação do alvo também foi
corrigida para não depender da ordem das chaves JSON.

A validação automatizada cobre o registro/listagem e um e2e de extração com
provedor HTTP falso (`scripts/e2e_memory_claim_agent.py`): o agente escolhido
recebe o trecho, o catálogo e o schema, e a proposta de claim é gravada.

### 4.5 Laço de observação de performance e correção automática no código gerado (Tracy + Modelo Padrão do Studio)

> Plano detalhado: [`quality/tracy-profiling-plan.md`](quality/tracy-profiling-plan.md).

Usar o **Tracy Profiler** (`third_party/tracy`) em um laço fechado de observação de performance e
autocorreção para os pipelines gerados pelo Arestrix:

- **Instrumentação automática do código gerado**: emitir macros/zonas do Tracy (`ZoneScoped`,
  `FrameMark`, contadores de memória, locks e eventos de tarefas Taskflow) no código C++20 gerado pelo
  `TaskflowGenerator`.
- **Captura e análise de profile**: executar o binário gerado com captura automatizada de traces do
  Tracy (CLI/headless capture e exportação estatística de zonas críticas, latência por vértice,
  alocações e contenção de threads).
- **Correção automática com o modelo padrão do Studio**: enviar o relatório estruturado de gargalos do
  Tracy e o trecho correspondente do código gerado para o **modelo padrão configurado no Studio**,
  operando em loop iterativo de diagnóstico → refatoração/otimização → recompilação → re-profile e
  validação funcional até atingir os critérios de performance sem regressão semântica.

### 4.6 Instrumentação do próprio Arestrix com Tracy e loop de auto-otimização

Incluir o **Tracy Profiler** diretamente no código-fonte do próprio Arestrix (`CppCore` e binários
`arestrix`, `arestrix-cli`, `arestrix-gateway` e `arestrix-memory`) e usar um laço semelhante de
observação e otimização contínua:

- **Instrumentação nativa**: cobrir o compilador/gerador Taskflow, runtime de agentes, motor de
  memória SQLite (busca lexical/híbrida, consolidação, travessia em feixe, ingestão), AI Gateway,
  cliente/servidor MCP, ponte IPC/WebUI e operações pesadas do Studio.
- **Loop de observação e correção no próprio Arestrix**: capturar traces reais durante baterias de
  execução, identificar hotspots e regressões de latência/memória via Tracy e acionar o ciclo assistido
  pelo modelo padrão para propor, validar e aplicar otimizações no próprio código do Arestrix.

## 5. Pendências de release

- **Ciclo completo e detalhado de testes e otimização de performance a cada processo de release**:
  - Integrar ao gate de release (`scripts/release_check.py`) um ciclo obrigatório de profiling,
    benchmarking detalhado e otimização antes do fechamento de cada versão.
  - Testar de ponta a ponta **todas as operações em C++, TypeScript e integrações**:
    - **C++ (`CppCore`)**: geração e compilação de código Taskflow, execução de grafos e subflows,
      operações do banco e multigrafo de memória SQLite (queries FTS/vetoriais, consolidação,
      extração, snapshots, travessia e pontes), roteamento e orçamentos do `arestrix-gateway`,
      endpoints REST/MCP do `arestrix-memory`, sandboxes e serialização JSON (`simdjson`).
    - **TypeScript / Studio (`src/`)**: operações do motor de grafo em memória (`Graph`, `Validator`),
      histórico de comandos (`undo`/`redo`), serialização/desserialização de projetos, renderização e
      interação no canvas WebGL/Three.js e camada SVG (`NodeView`, `ConnectionView`), resolução de
      escopos de propriedades (`Studio`/`Workspace`/`Project`), auditoria e hashing de skills/MCP e
      tempo de resposta da interface.
  - Comparar métricas contra a linha de base da release anterior, bloquear regressões acima do limiar e
    executar o ciclo de otimização guiado pelo Tracy antes de empacotar os artefatos.
- Instalador 2.7.0 validado localmente em 2026-11-11: instalação silenciosa em máquina sem
  Arestrix, smoke dos binários, abertura do Studio, upgrade 2.1.0-beta.1 → 2.7.0 preservando
  projeto do usuário e desinstalação completa. Faltam uma máquina realmente limpa (apenas o
  runtime VC++) e Intune/SCCM.
- Pacote Linux e matriz de CI Linux/macOS.
- Catálogo remoto assinado.

## 6. Fase D encerrada

A Fase D foi concluída com:

1. especificação de fontes, claims, lentes/facetas, caminhos, travessia,
   roteamento e pontes;
2. schema idempotente compatível com o banco atual;
3. API, CLI, painel e ferramentas do assistente (`memory_inspect`,
   `memory_search`, `memory_retire_path`);
4. conjunto de avaliação versionado e reproduzível em
   `tests/fixtures/memory-eval-v1-report.json`;
5. testes unitários, de integração, E2E e linha de base verdes;
6. documentação PT/EN atualizada.

Os padrões permanecem conservadores: os interruptores novos só devem ser
ligados após medição em projetos reais de cliente. O corpus curado, o corpus
derivado dos 14 exemplos e o A/B real do roteamento binário estão concluídos,
mas não substituem dados de cliente.
