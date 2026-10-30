# Workspaces, memória, skills e MCP

> **Aplica-se a:** Arestrix Beta 2.3 (`2.3.0-beta.2`) · Última revisão: 2026-10-25

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
| `git` | Sincronização opcional com repositório Git/GitHub (`enabled`, `provider`, `serverUrl`, `repositoryName`, `remoteUrl`, `isPrivate`, `autoCommitOnRun`, `autoCommitOnShutdown`, `timedPushEnabled`, `timedPushIntervalMinutes`, `lastSyncAt`). |
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
2. **Bancos de Dados Fora do Git (Privacidade & Conformidade LGPD/GDPR):**
   - O Arestrix não envia ao Git os bancos SQLite (`storage/database.sqlite` e `.arestrix/memory/memory.db`) nem gera dumps SQL/JSON de backup. O repositório remoto contém apenas arquivos de projeto (grafos, pipelines, skills e configurações).
   - O `.gitignore` seguro do projeto bloqueia bancos binários, dumps SQL, snapshots JSON e arquivos de quarentena.
3. **Commit e Push inteligentes e sob demanda (Proteção de Cotas GitHub):**
   - **A cada execução do pipeline (`F5`):** Um commit **local** com mensagem contextual é gerado no clone local sem disparar `push` remoto. Isso elimina o risco de *rate limiting* e consumo desnecessário de cota de rede no GitHub durante sessões intensas de desenvolvimento e depuração interativa.
   - **Push temporizado automático (padrão a cada 10 minutos):** Caso existam alterações locais pendentes ou commits não enviados, o Arestrix realiza o push em lote de forma transparente e periódica para o repositório remoto. O intervalo e a ativação podem ser configurados no manifesto e modal do projeto.
   - **Botão de Sincronização Manual (Push Imediato):** Disponível diretamente na barra superior (Quick Access Toolbar) e na aba de automação do modal, permitindo enviar alterações ao GitHub a qualquer momento com um único clique.
   - **No encerramento do Arestrix (`onShutdown`):** Se o Git estiver habilitado, disparado nativamente pelo backend C++ ao fechar o aplicativo ou a janela, garantindo que nenhum trabalho fique não versionado no servidor remoto. Se o Git estiver desativado, o encerramento conclui imediatamente sem executar processos externos.
4. **Sem Dumps Automáticos de Banco:**
   - O Arestrix não exporta, versiona nem restaura dumps de banco de dados. Backup, redundância e recuperação de desastre são responsabilidade da empresa ou usuário e devem ser executados fora do produto, com a ferramenta que preferir.
   - No encerramento, o Arestrix apenas remove do rastreamento do Git arquivos de banco/dump que tenham sido commitados anteriormente (`git rm --cached`); o histórico remoto não é reescrito.
5. **Auditoria de Pré-Commit Nativa (Secret Scanning):**
   - O `GitSyncService` audita automaticamente o *staged diff* antes de cada commit contra padrões de credenciais expostas (`sk-...`, `AIza...`, `AKIA...`, `ghp_...`). A operação é bloqueada sumariamente (*fail-closed*) caso qualquer segredo seja detectado.
6. **Exclusões Robustas no `.gitignore` (`ensureSafeGitignore`):**
   - Exclusão forçada e automática de bancos SQLite binários (`*.sqlite`, `*.db`), dumps SQL (`*.sql`), snapshots (`*.dump.json`), quarentenas (`*.db.corrupt-*`), caches de runtime (`.arestrix/`), pesos de modelos locais de IA (`*.gguf`, `*.safetensors`, `*.bin`, `*.onnx`, `models/`), datasets colunares pesados (`*.parquet`, `*.arrow`, `*.feather`, `*.h5`), mídias e arquivos temporários.
7. **Segurança Absoluta (Zero-Secrets no Arestrix):**
   - Nenhum dado de autenticação, senha ou Personal Access Token (PAT) é registrado nos arquivos, manifestos ou banco do Arestrix. O registro de identidade e autenticação é gerenciado **exclusivamente pelo Sistema Operacional** através do **Git Credential Manager (GCM)** / Cofre de Credenciais do Windows.
8. **Repositórios Privados por Padrão:** Novos repositórios criados via GitHub CLI (`gh`) são marcados obrigatoriamente como **privados** (`isPrivate: true`).
9. **Aviso de Governança para Serviços Comerciais:** Quando o servidor configurado for um serviço comercial externo (como `github.com` ou `gitlab.com`), o Arestrix exibe obrigatoriamente um aviso legal de que os dados do projeto residirão em infraestrutura de terceiros fora do controle do software, exigindo consentimento explícito do usuário.

