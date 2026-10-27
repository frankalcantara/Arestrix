# Tour pela interface

> **Aplica-se a:** Arestrix Beta 2.2 (`2.2.0-beta.1`) · Última revisão: 2026-10-27

[English](../en/interface.md) · [Índice](../README.md)

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

### Aba contextual do vértice

Ao selecionar um vértice, a ribbon ganha uma **aba contextual** com os comandos daquele tipo
(agente, decisor, switch, MCP, subfluxo, seção crítica, chamada, saída etc.). A aba volta para a
última aba normal usada quando o vértice é deselecionado.

Os comandos gravam nos campos que o gerador C++ realmente consome: modelo/sandbox, memória,
`switchSettings`, `outputSettings`, `callSettings`, `customLambdaBody`, `criticalSectionId` e
`parameters`. Edições entram no desfazer/refazer e no salvamento automático. O Switch insere novos
casos **antes** do `default`; seções críticas geram um `tf::Semaphore` por chave; e os diálogos da
ribbon são próprios do Studio, sem depender de `window.prompt`/`window.confirm` do WebView.

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
  [Primeiros passos](primeiros-passos.md#modelo-padrão-do-studio)). O resultado fica na sua máquina, em
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
físico e o **Runner Standalone**), o hub de **Propriedades**, a tela **Abrir**, a tela unificada
**Salvar** e as **Opções** (atalhos de execução e governança do workspace). Idioma, tema e as
configurações editáveis agora ficam em **Propriedades**.

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

## Propriedades

**Arquivo > Propriedades** abre o hub compartilhado no padrão Office para os três escopos. Cada
escopo usa a mesma barra de título, navegação lateral, cartões de conteúdo e rodapé Cancelar/Salvar.

![Hub de Propriedades para Studio, Workspace e Projeto](../assets/screenshots/properties-hub.webp)

| Escopo | É dono de | Exemplos |
| :--- | :--- | :--- |
| **Studio** | Preferências da instalação | Idioma, tema, canvas, modelo padrão, chaves de provedores, AI Gateway, integrações, reset do arquivo de configuração |
| **Workspace** | O workspace físico do projeto ativo | Sandbox padrão, política de auditoria/segurança, reconstrução de `.workspace`, restauração de padrões |
| **Projeto** | O manifesto do projeto ativo | Nome, versão, descrição, saída, caminho SQLite, defaults de modelo/ferramentas, ambiente, resumos de Git/Runner, pipelines |

### Propriedades do Studio

Idioma, tema (Claro / Escuro / Seguir o sistema) e canvas são aplicados ao salvar. As seções de
modelo e gateway gravam apenas no ambiente protegido; as chaves dos provedores são campos write-only
com status.

![Propriedades do Studio](../assets/screenshots/studio-properties.webp)

![Propriedades do Studio no tema escuro](../assets/screenshots/studio-properties-dark.webp)

### Propriedades do Workspace

O Workspace é dono da política padrão de sandbox e auditoria. Reconstruir `.workspace` regrava as
skills e MCPs aprovados preservando as propriedades editadas.

![Propriedades de sandbox do Workspace](../assets/screenshots/workspace-properties.webp)

### Propriedades do Projeto

A tela do Projeto edita o manifesto, defaults de modelo/ferramentas, referências de ambiente
sanitizadas e o caminho SQLite. Git e Runner continuam em diálogos especializados abertos pelos
respectivos resumos.

![Propriedades do Projeto](../assets/screenshots/project-properties.webp)

![Defaults de modelo e ferramentas do Projeto](../assets/screenshots/project-defaults.webp)

### Herança explícita

Os valores resolvidos seguem **Studio → Workspace → Projeto → Vértice**. A tela mostra a origem
atual, e salvar um valor no Projeto cria um override explícito. Os diálogos de vértice mantêm o
próprio toggle de herança.

![Diálogo de sandbox do vértice usando o shell Office compartilhado](../assets/screenshots/vertex-sandbox.webp)

![Diálogo de modelo do vértice usando o shell Office compartilhado](../assets/screenshots/vertex-model.webp)

## Assistente

O painel lateral tem duas abas: **Propriedades** e **Assistente**. O Assistente é um chat com a IA padrão do
Studio (Propriedades > Modelo padrão do Studio) que ajuda a **operar** e a **aprender** o Studio:

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

## Canvas e Viewport

O espaço de trabalho é dividido em três áreas integradas:
1. **Área Lateral:** Painel de Tarefas, Inspetor e Assistente com splitter vertical de largura livre.
2. **Área Central (Diagrama):** Canvas infinito acelerado via WebGL/Three.js com nós em SVG e texto vetorial de alto contraste.
3. **Área Inferior (Console):** Gaveta acoplada de observabilidade e artefatos com splitter horizontal.

### Redimensionamento e Zoom Invariante

