# Pendências do projeto Arestrix

> **Versão:** 2.5.0 · **Atualizado em:** 2026-10-31
>
> Documento único de pendências do projeto. O plano de memória (Fases A–C) e o
> multigrafo avançado (Fase D / E11–E20) estão concluídos. As pendências
> restantes são operacionais.

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

## 5. Pendências de release

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