### Bancos de dados e versionamento seguro

O Arestrix opera com dois bancos de dados SQLite no escopo de cada projeto:

1. **Banco Operacional / Analítico do Projeto (`storage/database.sqlite`):** Armazena tabelas manipuladas pelos agentes e nós do pipeline via MCP `workspace-sqlite` ou ferramentas nativas (`sql_query`, `sql_execute`), além de tabelas importadas (ex: planilhas Excel via `ImportExcelModal`).
2. **Banco de conhecimento AMG (`.arestrix/memory/memory.db`):** armazena o multigrafo temporal do projeto (lições, fatos e evidências). A telemetria fica em `.arestrix/telemetry/telemetry.db`.

#### Por que os bancos não são enviados ao Git?
* **Limites de Repositório do GitHub:** O GitHub recomenda manter repositórios com menos de 1 GB (com teto recomendado de 5 GB a 10 GB) e bloqueia sumariamente arquivos individuais acima de 100 MB.
* **Inflamento Rápido do Histórico (Packfiles):** Bancos SQLite são arquivos binários mutáveis. Cada execução do pipeline altera páginas no arquivo; como o Git não calcula diffs de texto em binários, uma cópia inteira do arquivo seria adicionada ao histórico a cada commit, inflando o repositório em poucos dias.
* **Políticas de Uso Aceitável:** O GitHub proíbe expressamente o uso de repositórios como serviço de backup automatizado em nuvem ou hospedagem de banco de dados ativo.

#### Backup e recuperação fora do Arestrix

O Arestrix não exporta, versiona nem restaura dumps de bancos. O produto não gera `storage/database.sql` nem `storage/memory.dump.json`. A política de backup, redundância e recuperação de desastre é responsabilidade da empresa ou usuário e deve ser executada externamente, com a ferramenta de backup que preferir.

No encerramento, se o Git estiver habilitado, o Arestrix apenas remove do rastreamento (`git rm --cached`) bancos e dumps que tenham sido commitados antes e mantém o `.gitignore` seguro. O histórico remoto não é reescrito.

Localmente, o Arestrix continua zelando pela integridade do banco de memória: um `.db` corrompido é colocado em quarentena (`memory.db.corrupt-*`) e a retenção de telemetria é aplicada no shutdown.


## Padrões globais e herança

Os **padrões globais** (barra de ferramentas) guardam as configurações de modelo, sandbox, ferramentas e
memória. Cada vértice pode herdá-los (uma caixa de seleção no vértice) ou substituí-los. Chaves do Studio nunca se misturam com as variáveis `.env` do projeto.

## Memória

A memória é opcional, local e pertence ao projeto. O modelo atual tem três modos por vértice:

| Modo | Efeito |
| :--- | :--- |
| `off` | Não lê nem grava conhecimento. |
| `consult` | Injeta o brief autorizado no prompt e registra hits. Não grava conhecimento. |
| `learn` | Consulta, grava um episódio e propõe lições ao final da execução. |

Aprender implica salvar, mas nada entra sozinho: lições e playbooks nascem como proposta ou
candidato e só viram conhecimento ativo depois de aprovação no painel **Memória do Projeto**.
Telemetria fica em `.arestrix/telemetry/telemetry.db`, separada do conhecimento em
`.arestrix/memory/memory.db`; limpar sessão ou apagar telemetria não apaga conhecimento.

O brief é montado dentro de `contextBudgetTokens` com lições ativas, fatos, evidências e o estado
de trabalho legado quando habilitado. Playbooks compilados ficam como candidatos e não entram no
brief até serem promovidos. Cada item injetado recebe `[MEMO:<id>]`; só o ID citado na resposta final
conta como usado. Uso altera utilidade, não confiança.

O agente tem `memory_search` (somente leitura), `memory_propose_lesson` (proposta pendente) e as
ferramentas de estado `memory_state_get`, `memory_state_set`, `memory_state_list` e
`memory_state_history`. `memory_record_lesson`, `memoryWriteKeys` e o servidor nativo
`arestrix.memoria` foram removidos.

O estado de continuação é declarado pelo projeto e fica fora do brief. Escopos: `project`,
`task_type` e `item`; TTL, CAS, schema JSON e modo `replace`/`versioned` são suportados. O CLI expõe
`memory state declare|list|get|set`.

