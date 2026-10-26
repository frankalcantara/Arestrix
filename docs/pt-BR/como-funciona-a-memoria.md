# Como funciona a memória do Arestrix

> **Aplica-se a:** Arestrix Beta 1.5 (`1.5.0-beta.1`) — última revisão: 2026-10-25
>
> Este guia descreve a memória implementada no núcleo C++20 + SQLite. Ele cobre o ciclo
> completo: captura, proposta, promoção, consolidação, recuperação, embeddings, playbooks,
> workspace e o contrato institucional local.

## 1. Princípios

A memória do Arestrix foi projetada para ser:

- **local-first:** projeto, workspace e instituição usam SQLite local; nada depende de nuvem;
- **nativa:** o caminho de execução é C++20 + SQLite, sem Python ou JavaScript;
- **aditiva:** schemas novos são criados/migrados sem apagar dados antigos;
- **auditável:** promoções, conflitos, decay, políticas e expurgos deixam rastro;
- **opt-in:** memória e embeddings são desligados por padrão;
- **temporal:** fatos têm validade, confiança e histórico; nada é sobrescrito silenciosamente;
- **recuperável:** snapshots versionados permitem restaurar o grafo cognitivo.

O princípio operacional é:

```text
capturar → propor → provar → promover → consolidar → recuperar → medir
```

## 2. Onde a memória fica

| Escopo | Banco | Papel |
| :--- | :--- | :--- |
| Projeto | `<projeto>/.arestrix/memory/memory.db` | memória privada do projeto |
| Workspace | `<projeto>/.workspace/memory/memory.db` | memória compartilhada, somente promovida |
| Instituição | banco próprio do contrato institucional | memória institucional autenticada |

O caminho do banco do projeto pode ser sobrescrito por `ARESTRIX_MEMORY_DB`. A raiz usada pelo
binário gerado vem de `ARESTRIX_PROJECT_ROOT`. O workspace só é aberto automaticamente quando
`.workspace/memory/memory.db` já existe; caso contrário, use `memory workspace init`.

## 3. Arquitetura

```text
Agente / Tools / Studio / CLI
            │
            ▼
     MemoryService  ──────────────►  MemoryInstitutionService
            │                              │
            ▼                              ▼
       MemoryStore  ◄──── MemoryConsolidator / MemoryPlaybookCompiler
            │
            ├── MemorySanitizer
            ├── MemoryExtractor
            ├── MemoryIndexer ──► MemoryVectorIndex / EmbeddingProvider
            └── MemoryRanker
```

### Componentes

- **`MemoryService`**: fachada pública para projeto, workspace, embeddings, consolidação,
  propostas, playbooks e status.
- **`MemoryStore`**: persistência SQLite, schema, FTS5, transações, snapshot, purge, prune,
  aliases, políticas e métricas.
- **`MemorySanitizer`**: remove segredos antes de qualquer escrita, leitura legada ou export.
- **`MemoryExtractor`**: transforma episódios sanitizados em propostas determinísticas.
- **`MemoryConsolidator`**: dedup, conflitos, expiração, decay e generalização.
- **`MemoryPlaybookCompiler`**: transforma trajetórias bem-sucedidas em playbooks.
- **`EmbeddingProvider`**: provider OpenAI-compatible, opcional.
- **`MemoryVectorIndex` / `MemoryIndexer` / `MemoryRanker`**: índice vetorial, indexação e RRF.
- **`MemoryInstitutionService`**: contrato local de tokens, papéis e políticas.
- **`DatabaseBackupService`**: snapshot, restore, quarentena e retenção no shutdown.
- **`Agent` / `Tools`**: gravam episódios, lições, hits e usage; expõem `memory_search`.

## 4. Modelo de dados

O banco é um multigrafo temporal. As principais tabelas são:

| Tabela | Conteúdo |
| :--- | :--- |
| `memory_scope` | projeto, workspace ou instituição |
| `memory_scope_closure` | ancestralidade e profundidade entre escopos |
| `memory_node` | entidades, domínios, regras, sumários e conflitos |
| `memory_edge` | fatos, lições (`HAS_LESSON`), conflitos e generalizações |
| `memory_episode` | execução sanitizada: outcome, ferramentas, arquivos, artefatos, evidência |
| `memory_working` | Blackboard: chave/valor por item de pipeline |
| `memory_evidence` | evidência estruturada ligada a arestas/episódios |
| `memory_hit` | memória injetada, consultada e usada por execução |
| `memory_usage` | tokens, chamadas, latência e contexto de memória |
| `memory_audit` | ações de escrita, promoção, consolidação, decay e política |
| `memory_proposal` | fila `pending/accepted/rejected/superseded` |
| `memory_embedding` | vetores float32 little-endian por modelo/objeto |
| `memory_playbook` | passos, contadores, confiança, origem e evidência |
| `memory_alias` | resolução de entidades por nome normalizado |
| `memory_policy` | políticas invariantes por escopo |

