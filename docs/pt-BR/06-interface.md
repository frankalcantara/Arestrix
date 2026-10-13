# Tour pela interface

[English](../en/06-interface.md) · [Índice](../../README.pt-BR.md#documentação)

O Studio segue o layout do Microsoft Office, porque a maioria das pessoas já o conhece.

## Ribbon

| Aba | O que há nela |
| :--- | :--- |
| **Arquivo** | Abre a visão **Backstage** (abaixo). |
| **Página Inicial** | Edição e exibição, *Vértices do Grafo* (**+ Tarefa**, seletor de tipo, **Catálogo**), execução e governança. |
| **Inserir** | Fluxo e concorrência (condições, subfluxos, fork/join, seções críticas), aceleração por GPU, agentes cognitivos. |
| **Agentes** | Segurança e skills do workspace, servidores MCP, o catálogo global de skills do GitHub. |
| **Conectores** | Importar projetos Pi, Claude, OpenAI, Hermes, Excel. |
| **Dados** | *Memória do projeto* e *Armazenamento do workspace*, mantidos visivelmente separados. |
| **Executar** | Compilar e Executar, o motor Taskflow e o orquestrador de Lote e Retomada. |

Outras comodidades: a **Barra de Acesso Rápido** (Salvar, Desfazer, Refazer, Executar), uma chave de
salvamento automático, a caixa de busca *O que você deseja fazer?* e `Ctrl+F1` para recolher o ribbon.

### Atalhos de teclado

| Teclas | Ação |
| :--- | :--- |
| `F5` / `Ctrl+Enter` | Compilar e executar |
| `Shift+F5` | Interromper a execução |
| `Ctrl+S` / `Ctrl+Shift+S` | Salvar / Salvar Como |
| `Ctrl+Z`, `Ctrl+Y` | Desfazer, refazer |
| `Ctrl+C`, `Ctrl+X`, `Ctrl+V`, `Ctrl+D` | Copiar, recortar, colar, duplicar o vértice selecionado |
| `Del` / `Backspace` | Apagar o vértice ou a aresta selecionados |
| `Esc` | Sair de um subfluxo |
| `Ctrl+F1` | Recolher ou expandir o ribbon |

## Backstage

Aberta por **Arquivo**. Traz as informações do projeto (dois cartões: o projeto ativo e o workspace
físico), a tela de abrir/recentes, o *Salvar Como* com navegação por pastas e unidades, e as **Opções**
(padrões do Studio, governança do workspace e o seletor de **idioma**).

## Canvas

O canvas é infinito. Os cartões são SVG, desenhados sobre a cena WebGL, com texto vetorial nítido e sem
sombras. Ao afastar o zoom, os cartões perdem detalhes. Passar o mouse sobre portas e botões mostra dicas.

Cada cartão mostra um cabeçalho com o ícone e a cor do tipo, o título, as portas e, quando selecionado, a
barra de ações rápidas. Durante a execução, selos mostram `thinking`, chamadas de ferramenta, `429 retry` e a conclusão.

## Inspetor

O painel da direita mostra o vértice selecionado: nome, tipo, corpo C++ e configurações do agente. Sem
seleção, mostra dois cartões de visão geral: o grafo do projeto ativo e o ambiente do workspace. Cada
botão de ação leva a etiqueta do escopo: `[Projeto]` para Modelo, Memória e Ferramentas, `[Workspace]` para
Sandbox, `[Workspace / Projeto]` para Skills.

## Modais

Os modais são janelas flutuantes: arrastáveis pela barra de título, redimensionáveis e empilháveis.

| Modal | Escopo |
| :--- | :--- |
| Padrões globais | Chaves do Studio, sandbox e ferramentas do Workspace, modelo e memória do Projeto |
| Biblioteca de vértices | Templates, preset, importar/exportar |
| Sandbox, Modelo, Ferramentas, Memória, Skills | Por vértice |
| Editor de skill | Editor Markdown com pré-visualização |
| Repositórios GitHub | Catálogo de skills de todo o Studio |
| Servidores MCP | Workspace |
| Gerenciador de projetos | Metadados do projeto, caminho do workspace, `.env` do projeto |
| Lote e Retomada | Executar |

## Console

Uma gaveta acoplada na base mostra a saída da compilação e o log de execução ao vivo. A barra de status
abaixo dela mostra o estado do grafo e do projeto.

## Idioma

Escolha o idioma em **Backstage → Opções**: Português (Brasil) ou Inglês (EUA). Para adicionar outro,
veja [Desenvolvimento](07-desenvolvimento.md#adicionando-um-idioma).

## Relacionados

- [Grafo e vértices](03-grafo-e-vertices.md)
- [Workspaces, memória, skills e MCP](05-workspaces-memoria-skills-mcp.md)
