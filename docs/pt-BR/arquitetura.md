# Arquitetura

> **Aplica-se a:** Arestrix Beta 1.5 (`1.5.0-beta.1`) · Última revisão: 2026-10-19

[English](../en/architecture.md) · [Índice](../README.md)

O Arestrix transforma um desenho de agentes em um programa. Você edita um grafo num canvas web, o
backend em C++ gera um programa **C++20 / Taskflow** a partir dele, compila e executa. Esta página
explica as peças e como elas se encaixam.

## Visão geral

```
 ┌────────────────────────────┐        RPC WebUI        ┌────────────────────────────────┐
 │ Frontend (TypeScript)      │ ◄─────────────────────► │ Backend (C++20, CppCore/)      │
 │  src/core  grafo + histórico│                        │  projeto, build, sandbox,      │
 │  src/view  canvas Three.js │                         │  memória, MCP, gerador de código│
 │  src/ui    ribbon + modais │                         └───────────────┬────────────────┘
 └────────────────────────────┘                                         │ gera + compila
                                                                        ▼
                                                        ┌────────────────────────────────┐
                                                        │ Programa gerado (main.cpp)     │
                                                        │  grafo Taskflow + runtime      │
                                                        │  arestrix_core (agentes, HTTP) │
                                                        └────────────────────────────────┘
```

A janela é uma janela nativa **WebUI** (sem Electron). A interface é empacotada pelo Vite em um único
`dist/index.html`, que o executável serve a partir da memória.

## Camadas do frontend (`src/`)

| Camada | Pasta | Responsabilidade |
| :--- | :--- | :--- |
| Lógica | `src/core/` | `Graph` em memória, validação, hierarquia de subfluxos, histórico com **padrão Command** (desfazer/refazer), templates, skills, projetos e workspaces, sincronização Git e orquestrador do Runner Standalone (`RunnerManager`). Sem renderização. |
| Visual | `src/view/` | Canvas ortográfico Three.js, câmera (pan/zoom), grade infinita, arestas de Bézier e a camada SVG que desenha os cartões dos vértices por cima. |
| Interface | `src/ui/` | Ribbon estilo Office, visão Backstage, inspetor, modais (`StandaloneRunnerModal`, `GitAccountModal`), console, menu de contexto. |
| i18n | `src/i18n/` | Catálogos `pt-BR` e `en-US`, carregados via i18next. |

Toda alteração no grafo passa por um comando (`AddNodeCommand`, `RemoveNodeCommand`,
`MoveNodeCommand`, `AddEdgeCommand`, `UpdateNodeCommand`, `CompoundCommand`...). Por isso desfazer e
refazer funcionam para tudo, inclusive colar, duplicar e apagar.

## Módulos do backend (`CppCore/src/`)

| Módulo | O que faz |
| :--- | :--- |
| `core/` | Tipos de grafo e vértice, serialização JSON. |
| `compiler/` | `TaskflowGenerator`: grafo → `main.cpp`. Detecta arestas de laço (feedback). |
| `build/` | `BuildService`: compila o programa gerado com MSVC, g++ ou clang++ e o executa. |
| `rt/` | O **runtime** ao qual todo programa gerado se liga: clientes HTTPS, o laço de uso de ferramentas dos agentes, a decisão do switch (modelo de IA ou palavras), ferramentas de arquivo confinadas e a proteção de rede contra DNS rebinding. |
| `local/` | Modelos locais gerenciados: catálogo GGUF, downloads retomáveis, ciclo de vida do llama.cpp, detecção de hardware e o router nativo do `llama-server`. |
| `gateway/` | AI Gateway on-prem: chaves virtuais, budgets, rate limits, ledger de uso e proxy compatível com OpenAI/Anthropic. |
| `sandbox/` | Drivers de isolamento (processo do host, Docker, MicroVM), emissor de telemetria, motor de retomada. |
| `native/` | Os servidores de ferramentas nativos (arquivos, HTTP, dados, memória, tempo, Git, SQLite): C++ que roda dentro do programa, com confinamento ao projeto. |
| `mcp/` | Cliente nativo do Model Context Protocol com sessões persistentes (stdio e HTTP) e o registro de servidores. |
| `memory/` | Memória Blackboard e de lições semânticas. |
| `project/` | Projetos e workspaces (`arestrix.project.json`), serviço de sincronização Git (`GitSyncService`), exportador do Runner Standalone (`RunnerService`) com auditoria zero-segredos, auto-commit/push e criação de repositórios privados. |
| `search/` | Busca no projeto: ranking lexical (BM25 + trigramas de caracteres) sobre todos os pipelines, subfluxos e skills, com índice por hash de conteúdo. |
| `pi/` | Scanner para importar projetos de agentes Pi. |
| `cli/` | `arestrix-cli`, o executor sem interface. |
| `util/` | Camada JSON própria sobre o **simdjson**. |