### Estados

- Arestas: `candidate`, `active`, `rejected`, `superseded`.
- Propostas: `pending`, `accepted`, `rejected`, `superseded`.
- Playbooks: `candidate`, `active`; o schema aceita outros estados para evolução.
- **Dormant** não é um status de aresta: é `dormant_at` preenchido. A aresta continua no banco,
  mas sai da recuperação.
- Fatos expirados mantêm `valid_to` e são marcados `superseded` com `metadata.expired`.

## 5. Escrita: o que entra na memória

### Lições

`memory_record_lesson` (e a promoção) passam por governança:

- domínio obrigatório quando o agente tem `memoryDomains`;
- `writeKeys` limita o Blackboard;
- `maxLessonsPerRun` limita a quantidade por execução;
- `maxLessonChars` limita regra e gatilho;
- teto global de 5.000 lições ativas por projeto para escritas de agente;
- dedup exato por domínio + regra.

Falhas de agente também geram lição automática quando `recordLessons` está habilitado.

### Episódios

No fim de cada execução de agente, o runtime grava um episódio com:

- `task_type`, `outcome`, `error_class`;
- sequência de ferramentas (`toolCalls`);
- arquivos tocados (`filesTouched`);
- artefatos (`artifacts`);
- evidência e sinais de sucesso;
- contagem de redactions.

Prompts/respostas brutos e argumentos de ferramentas **não** são armazenados.

### Blackboard

O Blackboard é um espaço de trabalho por `item_id`. Leitura e escrita são controladas por
`allowedKeys`/`writeKeys`. `contextBudgetTokens = 0` desliga o brief.

### Sanitização

Antes de persistir, `MemorySanitizer` remove padrões como chaves OpenAI/Stripe, Google, AWS,
GitHub, Slack, `Bearer`, atribuições `api_key=...`, parâmetros de URL e chaves PEM. A
sanitização também é aplicada em leituras de dados legados e no export de snapshot.

## 6. Propostas e promoção

### Extração determinística

O `MemoryExtractor` lê um episódio sanitizado e gera propostas:

- **sucesso:** fatos de ferramenta (`USES_TOOL`), artefatos (`PRODUCES`), entidades, playbook e
  sumário;
- **falha:** apenas uma lição procedural; nunca um fato;
- confiança calculada por outcome, artefatos e evidência;
- limite de 20 propostas por episódio.

As propostas ficam `pending`. Nada é promovido automaticamente pelo extrator.

### Revisão

- Studio → **Memória do Projeto → Propostas**;
- CLI → `arestrix-cli memory proposals list|accept|reject`.

### Promoção

Promover exige evidência não vazia. O destino depende do tipo:

- `lesson` → `recordLesson` com `source_kind = promotion`;
- `fact` → resolução de entidades + aresta `active` com `source_kind = promotion`;
- `entity` → nó canônico;
- `playbook` → playbook `active`, reutilizando a assinatura existente;
- `summary` → nó `summary`.

Toda promoção grava `memory_audit`. Fatos duplicados reutilizam a aresta ativa; o mesmo fato não
cria aresta duplicada.

## 7. Recuperação

Há dois caminhos principais.

### `memory_search` (busca explícita)

1. FTS5 sobre nomes de nós;
2. arestas ativas no grafo;
3. lições;
4. playbooks;
5. embeddings, se habilitados;
6. fusão RRF lexical + vetorial;
7. filtro de `memoryDomains` e limite.

Se o provider de embeddings falhar, a busca cai automaticamente para lexical. O resultado
registra hits:

- `injected=1` quando entrou no brief;
- `used=1` quando o agente consultou/usou;
- `consulted` para memória descoberta por busca.

### Brief injetado

`buildBrief` monta, dentro do orçamento de tokens:

1. lições do projeto;
2. lições promovidas do workspace;
3. fatos relevantes (híbrido, quando ligado);
4. playbooks ativos por `task_type`;
5. Blackboard autorizado.

