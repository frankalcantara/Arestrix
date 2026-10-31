# Como funciona a memória do Arestrix

> **Aplica-se a:** Arestrix 2.4 — última revisão: 2026-10-30
>
> Guia de uma página e referência da memória de projeto. A memória é opcional, local e
> pertence ao projeto. Não há importação automática de formatos antigos.

## 1. Princípios

- **Desligada por padrão.** Nenhum vértice consulta ou grava conhecimento sem configuração explícita.
- **Três modos.** `off`, `consult` e `learn`. O modo é decidido por vértice.
- **Aprender implica salvar.** `learn` grava um episódio e pode propor conhecimento; `consult` só lê.
- **Nada entra sozinho.** Lições e playbooks nascem como proposta/candidato e só viram conhecimento
  ativo depois de uma aprovação.
- **Telemetria separada.** Episódios, hits e uso de API ficam em banco próprio; limpar sessão ou
  apagar telemetria nunca apaga o conhecimento.
- **Local-first.** SQLite no disco do projeto, sem dependência de nuvem.

O ciclo é:

```text
consultar → executar → observar → propor → aprovar → consolidar → consultar
```

## 2. Onde a memória fica

| Escopo | Caminho | Papel |
| :--- | :--- | :--- |
| Conhecimento do projeto | `<projeto>/.arestrix/memory/memory.db` | lições, fatos, evidências e estado de trabalho |
| Telemetria do projeto | `<projeto>/.arestrix/telemetry/telemetry.db` | episódios, hits, uso de tokens e validações |
| Workspace | `<projeto>/.workspace/memory/memory.db` | conhecimento promovido, compartilhado |
| Instituição | banco do serviço institucional | conhecimento autenticado de vários projetos |

`ARESTRIX_MEMORY_DB` troca o banco de conhecimento e `ARESTRIX_TELEMETRY_DB` troca o de
telemetria. A raiz do projeto vem de `ARESTRIX_PROJECT_ROOT`.

## 3. Configuração do vértice

| Campo | Efeito |
| :--- | :--- |
| `mode: off` | Não lê nem grava memória. |
| `mode: consult` | Injeta o brief autorizado no prompt. Não grava conhecimento. |
| `mode: learn` | Consulta e, ao terminar, grava episódio e propostas de lição. |
| `theme` | Tema principal de leitura e escrita; fallback para `domains[0]`. |
| `readThemes` | Temas extras apenas de leitura. |
| `taskType` | Identidade estável de continuidade; padrão é o id do vértice. |
| `contextBudgetTokens` | Orçamento do brief; `0` desativa a injeção. |
| `maxLessonsPerRun` | Limite de propostas por execução. |
| `maxLessonChars` | Limite de caracteres por lição/trigger. |

A configuração do projeto define o padrão herdado pelos vértices: modo, tema, retenção,
consolidação e embeddings. O vértice pode sobrescrever a memória quando `inheritMemory` é falso.

## 4. O que o brief contém

O brief é montado no início da execução, dentro do orçamento configurado:

1. **Lições ativas** do tema, ordenadas por confiança e recência.
2. **Fatos ativos** ligados ao tema e às consultas da tarefa.
3. **Evidências** das lições e fatos injetados.
4. **Estado de trabalho** apenas quando o formato legado/manual o habilita. O grafo gerado pelo
   Arestrix não usa esse bloco.

Playbooks compilados ficam como **candidatos** e não entram no brief até serem promovidos.
Conhecimento com baixa confiança, conflito aberto ou validade expirada não é injetado.

Cada item injetado recebe um rótulo `[MEMO:<id>]`. No fim da execução, o runtime só marca como
**usado** o item cujo ID foi citado pelo agente na resposta final. “Injetado”, “consultado” e
“usado” são sinais distintos.

O uso altera a **utilidade** do item, não a confiança. Confiança é sustentação (evidência,
validação e promoção); utilidade é o histórico de uso em execuções bem ou mal sucedidas.

## 5. Como o aprendizado entra

