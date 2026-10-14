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
| `globalDefaults` | Padrões de `model`, `sandbox`, `codeMode` (ferramentas permitidas) e `memory` para os vértices. |
| `environment` | Variáveis locais ao projeto (`.env`). |

O Studio mantém a lista de projetos recentes e restaura o último ao iniciar. *Salvar Como* gera um pacote
comprimido `.arestrix`.

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

**Duas formas de usar um servidor num pipeline:** um [**vértice MCP**](03-grafo-e-vertices.md#vértice-mcp) (uma chamada determinística
que você liga no grafo) ou como **ferramentas de um agente** (o modelo decide quando chamar).

**Desempenho.** O programa gerado conversa com os servidores MCP pelo **cliente nativo em C++** (sem Node nem Python no meio, do
lado do Arestrix). Cada servidor tem **uma sessão persistente** (um processo e um handshake para a execução inteira), compartilhada
com segurança por todas as threads: chamadas ao mesmo servidor são serializadas, servidores diferentes rodam em paralelo. Num
servidor de teste, 15 chamadas levaram 0,48 s com a sessão persistente contra 3,75 s abrindo um processo por chamada (7,8x). A sessão é
reaberta se o servidor morrer ou a configuração mudar, e uma chamada que falhou **nunca** é repetida automaticamente (ela pode já ter
tido efeito). As sessões são encerradas quando o programa termina.

Limitação: o modal **Ferramentas** de um vértice de *agente* ainda não lista os seus servidores (mostra uma lista de exemplo fixa); declare
`mcpServers` no JSON do projeto, como fazem os [exemplos](../../examples/README.pt-BR.md).

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