A busca combina FTS lexical com vetores opcionais e fusão RRF. A busca não indexa durante a
consulta; **Testar** e **Reindexar** no painel são ações explícitas. Perfis prontos:
EmbeddingGemma 2 (768 dimensões) e Qwen3-Embedding-4B (2560). Sem provedor, a busca continua
lexical. O índice guarda um fingerprint de endpoint/modelo/dimensão/prefixos; a busca só usa o
fingerprint atual. Estados: `ready` (híbrida), `stale`/`pending`/`partial`/`failed` (lexical).

O painel oferece **Comparar com e sem memória (contexto)**: cria uma cópia consistente com
`VACUUM INTO`, calcula o fingerprint SHA-256 e mede tokens/caracteres do brief por vértice ativo.
É o A/B v1, de custo e conteúdo de contexto; a taxa de aprovação do validador pertence ao
protocolo completo.

Playbooks só ativam com validação independente: 3 `input_hash` distintos nas últimas 20 execuções
validadas, validador estável e 1 promoção por `task_type` em 24h. Falhas de infraestrutura não viram
lição; `task_max_turns` é falha da tarefa.

Workspace e instituição continuam disponíveis para conhecimento promovido e compartilhado. A
promoção é explícita, exige evidência e é auditada. O projeto lê o workspace autorizado; o
workspace não lê a memória privada do projeto.

O guia completo, com configuração, retenção, segurança e referência rápida, está em
[Como funciona a memória](como-funciona-a-memoria.md).

### Exemplos

- [`04-memoria`](../../examples/04-memoria) — proposta de lição, aprovação, injeção no prompt,
  `memory_search` e entrega de artefato.
- [`05-matematica-com-memoria`](../../examples/05-matematica-com-memoria) — pesquisa web via MCP
  Keenable, brief de memória e redação de um texto de matemática com referências.

### Medindo o consumo de API

Cada execução de agente registra tokens, chamadas, latência e tamanho do contexto de memória na
tabela `memory_usage`. Para comparar o consumo com a memória ligada/desligada:

No **Studio**, abra **Arquivo → Propriedades → Projeto → Modelo e ferramentas → Gerenciar memória avançada** e use **Modo de Medição / A-B**:

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
(estimativa de caracteres / 4) em `usage_events`. A tela **Arquivo → Propriedades → Studio → Credenciais e Gateway** e
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
- **Bibliotecas:** a **Biblioteca Arestrix** embutida e as bibliotecas locais/curadas fornecem skills
  para importar. Gerencie fontes, crie bibliotecas e cadastre repositórios em
  **Arquivo → Propriedades → Studio/Workspace → Bibliotecas**. No topo da seção, o bloco
  **Fontes de repositório (GitHub)** recebe `organizacao/projeto`, referência e licença; clique
  **Salvar** e depois selecione a biblioteca criada para usar **Sincronizar GitHub**. A sincronização
  lê apenas o manifesto/árvore; o conteúdo de um asset é baixado sob demanda, auditado e só então
  promovido para uso. `REJECT` descarta o download.
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
| `arestrix.tempo` | `now`, `add_duration`, `diff` | ISO 8601, anos bissextos, fusos. |
| `arestrix.git` | `git_status`, `git_log`, `git_diff`, `git_show_file` | **Somente leitura.** Exige o Git instalado. |
| `arestrix.sqlite` | `list_tables`, `describe_table`, `sql_query`, `sql_execute` | Banco SQLite embutido, em um arquivo do projeto (padrão `data/arestrix.db`). Veja abaixo. |

**Regras de segurança, as mesmas para todos**
- Tudo fica dentro da pasta do projeto. A pasta `.git` não pode ser lida. **Pipelines, configuração do projeto, `.workspace`, `.arestrix` e
  `.githooks` nunca podem ser gravados** por estas ferramentas, nem pelas ferramentas `write` e `edit` dos próprios agentes: gravar ali
  injetaria código C++ ou servidores MCP que rodariam na próxima execução.
- **O HTTP nunca decide sozinho para onde os dados vão.** Liste os sites permitidos no Studio (**Arquivo → Propriedades → Projeto → Modelo e ferramentas → Gerenciar allowlist HTTP
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

**Escolher as ferramentas de um agente.** Na janela **Ferramentas** de um agente (e nas Propriedades → Modelo e ferramentas) todo servidor, nativo ou não, aparece com
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
