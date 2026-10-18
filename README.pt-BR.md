<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/arestrix-logo-reverse.svg">
    <img src="docs/assets/arestrix-logo.svg" alt="Arestrix" width="420">
  </picture>
</p>

<p align="center">
  <a href="README.md">English</a> &nbsp;|&nbsp; <b>Português</b>
</p>

<p align="center">
  <img alt="Grafite" src="https://img.shields.io/badge/Grafite-14151A?style=flat-square">
  <img alt="Papel" src="https://img.shields.io/badge/Papel-F4F2EE?style=flat-square">
  <img alt="Brasa" src="https://img.shields.io/badge/Brasa-E8590C?style=flat-square">
  <img alt="Ardósia" src="https://img.shields.io/badge/Ard%C3%B3sia-4B4F5C?style=flat-square">
  <img alt="Licença: MIT" src="https://img.shields.io/badge/licen%C3%A7a-MIT-14151A?style=flat-square">
</p>

## Por que isto existe

Eu estou fazendo este projeto porque gosto de coisas gráficas e simples de usar. O meu problema é que qualquer contador, advogado ou enfermeiro possa criar seu próprio enxame de agentes, de forma simples e direta, pagando apenas o custo da API que escolher, durante o trabalho que realizar, e que seja rápido, muito rápido.

## O que é

O **Arestrix Studio** é um editor visual de grafos de agentes de IA. Você desenha o pipeline em um canvas, e o Arestrix transforma o desenho em um **programa C++20 construído sobre o [Taskflow](https://github.com/taskflow/taskflow)**, compila e executa, com retorno ao vivo em cada vértice.

- **Desenhe, não programe.** Os vértices são tarefas, condições, subpipelines, forks e joins paralelos, kernels de GPU e agentes autônomos. As arestas são a ordem de execução, inclusive laços de feedback.
- **Agentes que trabalham de verdade.** Cada vértice de agente executa um laço de uso de ferramentas contra DeepSeek, OpenAI, Anthropic, Google, um modelo local ou qualquer servidor compatível com OpenAI, dentro de um sandbox que você escolhe.
- **Rápido e paralelo.** O programa gerado é C++20 nativo; ramos independentes executam concorrentemente.
- **Seguro por padrão.** Os sandboxes falham de forma fechada, o acesso a arquivos é confinado ao projeto e os laços de feedback têm um teto de gasto.
- **Reutilizável.** Uma biblioteca de vértices prontos, seus próprios templates salvos, skills e servidores MCP.
- **Sincronização Git por projeto.** Cada projeto é mapeado para um repositório Git privado (padrão GitHub) com auto-save no clone local e commit/push automáticos no F5 e ao encerrar. Credenciais gerenciadas com segurança no SO (GCM), com zero segredos no Arestrix e avisos de soberania para serviços comerciais.
- **Windows e Linux**, interface bilíngue (português e inglês), licença MIT.

## Início rápido

```bash
git clone --recurse-submodules https://github.com/frankalcantara/Arestrix.git
cd Arestrix
npm install
python CppCore/dev.py
```

Você precisa de Git, Node.js 18+, Python 3.10+, CMake 3.20+ e um compilador C++20. Detalhes em
[Primeiros passos](docs/pt-BR/02-primeiros-passos.md). Depois monte um grafo e pressione **F5**.

## Documentação

| | |
| :--- | :--- |
| [Arquitetura](docs/pt-BR/01-arquitetura.md) | Camadas, módulos do backend, Studio / Workspace / Projeto, do grafo ao programa. |
| [Primeiros passos](docs/pt-BR/02-primeiros-passos.md) | Requisitos, build, chaves de API, primeira execução. |
| [Grafo e vértices](docs/pt-BR/03-grafo-e-vertices.md) | Tipos de vértice, arestas sólidas e de feedback, validação, menu de contexto, biblioteca de vértices. |
| [Agentes e execução](docs/pt-BR/04-agentes-e-execucao.md) | Compilar e Executar, uso de ferramentas, sandboxes, segurança, telemetria, lotes e retomada. |
| [Workspaces, memória, skills e MCP](docs/pt-BR/05-workspaces-memoria-skills-mcp.md) | Projetos, memória, skills, governança, MCP, importação de trabalho existente. |
| [Tour pela interface](docs/pt-BR/06-interface.md) | Ribbon, Backstage, canvas, inspetor, modais, atalhos. |
| [Desenvolvimento](docs/pt-BR/07-desenvolvimento.md) | Testes, adicionar um idioma, política de terceiros, convenções. |
| [Marca](docs/pt-BR/08-marca.md) | Ícone, cores, tipografia. |
| [Exemplos](examples/README.pt-BR.md) | Três projetos prontos, do tutorial a uma triagem de suporte multiagente com MCP, e como os segredos ficam fora do Git. |

Um exemplo completo passo a passo: [`TUTORIAL_AGENTE_SVG_ARESTRIX.md`](TUTORIAL_AGENTE_SVG_ARESTRIX.md).

## Licença

[MIT](LICENSE). Projetado e desenvolvido com paixão e colaboração de IA.

## Agradecimentos

O Arestrix se apoia em trabalhos open source excepcionais. Nosso sincero agradecimento aos autores e às comunidades dos projetos incluídos em [`third_party/`](third_party):

| Projeto | Papel no Arestrix | Licença |
| :--- | :--- | :--- |
| [**WebUI**](https://github.com/webui-dev/webui) (Hassan Draga) e o servidor [**CivetWeb**](https://github.com/civetweb/civetweb) que ele embute | A janela nativa leve que hospeda a interface do Studio e a ponte RPC entre a UI web e o backend C++, sem Electron. | MIT |
| [**Taskflow**](https://github.com/taskflow/taskflow) (Dr. Tsung-Wei Huang e colaboradores) | O motor C++20 de programação paralela e heterogênea de tarefas que executa todo grafo. | MIT |
| [**simdjson**](https://github.com/simdjson/simdjson) (Daniel Lemire, Geoff Langdale e colaboradores) | Faz o parse de todo documento JSON que o motor lê, a gigabytes por segundo. | Apache-2.0 / MIT |
| [**SQLite**](https://sqlite.org) (D. Richard Hipp e colaboradores) | O banco de dados embutido por trás das ferramentas nativas `arestrix.sqlite`. | Domínio público |
| [**i18next**](https://github.com/i18next/i18next) | Internacionalização por trás de cada texto traduzido. | MIT |
| [**Monaco Editor**](https://github.com/microsoft/monaco-editor) (Microsoft) | O editor de código que exibe o C++ gerado. | MIT |
| [**SkillSpector**](https://github.com/NVIDIA/SkillSpector) (NVIDIA) | Scanner de segurança para skills de agentes de IA. | Apache-2.0 |
| [**Inter**](https://github.com/rsms/inter) (The Inter Project Authors) | Tipografia do texto do canvas. | OFL-1.1 |

Também: [Three.js](https://threejs.org), [Lucide](https://lucide.dev), [troika-three-text](https://github.com/protectwise/troika), [Vite](https://vitejs.dev) e [Vitest](https://vitest.dev).

Cada projeto permanece propriedade de seus autores e é distribuído sob a sua própria licença, disponível na respectiva pasta em `third_party/`. A política para código externo está em [Desenvolvimento](docs/pt-BR/07-desenvolvimento.md#política-de-código-de-terceiros).
