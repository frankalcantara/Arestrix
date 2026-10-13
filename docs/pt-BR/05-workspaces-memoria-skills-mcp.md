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

O Arestrix é um cliente MCP sobre stdio e SSE. Os servidores são registrados por workspace
(`arestrix-mcp-servers.json`), aparecem na interface como *Servidores MCP do Workspace* e são atribuídos aos
vértices na ação **Ferramentas**. As configurações MCP pré-aprovadas são provisionadas em `.workspace/mcps`.

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