O cálculo de projeção ortográfica do Viewport opera em escala métrica 1:1 baseada na resolução física em pixels:
- **Preservação de Escala:** Ao redimensionar a área lateral (horizontalmente) ou a área inferior do console (verticalmente), o nível de zoom dos cartões, nós e arestas **não sofre qualquer alteração**. A área do diagrama apenas expande ou contrai sua janela visível de corte (clipping), sem comprimir ou distorcer os elementos.
- **Tipografia Nítida:** Proibição estrita de sombras, borrões ou halos luminosos (`text-shadow`, `drop-shadow`) em textos, números e rótulos. As fontes permanecem com contornos 100% nítidos em qualquer resolução. Passar o mouse sobre portas e botões exibe dicas contextuais.
- **Feedback em Execução:** Durante uma execução, selos e badges limpos exibem `thinking`, chamadas de ferramenta ativas, retentativas de limite de taxa `429 retry` e conclusão.
- **Aparência do Canvas:** Os controles flutuantes incluem um botão de fundo que alterna entre o canvas claro e o canvas preto puro. A escolha fica salva localmente e é reaplicada ao iniciar. Os controles flutuantes ficam ocultos enquanto o pipeline compila ou executa e voltam quando ele termina.

## Inspetor

O painel da direita mostra o vértice selecionado: nome, tipo, corpo C++ e configurações do agente. Sem
seleção, mostra dois cartões de visão geral: o grafo do projeto ativo e o ambiente do workspace. Cada
botão de ação leva a etiqueta do escopo: `[Projeto]` para Modelo, Memória e Ferramentas, `[Workspace]` para
Sandbox, `[Workspace / Projeto]` para Skills.

## Modais e Arquitetura de Abas Office 365

Todos os modais do Studio seguem o padrão de design corporativo **Microsoft Office 365 (Fluent 2)**:
- **Janelas Flutuantes:** Arrastáveis pela barra de título, redimensionáveis, empilháveis com controle de foco e z-index automático.
- **Shell de Modal Compartilhado:** Todos os diálogos usam a mesma barra de título neutra de 46px, superfície elevada, controles de janela em SVG, raio de 10px e a marca Arestrix no lugar de ícones emoji avulsos.
- **Estrutura em Abas:** Formulários complexos são organizados em abas temáticas de 34px de altura com indicador ativo `#0f6cbd` e transição suave.
- **Preservação de Estado:** Alternar entre abas mantém todos os valores preenchidos nos formulários intactos, evitando perda de dados antes da confirmação.
- **Rodapé Padronizado:** Barra de ações de 58px com botões primários Fluent (`#0f6cbd`), cancelamento e atalhos de ajuda.
- **Tokens Claro e Escuro:** Chrome, superfícies, textos, bordas, campos, badges, callouts e rodapés compartilham tokens claro/escuro. O seletor global **Studio > Propriedades > Geral > Tema** aplica `html[data-theme="dark"]` na ribbon, task pane, console, Backstage e diálogos. O padrão continua claro, e o fundo do canvas é uma preferência independente.

| Modal | Abas Office 365 | Escopo |
| :--- | :--- | :--- |
| **Propriedades (Studio / Workspace / Projeto)** | Categorias por escopo com shell Office compartilhado | Studio / Workspace / Projeto |
| **Biblioteca de Vértices** | Catálogo de Modelos \| Presets de Pipeline \| Importar / Exportar Plugins | Studio |
| **Modelo do Nó** | Provedor & Modelo \| Instruções & Skills \| Parâmetros | Por vértice |
| **Ferramentas do Nó** | Ferramentas Nativas \| Servidores MCP \| Validação & Lote | Por vértice |
| **Sandbox do Nó** | Isolamento & Recursos \| Rede & Confinamento \| Pastas & Volumes | Por vértice |
| **Memória do Nó** | Blackboard \| Lições Cognitivas \| Regime & Escopo | Por vértice |
| **Skills do Nó** | Skills Habilitadas \| Catálogo do Workspace \| Importar Nova Skill | Por vértice |
| **Editor de Skill** | Editor Markdown \| Pré-visualização Renderizada \| Metadados & Teste | Por vértice / Workspace |
| **Repositórios GitHub** | Repositórios Autorizados \| Cadastrar Novo Repositório | Catálogo Studio |
| **Servidores MCP** | Servidores Ativos \| Conectar Novo Servidor \| Inspeção & Logs | Workspace |
| **Gerenciador de Projetos** | Projetos Recentes \| Novo Projeto \| Configurações (link para Propriedades) | Projeto |
| **Sincronização Git** | Conta & Identidade no SO \| Repositório Remoto \| Automação & Governança | Projeto / SO |
| **Runner Standalone** | Progresso \| Diagnóstico \| Operação \| Exportação | Projeto / Distribuição |
| **Execução em Lote** | Dados de Entrada \| Concorrência & Gargalos \| Matriz de Checkpoints | Executar |
| **Importadores Externos** | Arquivo / Conexão \| Mapeamento de Agentes \| Opções & Destino | Conectores |
| **Exportação Graphviz** | Visualização Gráfica \| Código DOT \| Opções de Exportação | Ferramentas |