Uma execução `learn` grava um **episódio** com entrada, resultado, validação e hashes. A partir
dele, o sistema pode criar:

- **proposta de lição** (`memory_propose_lesson`) quando o agente declara uma regra;
- **proposta automática de falha** quando a tarefa falha (o validador reprova ou o agente estoura
  `maxTurns`);
- **candidato de playbook** quando há sequência suficiente de passos bem-sucedidos.

Falhas de infraestrutura são classificadas por `errorClass` enumerado — transporte/timeout,
429, 5xx, autenticação/chave, gateway/orçamento, configuração, sandbox/ferramenta ou cancelamento —
e **não** geram proposta de lição. `task_max_turns` é falha da tarefa, não de infraestrutura.

Propostas ficam pendentes no painel **Memória do Projeto**. Aprovar promove o item para o
conhecimento ativo; rejeitar mantém o registro para auditoria. Não existe gravação direta de
lição por ferramenta.

Playbooks só saem de `candidate` com validação independente. O gate exige **3 `input_hash`
distintos** entre as últimas 20 execuções validadas do mesmo `task_type`, validador estável e no
máximo **1 promoção por `task_type` em 24h**. Duas falhas consecutivas ou taxa acima de 30% em 10
execuções validadas devolvem o playbook a candidato. `unpromote` está disponível na API.

Fatos também têm **cardinalidade de predicado**. Predicados de acumulação — `USES`, `USES_TOOL`,
`PRODUCES`, `READS`, `WRITES`, `TOUCHES`, `CITES`, `INCLUDES`, `TAGGED_WITH`, `HAS`, `DEPENDS_ON` —
aceitam vários objetos ativos. Predicados funcionais e desconhecidos são single-valued: promover um
novo valor encerra o anterior (`superseded`, com histórico), em vez de deixar dois valores ativos.
O detector de contradições ignora predicados multi-valued.

## 6. Ferramentas do agente

| Ferramenta | Efeito |
| :--- | :--- |
| `memory_search` | Busca somente leitura no conhecimento do projeto e no workspace autorizado. |
| `memory_propose_lesson` | Registra uma proposta pendente de lição; não ativa conhecimento. |
| `memory_state_get` | Lê uma chave de estado declarada; o valor volta rotulado como não confiável. |
| `memory_state_set` | Grava com CAS (`expected_version`); só em modo `learn`. |
| `memory_state_list` | Lista chaves visíveis e metadados, sem valores. |
| `memory_state_history` | Lista versões de uma chave `versioned`. |

`memory_record_lesson`, `memoryWriteKeys` e o servidor nativo `arestrix.memoria` foram removidos.
O conhecimento é acessado pelo runtime e pelo painel, não por um chave-valor paralelo.

## 7. Busca e embeddings

A busca combina:

- **lexical** (FTS) sempre disponível;
- **vetorial** opcional, quando um provedor de embeddings está configurado;
- **fusão RRF** para ordenar os dois resultados.

A busca **não indexa** durante a consulta. A indexação é explícita: o painel do projeto oferece
**Testar** e **Reindexar**. Perfis prontos:

| Perfil | Dimensão |
| :--- | :--- |
| EmbeddingGemma 2 | 768 |
| Qwen3-Embedding-4B | 2560 |

Sem provedor disponível, a busca continua funcionando no modo lexical.

O índice guarda um **fingerprint** SHA-256 de endpoint, modelo, dimensão efetiva, prefixos de
consulta/documento, normalização, métrica e versão do índice. A busca só usa vetores do fingerprint
atual; nunca mistura índices antigos. O status mostra modelo, dimensão, itens indexados, candidatos,
pendentes e um fingerprint curto, com os estados:

| Estado | Significado | Busca |
| :--- | :--- | :--- |
| `ready` | Fingerprint atual, índice completo | híbrida |
| `stale` | Modelo/dimensão/endpoint/template mudou | lexical |
| `pending` | Reindexação em andamento | lexical |
| `partial` | Índice incompleto | lexical + aviso |
| `failed` | Reindexação falhou | lexical + erro |

