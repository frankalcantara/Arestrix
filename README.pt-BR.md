<p align="center">
  <picture>
    <source media="(prefers-color-scheme: dark)" srcset="docs/assets/arestrix-logo-reverse.svg">
    <img src="docs/assets/arestrix-logo.svg" alt="Arestrix" width="440">
  </picture>
</p>

<p align="center">
  <a href="README.md">English</a> &nbsp;|&nbsp; <b>Português</b>
</p>

<p align="center">
  <a href="https://github.com/frankalcantara/Arestrix/releases"><img alt="Release Beta" src="https://img.shields.io/github/v/release/frankalcantara/Arestrix?include_prereleases&label=beta&style=flat-square"></a>
  <a href="LICENSE"><img alt="Licença: MIT" src="https://img.shields.io/badge/licen%C3%A7a-MIT-14151A?style=flat-square"></a>
  <img alt="Plataformas: Windows e Linux" src="https://img.shields.io/badge/plataforma-Windows%20%7C%20Linux-4B4F5C?style=flat-square">
  <img alt="C++20" src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&logo=c%2B%2B">
</p>

<p align="center">
  <b>Orquestração visual e compilação nativa em C++20 para pipelines de agentes de IA.</b><br>
  <i>Desenhe o grafo, compile para Taskflow e distribua um runner independente.</i>
</p>

<p align="center">
  <b>Beta 2.3</b> &nbsp;·&nbsp; <a href="CHANGELOG.md">Changelog</a> &nbsp;·&nbsp; <a href="docs/README.md">Documentação</a>
</p>

<p align="center">
  <img src="docs/assets/screenshots/studio-main.webp" alt="Arestrix Studio: pipeline multiagente em execução no canvas" width="960">
</p>

<p align="center">
  <img src="docs/assets/screenshots/properties-hub.webp" alt="Hub de Propriedades no padrão Office para Studio, Workspace e Projeto" width="470">
  <img src="docs/assets/screenshots/studio-dark.webp" alt="Arestrix Studio no tema escuro com um grafo multiagente" width="470">
</p>

---

## Teste sem compilar

