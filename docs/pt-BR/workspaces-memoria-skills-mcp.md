# Workspaces, memória, skills e MCP

> **Aplica-se a:** Arestrix Beta 2 (`2.0.0-beta.1`) · Última revisão: 2026-10-25

[English](../en/workspaces-memory-skills-mcp.md) · [Índice](../README.md)

Esta página cobre as partes do Arestrix que ficam fora de um único grafo. Para a divisão Studio /
Workspace / Projeto em si, veja [Arquitetura](arquitetura.md#studio-workspace-e-projeto).

Para o ciclo completo da memória — captura, propostas, embeddings, consolidação, playbooks e
workspace — veja [Como funciona a memória](como-funciona-a-memoria.md).

## Projetos e workspaces

Um projeto é descrito por `arestrix.project.json` na sua pasta:

| Campo | Significado |
| :--- | :--- |
| `projectId`, `name`, `version`, `description` | Identidade. O id é derivado de forma determinística do nome (SHA-256). |
| `rootPath` | A pasta física do workspace. |
| `pipelines` | Os grafos do projeto (`pipelines/main.json` é o principal). |
| `database` | O arquivo SQLite, por padrão `storage/database.sqlite`. |
| `git` | Sincronização opcional com repositório Git/GitHub (`enabled`, `backupMode`, `provider`, `serverUrl`, `repositoryName`, `remoteUrl`, `isPrivate`, `autoCommitOnRun`, `autoCommitOnShutdown`, `timedPushEnabled`, `timedPushIntervalMinutes`, `lastSyncAt`). |
| `globalDefaults` | Padrões de `model`, `sandbox`, `codeMode` (ferramentas permitidas) e `memory` para os vértices. |
| `environment` | Variáveis locais ao projeto (`.env`). |

O Studio mantém a lista de projetos recentes e restaura o último ao iniciar. *Salvar Como* gera um pacote
comprimido `.arestrix`.

### Sincronização Git por projeto (GitHub e repositórios remotos)

Cada projeto no Arestrix pode ser vinculado a um repositório Git independente em um servidor configurável (como GitHub, GitLab ou servidores privados):

1. **Uso do Git Estritamente Opcional (Opt-In por Padrão e Modo 100% Local):**
   - Ao criar qualquer novo projeto no Arestrix, a sincronização com Git ou GitHub vem **desabilitada por padrão** (`enabled: false`).
   - Se o usuário ou empresa optar por não utilizar o Git, o projeto opera em modo 100% local: todos os arquivos, pipelines, banco SQLite (`storage/database.sqlite`) e memória cognitiva (`.arestrix/memory/memory.db`) permanecem exclusivamente no disco rígido local. Nenhuma chamada de rede, comando do Git ou exportação de dump é executada. A governança, redundância e políticas de backup contra perda de dados passam a ser de inteira responsabilidade da empresa ou usuário.
   - O recurso pode ser ativado a qualquer momento clicando no botão Git da barra superior (Quick Access Toolbar) ou no menu Arquivo, marcando a opção *"Habilitar Sincronização Git / GitHub"*.
2. **Modo de Backup Configurável (Privacidade & Conformidade LGPD/GDPR):**
   - **Estrutura e Dados (DDL + INSERTs) (`ddl_and_data`):** Exporta o esquema e todos os dados tabulares em texto determinístico. Indicado para ambientes de desenvolvimento com dados sintetizados ou projetos sem dados sensíveis.
   - **Apenas Estrutura / DDL (Schema Only) (`schema_only`):** Exporta somente a definição das tabelas (`CREATE TABLE`, `CREATE INDEX`, `CREATE VIEW`), sem nenhum `INSERT INTO`. Indicado para conformidade estrita com LGPD/GDPR, proteção de dados pessoais (PII) e prevenção contra repositórios volumosos quando o banco local armazena grandes volumes de dados de negócio.
3. **Commit e Push inteligentes e sob demanda (Proteção de Cotas GitHub):**
   - **A cada execução do pipeline (`F5`):** Um commit **local** com mensagem contextual é gerado no clone local sem disparar `push` remoto. Isso elimina o risco de *rate limiting* e consumo desnecessário de cota de rede no GitHub durante sessões intensas de desenvolvimento e depuração interativa.
   - **Push temporizado automático (padrão a cada 10 minutos):** Caso existam alterações locais pendentes ou commits não enviados, o Arestrix realiza o push em lote de forma transparente e periódica para o repositório remoto. O intervalo e a ativação podem ser configurados no manifesto e modal do projeto.
   - **Botão de Sincronização Manual (Push Imediato):** Disponível diretamente na barra superior (Quick Access Toolbar) e na aba de automação do modal, permitindo enviar alterações ao GitHub a qualquer momento com um único clique.
   - **No encerramento do Arestrix (`onShutdown`):** Se o Git estiver habilitado, disparado nativamente pelo backend C++ ao fechar o aplicativo ou a janela, garantindo que nenhum trabalho fique não versionado no servidor remoto. Se o Git estiver desativado, o encerramento conclui imediatamente sem gerar dumps nem executar processos externos.
4. **Dumps Determinísticos e Prevenção de Inflamento de Histórico (Packfiles):**
   - Dumps SQL e snapshots de memória não incluem cabeçalhos voláteis de data/hora (`-- Gerado em: ...`). Caso o conteúdo dos dados e da memória não tenha sido alterado entre execuções, o arquivo em disco não é sobrescrito, eliminando "ghost commits" e mantendo o histórico do Git enxuto.
5. **Proteção Rigorosa contra Limites de Arquivo do GitHub (Teto de 95 MB):**
   - Para prevenir sumariamente a rejeição por erro de push `GH001` do GitHub (que bloqueia permanentemente arquivos superiores a 100 MB), o `DatabaseBackupService` impõe um teto estrito de segurança de 95 MB por dump. Caso o script SQL exceda esse limite, a exportação é abortada e o usuário é orientado a utilizar o modo *"Apenas Estrutura / DDL (Schema Only)"*.
6. **Auditoria de Pré-Commit Nativa (Secret Scanning):**
   - O `GitSyncService` audita automaticamente o *staged diff* antes de cada commit contra padrões de credenciais expostas (`sk-...`, `AIza...`, `AKIA...`, `ghp_...`). A operação é bloqueada sumariamente (*fail-closed*) caso qualquer segredo seja detectado.
7. **Exclusões Robustas no `.gitignore` (`ensureSafeGitignore`):**
   - Exclusão forçada e automática de bancos SQLite binários (`*.sqlite`, `*.db`), caches de runtime (`.arestrix/`), pesos de modelos locais de IA (`*.gguf`, `*.safetensors`, `*.bin`, `*.onnx`, `models/`), datasets colunares pesados (`*.parquet`, `*.arrow`, `*.feather`, `*.h5`), mídias e arquivos temporários.
8. **Segurança Absoluta (Zero-Secrets no Arestrix):**
   - Nenhum dado de autenticação, senha ou Personal Access Token (PAT) é registrado nos arquivos, manifestos ou banco do Arestrix. O registro de identidade e autenticação é gerenciado **exclusivamente pelo Sistema Operacional** através do **Git Credential Manager (GCM)** / Cofre de Credenciais do Windows.
9. **Repositórios Privados por Padrão:** Novos repositórios criados via GitHub CLI (`gh`) são marcados obrigatoriamente como **privados** (`isPrivate: true`).
10. **Aviso de Governança para Serviços Comerciais:** Quando o servidor configurado for um serviço comercial externo (como `github.com` ou `gitlab.com`), o Arestrix exibe obrigatoriamente um aviso legal de que os dados do projeto residirão em infraestrutura de terceiros fora do controle do software, exigindo consentimento explícito do usuário.

### Bancos de dados, versionamento seguro e recuperação de desastre

O Arestrix opera com dois bancos de dados SQLite no escopo de cada projeto:

1. **Banco Operacional / Analítico do Projeto (`storage/database.sqlite`):** Armazena tabelas manipuladas pelos agentes e nós do pipeline via MCP `workspace-sqlite` ou ferramentas nativas (`sql_query`, `sql_execute`), além de tabelas importadas (ex: planilhas Excel via `ImportExcelModal`).
2. **Banco de Memória Cognitiva AMG (`.arestrix/memory/memory.db`):** Armazena o multigrafo temporal de aprendizado dos agentes (lições aprendidas, relações conceituais, Blackboard compartilhado e telemetria de tokens).

#### Por que o banco binário não é enviado ao GitHub?
* **Limites de Repositório do GitHub:** O GitHub recomenda manter repositórios com menos de 1 GB (com teto recomendado de 5 GB a 10 GB) e bloqueia sumariamente arquivos individuais acima de 100 MB.
* **Inflamento Rápido do Histórico (Packfiles):** Bancos SQLite são arquivos binários mutáveis. Cada execução do pipeline altera páginas no arquivo; como o Git não calcula diffs de texto em binários, uma cópia inteira do arquivo seria adicionada ao histórico a cada commit, inflando o repositório em poucos dias.
* **Políticas de Uso Aceitável:** O GitHub proíbe expressamente o uso de repositórios como serviço de backup automatizado em nuvem ou hospedagem de banco de dados ativo.

#### Ciclo de Dump em Texto e Restauração Transparente (`DatabaseBackupService`)

Quando a sincronização Git está habilitada, o Arestrix implementa um ciclo automatizado de **Dump em Texto e Auto-Recuperação**:

* **Exportação no Encerramento (`onShutdown`):**
  * **Banco Operacional (`storage/database.sql`):** Exporta um script SQL determinístico contendo o esquema DDL (`CREATE TABLE`, `CREATE INDEX`, `CREATE VIEW`) e, opcionalmente, os dados tabulares conforme o modo de backup selecionado.
  * **Memória dos Agentes (`storage/memory.dump.json`):** Exporta um snapshot determinístico v2 com o grafo cognitivo completo: escopos, closure, nós, arestas temporais (validade, confiança, status e proveniência) e Blackboard. Mantém `lessons` e `working` para leitores v1. Telemetria (`memory_usage`, hits, episódios) fica de fora. O banco é ancorado na raiz do projeto, e um payload vazio não sobrescreve um snapshot com conhecimento.
* **Restauração Automática (*Disaster Recovery* / Clone limpo):**
  * Ao clonar o projeto em um novo computador ou caso o arquivo `.sqlite` seja corrompido ou apagado acidentalmente, o Arestrix detecta na abertura do projeto que os binários físicos estão ausentes.
  * O motor nativo C++ (`ProjectManager::loadProject`) executa a restauração automática do banco a partir de `storage/database.sql` e da memória a partir de `storage/memory.dump.json`, recriando as tabelas e o conhecimento dos agentes sem exigir comandos manuais do usuário.


## Padrões globais e herança

Os **padrões globais** (barra de ferramentas) guardam as configurações de modelo, sandbox, ferramentas e
memória. Cada vértice pode herdá-los (uma caixa de seleção no vértice) ou substituí-los. Chaves do Studio nunca se misturam com as variáveis `.env` do projeto.

## Memória

A memória é opcional e vem desativada por padrão. Na Beta 2 ela é implementada pelo **AMG
(Arestrix Memory Graph)**, um multigrafo temporal em SQLite, nativo em C++20.

| Tipo | Escopo | Finalidade |
| :--- | :--- | :--- |
| **Blackboard** | Projeto | Estado chave-valor compartilhado entre os agentes do mesmo projeto. |
| **Lições semânticas** | Projeto | Lições que os agentes registram e consultam depois. |
| **Efêmera ou persistente** | Por vértice | Só em RAM, ou gravada no SQLite do projeto. |
| **Consumo de API** | Projeto | Tokens, chamadas, latência e tamanho do contexto injetado. |

A memória é particionada por `projectId`, então dois projetos nunca leem os dados um do outro. Cada vértice
declara quais chaves pode ler e escrever no Blackboard, quais domínios semânticos pode consultar e
quantas lições pode gravar por execução; `memory_search` respeita esses domínios.

### O que existe hoje

- Banco SQLite em `.arestrix/memory/memory.db` por projeto.
- Esquema estruturado em duas camadas:
  * **Dados Indispensáveis (Cognitivos):** `memory_scope`/`memory_scope_closure` (hierarquia), `memory_node` (conceitos), `memory_edge` (lições aprendidas e relações semânticas com confiança/validade) e `memory_working` (Blackboard compartilhado de projeto).
  * **Dados Efêmeros e Telemetria:** `memory_usage` (contagem de tokens, chamadas e latência), `memory_hit` (estatísticas de injeção em prompts para benchmark A/B), `memory_episode`/`memory_evidence` (logs brutos de execuções passadas) e `memory_node_fts` (índice virtual FTS5).
- **Snapshot e Resiliência:** No encerramento da aplicação, o `DatabaseBackupService` exporta o grafo cognitivo completo (escopos, closure, nós, arestas temporais e Blackboard) para `storage/memory.dump.json` no formato v2, mantendo `lessons`/`working` para compatibilidade com v1. O banco é sempre ancorado na raiz do projeto, nunca no diretório de trabalho do processo. Um banco ausente não cria snapshot vazio, e um `.db` corrompido é colocado em quarentena (`memory.db.corrupt-*`) antes da restauração. A importação v2 é transacional: ou o grafo inteiro volta, ou nada é alterado; snapshots v1 também são importados em transação. Se o arquivo binário `.db` for excluído ou o projeto for clonado sem histórico local, a memória é reconstruída automaticamente a partir desse snapshot.
- O runtime grava episódios sanitizados de sucesso e falha por agente, com tokens, retries,
  duração, resultado, sequência de ferramentas (nome/sucesso), arquivos tocados, artefatos e
  mensagem de erro truncada; não grava prompt/resposta brutos nem argumentos de ferramentas.
- O schema L2 já contém `memory_proposal`, `memory_embedding`, `memory_playbook` e
  `memory_alias`, e `memory_episode` registra `task_type`, outcome, ferramentas, arquivos,
  artefatos, evidências, sinais de sucesso e contagem de redações.
- Quando a escrita de lições está habilitada (`recordLessons`), o runtime extrai propostas
  determinísticas do episódio (lição, fato, entidade, playbook e sumário). Elas ficam
  `pending` e podem ser aceitas/rejeitadas na Central de Memória ou por
  `arestrix-cli memory proposals`. A promoção é manual.
- A busca híbrida (FTS + grafo + embeddings) é opcional e **desligada por padrão**. O modelo
  pré-configurado é `embeddinggemma-2` (768 dimensões). Para ativar, use um
  endpoint OpenAI-compatible `/v1/embeddings` já existente (Ollama, llama.cpp, LM Studio,
  vLLM) em Global Defaults → Memória. Com Ollama: `ollama pull embeddinggemma-2` e endpoint
  `http://127.0.0.1:11434/v1`. Sem provider, o sistema continua 100% lexical.
  Ao ativar, os textos indexados (lições, fatos e sumários sanitizados) são enviados ao
  endpoint configurado; use um servidor local se quiser manter tudo na máquina.
- Consolidação determinística: dedup de lições/fatos, aliases de entidades,
  conflitos (`CONTRADICTS`), expiração, decay para `dormant` e generalização (`GENERALIZES`).
  Nada é apagado; duplicados/conflitos são rebaixados e auditados. Use **Consolidar memória**
  no Studio ou `arestrix-cli memory consolidate`.
- Consolidação em background opt-in por projeto (`consolidationMode=off|idle|after_episodes`),
  apenas em memória persistente. Roda fora do caminho de busca, expõe rodadas/última
  execução/próximo gatilho em `consolidationStatus` no processo do worker e encerra de forma
  determinística com o projeto/processo.
- O serviço `arestrix-memory` expõe `/metrics` autenticado com contadores REST/MCP, notificações,
  falhas 401, classes de resposta, uptime e versão.
- Playbooks procedurais: trajetórias bem-sucedidas viram passos reutilizáveis por
  `task_type`. A primeira evidência cria um `candidate`; a segunda ativa o playbook. Falhas
  atualizam os contadores e reduzem a confiança; o brief injeta playbooks ativos dentro do
  orçamento.
- Workspace: banco separado em `.workspace/memory/memory.db`. O projeto lê o workspace, mas o
  workspace não lê a memória privada do projeto. A promoção é explícita, exige evidência e é
  auditada. Use **Workspace → Inicializar** e **Promover ao workspace** no Studio, ou
  `arestrix-cli memory workspace init|status` / `memory promote`.
- Instituição: contrato nativo com tokens, papéis `viewer`/`editor`/`admin` e políticas
  invariantes de escopo. O binário `arestrix-memory` expõe o contrato por HTTP autenticado e MCP
  (stdio e `/mcp`); detalhes em [Como funciona a memória](como-funciona-a-memoria.md#12-instituição).
- Escopos atuais: **projeto** (`project:<projectId>`), **workspace** e **instituição**.
- Lições e Blackboard são lidos no início de cada nó de agente; a lição de falha é gravada ao final
  da execução e o Blackboard é gravado quando o vértice tem `writeBlackboard`.
- O agente pode consultar a memória com `memory_search` e, quando `recordLessons` está ligado,
  registrar uma lição com `memory_record_lesson`.
- A escrita é governada por vértice: `allowedKeys` limita a leitura do Blackboard, `writeKeys`
  limita a escrita automática, `maxLessonsPerRun` (padrão 20) limita lições por execução e
  `maxLessonChars` (padrão 4000) limita o tamanho de cada lição/gatilho. Há ainda um teto global
  de 5000 lições ativas por projeto. Esses campos são configuráveis no vértice e em
  **Padrões Globais → Memória**.
- Antes de persistir, lições, episódios, arestas e Blackboard passam por sanitização de segredos
  conhecidos (OpenAI/Stripe, Google, AWS, GitHub, Slack, Bearer, parâmetros de URL e chaves
  privadas PEM). O snapshot também é sanitizado antes de ir para o Git, cobrindo linhas antigas
  gravadas antes desta camada.
- O expurgo do projeto remove working, lições/arestas, episódios, evidências, nós, usage, hits e
  auditoria, e compacta o SQLite com `VACUUM`; o banco abre com `secure_delete=ON`. A retenção de
  telemetria é configurável em **Padrões Globais → Memória** (`retentionDays`, padrão 180;
  `0` = manter) e roda no encerramento e via `arestrix-cli memory prune`. `memory purge` também
  remove o snapshot, salvo `--keep-snapshot`.
- O binário gerado resolve o banco a partir de `ARESTRIX_PROJECT_ROOT` (fallback: diretório atual);
  `ARESTRIX_MEMORY_DB` tem prioridade explícita. `memory_hit`/`memory_usage` usam
  `ARESTRIX_RUN_ID`, e a chave única `(scope, run, node, memória)` evita hits duplicados. Escritas
  SQLite têm retry curto para `SQLITE_BUSY`/`SQLITE_LOCKED`.
- O brief é compilado por seções, com lições mais recentes/confiáveis primeiro e Blackboard
  completo ou omitido (nunca JSON parcial). `contextBudgetTokens` limita o brief; o padrão é 800
  tokens aproximados (cerca de 4 caracteres por token). **0 desativa o brief** naquele vértice.
- `memory_hit` registra quais lições/chaves foram injetadas, quais foram consultadas via
  `memory_search` (inclusive as que não estavam no brief) e o outcome da execução.
- `memory_usage` registra tokens, chamadas, retries, latência, contexto e sucesso/falha.
- O JSON legado (`arestrix-blackboard-*.json`, `arestrix-memory-*.json`) é importado uma única vez
  e marcado como `legacy_imported`.
- Nenhum LLM é usado para extrair ou consolidar memória por padrão. Extração, propostas e
  consolidação são determinísticas; a promoção de fatos/entidades continua manual.

### Como a memória chega ao agente

No pipeline gerado, o runtime:

1. abre o banco do projeto quando `persistent: true`;
2. compila um brief por seções com lições ordenadas por confiança/recência e o snapshot das chaves
   autorizadas;
3. injeta esse brief no system prompt como **dado histórico não confiável**;
4. registra `memory_hit` para cada lição/chave injetada e, quando o agente usa
   `memory_search`, registra também as memórias consultadas que não estavam no brief;
5. grava a lição, o Blackboard, o episódio e o outcome no fim da execução;
6. registra automaticamente uma lição quando um agente falha e `recordLessons` está ligado.

Lições de auditoria são registradas pelo nó que interpreta o parecer (por exemplo, um `condition`
que chama `recordLesson` no ramo de reprovação); o runtime não adivinha a semântica de um switch.

### Memória de projeto e `arestrix.memoria`

São coisas diferentes:

- **Memória de projeto** é o AMG em SQLite, usado pelos agentes e pelas ferramentas
  `memory_search` / `memory_record_lesson`.
- **`arestrix.memoria`** é uma ferramenta nativa pequena, chave-valor, que grava
  `.arestrix/memory.json`. Ela continua disponível para casos simples e manuais, mas não é o banco
  de memória dos agentes.

### Exemplos

- [`04-memoria`](../../examples/04-memoria) — lição persistente, Blackboard, injeção no prompt,
  `memory_search`, `memory_record_lesson` e entrega de artefato.
- [`05-matematica-com-memoria`](../../examples/05-matematica-com-memoria) — pesquisa web via MCP
  Keenable, brief de memória e redação de um texto de matemática com referências.

### Medindo o consumo de API

Cada execução de agente registra tokens, chamadas, latência e tamanho do contexto de memória na
tabela `memory_usage`. Para comparar o consumo com a memória ligada/desligada:

No **Studio**, abra **Padrões Globais → Memória** e use **Modo de Medição / A-B**:

- **Normal**: usa a configuração de memória dos vértices.
- **A/B: memória OFF neste projeto**: desliga brief e ferramentas de memória em runtime, sem editar
  os vértices.
- **Registrar métricas mesmo com a memória OFF**: mantém `memory_usage`, outcomes, retries e hits.

Depois rode o mesmo grafo normalmente e consulte **Memória do Projeto → Manutenção**. Para
automação/CI, as mesmas opções existem como variáveis de ambiente:

```bash
# 1) memória ON no mesmo grafo, gravando métricas
ARESTRIX_MEMORY_METRICS=1 arestrix-cli run pipelines/main.json --root <projeto>

# 2) memória OFF no MESMO grafo (sem editar o JSON)
ARESTRIX_MEMORY_METRICS=1 ARESTRIX_MEMORY_DISABLE=1 \
  arestrix-cli run pipelines/main.json --root <projeto>

# 3) relatório nativo
arestrix-cli memory report --project <projeto>
```

O relatório mostra sucesso/falha, retries, memórias injetadas, usadas e consultadas via busca.
`memory_report.py` continua opcional para desenvolvimento; o runtime não depende de Python. Veja
também [`arestrix-cli`](arestrix-cli.md).

O **Runner Standalone** recebe os tokens do agente por um único evento de telemetria emitido pelo
runtime C++ (`type: "usage"`), agregado pelo `RunnerManager`. Não há parsing de stdout nem processo
Python/JS auxiliar durante a execução do grafo.

Se o projeto usar o **AI Gateway**, o impacto da memória também é registrado no gateway. O runtime
envia `X-Arestrix-Memory-Enabled`, `X-Arestrix-Memory-Context-Chars`, `X-Arestrix-Agent` e
`X-Arestrix-Run-Id`; o gateway grava `memory_enabled`, `memory_context_chars` e `memory_tokens`
(estimativa de caracteres / 4) em `usage_events`. A tela **Padrões Globais → AI Gateway** e
`/v1/usage` mostram a proporção sobre a entrada e o comparativo memória ON/OFF. A coleta é feita
inteiramente em C++: nenhum Python ou processo JS participa da execução do grafo.

### Limitações atuais

- A visualização completa do grafo cognitivo e a edição de conflitos ainda não estão na UI;
  a lista de conflitos e o estado de consolidação já aparecem no painel.
- A extração determinística, as propostas, a busca híbrida opcional, a consolidação (manual e em
  background), os playbooks, o workspace e a exposição HTTP/MCP da instituição já existem no núcleo.
- Ainda faltam generalização/consolidação por LLM e coordenação de background entre múltiplos
  processos/instâncias.
- `memory_handoff` e consolidação por LLM ficam para as próximas fases.

## Skills

Uma **skill** é um arquivo `SKILL.md` (front matter YAML mais instruções) que vira o prompt de sistema do agente.

- **Skills do workspace** ficam em `.workspace/skills`, pré-aprovadas para aquele workspace.
- **Catálogo do Studio:** repositórios open source do GitHub autorizados, que fornecem skills para importar.
  Ative ou desative repositórios globalmente em **Agentes → Repositórios GitHub**.
- A ação **Skills** do vértice liga skills a ele; o editor embutido edita o `SKILL.md` como Markdown, com pré-visualização.

### Governança

Quando uma skill é importada ou alterada, o **Auditor de Skills do Workspace** aplica três camadas antes de
liberá-la em um pipeline:

1. **Integridade e proveniência:** front matter, licença, ferramentas declaradas, hash SHA-256 e ledger.
2. **Evidência estática NVIDIA SkillSpector:** regras determinísticas de injeção, esteganografia, exfiltração,
   trifeta letal e permissões excessivas. O catálogo de referência fica em `third_party/SkillSpector/`.
3. **Metodologia de fronteira Cloudflare:** a skill `third_party/security-audit-skill/` orienta a revisão
   adversarial de supply chain, persistência, isolamento de dados, exaustão de recursos e IPC local.

O Arestrix mantém o skill próprio **`arestrix-skill-guard`** em
[`.agents/skills/arestrix-skill-guard/SKILL.md`](../../.agents/skills/arestrix-skill-guard/SKILL.md), que
combina as duas referências e define o contrato de veredito. A política efetiva é versionada por
`policyVersion`; o cache do ledger só vale enquanto a skill, as referências e as regras forem as mesmas.
Atualizou uma referência ou uma regra, a skill volta a `PENDING` e precisa ser reauditada.

O scanner local enxerga o `SKILL.md`, o prompt e as ferramentas declaradas; scripts e anexos de um
repositório de skills continuam exigindo revisão manual pelo `arestrix-skill-guard`.

O lock `src/core/skills/skill-validation-policy.json` fixa commit, tree e hash do `SKILL.md` de cada
referência. O gate é:

```bash
npm run check:skill-references
```

O processo de revisão está em
[`docs/quality/skill-reference-review.md`](../quality/skill-reference-review.md).

## Ferramentas nativas (sem instalação, sem processo)

O Arestrix traz seus próprios servidores de ferramentas, escritos em C++ e executados **dentro do programa**: sem Node, sem Python,
sem processo filho e sem código de terceiros. Elas aparecem no vértice MCP e nos agentes exatamente como qualquer servidor MCP
(marcadas com ⚡ na lista), mas uma chamada é uma chamada de função.

| Servidor | Ferramentas | Observações |
| :--- | :--- | :--- |
| `arestrix.arquivos` | `read_file`, `write_file`, `list_directory`, `file_info`, `make_directory`, `search_text` | Confinado ao projeto. Só texto, 1 MB para ler, 2 MB para gravar. |
| `arestrix.http` | `http_get`, `http_post_json` | **Só hosts que o projeto autorizar** (abaixo). Somente HTTPS. |
| `arestrix.dados` | `json_query`, `csv_to_json`, `json_to_csv`, `regex_extract`, `base64_encode`, `base64_decode` | Transformação pura de dados. Padrões de regex que poderiam travar o programa são recusados. |
| `arestrix.memoria` | `memory_set`, `memory_get`, `memory_list`, `memory_delete` | Ferramenta manual independente do AMG: pequenos textos em `.arestrix/memory.json` (1000 chaves, 64 KB cada). |
| `arestrix.tempo` | `now`, `add_duration`, `diff` | ISO 8601, anos bissextos, fusos. |
| `arestrix.git` | `git_status`, `git_log`, `git_diff`, `git_show_file` | **Somente leitura.** Exige o Git instalado. |
| `arestrix.sqlite` | `list_tables`, `describe_table`, `sql_query`, `sql_execute` | Banco SQLite embutido, em um arquivo do projeto (padrão `data/arestrix.db`). Veja abaixo. |

**Regras de segurança, as mesmas para todos**
- Tudo fica dentro da pasta do projeto. A pasta `.git` não pode ser lida. **Pipelines, configuração do projeto, `.workspace`, `.arestrix` e
  `.githooks` nunca podem ser gravados** por estas ferramentas, nem pelas ferramentas `write` e `edit` dos próprios agentes: gravar ali
  injetaria código C++ ou servidores MCP que rodariam na próxima execução.
- **O HTTP nunca decide sozinho para onde os dados vão.** Liste os sites permitidos no Studio (**Padrões Globais → Ferramentas → Acesso à
  internet**, um por linha, como `api.exemplo.com` ou `*.exemplo.org`), que os grava em `.workspace/native/http.json`; sem nenhum site nada é
  enviado, e a mensagem de erro diz o que fazer. Curingas exigem um domínio completo (`*.com` é recusado).
  Endereços locais e privados são recusados mesmo se listados, redirecionamentos **não** são seguidos (cada destino precisa ser
  autorizado) e as credenciais só viajam por `bearer_env`, o **nome** de uma variável de ambiente, nunca o valor.
- **Um nome pode mentir, por isso o endereço também é conferido (DNS rebinding).** Um nome autorizado como `api.exemplo.com` poderia ser
  apontado depois, por quem controla o DNS dele, para `127.0.0.1`, para uma máquina da sua rede ou para o endereço de metadados de uma
  nuvem. Antes de conectar, o Arestrix resolve o nome e recusa a requisição se **qualquer** endereço devolvido não for público (loopback,
  privado, link-local, CGNAT, multicast, reservado, documentação e as formas IPv6 que embutem um IPv4, como `::ffff:10.0.0.1`, NAT64 e
  6to4). Devolver vários endereços, um deles interno, também é recusado. No Linux e no macOS a conexão é então **fixada** no endereço
  validado (`curl --resolve`), de modo que o nome não pode mudar entre a conferência e a conexão. No Windows o sistema não permite fixar,
  então o endereço **realmente conectado** é conferido antes de qualquer byte do corpo da requisição ser enviado. O HTTPS já dificulta o
  ataque (a máquina interna precisaria de um certificado válido para aquele nome); esta camada fecha a brecha mesmo assim. Limite: se o
  sistema usa um **proxy** (Windows), o endereço conectado é o do proxy, então vale só a conferência anterior à conexão, e o proxy é
  responsável pelo resto.
- Todo resultado é limitado a 64 KB, e um erro de ferramenta interrompe o pipeline com uma mensagem clara.

**Banco de dados SQLite.** O `arestrix.sqlite` embute o SQLite (domínio público, `third_party/sqlite`), sem servidor e sem instalação. Cada
chamada abre o arquivo, executa **um** comando e o fecha. O arquivo precisa estar dentro do projeto e terminar em `.db`, `.sqlite` ou
`.sqlite3`, nunca em `.git` nem nas pastas protegidas. Os valores vão em `params` (`?` no comando), nunca concatenados no texto. O `sql_query`
abre o arquivo **somente para leitura** e só executa `SELECT`; o `sql_execute` altera dados e esquema. O motor recusa `ATTACH`, `PRAGMA`,
tabelas virtuais, gatilhos, transações e carga de extensões, um comando é interrompido após 5 segundos e o banco é limitado a 200 MB.

**Escolher as ferramentas de um agente.** Na janela **Ferramentas** de um agente (e nos Padrões Globais) todo servidor, nativo ou não, aparece com
uma caixa, e "Escolher ferramentas" liga e desliga cada ferramenta. Só os nomes do servidor e das ferramentas são gravados.

**Velocidade.** Na máquina de desenvolvimento, uma chamada nativa levou cerca de 4 microssegundos; um servidor MCP externo, com sessão
persistente, cerca de 56 microssegundos para uma ferramenta trivial, e abrir um processo por chamada levou cerca de 190 milissegundos.

## Servidores MCP

O Arestrix é um cliente MCP sobre stdio e HTTP/SSE (Streamable HTTP). Há duas coisas distintas:

- **O cadastro do Studio** (`arestrix-mcp-servers.json`, tela *Servidores MCP do Workspace*): para registrar e testar servidores.
- **Ferramentas dos agentes em tempo de execução:** um agente pode chamar as ferramentas dos servidores MCP **configurados no
  projeto**, em `.workspace/mcps/*.json` (um objeto `McpServerConfig`, ou uma lista, por arquivo). O agente lista os servidores que
  pode usar nas configurações de code mode (`codeMode.mcpServers` e, opcionalmente, `codeMode.allowedMcpTools` para restringir
  as ferramentas). Cada ferramenta vira `mcp__<servidor>__<ferramenta>` para o modelo. Se um servidor não existir, estiver
  desabilitado, inacessível ou citar uma variável não definida, o agente **falha com um erro claro** em vez de rodar sem a ferramenta.

```json
{ "id": "clientes", "transport": "stdio", "command": "${ARESTRIX_PYTHON:-python}", "args": ["mcp/server.py"],
  "env": { "API_TOKEN": "${CRM_TOKEN}" }, "isEnabled": true }
```

Servidores remotos usam `"transport": "http"` (ou `"sse"`), `url` e `headers` opcionais. Exemplo com a
Keenable (pesquisa web), sem a chave no repositório:

```json
{ "id": "keenable", "transport": "http", "url": "https://api.keenable.ai/mcp",
  "headers": { "X-API-Key": "${KEENABLE_API}" }, "isEnabled": true }
```

**Segredos nunca vão neste arquivo.** `command`, `args`, `url`, todo valor de `env` e todo valor de `headers` aceitam `${NOME}` (o valor de uma variável de
ambiente do seu usuário, resolvido quando o pipeline executa) e `${NOME:-padrão}`. O verificador de segredos recusa um valor
literal em `env`; a interface e o backend recusam valores literais em headers sensíveis (chave, token, segredo, autenticação).
Servidores stdio rodam como processos comuns do seu usuário, **fora** do sandbox do agente: só configure
servidores em que você confia.

**Transporte endurecido.** Como esses arquivos podem ser escritos por terceiros (projetos importados), o cliente MCP recusa entradas
perigosas em vez de tentar torná-las seguras: no Windows, argumentos de `npx`/`npm`/`uvx` com aspas ou metacaracteres de shell
(`& | < > ^ % !`) são rejeitados antes de qualquer processo nascer; o transporte HTTP aceita apenas `https://` na porta 443, host
que resolve **somente** para endereços públicos (proteção contra DNS rebinding, como a ferramenta nativa de HTTP), sem
redirecionamentos, sem `usuario@` e sem IPv6 literal (servidores locais devem usar stdio); e os processos filhos do MCP **não**
herdam as chaves de API do seu ambiente — recebem apenas as variáveis declaradas em `env`.

**Erros traduzidos.** Toda falha de MCP que chega ao Studio carrega um `errorCode` estável (definido em
`CppCore/src/mcp/McpErrors.hpp`), o texto de fallback e um `errorDetail` curto e dinâmico; a interface traduz o código
(chaves `mcp_errors.*`) e só recorre ao texto do backend para códigos que não conhece — assim códigos novos nunca quebram
interfaces antigas.

**Duas formas de usar um servidor num pipeline:** um [**vértice MCP**](grafo-e-vertices.md#vértice-mcp) (uma chamada determinística
que você liga no grafo) ou como **ferramentas de um agente** (o modelo decide quando chamar).

**Desempenho.** O programa gerado conversa com os servidores MCP pelo **cliente nativo em C++** (sem Node nem Python no meio, do
lado do Arestrix). Cada servidor tem **uma sessão persistente** (um processo e um handshake para a execução inteira), compartilhada
com segurança por todas as threads: chamadas ao mesmo servidor são serializadas, servidores diferentes rodam em paralelo. Num
servidor de teste, 15 chamadas levaram 0,19 s com a sessão persistente contra 2,8 s abrindo um processo por chamada (15x). A sessão é
reaberta se o servidor morrer ou a configuração mudar, e uma chamada que falhou **nunca** é repetida automaticamente (ela pode já ter
tido efeito). As sessões são encerradas quando o programa termina.

A janela **Ferramentas** de um vértice de *agente* lista os servidores do projeto (os nativos primeiro) e as suas ferramentas, como descrito
acima, então não é mais preciso editar `mcpServers` no JSON (os [exemplos](../../examples/README.pt-BR.md) ainda o declaram no arquivo, o que continua funcionando).

## Importando trabalho existente

A aba **Conectores** converte material externo em um grafo:

| Origem | Resultado |
| :--- | :--- |
| **Projetos de agentes Pi** | Varre `.pi/settings.json`, `.agents/*/SKILL.md`, `plano.md` e `pipeline-state.json`, e sintetiza um pipeline Taskflow com agentes, ferramentas e validação de artefatos. |
| **Claude, OpenAI, Hermes** | Importa prompts, assistentes e lógica de scratchpad como vértices de agente. |
| **Excel** | Divide uma planilha de várias abas em um banco SQLite e um grafo concorrente de quatro agentes. |

## Relacionados

- [Agentes e execução](agentes-e-execucao.md)
- [Tour pela interface](interface.md)
