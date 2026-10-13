# Arestrix ⚡

<p align="center">
  <a href="README.md">🇺🇸 English</a> &nbsp;|&nbsp; <b>🇧🇷 Português</b>
</p>

**Arestrix** é um orquestrador visual de grafos de nós e IDE de alto desempenho e de nova geração, construído com **Three.js (WebGL)**, **TypeScript** e um **backend nativo em C++ (WebUI)**, para modelar, visualizar e executar pipelines concorrentes e heterogêneos de agentes, movidos pelo **Taskflow em C++20**.

---

## 💡 O que é o Arestrix?

Aplicações concorrentes modernas, de pipelines autônomos multiagente a motores de computação e processamento de dados de alta vazão, exigem paralelismo de tarefas de granularidade fina, decomposição dinâmica em subflows, desvios condicionais estocásticos e isolamento rigoroso de execução.

Embora o C++20 e a biblioteca **Taskflow** ofereçam um runtime paralelo de primeira linha, escrever à mão grafos de tarefas complexos, com laços de realimentação, subflows aninhados e workers de IA em sandbox, é tedioso e propenso a erros.

O **Arestrix** preenche essa lacuna com um canvas visual 2.5D intuitivo, geração instantânea de código C++20, compilação automatizada com MSVC, drivers nativos de execução em sandbox, telemetria em streaming em tempo real e um motor persistente de retomada.

### Camadas Centrais da Arquitetura

O Arestrix segue uma arquitetura desacoplada em três camadas:

1. **Camada Lógica Pura (Frontend `src/core/`)**:
   - Estrutura de dados de grafo em memória (`Graph`) com ordenação topológica e detecção de ciclos.
   - Hierarquia de subflows e grafos filhos aninhados.
   - Histórico de edição reversível baseado no **Command Pattern** (`CommandManager`, `ICommand`), com suporte a desfazer/refazer (`Ctrl+Z` / `Ctrl+Y`).
   - Gerador headless de código Taskflow em C++20 (`TaskflowCodegen` e `TaskflowGenerator`).

2. **Camada Visual e de Interação (Frontend `src/view/`)**:
   - Canvas WebGL em Three.js acelerado por hardware.
   - Câmera ortográfica 2.5D suave, com zoom exponencial centrado no cursor e panorâmica por arraste (`CameraController`).
   - Grade de fundo infinita e procedural, renderizada com shaders de GPU customizados (`InfiniteGrid`).
   - Cartões de nós interativos, badges de telemetria de execução ao vivo e navegação hierárquica por breadcrumbs.
   - Conexões suaves em Bézier cúbica usando Fat Lines do Three.js (`Line2`).

3. **Ponte de Compilador e Toolchain Nativa (Backend `CppCore/`)**:
   - Uma aplicação C++ nativa e leve, empacotada com **WebUI**, que serve estaticamente o frontend gerado pelo Vite.
   - Suporte de primeira classe ao **MSVC (`cl.exe`)** no Windows como compilador C++20 padrão.
   - Drivers de execução integrados para processos do host, contêineres Docker e MicroVMs.
   - Emissor de telemetria em streaming thread-safe e motor de persistência de checkpoints.

---

## 🤖 Agentes Autônomos e Orquestração em Code Mode

O Arestrix oferece suporte de primeira classe a agentes autônomos de IA baseados no paradigma **Code Mode**, permitindo que cada nó do DAG atue como um worker isolado com permissões de granularidade fina:

### 1. Sandbox Isolado e Drivers de MicroVM
Cada nó de agente pode ser configurado para executar em seu próprio ambiente seguro:
- **Contêineres Docker**: Contêineres de execução efêmeros com montagens de sistema somente leitura (`RO`), diretórios de trabalho dedicados de leitura e escrita (`RW`), limites rígidos de RAM e políticas de rede configuráveis (`offline`, `egress_llm_only` ou `full`).
- **MicroVMs**: Virtualização leve WSL2 / MicroVM para isolamento rigoroso em nível de kernel.
- **Processo do Host**: Execução de subprocessos nativos do sistema operacional, supervisionada com job controls do Win32, redirecionamento dos fluxos stdout/stderr e watchdogs de timeout.