Cada seção respeita o orçamento. O brief nunca corta JSON parcial: ele trunca por linha/seção e
marca `truncated`. `contextBudgetTokens = 0` não injeta nada.

## 8. Embeddings: EmbeddingGemma 2

O provider é opcional e desligado por padrão.

- Modelo padrão: **`embeddinggemma-2`**;
- dimensão nativa: **768** (MRL: 512/256/128);
- contexto: até **8K tokens**;
- licença: Apache 2.0;
- endpoint: OpenAI-compatible `/v1/embeddings`;
- prefixos assimétricos:
  - consulta: `task: search result | query: ...`
  - documento: `title: none | text: ...`
- fallback: sem provider, memória 100% lexical.

### Como ativar com Ollama

```powershell
ollama pull embeddinggemma-2
```

No Studio:

- **Global Defaults → Memória**;
- habilitar busca semântica;
- endpoint `http://127.0.0.1:11434/v1`;
- modelo `embeddinggemma-2`;
- dimensões `768`;
- **Testar embeddings**;
- **Reindexar**.

### Índice

O índice grava vetores em `memory_embedding` com `content_hash`, modelo e domínio. A indexação é
incremental; `memory reindex` reconstrói tudo. O snapshot cognitivo **não** carrega vetores:
após um restore, rode `memory reindex`.

## 9. Consolidação

Executada sob demanda por `memory consolidate` ou pelo botão **Consolidar memória**.

- **Dedup:** normalização + Jaccard para lições/fatos; duplicados viram `superseded` e a
  evidência é fundida no canônico.
- **Conflitos:** mesmo sujeito + predicado com objeto diferente; o perdedor vira `candidate`,
  ambos recebem `conflictGroup`, e um nó `conflict` + arestas `CONTRADICTS` são criados.
- **Expiração:** `valid_to` vencido vira `superseded` com `metadata.expired`.
- **Decay:** fatos sem uso viram `dormant_at`; não são apagados.
- **Generalização:** domínios com N lições ganham nó `summary` + arestas `GENERALIZES`.
- **Reforço:** hit usado em sucesso soma `success_count`, limpa `dormant_at` e sobe confiança;
  falha soma `failure_count` e reduz confiança.

A consolidação é idempotente e auditada. Ela não roda no hot path do grafo.

### Consolidação em background

O `MemoryService` mantém um worker in-process, opt-in e desligado por padrão, que consome apenas
episódios já persistidos. Ele não participa da busca nem bloqueia a gravação de um episódio.

- `consolidationMode=off`: somente manual (padrão);
- `consolidationMode=idle`: consolida após `consolidationIdleSeconds` sem novos episódios;
- `consolidationMode=after_episodes`: consolida a cada `consolidationEpisodeThreshold` episódios;
- `consolidationDormantDays` controla o decay aplicado na rodada.

O worker só é iniciado para projetos persistentes. Ao fechar o projeto ou encerrar o processo, a
thread é sinalizada e aguardada fora do mutex do serviço, sem deixar trabalho em segundo plano
órfão. `consolidationStatus` expõe `runs`, `lastRunAt`, `lastReason`, `lastError`, `lastReport`,
`nextInEpisodes` e `nextInSeconds` no processo que hospeda o worker. O painel do Studio mostra o
modo configurado no grafo; como o pipeline gerado roda em outro processo, os contadores ao vivo
pertencem ao processo que está executando o worker.

## 10. Playbooks

O `MemoryPlaybookCompiler` observa episódios quando `recordLessons` está habilitado.

- assinatura = `task_type` + sequência de ferramentas;
- 1º sucesso → `candidate`;
- 2º sucesso → `active`;
- falha incrementa `failure_count`, reduz confiança e pode rebaixar para `candidate`;
- falha sem playbook correspondente não cria nada.

Playbooks ativos entram no brief por `task_type`, com passos compactos, dentro do orçamento.

## 11. Workspace

- banco próprio em `.workspace/memory/memory.db`;
- o projeto **lê** o workspace;
- o workspace **não lê** a memória privada do projeto;
- promoção explícita projeto → workspace para `lesson`, `fact` e `playbook`;
- promoção exige evidência e grava auditoria nos dois bancos;
- promoção não apaga o original;
- irmãos não se enxergam.

CLI:

```powershell
arestrix-cli memory workspace init --project .\meu-projeto --workspace-id equipe
arestrix-cli memory workspace status --project .\meu-projeto --workspace-id equipe
arestrix-cli memory promote --project .\meu-projeto --workspace-id equipe `
  --kind lesson --id <lesson-id> --evidence "revisada por duas pessoas"