Todo código externo fica em `third_party/` (veja [Desenvolvimento](desenvolvimento.md)).

## Modelos locais e AI Gateway

Modelos hospedados e locais usam caminhos diferentes:

```text
Studio / Runner ──(chave virtual)──► arestrix-gateway ──(chaves reais)──► OpenAI/Anthropic/...
Studio / Runner ──(direto)─────────► llama-server (local, isento de budgets)
```

- **Runtime local (`CppCore/src/local/`).** O Arestrix baixa um build pinado do `llama.cpp` sob demanda,
  verifica o SHA-256, extrai e inicia o `llama-server` no modo router nativo. O servidor escuta em
  `127.0.0.1`, usa chave efêmera e um preset gerado. Endpoint/chave ficam apenas no ambiente do processo;
  modelos locais nunca passam pelo gateway.
- **AI Gateway (`arestrix-gateway`).** Executável separado que guarda as chaves reais dos provedores e
  expõe chaves virtuais ao Studio e aos runners. Ele impõe budgets por chave/projeto/usuário/provedor/modelo,
  rate limits e ledger de uso. Provedores hospedados podem ser configurados como fail-closed via
  `ARESTRIX_REQUIRE_AI_GATEWAY` ou pela política empresarial bloqueada.
- **Portabilidade do runner.** Runners gerados leem URL/chave do gateway ou endpoint local do ambiente de
  destino; chaves de provedor e segredos do runtime não são embutidos no binário.

## Studio, Workspace e Projeto

O Arestrix separa três escopos, no código e na interface. A separação existe para que credenciais,
memória e arquivos nunca vazem entre coisas que devem ser independentes.

| | **Studio** | **Workspace** | **Projeto** |
| :--- | :--- | :--- | :--- |
| O que é | O aplicativo: canvas, toolchain de compilação | A pasta física e sua infraestrutura | Um propósito de negócio e seus pipelines |
| Guarda | Chaves de API globais, repositórios GitHub de skills autorizados, projetos recentes | Skills e MCPs em `.workspace/`, banco SQLite, sandboxes, limites | `arestrix.project.json`, `pipelines/*.json`, repositório Git dedicado (clone local com sync remoto), prompts, memória isolada por `projectId` |
| Analogia | A fábrica | A oficina | O produto em construção |

Os elementos da interface levam a etiqueta do escopo (`Studio`, `Workspace`, `Projeto`) para que você
saiba sempre o que está alterando. Mais em
[Workspaces, memória, skills e MCP](workspaces-memoria-skills-mcp.md).

## Do grafo ao programa em execução

1. O grafo é validado no frontend (veja [Grafo e vértices](grafo-e-vertices.md)).
2. É enviado ao backend como JSON.
3. O `TaskflowGenerator` emite um `main.cpp`: cada vértice vira uma tarefa Taskflow, as arestas viram
   dependências `precede()`, vértices de condição retornam o índice do ramo, arestas de laço viram laços do Taskflow.
4. O `BuildService` compila contra o `arestrix_core` e executa na pasta do projeto. Um grafo
   inalterado reaproveita o binário anterior.
5. Logs e telemetria voltam em fluxo para o console e para os selos no canvas.

Os passos 4 e 5 estão detalhados em [Agentes e execução](agentes-e-execucao.md).

## Arquitetura do Runner Standalone

O **Runner Standalone** desacopla a execução do pipeline do ambiente de edição do Studio:
1. **Frontend (`RunnerManager`):** Controla o ciclo de vida da execução simplificada, orquestra os eventos do `EventBus` para as telas de Progresso, Diagnóstico e Operação, e realiza a pré-auditoria de segurança.
2. **Backend (`RunnerService`):**
   - Compila o grafo nativo Taskflow diretamente em um binário executável autocontido.
   - Gera o manifesto de distribuição (`runner_manifest.json`) com schema de parâmetros dinâmicos.
   - Copia as dependências de skills confinadas ao projeto.
   - Executa a auditoria estrita *fail-closed* contra vazamento de segredos em arquivos e no binário antes de finalizar a exportação.
3. **Resolução de Segredos em Runtime:** O binário gerado não contém chaves embutidas; ele consulta as variáveis de ambiente da máquina de destino no SO (`platform::getEnv`), garantindo total portabilidade segura. Em deployments empresariais, provedores hospedados recebem uma chave virtual do gateway em vez de credenciais de provedor; provedores locais usam o endpoint local e ficam isentos dos budgets do gateway.

## Relacionados

- [Primeiros passos](primeiros-passos.md)
- [Grafo e vértices](grafo-e-vertices.md)
