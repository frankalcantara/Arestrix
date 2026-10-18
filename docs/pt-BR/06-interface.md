# Tour pela interface

[English](../en/06-interface.md) · [Índice](../../README.pt-BR.md#documentação)

O Studio segue o layout do Microsoft Office, porque a maioria das pessoas já o conhece.

## Ribbon

| Aba | O que há nela |
| :--- | :--- |
| **Arquivo** | Abre a visão **Backstage** (abaixo). |
| **Página Inicial** | Edição e exibição, *Vértices do Grafo* (**+ Tarefa**, seletor de tipo, **Catálogo**), execução e governança. |
| **Inserir** | Fluxo e concorrência (condições, switch, ferramenta MCP, subfluxos, fork/join, seções críticas), aceleração por GPU, agentes cognitivos. |
| **Agentes** | Segurança e skills do workspace, servidores MCP, o catálogo global de skills do GitHub. |
| **Conectores** | Importar projetos Pi, Claude, OpenAI, Hermes, Excel. |
| **Dados** | *Memória do projeto* e *Armazenamento do workspace*, mantidos visivelmente separados. |
| **Executar** | Compilar e Executar, o motor Taskflow, o orquestrador de Lote e Retomada e a Distribuição (**Runner Standalone**). |

Outras comodidades: a **Barra de Acesso Rápido** (Salvar, Desfazer, Refazer, Executar e o botão de **Sincronização Git** com indicador de status em tempo real), uma chave de
salvamento automático, a caixa de busca no projeto (abaixo) e `Ctrl+F1` para recolher o ribbon.

### Busca no projeto

A caixa no canto superior direito busca no **projeto aberto**: todos os vértices (inclusive os dentro de
subfluxos e módulos), as skills do workspace e os pipelines. Pressione `Ctrl+K` para focá-la.

- Os resultados são agrupados em *Vértices*, *Skills*, *Pipelines* e *Comandos*; `↑` `↓` escolhem, `Enter` abre, `Esc` fecha.
- Um resultado de vértice seleciona o vértice, entra nos subfluxos que o contêm e centraliza o canvas nele.
  Um resultado de skill abre a skill somente leitura no editor.
- A busca considera rótulo, tipo, corpo C++, parâmetros, prompt do agente e texto das skills, ignorando
  acentos e plurais, tolerando erros de digitação e palavras parciais, e conhecendo alguns sinônimos
  português/inglês (por exemplo, *decisão* acha um vértice de condição, *banco* acha o SQLite).
- O ranking é calculado pelo backend C++, sem modelo de IA e sem rede. O salvamento automático (cerca de meio
  segundo depois de cada edição) reindexa só os arquivos cujo hash de conteúdo mudou; se o hash do projeto não
  mudou, nada é refeito. O índice fica em cache em `.arestrix/search-index.json`, dentro do projeto.
- **Sinônimos por idioma.** Português e inglês vêm embutidos. Ao selecionar qualquer outro idioma, o Studio pede
  ao seu **modelo padrão** que escreva, em segundo plano, o dicionário de sinônimos desse idioma (veja
  [Primeiros passos](02-primeiros-passos.md#modelo-padrão-do-studio)). O resultado fica na sua máquina, em
  `%APPDATA%\Arestrix\synonyms\<idioma>.json` (ou `~/.config/arestrix/synonyms/`), e não no projeto. Sem
  modelo configurado nada é gerado, e a busca continua funcionando sem os sinônimos daquele idioma.
- Limitação: só o pipeline aberto no canvas (`pipelines/main.json`) pode ser navegado. Resultados de outros
  pipelines do projeto são listados, mas ainda não podem ser abertos.
- A busca semântica (embeddings) está planejada como uma camada futura.

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
| `Ctrl+K` | Focar a busca no projeto |
| `Ctrl+F1` | Recolher ou expandir o ribbon |

## Backstage

Aberta por **Arquivo**. Traz as informações do projeto (quatro cartões: o projeto ativo, a sincronização do repositório Git, o workspace
físico e o **Runner Standalone**), a tela **Abrir**, a tela unificada **Salvar** e as **Opções**
(padrões do Studio, governança do workspace e o seletor de **idioma**).

A tela **Abrir** segue o Abrir do Office. À esquerda ficam os locais:

- **Recentes**: busca e duas abas, **Projetos** (agrupados por Hoje, Ontem, Últimos 7 dias, Últimos 30
  dias e Mais antigos) e **Grafos** (os pipelines do projeto atual).
- **Este Computador**: navega pelas unidades e pastas. Mostra subpastas, pastas de projeto
  (com `arestrix.project.json`), pacotes `.arestrix` e arquivos `.json`. Clicar numa pasta de projeto
  abre o projeto; o botão ao lado entra na pasta sem abri-lo.
- **Procurar**: abre o seletor de arquivos do sistema.

Um pacote `.arestrix` é extraído numa pasta nova ao lado dele (`nome`, `nome (2)`, ...) e aberto como
projeto. Um grafo `.json` dentro de `<projeto>/pipelines` abre o projeto inteiro; um grafo avulso só
substitui o canvas do projeto atual depois de confirmação.

A tela **Salvar** unifica o salvamento rápido e o *Salvar Como* no estilo do Office:

- À esquerda, permite **Salvar Projeto Atual** diretamente no local existente ou escolher onde salvar via
  **Este Computador**, **Locais Recentes** ou **Procurar Pasta**.
- À direita, destaca no topo o seletor de escopo: **Projeto Completo** (pacote `.arestrix` ou pasta estruturada
  com `arestrix.project.json` + `pipelines/main.json`) ou **Apenas um Grafo do Projeto** (arquivo `.json` do
  pipeline). Inclui navegação por unidades, pastas e breadcrumbs, atalhos para a raiz e para a pasta
  `pipelines/` do projeto ativo, além de registro automático do grafo no manifesto quando salvo dentro do
  projeto aberto.

## Assistente

O painel lateral tem duas abas: **Propriedades** e **Assistente**. O Assistente é um chat com a IA padrão do
Studio (Padrões Globais > Modelo padrão do Studio) que ajuda a **operar** e a **aprender** o Studio:

- Responde dúvidas consultando esta documentação (no idioma do Studio).
- Cria projetos e monta ou altera grafos: adiciona, liga, altera, move e remove vértices, valida o grafo.
- Salva o projeto e executa o pipeline.

As edições no grafo aparecem na hora no canvas. Tudo o que o Assistente fizer numa resposta pode ser
desfeito de uma vez com **Ctrl+Z**. Criar, abrir ou limpar um grafo, salvar e executar pedem
confirmação antes (botões **Permitir** e **Recusar** no chat).

Cada conversa é uma **sessão** gravada no projeto, em `.arestrix/assistant/sessions/`; o seletor no topo da
aba reabre as anteriores. O Assistente responde no idioma do Studio.

O Assistente precisa de um provedor com **chamada de ferramentas**. Os provedores sem esse recurso (Jev e
modelo local) aparecem marcados como "sem suporte ao Assistente" no seletor do modelo padrão, e a aba
indica a impossibilidade. Todo o laço da conversa, as ferramentas e a gravação das sessões rodam no
backend C++; a aba só desenha a conversa e aplica as alterações no canvas.

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
| Sincronização Git do projeto | Projeto / SO (Configuração de repositório privado, login seguro no SO e consentimento comercial) |
| Runner Standalone | Projeto / Distribuição (Telas de Progresso, Diagnóstico, Operação e Empacotamento) |
| Lote e Retomada | Executar |

## Console e Painel de Artefatos

Uma gaveta acoplada na base da tela (redimensionável por splitter e minimizável) centraliza a observabilidade e os produtos da execução em três abas:

1. **Terminal MSVC C++20:** Registra a geração do código-fonte, a compilação nativa pelo compilador e os logs brutos da execução da engine Taskflow.
2. **Telemetria em Tempo Real:** Monitora as etapas cognitivas dos agentes (início e término de raciocínio *thinking*, invocação de ferramentas, parâmetros e retries de *rate limit* 429).
3. **Artefatos (Entregáveis):** Painel multiformato dedicado à inspeção e entrega dos arquivos produzidos pelo pipeline:
   - **Vetor SVG:** Renderização vetorial nativa e interativa com pan/zoom e alternância instantânea entre a cena gráfica e o código-fonte XML/SVG limpo.
   - **Planilhas e Dados Tabulares (CSV/TSV):** Grid interativo com busca instantânea por termos em células e visualização de dados brutos.
   - **Documentos Técnicos e Markdown:** Leitor com realce de blocos de código e botão de cópia rápida.
   - **PDFs e JSONs:** Visualização limpa e botão para abertura direta no aplicativo padrão do sistema operacional.
   - **Ações Rápidas de Entrega:** Botão **"No Explorer"** para abrir a pasta `exports/` no Windows Explorer, **"Copiar"** e **"Baixar"** para exportação local direta.

## Idioma

Escolha o idioma em **Backstage → Opções**: Português (Brasil) ou Inglês (EUA). Para adicionar outro,
veja [Desenvolvimento](07-desenvolvimento.md#adicionando-um-idioma).

## Relacionados

- [Grafo e vértices](03-grafo-e-vertices.md)
- [Workspaces, memória, skills e MCP](05-workspaces-memoria-skills-mcp.md)