### Modal do Runner Standalone

O modal do **Runner Standalone** (acionado na aba **Executar** > grupo **Distribuição** ou no **Backstage**) oferece uma interface limpa, de alto contraste e sem dependência do editor gráfico completo. Ele é estruturado em 4 abas:

1. **Progresso:**
   - **Indicador de Estado:** Selo de status dinâmico (`Aguardando`, `Compilando`, `Executando`, `Sucesso`, `Erro`, `Interrompido`).
   - **Barra de Progresso Linear e Cronômetro:** Acompanhamento percentual contínuo com contagem de tempo decorrido.
   - **Linha do Tempo de Etapas:** Exibe a sequência de vértices do pipeline, duração individual, carimbo de horário e status de conclusão.
2. **Diagnóstico:**
   - **KPIs de Telemetria:** Painel com cartões de contagem de tokens (Prompt, Conclusão, Total), chamadas a ferramentas, retries de limite de taxa 429 e latência média.
   - **Streaming de Thinking:** Área de exibição em tempo real do raciocínio cognitivo emitido pelos modelos de IA.
   - **Inspeção de Chamadas:** Tabela de ferramentas invocadas, parâmetros recebidos, tempo de execução e resultado retornado.
   - **Exportar Diagnóstico:** Copia todos os logs e métricas consolidadas em JSON para a área de transferência.
3. **Operação:**
   - **Controles de Ação:** Botões para Iniciar (`F5`), Parar (`Shift+F5`) e Reiniciar o pipeline.
   - **Formulário de Parâmetros:** Campos dinâmicos gerados a partir do schema de parâmetros declarado no projeto, permitindo ajustar entradas sem tocar no grafo.
   - **Entregáveis:** Atalho direto para abrir a pasta de artefatos (`exports/`) no Windows Explorer.
4. **Exportar:**
   - Seleção da pasta de saída (padrão `dist/runner`).
   - Compilação nativa e empacotamento com manifesto `runner_manifest.json` e script `run.bat`.
   - Execução da auditoria automática **Zero-Segredos** (bloqueio *fail-closed* contra qualquer credencial ou chave embutida).

## Console e Painel de Artefatos

Uma gaveta acoplada na base da tela (redimensionável por splitter e minimizável) centraliza a observabilidade e os produtos da execução. Sua barra de abas adota o padrão visual corporativo **Microsoft Office / Excel**:
- **Fita de Abas Fluent:** Abas conectadas de 34px de altura, fonte 'Segoe UI' 12px / 600, fundo `#ffffff` na aba ativa e linha de realce azul `#0f6cbd`.
- **Ícones Vetoriais e Badges Nítidos:** Sem emojis soltos ou sombras em fontes; contagem de artefatos com badge em pílula de alto contraste e ações compactas de cópia, limpeza e alternância de estado.
- **Estado Recolhido Estável:** Rodar um pipeline nunca expande o console recolhido; abra-o explicitamente quando precisar dos logs.
- **Três Abas Especializadas:**
  1. **Terminal MSVC C++20:** Registra a geração do código-fonte, a compilação nativa pelo compilador e os logs brutos da execução da engine Taskflow.
  2. **Telemetria em Tempo Real:** Monitora as etapas cognitivas dos agentes (início e término de raciocínio *thinking*, invocação de ferramentas, parâmetros e retries de *rate limit* 429).
  3. **Artefatos (Entregáveis):** Painel multiformato dedicado à inspeção e entrega dos arquivos produzidos pelo pipeline:
   - **Vetor SVG:** Renderização vetorial nativa e interativa com pan/zoom e alternância instantânea entre a cena gráfica e o código-fonte XML/SVG limpo.
   - **Planilhas e Dados Tabulares (CSV/TSV):** Grid interativo com busca instantânea por termos em células e visualização de dados brutos.
   - **Documentos Técnicos e Markdown:** Leitor com realce de blocos de código e botão de cópia rápida.
   - **PDFs e JSONs:** Visualização limpa e botão para abertura direta no aplicativo padrão do sistema operacional.
   - **Ações Rápidas de Entrega:** Botão **"No Explorer"** para abrir a pasta `exports/` no Windows Explorer, **"Copiar"** e **"Baixar"** para exportação local direta.

## Idioma

Escolha o idioma em **Arquivo → Propriedades → Studio → Geral**: Português (Brasil) ou Inglês
(EUA). Para adicionar outro, veja
[Desenvolvimento](desenvolvimento.md#adicionando-um-idioma).

## Relacionados

- [Grafo e vértices](grafo-e-vertices.md)
- [Workspaces, memória, skills e MCP](workspaces-memoria-skills-mcp.md)