### 2. Roteamento de Modelos Multiprovedor e Modais por Nó
Modais flutuantes de configuração, diretamente sobre cada nó do canvas, permitem ajustar os parâmetros de execução:
- **Configuração de Modelo e Skill (🧠)**: Roteie tarefas entre diversos provedores (Anthropic Claude Opus/Sonnet, DeepSeek v4-pro/Flash, Google Gemini, motor visual Jev ou modelos locais), personalize templates de prompt e vincule arquivos de skill `SKILL.md` especializados.
- **Ferramentas Granulares de Code Mode (🔧)**: Restrinja as permissões a ferramentas específicas (`write`, `edit`, `read`, `bash`, `validate_svg`) e a scripts de validação autorizados.
- **Padrões Globais do Pipeline (⚙️)**: Configure, pela barra de ferramentas, padrões válidos para todo o workspace, com herança em cascata opcional em cada nó (`[x] Herdar Padrão Global`).

### 3. Telemetria em Streaming em Tempo Real
A integração nativa de streaming em C++ (`AgentTelemetryEmitter`) entrega retorno ao vivo dos agentes ativos:
- **Acúmulo de Raciocínio (`thinking_delta`)**: Contador de tokens/bytes em tempo real e pré-visualização do processo interno de raciocínio do agente.
- **Monitoramento da Execução de Ferramentas**: Notificação instantânea das invocações de ferramentas (`tool_start` / `tool_end`), com argumentos e status de sucesso.
- **Tratamento Automático de Rate Limit**: Detecção por regex de esgotamento de cota HTTP 429 (`RESOURCE_EXHAUSTED`, `Rate limit reached`), com contadores regressivos para as novas tentativas agendadas.
- **Badges ao Vivo no Canvas**: Os nós refletem visualmente o status da execução (`🧠 thinking`, `🔧 tool call`, `⏳ 429 retry`, `✔ completed`).

### 4. Pipelines em Lote e Motor de Retomada Tolerante a Falhas
Para pipelines complexos de vários estágios que processam grandes coleções de itens (por exemplo, artigos ou lotes de dados):
- **Persistência Atômica de Checkpoints (`PipelineResumeEngine`)**: Registra os estados de execução por item e por estágio em `pipeline-state.json`, com troca atômica de arquivos.
- **Validação Física de Artefatos e Autorrecuperação**: Verifica os artefatos no sistema de arquivos (existência do arquivo, tamanho mínimo em bytes e verificação de conteúdo por regex, como frontmatter YAML) ou os códigos de saída nos logs. Estágios com saídas válidas são marcados automaticamente como concluídos (`recoveredFromArtifact: true`), evitando chamadas redundantes de API.
- **Encadeamento Serializado de Gargalos**: Defina estágios pesados (por exemplo, redação) para rodar sequencialmente entre os itens e respeitar os limites do provedor, enquanto os estágios seguintes (geração de diagramas, frontmatter, animação) rodam concorrentemente.
- **Modal do Orquestrador de Lotes (📦)**: Interface dedicada na barra de ferramentas para definir os itens do lote, configurar a concorrência, alternar estágios serializados e acompanhar o progresso ao vivo na matriz de checkpoints.

---

## 🚀 Como Executar o Arestrix

O projeto é unificado sob um backend C++ rápido que usa **WebUI**, oferecendo uma experiência nativa de desktop no Windows e aproveitando o canvas WebGL do Three.js.

### 📦 Pré-requisitos e Configuração da Toolchain

Garanta que os seguintes pré-requisitos e pacotes estejam instalados no seu ambiente Windows:

