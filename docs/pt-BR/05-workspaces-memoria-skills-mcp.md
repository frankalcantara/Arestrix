# Workspaces, memória, skills e MCP

[English](../en/05-workspaces-memory-skills-mcp.md) · [Índice](../../README.pt-BR.md#documentação)

Esta página cobre as partes do Arestrix que ficam fora de um único grafo. Para a divisão Studio /
Workspace / Projeto em si, veja [Arquitetura](01-arquitetura.md#studio-workspace-e-projeto).

## Projetos e workspaces

Um projeto é descrito por `arestrix.project.json` na sua pasta:

| Campo | Significado |
| :--- | :--- |
| `projectId`, `name`, `version`, `description` | Identidade. O id é derivado de forma determinística do nome (SHA-256). |
| `rootPath` | A pasta física do workspace. |
| `pipelines` | Os grafos do projeto (`pipelines/main.json` é o principal). |
| `database` | O arquivo SQLite, por padrão `storage/database.sqlite`. |
| `git` | Sincronização com repositório Git dedicado (`provider`, `serverUrl`, `repositoryName`, `remoteUrl`, `isPrivate`, `autoCommitOnRun`, `autoCommitOnShutdown`, `lastSyncAt`). |
| `globalDefaults` | Padrões de `model`, `sandbox`, `codeMode` (ferramentas permitidas) e `memory` para os vértices. |
| `environment` | Variáveis locais ao projeto (`.env`). |

O Studio mantém a lista de projetos recentes e restaura o último ao iniciar. *Salvar Como* gera um pacote
comprimido `.arestrix`.

### Sincronização Git por projeto (GitHub e repositórios remotos)

Cada projeto no Arestrix é mapeado para um **repositório Git independente** em um servidor Git configurável (por padrão, uma conta do **GitHub**):

1. **Auto-save no clone local:** O salvamento automático contínuo continua operando em tempo real na pasta local do projeto, que atua como o clone/repositório local do Git.
2. **Commit e Push automáticos:**
   - **A cada execução do pipeline (`F5`):** Um commit com mensagem contextual é gerado e enviado (`push`) imediatamente ao servidor remoto antes da inicialização do grafo.
   - **No encerramento do Arestrix:** Disparado nativamente pelo backend C++ ao fechar o aplicativo ou a janela, garantindo que nenhum trabalho fique não versionado no servidor remoto.
3. **Segurança Absoluta (Zero-Secrets no Arestrix):** Nenhum dado de autenticação, senha ou Personal Access Token (PAT) é registrado nos arquivos, manifestos ou banco do Arestrix. O processo de registro de identidade e autenticação é orientado pelo Arestrix mas gerenciado **exclusivamente pelo Sistema Operacional** através do **Git Credential Manager (GCM)** / Cofre de Credenciais do Windows.
4. **Repositórios Privados por Padrão:** Novos repositórios são criados de forma automática e transparente com o nome do projeto e marcados obrigatoriamente como **privados** (`isPrivate: true`).
5. **Aviso de Governança para Serviços Comerciais:** Quando o servidor configurado for um serviço comercial externo (como `github.com` ou `gitlab.com`), o Arestrix exibe obrigatoriamente um aviso legal de que os dados do projeto (instruções de agentes, grafos e metadados) residirão em infraestrutura de terceiros fora do controle do software, exigindo consentimento explícito do usuário.


## Padrões globais e herança

Os **padrões globais** (barra de ferramentas) guardam as configurações de modelo, sandbox, ferramentas e
memória. Cada vértice pode herdá-los (uma caixa de seleção no vértice) ou substituí-los. Chaves do Studio nunca se misturam com as variáveis `.env` do projeto.

## Memória

A memória é opcional e vem desativada por padrão.

| Tipo | Escopo | Finalidade |
| :--- | :--- | :--- |
| **Blackboard** | Projeto | Estado chave-valor compartilhado entre os agentes do mesmo projeto. |
| **Lições semânticas** | Projeto | Lições que os agentes registram e consultam depois. |
| **Efêmera ou persistente** | Por vértice | Só em RAM, ou gravada em disco no banco do workspace. |

A memória é particionada por `projectId`, então dois projetos nunca leem os dados um do outro. Cada vértice
declara quais chaves pode ler ou escrever.

## Skills

Uma **skill** é um arquivo `SKILL.md` (front matter YAML mais instruções) que vira o prompt de sistema do agente.

- **Skills do workspace** ficam em `.workspace/skills`, pré-aprovadas para aquele workspace.
- **Catálogo do Studio:** repositórios open source do GitHub autorizados, que fornecem skills para importar.
  Ative ou desative repositórios globalmente em **Agentes → Repositórios GitHub**.
- A ação **Skills** do vértice liga skills a ele; o editor embutido edita o `SKILL.md` como Markdown, com pré-visualização.

### Governança

Quando uma skill é importada, o **Auditor de Skills do Workspace** inspeciona seu front matter, as permissões
solicitadas, as dependências de script e a segurança de sandbox antes que ela possa ser usada em um pipeline. O
repositório também inclui o scanner **SkillSpector** da NVIDIA em `third_party/`.

## Ferramentas nativas (sem instalação, sem processo)

O Arestrix traz seus próprios servidores de ferramentas, escritos em C++ e executados **dentro do programa**: sem Node, sem Python,
sem processo filho e sem código de terceiros. Elas aparecem no vértice MCP e nos agentes exatamente como qualquer servidor MCP
(marcadas com ⚡ na lista), mas uma chamada é uma chamada de função.

| Servidor | Ferramentas | Observações |
| :--- | :--- | :--- |
| `arestrix.arquivos` | `read_file`, `write_file`, `list_directory`, `file_info`, `make_directory`, `search_text` | Confinado ao projeto. Só texto, 1 MB para ler, 2 MB para gravar. |
| `arestrix.http` | `http_get`, `http_post_json` | **Só hosts que o projeto autorizar** (abaixo). Somente HTTPS. |
| `arestrix.dados` | `json_query`, `csv_to_json`, `json_to_csv`, `regex_extract`, `base64_encode`, `base64_decode` | Transformação pura de dados. Padrões de regex que poderiam travar o programa são recusados. |
| `arestrix.memoria` | `memory_set`, `memory_get`, `memory_list`, `memory_delete` | Pequenos textos em `.arestrix/memory.json` (1000 chaves, 64 KB cada). |
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

O Arestrix é um cliente MCP sobre stdio e SSE. Há duas coisas distintas:

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

**Segredos nunca vão neste arquivo.** `command`, `args`, `url` e todo valor de `env` aceitam `${NOME}` (o valor de uma variável de
ambiente do seu usuário, resolvido quando o pipeline executa) e `${NOME:-padrão}`. O verificador de segredos recusa um valor
literal em `env`. Servidores stdio rodam como processos comuns do seu usuário, **fora** do sandbox do agente: só configure
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

**Duas formas de usar um servidor num pipeline:** um [**vértice MCP**](03-grafo-e-vertices.md#vértice-mcp) (uma chamada determinística
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

- [Agentes e execução](04-agentes-e-execucao.md)
- [Tour pela interface](06-interface.md)
