# Pendências do projeto Arestrix

> **Versão:** 2.5.0 · **Atualizado em:** 2026-10-31
>
> Documento único de pendências do projeto. O plano de implantação da memória (Fases A–C) está
> concluído. A Fase D é um programa separado, fora deste plano, e não bloqueia o release. As
> pendências restantes são operacionais.

## 1. Resumo

| Área | Estado |
| :--- | :--- |
| Fases A–C do plano de memória | **Concluídas** |
| Modos, propostas, telemetria, validação, brief, estado, embeddings e A/B | **Concluídos** |
| Fase D — multigrafo avançado | Programa separado, fora deste plano |
| Harness de execução do A/B v2 | Pendente operacional |
| Nível `institution` no `memory_inspect` do assistente | Pendente operacional |
| Validação com provedores reais de embeddings | Pendente operacional |
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
- Tokens de prompt/cache em `run_usage`.
- Predicados de fato single-valued e multi-valued.
- A/B v2 com conjunto versionado 12/4/4, ordem sorteada, repetições, deltas pareados e IC 95%.
- Ferramentas do assistente do Studio: `memory_inspect`, `memory_review_proposal`,
  `memory_declare_state_key` e `memory_unpromote_playbook`.
- CLI para memória, estado, embeddings, propostas, workspace, unpromote e A/B.

## 3. Fase D — programa separado, fora deste plano

A recuperação com embeddings já existe: perfis EmbeddingGemma 2 e Qwen3-Embedding-4B, fingerprint,
fusão RRF e fallback lexical. A Fase D é o **multigrafo avançado**, um programa separado que não faz
parte do plano de simplificação encerrado neste release:

1. **Facetas** — classificação semântica dos nós e arestas para consultas por aspecto.
2. **Conhecimento de domínio** — camadas de domínio reutilizáveis entre projetos/workspace.
3. **Caminhos sobre afirmações** — navegação explícita por claims, evidências e contradições.
4. **Roteamento de modelos** — escolha de modelo por tipo de consulta/tarefa com base no grafo.
5. **Avaliação semântica** — conjunto de consultas, métricas de relevância e comparação com o
   baseline lexical/híbrido atual.

Dependências já prontas para a Fase D: fingerprint, validação, estado de continuação e `task_type`.

### Fora de escopo (não é pendência)

- Extração ou consolidação por LLM; a memória atual é determinística.
- Sincronização multi-máquina e coordenação de múltiplos workers.
- Checkpoint/retomada do grafo.
- Mudança do protocolo institucional HTTP/MCP.
- Migração automática de projetos antigos; não há importador legado.

## 4. Pendências operacionais

### 4.1 Harness de execução do A/B v2

O núcleo do A/B v2 está pronto: validação do conjunto, ordem, análise pareada e IC. Falta o harness
que executa as 20 tarefas, aplica o validador e grava `ab-observations.json` com `ab_run_id`, `arm`,
`ok`, tokens, turnos, custo e latência. Sem ele, o relatório é apenas analítico.

### 4.2 Nível institucional no assistente

`memory_inspect` cobre `project`, `workspace`, `vertex` e `all`. O nível `institution` depende de um
status do serviço institucional no `MemoryService` e de configuração explícita de MCP/token.

### 4.3 Provedores reais de embeddings

Os perfis e o fingerprint estão implementados, mas a validação de qualidade e latência com
EmbeddingGemma 2 e Qwen3-Embedding-4B reais ainda deve ser registrada. O fallback lexical é o
comportamento seguro quando o provedor não está disponível.

## 5. Pendências de release

- Validação do instalador em máquina limpa, upgrade, uninstall e Intune/SCCM.
- Assinatura de código via SignPath ou certificado corporativo.
- Pacote Linux e matriz de CI Linux/macOS.
- Catálogo remoto assinado.

## 6. Critério para iniciar e encerrar a Fase D

A Fase D pode ser considerada concluída quando houver:

1. especificação de facetas, domínio, claims e roteamento;
2. schema e migração compatíveis com o banco atual;
3. API/UI e ferramentas do assistente para consultar e operar esses níveis;
4. conjunto de avaliação versionado e resultados reprodutíveis;
5. testes unitários, de integração e E2E verdes;
6. documentação PT/EN atualizada e pendência removida deste documento.