1. **Node.js (v18.0+) e npm**:
   - Baixe em [nodejs.org](https://nodejs.org/) ou instale pelo Gerenciador de Pacotes do Windows:
     ```powershell
     winget install OpenJS.NodeJS.LTS
     ```
   - Instala os pacotes do frontend: Three.js, Vite, TypeScript, Vitest e `vite-plugin-singlefile`.

2. **Python 3 (v3.10+)**:
   - Baixe em [python.org](https://www.python.org/) ou instale pelo winget:
     ```powershell
     winget install Python.Python.3.12
     ```
   - Executa o script de build automatizado (`CppCore/dev.py`), que usa apenas utilitários da biblioteca padrão.

3. **Visual Studio 2022 (Community ou Build Tools) com MSVC C++20**:
   - Instale o Visual Studio Community ou o Visual Studio Build Tools:
     ```powershell
     winget install Microsoft.VisualStudio.2022.Community
     ```
   - **Carga de Trabalho Obrigatória**: Selecione **"Desenvolvimento para Desktop com C++"** (*Desktop development with C++*).
   - **Componentes Principais**: MSVC v143 (ferramentas de build C++20 x64/x86, `/std:c++20`), SDK do Windows 10/11 e `nmake.exe`.

4. **Complementos Opcionais**:
   - **Docker Desktop / Podman**: Caso pretenda isolar as tarefas dos agentes em sandboxes de contêiner (Fases 9/11).
   - **Graphviz (`dot`)**: Caso queira exportar diagramas de arquitetura em SVG independentes:
     ```powershell
     winget install Graphviz.Graphviz
     ```

---

### 🔧 Primeira Instalação

Clone o repositório e instale as dependências do frontend:

```powershell
npm install
```

---

### 🏃 Modos de Execução

#### Modo 1: Aplicação Desktop Standalone Completa (C++ Nativo + WebUI) — *Recomendado*

Este modo compila o backend nativo em C++ (`arestrix.exe`), abre uma janela desktop dedicada e dá acesso completo à compilação nativa com MSVC, às sandboxes, à telemetria em streaming e aos transportes de servidores MCP.

Para compilar tudo automaticamente e iniciar:

```powershell
cd CppCore
python dev.py
```

**O que o `python dev.py` executa automaticamente por baixo dos panos:**
1. **Compilação do Frontend**: Roda `npm run build` na pasta raiz, compilando TypeScript, Three.js e CSS em um único `dist/index.html` empacotado (via `vite-plugin-singlefile`).
2. **Biblioteca Estática do WebUI**: Verifica e extrai a `webui-2-static.lib` pré-compilada em `vendor/webui`, caso esteja ausente.
3. **Autodetecção do Ambiente MSVC**: Usa o `vswhere.exe` para localizar o Visual Studio automaticamente, ativa a toolchain de 64 bits `vcvars64.bat` e invoca `nmake -f Makefile`.
4. **Compilação do C++**: Compila todos os fontes do motor em `arestrix.exe` (e o sincroniza tanto em `CppCore/` quanto na raiz do projeto).
5. **Abertura do Studio Desktop**: Inicia o `arestrix.exe`, abrindo a janela desktop standalone do Arestrix Studio com bindings RPC bidirecionais.

💡 **Início Rápido Instantâneo (Quando Já Compilado):**  
Se `arestrix.exe` e `dist/index.html` já estiverem compilados, você pode iniciar o aplicativo diretamente da raiz do projeto ou de `CppCore`:
```powershell
# Da raiz do projeto:
.\arestrix.exe

# Ou de CppCore:
cd CppCore
.\arestrix.exe
```
*(Ou simplesmente dê um duplo clique em `arestrix.exe` no Windows Explorer).*

---

#### Modo 2: Modo de Desenvolvimento Web do Studio (Hot-Module Replacement do Vite)

Se você estiver editando o canvas 2.5D em Three.js, shaders, modais da interface ou estilos e quiser Hot-Module Replacement (HMR) instantâneo no navegador:

```powershell
cd c:\Projetos\NodeForge
npm run dev
```

- Abre o servidor de desenvolvimento do Vite em `http://localhost:5173/`.
- **Disponível no Modo Web**: Edição visual do DAG, pan/zoom da câmera 2.5D, breadcrumbs de subflows, pré-visualização do código C++ Taskflow e exportação/importação do grafo em JSON.
- *Nota:* A execução de subprocessos nativos do Windows, a compilação direta com MSVC (`cl.exe`) e a criação ao vivo de processos MCP via stdio exigem o backend nativo em C++ (Modo 1).

---

#### Modo 3: Suíte Automatizada de Testes Unitários (Vitest)

Para rodar a suíte completa de testes, que verifica a validação do DAG, o histórico de edição reversível, a herança de configurações dos agentes, a telemetria em streaming ao vivo, o motor de retomada de lotes, a arquitetura de memória e a integração com o protocolo MCP:

```powershell
cd c:\Projetos\NodeForge
npm test
```

**Resumo da Cobertura de Testes (164/164 testes passando em 26 suítes):**
- `tests/graph-validation.spec.ts`: Ordenação topológica, verificação de ciclos por DFS, laços de realimentação, desvios condicionais.
- `tests/history-undo-redo.spec.ts`: Pilha de desfazer/refazer do Command Pattern (`Ctrl+Z` / `Ctrl+Y`) e mutações de parâmetros.
- `tests/subflow-hierarchy.spec.ts`: Navegação em profundidade por subflows aninhados, isolamento do grafo pai e navegação por breadcrumbs.
- `tests/agent-templates.spec.ts`: Catálogo de templates, vinculação dinâmica de parâmetros e geração de lambdas customizadas.
- `tests/agent-settings-inheritance.spec.ts`: Cascata dos padrões globais do pipeline, sobrescritas por nó e configuração de ferramentas.
- `tests/agent-telemetry.spec.ts`: Acúmulo de raciocínio em streaming em tempo real, eventos de execução de ferramentas e backoff de rate limit 429.
- `tests/batch-resume-engine.spec.ts`: Arquivos de estado de checkpoint atômicos, validação física de artefatos no sistema de arquivos e autorrecuperação.
- `tests/agent-memory-config.spec.ts`: Arquitetura de memória heterogênea, regras opt-in (desativadas por padrão), RAM efêmera versus disco persistente e escopo em nível de projeto.
- `tests/mcp-protocol.spec.ts`: Configuração de servidores do Model Context Protocol, transportes stdio/sse, atribuição de ferramentas e suporte a desfazer/refazer.
- `tests/project-management.spec.ts`: Isolamento de workspaces multiprojeto, validação do esquema do manifesto, histórico MRU e separação dos escopos de memória.
- `tests/project-creation-hash.spec.ts`: Hash determinístico do ID do projeto (`hashProjectName`), caminhos de sistema de arquivos sanitizados e fluxo de criação pela interface.
- `tests/workspace-governance.spec.ts`: Catálogo de skills e MCPs pré-aprovados do workspace e validação automatizada pelo `WorkspaceSkillAuditorAgent`.
- `tests/github-skills-separation.spec.ts`: Segregação entre o catálogo open source global do Studio e a vinculação e importação de skills por nó.
- `tests/node-view-typography.spec.ts` e `tests/node-action-tooltips.spec.ts`: Tipografia vetorial SDF em alta DPI, pílulas de badge limpas, sem text-shadow, e tooltips de ação interativos.
- `tests/modal-window-manager.spec.ts`: Empilhamento de modais em múltiplas janelas, alças de arraste no cabeçalho e viewports de diálogo responsivos.
- `tests/office-ribbon-interface.spec.ts` e `tests/office-workspace-interface.spec.ts`: Interface Fluent Ribbon do Microsoft Office, Backstage View, atalhos da QAT e ingestão por conectores.
- `tests/pi-project-importer.spec.ts` e `tests/generic-pi-import.spec.ts`: Scanner nativo de projetos Pi, ingestão das configurações da CLI e síntese automatizada de DAGs Taskflow em C++20.

---

#### Modo 4: Build Standalone do Frontend

Para compilar apenas os assets do frontend em arquivos de distribuição de produção:

```powershell
npm run build
```
- Compila o TypeScript e empacota todo o CSS e os assets em um único `dist/index.html` com tudo embutido, consumido em memória pelo `arestrix.exe`.

---

## 🧩 Mapeamento de Nós para o Taskflow

O Arestrix mapeia os nós visuais do grafo diretamente para primitivas nativas do **Taskflow**:

| Tipo de Nó | Cor / Badge | Semântica no Taskflow | Papel no Pipeline Concorrente |
| :--- | :--- | :--- | :--- |
| **Static Task** | Azul (`#3B82F6`) | `tf::Task (taskflow.emplace([](){}))` | Tarefas de computação determinísticas e workers em segundo plano. |
| **Condition Task** | Laranja (`#FF7828`) | `tf::Task (returns int branch)` | Decisões estocásticas, verificações de convergência e laços de realimentação. |
| **Subflow Task** | Roxo (`#8B5CF6`) | `tf::Task (accepts tf::Subflow&)` | Decomposição dinâmica do grafo em tempo de execução, com subgrafos filhos. |
| **Module Task** | Verde (`#10B981`) | `tf::Task (tf.composed_of(...))` | Subgrafos de tarefas modulares e reutilizáveis. |
| **Critical Section**| Ciano (`#06B6D4`) | `tf::CriticalSection (acquire/release)` | Seções atômicas protegidas por mutex entre threads concorrentes. |
| **Agent Task** | Índigo (`#6366F1`) | `tf::Task (Sandbox / MicroVM)` | Agente autônomo em Code Mode com VM isolada, roteamento de modelos multiprovedor e validação de artefatos. |

---

## 🏛️ Arquitetura Moderna: Office Ribbon, Workspaces e Ingestão

O Arestrix inclui uma experiência de desktop de nível corporativo e recursos de ingestão de modelos:

1. **Padrão de Interface Ribbon do Microsoft Office (Fase Concluída ✅)**:
   - Barra Ribbon moderna com abas funcionais (*Arquivo / Backstage*, *Página Inicial*, *Inserir*, *Agentes & Tools*, *Conectores & Importação*, *Dados & Memória*, *Execução & Lote*).
   - Grupos de ferramentas em blocos com rótulos inferiores, Barra de Ferramentas de Acesso Rápido (Salvar, Desfazer, Refazer, Executar F5) e badges limpos em neomorfismo suave, respeitando estritamente a regra de zero text-shadow.
   - Caixa de busca rápida / Diga-me (*"💡 O que você deseja fazer?"*) e atalho para recolher a Ribbon automaticamente (`Ctrl+F1`).

2. **Governança de Projetos e Encapsulamento de Workspace (Fase Concluída ✅)**:
   - Entidades de Projeto de primeira classe, com gerenciamento nativo no backend C++20 (`arestrix.project.json`).
   - Isolamento rigoroso de namespaces de memória (Blackboard / Lições Semânticas), sandboxes, diretórios de trabalho e pipelines por workspace.
   - Gerenciamento MRU de projetos recentes e Hub Gerenciador de Projetos com 3 abas.

### 🏛️ Arquitetura Tripartite e Separação de Escopos na Interface: Studio vs. Workspace vs. Projeto

O Arestrix impõe rigorosamente uma **separação tripartite de responsabilidades**, tanto no backend arquitetural quanto na interface do usuário. Isso garante modularidade limpa, isolamento multi-tenant e governança de segurança corporativa, além de impedir a contaminação cruzada acidental de credenciais, memória cognitiva e fronteiras do sistema de arquivos.

```
+-----------------------------------------------------------------------------------+
| 🏢 STUDIO (Aplicação Desktop e Motor Global)                                      |
| • Canvas WebGL 2.5D e Câmera     • Gerador de Código Taskflow C++20 e MSVC        |
| • Credenciais Globais de LLM     • Repositórios Open Source Autorizados do GitHub |
+-----------------------------------------------------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
| 🛠️ WORKSPACE (Ambiente Físico do Host e Infraestrutura de Armazenamento Local)     |
| • Raiz de Arquivos (.workspace/) • Armazenamento SQLite (database.sqlite)         |
| • Sandboxes Docker / MicroVM     • Servidores MCP e Skills Pré-aprovados          |
| • WorkspaceSkillAuditorAgent     • Limites de Execução e Montagens de Diretórios  |
+-----------------------------------------------------------------------------------+
                                          |
                                          v
+-----------------------------------------------------------------------------------+
| 📦 PROJETO (Propósito Lógico de Negócio e Pipelines DAG Concorrentes)             |
| • arestrix.project.json          • Grafos de Pipeline DAG (pipelines/main.json)   |
| • Prompts e Sobrescritas de Nós  • Variáveis .env Locais do Projeto               |
| • Memória Blackboard com Escopo  • Lições Semânticas Isoladas por projectId       |
+-----------------------------------------------------------------------------------+
```

#### 📊 Matriz Comparativa: Os Três Ambientes Operacionais

| Dimensão | 🏢 Studio (Aplicativo Desktop / Global) | 🛠️ Workspace (Infraestrutura Física) | 📦 Projeto (Unidade Lógica de Negócio) |
| :--- | :--- | :--- | :--- |
| **Conceito Central** | O host da aplicação desktop, o motor de renderização e a toolchain universal de compilação. | A sandbox física do sistema de arquivos, o contêiner de isolamento do sistema operacional e o runtime de armazenamento local. | A solução de negócio com propósito definido, a identidade do fluxo de trabalho e o grafo de execução DAG concorrente. |
| **Manifesto / Arquivos Principais** | `arestrix.exe`, configuração global e layouts de janela. | Diretório físico (`rootPath`) contendo `.workspace/`, `storage/` e `output/`. | `arestrix.project.json` (`projectId`, `name`, `version`, `description`) e `pipelines/main.json`. |
| **Persistência e Armazenamento** | Configurações da aplicação e índice MRU de projetos recentes. | Banco de dados relacional SQLite local (`storage/database.sqlite`), estados de checkpoint (`pipeline-state.json`). | Definições de pipeline, esquemas de grafos de subflow e artefatos de saída com escopo. |
| **Credenciais e Execução** | Chaves globais de API de LLM armazenadas com segurança pelo `EnvironmentManager` (Anthropic, OpenAI, DeepSeek, Google). | Sandboxes de execução (Docker, MicroVM, Job Objects do host), cotas de memória/CPU, montagens de diretórios. | Sobrescritas de modelo por nó, templates de prompt e variáveis `.env` locais do pipeline. |
| **Governança de Skills e MCP** | Catálogo global de repositórios open source autorizados do GitHub (`GitHubSkillsRegistry`). | Skills pré-aprovados do workspace (`.workspace/skills`), daemons de servidores MCP aprovados e `WorkspaceSkillAuditorAgent`. | Os nós vinculam skills curados e ferramentas MCP diretamente à execução das tarefas dos agentes. |
| **Memória Cognitiva** | Nenhuma (apenas estado em nível de aplicação). | Tabelas SQLite físicas, índices locais de embeddings vetoriais e primitivas de lock thread-safe. | Blackboard e Lições Semânticas com escopo, estritamente isolados por `projectId`, impedindo vazamento de dados. |
| **Analogia Conceitual** | **A Fábrica / Parque Industrial** (o prédio, a eletricidade, as máquinas-ferramenta universais). | **A Oficina / Baia de Montagem** (a bancada, as caixas de armazenamento, os equipamentos de segurança, a caixa de ferramentas local). | **O Produto em Construção** (as plantas, a lista de materiais, as etapas de montagem, as verificações de qualidade). |

---

#### 🖥️ Segregação da Interface entre Telas e Modais

Para que o usuário sempre saiba qual escopo está configurando, cada tela, grupo da ribbon, painel do inspetor e modal do Arestrix traz badges de escopo explícitos e seções separadas:

1. **Barra Office Ribbon (`OfficeRibbon.ts`)**:
   - **Aba Dados & Memória**: Particionada explicitamente em **📦 Memória do Projeto** (Blackboard e Lições Semânticas com escopo) e **🛠️ Armazenamento do Workspace** (arquivo do banco SQLite e diretório físico do workspace).
   - **Aba Tools**: Destaca os **🛠️ MCP Servers** como ferramentas de infraestrutura do workspace disponíveis para os nós do projeto.

2. **Backstage View (`BackstageView.ts`)**:
   - **Seção Informações (`info`)**: Renderiza dois cartões distintos: *📦 Informações do Projeto Ativo* (detalhes do manifesto, versão e pipelines registrados) e *🛠️ Infraestrutura do Workspace Físico* (caminho físico, localização do banco de dados e status do auditor do workspace).
   - **Seção Abrir (`open`)**: Exibe *📊 Grafos Recentes do Workspace Ativo `(📦 Pipelines do Projeto)`*, seguido de *📂 Projetos Recentes*.
   - **Seção Opções (`options`)**: Rotulada formalmente como *🏢 Opções Globais do Studio & 🛠️ Governança do Workspace*, dá acesso aos padrões do Studio e à auditoria de segurança do workspace.

3. **Painel Inspetor de Nós (`Inspector.ts`)**:
   - **Estado Vazio**: Exibe cartões de visão geral separados para o *📦 Grafo do Projeto Ativo* e o *🛠️ Ambiente do Workspace*.
   - **Barra de Ações do Nó**: Cada botão de ação de modal é marcado com o seu escopo operacional:
     - `[📦 Projeto]`: Configuração de Modelo (`NodeModelModal`), Memória Cognitiva (`NodeMemoryModal`) e Permissões de Ferramentas (`NodeToolsModal`).
     - `[🛠️ Workspace]`: Isolamento de Sandbox e Limites do Host (`NodeSandboxModal`).
     - `[🛠️ / 📦]`: Skills do Nó (`NodeSkillsModal`, que vincula os nós de agente do projeto a skills verificados do workspace).

4. **Diálogos e Modais Dedicados**:
   - **Padrões Globais (`GlobalDefaultsModal.ts`)**: Estruturado em badges de escopo distintos: `🏢 Studio` para as chaves universais de API de LLM e os padrões, `🛠️ Workspace` para o isolamento de contêineres de sandbox e as ferramentas aprovadas, e `📦 Projeto` para os modelos padrão dos nós e a retenção de memória.
   - **Gerenciador de Projetos (`ProjectManagerModal.ts`)**: Separa claramente os metadados lógicos do projeto (`arestrix.project.json`) do diretório físico (`Diretório Raiz do Workspace 🛠️`). As variáveis de ambiente locais do projeto (`.env`) ficam estritamente separadas das chaves globais do Studio.
   - **Repositórios do GitHub (`GitHubReposModal.ts`)**: Identificado como `🏢 Catálogo Global do Studio`, gerencia os repositórios open source autorizados em toda a aplicação.
   - **Servidores MCP (`McpServersModal.ts`)**: Identificado como `🛠️ Servidores MCP do Workspace`, fornece os daemons físicos de ferramentas para o ambiente de host atual.
   - **Modais de Memória Cognitiva (`ProjectMemoryModal.ts` e `NodeMemoryModal.ts`)**: Rotulados como `📦 Escopo do Projeto`, garantem que as consultas de memória e os pares chave-valor do blackboard permaneçam isolados no `projectId` ativo.

---

#### 🛡️ Governança de Segurança de Skills e MCP do Workspace

Para proteger os usuários contra a execução de ferramentas não confiáveis, o Arestrix inclui um fluxo de governança automatizado:
1. **Repositórios Pré-aprovados do Workspace**: Skills e configurações de MCP aprovados são provisionados em `.workspace/skills` e `.workspace/mcps` na inicialização do workspace.
2. **Auditor Automatizado de Skills (`WorkspaceSkillAuditorAgent`)**: Sempre que um usuário ou um agente importa um skill externo, o auditor do workspace analisa automaticamente o frontmatter YAML, os requisitos de permissão, as dependências de scripts e a segurança de sandbox antes de autorizar a execução nos pipelines do projeto.


3. **Motor de Ingestão de Assistentes de IA Externos e Planilhas (Fases 15 a 20)**:
   - **Ingestão de Projetos Reais e da CLI do Pi Agent (Fase Concluída ✅ 🎯)**:
     - Importação em um clique de projetos locais do Pi CLI, com varredura dinâmica de qualquer repositório.
     - Scanner nativo em C++ e TS que inspeciona `.pi/settings.json`, as habilidades em `.agents/*/SKILL.md`, o `plano.md` e os checkpoints em `pipeline-state.json`.
     - Síntese visual imediata de um pipeline Taskflow C++20: agentes autônomos, ferramentas MCP e validação física de artefatos.
   - **Claude (Anthropic)**: Conversão de prompts estruturados em XML e de artefatos.
   - **OpenAI (ChatGPT / Assistants)**: Exportação de threads e mapeamento de ferramentas.
   - **Hermes (Nous Research)**: Lógica de scratchpad para microVMs locais offline.
   - **Motor de Modernização de Excel**: Decomposição de planilhas complexas de várias abas em bancos de dados relacionais SQLite e DAGs concorrentes de 4 agentes.

---

## 🌐 Internacionalização (i18n) e Como Adicionar Novos Idiomas

O Arestrix Studio é uma aplicação 100% poliglota. Todos os títulos, menus da Ribbon, botões, modais, tooltips e mensagens são carregados dinamicamente a partir de catálogos de tradução em JSON.

A biblioteca [`i18next`](https://github.com/i18next/i18next) é gerenciada diretamente como um **submódulo Git** em `third_party/i18next`, o que permite verificá-la e atualizá-la automaticamente, sem depender de pacotes externos no `node_modules`.

Idiomas nativos de fábrica:
- **Português do Brasil (`pt-BR`)** (Padrão)
- **English - United States (`en-US`)**

---

### 🛠️ Passo a Passo para Instalar e Adicionar um Novo Idioma

Para adicionar um novo idioma (por exemplo, Espanhol `es-ES`, Francês `fr-FR` ou Alemão `de-DE`), siga as etapas abaixo:

#### 1. Criar o Arquivo de Dicionário JSON
Duplique um dos dicionários existentes em `src/i18n/locales/` com o código BCP-47 correspondente:
```bash
# Exemplo para Espanhol (Espanha / Internacional):
cp src/i18n/locales/en-US.json src/i18n/locales/es-ES.json
```
Edite o novo arquivo `src/i18n/locales/es-ES.json` e traduza os valores das chaves, mantendo a mesma estrutura hierárquica (chaves idênticas às de `pt-BR.json` e `en-US.json`).

#### 2. Registrar o Novo Idioma em `src/i18n/types.ts`
Adicione o novo código à união de tipos `SupportedLocale` e declare seus metadados na lista `SUPPORTED_LOCALES`:

```typescript
// src/i18n/types.ts
export type SupportedLocale = 'pt-BR' | 'en-US' | 'es-ES';

export const SUPPORTED_LOCALES: readonly ILocaleOption[] = [
  {
    code: 'pt-BR',
    name: 'Português (Brasil)',
    nativeName: 'Português do Brasil',
    flag: '🇧🇷'
  },
  {
    code: 'en-US',
    name: 'English (US)',
    nativeName: 'US English',
    flag: '🇺🇸'
  },
  {
    code: 'es-ES',
    name: 'Español (España)',
    nativeName: 'Español',
    flag: '🇪🇸'
  }
];
```

#### 3. Importar e Registrar o Catálogo em `src/i18n/index.ts`
Importe o arquivo JSON e inclua-o no objeto `resources` da inicialização do `i18next`:

```typescript
// src/i18n/index.ts
import esES from './locales/es-ES.json';

// No i18next.init:
resources: {
  'pt-BR': { translation: ptBR },
  'en-US': { translation: enUS },
  'es-ES': { translation: esES }
}
```

Atualize também a validação de sanitização em `setLocale`:
```typescript
if (locale !== 'pt-BR' && locale !== 'en-US' && locale !== 'es-ES') {
  locale = 'pt-BR';
}
```

#### 4. Adicionar a Opção no Dropdown da Backstage View
No arquivo `src/ui/ribbon/BackstageView.ts`, adicione a nova `<option>` dentro do elemento `<select id="backstage-language-select">`:

```html
<select id="backstage-language-select" class="backstage-language-dropdown">
  <option value="pt-BR" ${currentLocale === 'pt-BR' ? 'selected' : ''}>BR Português (Brasil)</option>
  <option value="en-US" ${currentLocale === 'en-US' ? 'selected' : ''}>US English (United States)</option>
  <option value="es-ES" ${currentLocale === 'es-ES' ? 'selected' : ''}>ES Español (España)</option>
</select>
```

#### 5. Executar os Testes Automatizados de Paridade
O Arestrix possui testes unitários estritos no Vitest que verificam a consistência e a paridade de todas as chaves entre os idiomas:

```powershell
npm test
```
*Se alguma chave tiver sido esquecida ou nomeada incorretamente, o teste apontará a divergência imediatamente.*

#### 6. Compilar e Sincronizar com o Binário C++ Desktop
Execute a compilação de produção:

```powershell
npm run build
```
*O script compila os assets via Vite e sincroniza automaticamente a pasta `dist/` com `CppCore/dist/`, tornando o novo idioma disponível imediatamente no executável desktop `arestrix.exe`.*

---

### 🔄 Gerenciamento do Submódulo `third_party/i18next`

Para auditar ou atualizar o código da biblioteca oficial do `i18next`:

```powershell
# 1. Verificar se o repositório remoto do i18next possui novas atualizações (sem alterar arquivos)
npm run submodule:check

# 2. Atualizar o submódulo para a versão mais recente da branch remota master
npm run submodule:update
```

---

## 📜 Licença

Licença MIT. Projetado e desenvolvido com paixão e colaboração de IA.

---

## 🙏 Agradecimentos

O Arestrix se apoia em trabalhos open source excepcionais. Nosso sincero agradecimento aos autores e às comunidades dos projetos incluídos como submódulos Git em [`third_party/`](third_party):

| Projeto | Papel no Arestrix | Licença |
| :--- | :--- | :--- |
| [**i18next**](https://github.com/i18next/i18next) | Framework de internacionalização por trás de cada texto traduzido do Studio. | MIT |
| [**Monaco Editor**](https://github.com/microsoft/monaco-editor) (Microsoft) | Editor de código que alimenta a visualização do código C++ gerado. | MIT |
| [**SkillSpector**](https://github.com/NVIDIA/SkillSpector) (NVIDIA) | Scanner de segurança para skills de agentes de IA. | Apache-2.0 |

Cada projeto permanece propriedade de seus respectivos autores e é distribuído sob a sua própria licença, disponível na respectiva pasta em `third_party/`.