Trocar de perfil invalida o índice e oferece reindexação. A reindexação é idempotente e descarta
vetores de fingerprints antigos.

## 8. Medição A/B

O painel oferece **Comparar com e sem memória (contexto)**. A ação:

1. cria uma cópia consistente do conhecimento com `VACUUM INTO`;
2. calcula o fingerprint SHA-256 da cópia;
3. mede, por vértice ativo, tokens e caracteres do brief, lições, fatos e playbooks;
4. compara com o baseline sem memória (zero).

Essa é a versão **A/B v1**: mede custo e conteúdo do contexto, não a taxa de aprovação do
validador. O protocolo completo, com tarefas fixas, repetições e telemetria por braço, é a
evolução seguinte.

## 9. Retenção, segurança e escopo

- **Escopo estrito:** o conhecimento do projeto não vaza para outro projeto; o workspace só é
  lido quando configurado e autorizado.
- **Sanitização:** segredos conhecidos são removidos antes de qualquer gravação, leitura legada
  ou exportação.
- **Retenção:** afeta telemetria (episódios, hits, uso). Conhecimento não é apagado por rotina de
  retenção.
- **Limpeza de sessão:** em memória efêmera, limpa apenas a sessão; nunca purga um banco de arquivo.
- **Auditoria:** aprovações, rejeições, promoções, conflitos e expurgos deixam rastro.

## 10. Estado de continuação

O estado de continuação substitui o antigo Blackboard e o `arestrix.memoria`. Ele fica **fora do
brief** e só é acessível pelas ferramentas.

- A chave é **declarada pelo projeto**; o agente nunca cria chave.
- Identidade: `(scope_kind, scope_owner, key)`.
- Escopos: `project`, `task_type` e `item`. Cada chave tem um único escopo, sem merge entre eles.
- O valor é validado por um subconjunto de JSON Schema **depois** da sanitização de segredos.
- `replace` mantém só a versão atual; `versioned` mantém as últimas K versões.
- TTL é aplicado na leitura.
- CAS: `expected_version` ausente cria; versão igual grava; versão diferente devolve conflito com a
  versão atual, sem sobrescrever.
- Mudança de schema marca o valor antigo como `stale`; a próxima escrita informa a versão atual.
- `memory_state_list` não devolve valores. Leituras são rotuladas como dados de execução anterior
  não confiáveis.
- Escritas, recusas e conflitos são auditados; leituras não.

Declaração e operação manual pela CLI:

```bash
arestrix-cli memory state declare --project . --key cursor --scope project \
  --schema '{"type":"object","properties":{"n":{"type":"integer"}},"required":["n"]}'
arestrix-cli memory state set --project . --key cursor --value '{"n":1}'
arestrix-cli memory state get --project . --key cursor --json
arestrix-cli memory state list --project . --json
```

## 11. Referência rápida

```text
Projeto
  .arestrix/memory/memory.db       conhecimento
  .arestrix/telemetry/telemetry.db telemetria
  .workspace/memory/memory.db      workspace (opcional)

Vértice
  off      não usa memória
  consult  lê o brief e registra hits
  learn    lê, grava episódio e propõe lições

Ciclo
  executar -> episódio -> proposta -> aprovação -> conhecimento ativo -> brief

Estado de continuação
  chave declarada -> memory_state_get/set/list/history -> fora do brief
```

Para operar pela linha de comando, consulte [`arestrix-cli.md`](arestrix-cli.md). Para a visão de
produto e os limites atuais, consulte [`workspaces-memoria-skills-mcp.md`](workspaces-memoria-skills-mcp.md).

## 12. Instituição

O serviço `arestrix-memory` expõe o conhecimento institucional por HTTP autenticado e MCP (stdio e
`/mcp`), com tokens, papéis `viewer`/`editor`/`admin` e escopo invariante. Ele é opcional e separado
do banco privado do projeto.