Baixe o build **Windows x64** pronto em
[**Releases → latest**](https://github.com/frankalcantara/Arestrix/releases/tag/latest):

- **Instalador (recomendado):** execute `arestrix-<versão>-windows-x64-setup.exe`. Ele instala por
  usuário em `%LOCALAPPDATA%\Programs\Arestrix`, sem privilégios administrativos, e verifica WebView2,
  Visual C++ Redistributable, Git/credencial GitHub, MSVC Build Tools e chaves de API sem expor os
  valores.
- **Portátil:** extraia o ZIP em qualquer pasta e execute `start-arestrix.bat`. A pasta extraída vira
  o `ARESTRIX_HOME`.

O CLI é instalado junto com o Studio (`arestrix-cli.exe`). Para compilar pipelines, instale também o
MSVC Build Tools ("Desenvolvimento para Desktop com C++"); o pacote não inclui compilador. Cada build
publica `SHA256SUMS`, `SBOM.json` e `package-manifest.json` com hash por arquivo. `latest` é um
pré-release contínuo gerado a partir da `main`; tags de versão publicam releases próprios.

## O que é o Arestrix?

O Arestrix é um estúdio desktop e um compilador nativo para fluxos de agentes de IA. Você desenha
um pipeline em um canvas de grafo; o Arestrix valida, gera código C++20 e compila com
[Taskflow](https://github.com/taskflow/taskflow) em DAGs de tarefas paralelas.

- **Design visual, execução nativa.** O grafo não é interpretado: ele se transforma em um programa
  compilado.
- **Local-first por padrão.** Projetos, memória, logs e bancos ficam na máquina; só saem chamadas
  para modelos e servidores MCP.
- **Feito para distribuição.** Exporte um runner independente, com empacotamento MSI/MSIX opcional
  para implantação gerenciada no Windows.
- **Segredos fora do projeto.** Credenciais são resolvidas em tempo de execução via Windows DPAPI ou
  Azure Key Vault / Managed Identity.
- **Artefatos revisáveis.** Grafos, C++ gerado e metadados de empacotamento são arquivos de texto
  que podem ser versionados e revisados.

## Por que Arestrix?

| Preocupação | Como o Arestrix responde |
| :--- | :--- |
| **Da prototipagem à produção** | O mesmo grafo roda no Studio ou é compilado em um runner distribuível. |
| **Consumo de runtime** | O Studio evita Electron e o programa gerado roda como binário nativo. |
| **Exposição de credenciais** | Arquivos de projeto guardam nomes de variáveis ou referências `${NAME}`, nunca valores. |
| **Confiança operacional** | Pacotes exportados carregam um manifesto de integridade; o runner pode verificá-lo antes da execução. |
| **Aprisionamento de fornecedor** | Licença MIT, formato de projeto local e suporte a múltiplos provedores de modelo e servidores MCP. |

## Recursos em destaque

- **Editor visual de grafos** - ribbon inspirada no Office, templates, vértices de decisão, ciclos
  de feedback, undo/redo e validação do grafo.
- **Hub de Propriedades Office** - Studio, Workspace e Projeto em uma única superfície no padrão
  Office, com herança explícita, badges de origem e editores especializados de credenciais, Git e memória.
- **Temas claro e escuro** - Claro, escuro ou seguir o sistema na ribbon, task pane, console,
  Backstage e todos os diálogos Office. O canvas permanece uma preferência independente.
- **Compilador C++20 nativo** — inspecione o código Taskflow gerado no Monaco, compile com MSVC ou
  GCC/Clang e reutilize builds inalterados.
- **Runtime de agentes** — laço de ferramentas, sandboxes, modelo por vértice, telemetria ao vivo e
  retomada de execuções em lote.
- **Modelos locais** — runtime llama.cpp gerenciado, catálogo GGUF pré-aprovado, recomendação por
  hardware e fallback para CPU. Nenhum runtime de inferência é incluído por padrão.
- **AI Gateway empresarial** — proxy on-prem com chaves virtuais, budgets e rate limits por
  chave/projeto/usuário/provedor/modelo, ledger de uso e roteamento fail-closed. Modelos locais são isentos.
- **Memória de projeto (AMG)** — multigrafo temporal em SQLite com lições, Blackboard,
  `memory_search`/`memory_record_lesson`, episódios de sucesso/falha, rastreio de memórias
  injetadas/usadas, brief com orçamento de tokens, métricas de API e A/B no Studio.
- **Biblioteca Arestrix** — assets de skill, MCP e template auditados, com escopos Studio,
  Workspace e Projeto, pins por projeto, importação do catálogo embutido/workspace e fonte GitHub
  nativa com commit pin, checagem de licença e download sob demanda.
- **Workspaces, skills e MCP** — catálogos locais de skills e servidores MCP com sessão persistente
  por servidor, incluindo HTTP/Streamable HTTP e headers por servidor.
- **Entrega e governança** — runner independente, empacotamento MSI/MSIX, integração com DPAPI e
  Azure Key Vault, além de verificação de integridade HMAC.
- **Bilíngue** — interface e documentação em inglês e português.

Veja a [galeria de recursos](docs/product/showcase.md) para capturas de cada área.

## Início rápido

### Requisitos

- Git 2.30+
- Node.js 20+
- Python 3.10+
- CMake 3.20+
- Compilador C++20: MSVC v143+ no Windows, GCC 11+ ou Clang 14+ no Linux

### Compilar e abrir

```bash
# Clone com os submódulos de terceiros pinados
git clone --recurse-submodules https://github.com/frankalcantara/Arestrix.git
cd Arestrix

# Instale as dependências do frontend
npm ci

# Compile frontend e backend C++ e abra o Studio
python CppCore/dev.py
```

Opcional: ative o hook de verificação de segredos uma vez por clone com
`npm run hooks:install`.

Dentro do Studio:

1. Abra um dos [exemplos](examples/README.pt-BR.md) ou crie um projeto.
2. Escolha provedor e modelo em **Arquivo > Propriedades > Projeto > Modelo e ferramentas**; as
   chaves vêm de variáveis de ambiente ou do seu cofre de credenciais.
3. Pressione **F5** para compilar e executar o grafo com telemetria ao vivo.
4. Use **Exportar Runner Independente** para empacotar o pipeline para entrega.

Para CI, servidores ou fluxo somente terminal, use o executor headless gerado pelo mesmo build:

```powershell
.\arestrix-cli.exe run .\examples\04-memoria\pipelines\main.json --root .\examples\04-memoria
```

Veja [`arestrix-cli`](docs/pt-BR/arestrix-cli.md) para `gen`, `env`, `check`, `doctor`,
`memory report` nativo, `mcp`, `project`, `--ci`, modo lote, códigos de saída e solução de problemas.

Nenhuma chave de API é necessária para rodar os exemplos ponta a ponta com LLM falso:

```bash
python scripts/e2e_examples.py
```

## Documentação

| Comece por | O que você encontra |
| :--- | :--- |
| [Central de documentação](docs/README.md) | O guia completo, organizado por idioma e assunto. |
| [Primeiros passos](docs/pt-BR/primeiros-passos.md) | Requisitos, build, configuração de modelo e primeira execução. |
| [Arquitetura](docs/pt-BR/arquitetura.md) | Studio, engine, WebUI e o caminho do grafo ao C++. |
| [`arestrix-cli`](docs/pt-BR/arestrix-cli.md) | Execução headless, CI, códigos de saída e solução de problemas. |
| [Memória institucional](docs/pt-BR/como-funciona-a-memoria.md#12-instituição) | `arestrix-memory`: API HTTP autenticada e MCP (stdio e `/mcp`). |
| [Validação de skills](docs/quality/skill-reference-review.md) | Arestrix Skill Guard: NVIDIA SkillSpector + Cloudflare security-audit, lock e revisão de referências. |
| [Exemplos](examples/README.pt-BR.md) | Cinco projetos executáveis, de um pipeline de dois agentes a fluxos compostos, de memória e de pesquisa web. |
| [Registro de auditoria de testes](docs/quality/test-audit-log.md) | Rodadas cronológicas de testes e auditorias de integridade. |
| [Plano de testes do Beta 2.3](docs/quality/beta2.3-test-plan.md) | Escopo registrado e critérios de aceite pendentes. |
| [Changelog](CHANGELOG.md) | Histórico de releases, mudanças e limitações conhecidas. |

## Status do projeto

A **Beta 2.3** (`2.3.0-beta.1`) é um pré-release de desenvolvimento. O Studio roda no Windows e no
Linux; programas gerados a partir de um grafo atualmente têm o Windows como alvo. APIs, formatos de
arquivo e flags de linha de comando podem mudar antes da disponibilidade geral. Consulte o
[changelog](CHANGELOG.md) para as limitações atuais. O instalador Windows e a assinatura de código
estão incluídos; validação em máquina limpa, pacote Linux e a matriz completa de testes ainda estão
pendentes. Acompanhe no [plano de testes do Beta 2.3](docs/quality/beta2.3-test-plan.md). Os artefatos
Windows são assinados pela [SignPath Foundation](https://signpath.org) quando a assinatura está
habilitada; consulte a [política de assinatura de código](CODE_SIGNING.md).

## Segurança

- Reporte vulnerabilidades em canal privado, conforme o [SECURITY.md](SECURITY.md).
- Leia o [modelo de segurança e governança](docs/security/governance.md).
- Leia a [política de assinatura de código](CODE_SIGNING.md) e a [política de privacidade](PRIVACY.md).
- Consulte os [avisos de dependências](docs/security/dependency-advisories.md) antes de atualizar
  pacotes npm.
- Componentes e licenças de terceiros estão em [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

## Contribuição

Contribuições são bem-vindas. Leia o [CONTRIBUTING.md](CONTRIBUTING.md) para setup, testes,
convenções de commit e checklist de pull request, e siga o [Código de Conduta](CODE_OF_CONDUCT.md).

- Use o [GitHub Discussions](https://github.com/frankalcantara/Arestrix/discussions) para dúvidas e
  ideias.
- Use os templates de issue para bugs reproduzíveis e pedidos de recurso.
- Não abra issue pública para vulnerabilidades de segurança.

## Agradecimentos

O Arestrix é construído sobre Taskflow, simdjson, WebUI, SQLite, Monaco Editor, i18next e Inter,
entre outros projetos open source. Consulte [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) e
[`third_party/`](third_party/README.md) para versões, papéis e licenças.

## Contato

- Assuntos gerais, comerciais e de conduta: [contact@arestrix.com](mailto:contact@arestrix.com)
- Relatos de segurança: siga o [SECURITY.md](SECURITY.md) e use o reporte privado do GitHub primeiro.
- Dúvidas técnicas: [GitHub Discussions](https://github.com/frankalcantara/Arestrix/discussions)

## Licença

Distribuído sob a [Licença MIT](LICENSE). Livre para uso comercial e acadêmico.
