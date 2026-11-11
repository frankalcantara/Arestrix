# Pendências do projeto Arestrix

> **Versão:** 2.7.0 · **Atualizado em:** 2026-11-10
>
> Documento único de pendências do projeto. O plano de memória (Fases A–C) e o
> multigrafo avançado (Fase D / E11–E20) estão concluídos. As pendências
> restantes são operacionais, de performance e de release.

## 1. Resumo

| Área | Estado |
| :--- | :--- |
| Fases A–C do plano de memória | **Concluídas** |
| Modos, propostas, telemetria, validação, brief, estado, embeddings e A/B | **Concluídos** |
| Fase D — multigrafo avançado | **Implementada; interruptores opt-in** |
| Harness de execução do A/B v2 | **Implementado** |
| Nível `institution` no `memory_inspect` do assistente | Pendente operacional |
| Validação com provedores reais de embeddings | Pendente operacional |
| Interface gráfica para configurar o agente de memória | Pendente de produto |
| Laço de profiling (Tracy) e correção automática do código gerado via modelo padrão do Studio | Pendente de engenharia / produto |
| Instrumentação do próprio Arestrix com Tracy e laço de auto-otimização | Pendente de engenharia |
| Ciclo completo de testes e otimização de performance (C++, TypeScript, etc.) a cada release | Pendente de release |
| Validação de release em máquina limpa / assinatura | Pendente de release |

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
`memory.bridges` e `routing.mode` ficam desligados até uma medição com dados
reais. O relatório final está em `tests/fixtures/memory-eval-v1-report.json`.

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
calculado. O que resta é executá-lo com provedores reais e registrar o resultado
no relatório.

### 4.2 Nível institucional no assistente

`memory_inspect` cobre `project`, `workspace`, `vertex` e `all`. O nível `institution` depende de um
status do serviço institucional no `MemoryService` e de configuração explícita de MCP/token.

### 4.3 Provedores reais de embeddings

Os perfis e o fingerprint estão implementados, mas a validação de qualidade e latência com
EmbeddingGemma 2 e Qwen3-Embedding-4B reais ainda deve ser registrada. O fallback lexical é o
comportamento seguro quando o provedor não está disponível.

### 4.4 Interface gráfica do agente de memória

O contrato `memory.claimExtraction.agent` já aceita um id de agente do Studio ou
`{ id, graph, node }` para um agente baseado em grafo. Falta a interface gráfica
do Studio para escolher esse alvo por projeto, listar os agentes/grafos
registrados, mostrar o fallback para o modelo padrão e explicar o erro quando o
alvo configurado não está disponível.

### 4.5 Laço de observação de performance e correção automática no código gerado (Tracy + Modelo Padrão do Studio)

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
    executar o ciclo de otimização guiado pelo Tracy antes de assinar e empacotar os artefatos.
- Validação do instalador em máquina limpa, upgrade, uninstall e Intune/SCCM.
- Assinatura de código via SignPath ou certificado corporativo.
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
ligados após medição com dados reais.