```

## 12. Instituição

O núcleo tem `MemoryInstitutionService` e o executável `arestrix-memory`, que expõe o contrato
institucional por HTTP e por MCP:

- tokens mantidos apenas em memória;
- papéis `viewer`, `editor`, `admin`;
- leitura autenticada;
- escrita exige `editor`;
- políticas exigem `admin`;
- `memory_policy` permite política imutável de escopo ancestral; filho não sobrescreve.

### Iniciar o serviço

```powershell
arestrix-memory token --principal ana --role viewer
arestrix-memory init --db C:\memory\institution.db --institution acme
arestrix-memory serve --db C:\memory\institution.db --institution acme `
  --host 127.0.0.1 --port 8790 --tokens-file C:\memory\tokens.txt
```

O arquivo de tokens tem uma linha por principal:

```text
# token principal papel
<token-da-ana> ana viewer
<token-do-bruno> bruno editor
<token-da-carla> carla admin
```

Também é possível usar `--tokens-json` ou `ARESTRIX_INSTITUTION_TOKENS` com um array:

```json
[{"token":"<token>","principal":"ana","role":"viewer"}]
```

Sem tokens configurados, o serviço inicia em modo fechado: todas as rotas autenticadas respondem
`401`. O serviço não persiste tokens; reiniciar exige recarregar o arquivo ou a variável. Guarde o
arquivo de tokens fora do repositório e restrinja as permissões do sistema operacional.

### API HTTP

| Método | Rota | Papel | Efeito |
| :--- | :--- | :--- | :--- |
| `GET` | `/health` | — | liveness, sem autenticação |
| `GET` | `/v1/status` | `viewer` | instituição, principal, papel e lições |
| `POST` | `/v1/context` | `viewer` | busca `{ "query", "limit", "domains" }` |
| `POST` | `/v1/lessons` | `editor` | grava `{ "domain", "triggerError", "lessonRule" }` |
| `POST` | `/v1/policies` | `admin` | grava `{ "key", "value", "immutable" }` |
| `POST` | `/mcp` | bearer | JSON-RPC 2.0 do MCP |
| `GET` | `/metrics` | `viewer` | contadores Prometheus do processo |

Exemplo:

```powershell
curl.exe -H "Authorization: Bearer $env:ARESTRIX_INSTITUTION_TOKEN" `
  -H "Content-Type: application/json" `
  -d '{"query":"convencao de estilo","limit":5}' `
  http://127.0.0.1:8790/v1/context
