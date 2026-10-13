# Arquitetura

[English](../en/01-architecture.md) · [Índice](../../README.pt-BR.md#documentação)

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
| Lógica | `src/core/` | `Graph` em memória, validação, hierarquia de subfluxos, histórico com **padrão Command** (desfazer/refazer), templates, skills, projetos e workspaces. Sem renderização. |
| Visual | `src/view/` | Canvas ortográfico Three.js, câmera (pan/zoom), grade infinita, arestas de Bézier e a camada SVG que desenha os cartões dos vértices por cima. |
| Interface | `src/ui/` | Ribbon estilo Office, visão Backstage, inspetor, modais, console, menu de contexto. |
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
| `rt/` | O **runtime** ao qual todo programa gerado se liga: clientes HTTPS, o laço de uso de ferramentas dos agentes, a decisão do switch (modelo de IA ou palavras), ferramentas de arquivo confinadas. |
| `sandbox/` | Drivers de isolamento (processo do host, Docker, MicroVM), emissor de telemetria, motor de retomada. |
| `mcp/` | Cliente do Model Context Protocol e registro de servidores. |
| `memory/` | Memória Blackboard e de lições semânticas. |
| `project/` | Projetos e workspaces (`arestrix.project.json`). |
| `search/` | Busca no projeto: ranking lexical (BM25 + trigramas de caracteres) sobre todos os pipelines, subfluxos e skills, com índice por hash de conteúdo. |
| `pi/` | Scanner para importar projetos de agentes Pi. |
| `cli/` | `arestrix-cli`, o executor sem interface. |
| `util/` | Camada JSON própria sobre o **simdjson**. |

Todo código externo fica em `third_party/` (veja [Desenvolvimento](07-desenvolvimento.md)).

## Studio, Workspace e Projeto

O Arestrix separa três escopos, no código e na interface. A separação existe para que credenciais,
memória e arquivos nunca vazem entre coisas que devem ser independentes.

| | **Studio** | **Workspace** | **Projeto** |
| :--- | :--- | :--- | :--- |
| O que é | O aplicativo: canvas, toolchain de compilação | A pasta física e sua infraestrutura | Um propósito de negócio e seus pipelines |
| Guarda | Chaves de API globais, repositórios GitHub de skills autorizados, projetos recentes | Skills e MCPs em `.workspace/`, banco SQLite, sandboxes, limites | `arestrix.project.json`, `pipelines/*.json`, prompts, memória isolada por `projectId` |
| Analogia | A fábrica | A oficina | O produto em construção |

Os elementos da interface levam a etiqueta do escopo (`Studio`, `Workspace`, `Projeto`) para que você
saiba sempre o que está alterando. Mais em
[Workspaces, memória, skills e MCP](05-workspaces-memoria-skills-mcp.md).

## Do grafo ao programa em execução

1. O grafo é validado no frontend (veja [Grafo e vértices](03-grafo-e-vertices.md)).
2. É enviado ao backend como JSON.
3. O `TaskflowGenerator` emite um `main.cpp`: cada vértice vira uma tarefa Taskflow, as arestas viram
   dependências `precede()`, vértices de condição retornam o índice do ramo, arestas de laço viram laços do Taskflow.
4. O `BuildService` compila contra o `arestrix_core` e executa na pasta do projeto. Um grafo
   inalterado reaproveita o binário anterior.
5. Logs e telemetria voltam em fluxo para o console e para os selos no canvas.

Os passos 4 e 5 estão detalhados em [Agentes e execução](04-agentes-e-execucao.md).

## Relacionados

- [Primeiros passos](02-primeiros-passos.md)
- [Grafo e vértices](03-grafo-e-vertices.md)