```

### MCP

O modo `mcp` usa JSON-RPC por linha no stdin/stdout:

```powershell
$env:ARESTRIX_INSTITUTION_TOKEN = "<token>"
arestrix-memory mcp --db C:\memory\institution.db --institution acme --role editor
```

Ferramentas expostas:

- `memory_institution_context` (`viewer`);
- `memory_institution_write_lesson` (`editor`);
- `memory_institution_status` (`viewer`);
- `memory_institution_set_policy` (`admin`).

No Studio, configure um servidor MCP stdio apontando para o binário:

```json
{
  "id": "arestrix-memory",
  "transport": "stdio",
  "command": "C:\\Arestrix\\arestrix-memory.exe",
  "args": ["mcp", "--db", "C:\\memory\\institution.db", "--institution", "acme", "--role", "editor"],
  "env": { "ARESTRIX_INSTITUTION_TOKEN": "${ARESTRIX_INSTITUTION_TOKEN}" }
}
```

O endpoint HTTP `/mcp` aceita o mesmo JSON-RPC com `Authorization: Bearer ...`.

### Operação

- escute em `127.0.0.1` e coloque um **reverse proxy TLS** na frente para exposição em rede;
- o banco é SQLite single-node; não há sincronização multi-máquina nesta versão;
- o serviço não implementa rate limit próprio; use o proxy/gateway para isso;
- logs não incluem tokens, corpos de lições ou conteúdo de memória;
- `/metrics` também exige Bearer token (`viewer` suficiente) e expõe apenas contadores agregados:
  requisições REST/MCP, notificações, falhas 401, respostas 2xx/4xx/5xx, uptime e versão;
- o worker de consolidação é por processo. Duas instâncias do `arestrix-memory` não coordenam a
  mesma rodada; para operação multi-instância, mantenha a consolidação manual ou externa.

## 13. Snapshot, retenção e expurgo

- Snapshot v2 inclui escopos, closure, nós, arestas, working, lições, aliases e playbooks.
- Snapshot v1 continua importável.
- Escrita atômica e proteção contra snapshot vazio sobrescrevendo memória existente.
- Banco corrompido é colocado em quarentena como `memory.db.corrupt-<stamp>`.
- `memory purge` remove grafo, propostas, playbooks, embeddings, aliases, hits, usage,
  episódios e (opcionalmente) auditoria; depois roda `VACUUM`.
- `memory prune --days N` remove telemetria antiga sem tocar no grafo cognitivo.
- Retenção também roda no shutdown, independentemente de Git estar habilitado.

## 14. Configuração

Campos em **Global Defaults → Memória**:

| Campo | Padrão |
| :--- | :--- |
| `enabled` | `false` |
| `persistent` | `false` |
| `readBlackboard` / `writeBlackboard` | `false` |
| `semanticLessons` / `recordLessons` | `false` |
| `writeKeys` | `[]` |
| `maxLessonsPerRun` | `20` |
| `maxLessonChars` | `4000` |
| `retentionDays` | `180` |
| `consolidationMode` | `off` |
| `consolidationIdleSeconds` | `300` |
| `consolidationEpisodeThreshold` | `20` |
| `consolidationDormantDays` | `180` |
| `contextBudgetTokens` | `800` |
| `embeddingEnabled` | `false` |
| `embeddingModel` | `embeddinggemma-2` |
| `embeddingDimensions` | `768` |
| `embeddingBatchSize` | `8` |
| `embeddingMaxChars` | `8000` |
| `embeddingTimeoutSeconds` | `60` |

Variáveis de ambiente de embeddings:

```text
ARESTRIX_EMBEDDINGS_ENABLED
ARESTRIX_EMBEDDING_BASE_URL
ARESTRIX_EMBEDDING_MODEL
ARESTRIX_EMBEDDING_BATCH
ARESTRIX_EMBEDDING_TIMEOUT
ARESTRIX_EMBEDDING_MAX_CHARS
ARESTRIX_EMBEDDING_DIM
ARESTRIX_EMBEDDING_API_KEY
```

## 15. CLI operacional

```powershell
arestrix-cli memory report --project .\meu-projeto
arestrix-cli memory proposals list --project .\meu-projeto
arestrix-cli memory consolidate --project .\meu-projeto
arestrix-cli memory embeddings status --project .\meu-projeto
arestrix-cli memory embeddings test --project .\meu-projeto `
  --base-url http://127.0.0.1:11434/v1 --model embeddinggemma-2
arestrix-cli memory reindex --project .\meu-projeto
arestrix-cli memory workspace init --project .\meu-projeto --workspace-id equipe
arestrix-cli memory prune --project .\meu-projeto --days 180
arestrix-cli memory purge --project .\meu-projeto

# Serviço institucional (binário separado)
arestrix-memory token --principal ana --role viewer
arestrix-memory init --db C:\memory\institution.db --institution acme
arestrix-memory serve --db C:\memory\institution.db --institution acme `
  --port 8790 --tokens-file C:\memory\tokens.txt
arestrix-memory mcp --db C:\memory\institution.db --institution acme --role editor
```

## 16. Métricas e saúde

`memory report` mostra tokens, chamadas, retries, latência, contexto, hits e embeddings.
`memory embeddings status` mostra modelo, endpoint efetivo, estado, vetores indexados e
pendentes. `memory embeddings test` faz uma chamada real; se o Ollama estiver fora, a mensagem
indica o comando de pull.

## 17. Limitações conhecidas

- o servidor institucional não faz TLS nem rate limit próprios; use um reverse proxy;
- tokens institucionais não persistem entre reinícios;
- consolidação em background ainda é manual;
- generalização por LLM não faz parte deste núcleo;
- o snapshot não carrega vetores; reindexe após restore;
- políticas institucionais são locais e tokens não persistem;
- a dívida da ribbon no Studio é independente da memória.

## 18. Checklist para colocar embeddings em produção local

1. `ollama pull embeddinggemma-2`;
2. endpoint `http://127.0.0.1:11434/v1`;
3. modelo `embeddinggemma-2`, 768 dimensões;
4. **Testar embeddings**;
5. **Reindexar**;
6. confirmar `indexed > 0` e `pending = 0`;
7. rodar uma busca semântica e comparar com o modo lexical;
8. manter o fallback lexical habilitado (é automático).
