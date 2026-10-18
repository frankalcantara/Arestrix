# Historico do projeto Arestrix (antes NodeForge/TaskForge)

Compilado em 2026-10-13 a partir do `git log` (66 commits, 2026-10-03 a 2026-10-13) e das
transcricoes das sessoes do Claude Code nesta pasta. Datas das sessoes em UTC.
Para a sessao de validacao do clone novo, ver `HISTORICO_SESSAO_2026-10-13.md`.

Visao: orquestrador visual de grafos de agentes, interface em webview (WebUI) e backend em C++20
(Taskflow), para que qualquer pessoa (contador, advogado, enfermeiro) monte seu enxame de agentes,
pagando so o custo da API escolhida (ou usando modelos locais). Tudo open source.

## Sessoes (Claude Code)

### 2026-10-10 - Ambiente (sessao 44904600)
Configuracao do ambiente da maquina e do Claude Code (Windows e WSL): node no WSL, branch
`importa_excel` no WSL, uso das configuracoes de agentes no Ubuntu. Tambem pediu um tutorial de
configuracao do Claude Code (Linux e Windows) para `frankalcantara.github.io/_posts/IAaplicada`,
no padrao de front matter e estilo do blog. Nao alterou o codigo do Arestrix.

### 2026-10-12 - Submodulos NVIDIA (sessao fa51cc12)
Primeiros submodulos em `third_party/`: NVIDIA/skills e NVIDIA/SkillSpector. Depois o submodulo
`skills` foi removido (commits 7fec779, 444aa82); ficou so o SkillSpector.

### 2026-10-13 manha - README bilingue (sessao 675969ab)
- README com en-US (padrao) e pt-BR com seletor de idioma e agradecimentos/links dos projetos de
  `third_party` (75b9c09).
- Refeito sem icones, tom sobrio, abrindo com a motivacao do projeto (4c0707f).

### 2026-10-13 - Motor de execucao (sessao cc0aec22)
Analise do projeto e roadmap: Studio roda em Windows e Linux; a geracao de apps runner
fica para depois que o Studio criar e rodar grafos completos. Entregas (commit e82b6f1):
- Submodulos Taskflow 4.1.0, simdjson 5.0.4 e WebUI 2.4.2 em `third_party/`, com agradecimentos.
- Todo parse JSON via simdjson; nlohmann removido e substituido por `taskforge::Json`.
- Motor: clientes HTTPS (WinHTTP/curl) com laco de tool-use (DeepSeek, OpenAI, Anthropic, Gemini,
  Jev, locais), ferramentas confinadas, F5 gera/compila/executa C++ Taskflow com telemetria,
  `arestrix-cli`; tutorial `TUTORIAL_AGENTE_SVG_ARESTRIX.md` roda ponta a ponta
  (`scripts/e2e_tutorial.py`).
- Seguranca: sem `std::system`, caminhos confinados, sandbox fail-closed, MCP real.
- Build: CMake (Win/Linux) no lugar do nmake; `dev.py` reescrito.
- Politica: todo codigo externo compativel com a licenca do Taskflow (MIT) e em `third_party/`;
  `scripts/check_third_party_licenses.py` a verifica. Three.js, troika e Lucide ficaram como estao.
- Revisao de segredos nos JSON de configuracao antes de tornar o repositorio publico.

### 2026-10-13 tarde - Validacao do clone novo (esta sessao)
Falha de CMake por `build/` no `.gitignore`, analise do `npm audit`; ver
`HISTORICO_SESSAO_2026-10-13.md` e `SEGURANCA_DEPENDENCIAS.md`.

### 2026-10-18 - Sincronizacao Git por Projeto e Zero-Secrets
- Cada projeto mapeado para um repositorio Git configuravel (padrao GitHub) com clone local e auto-save continuo.
- Commit e push automaticos na execucao (F5) e no encerramento do Arestrix (backend C++ hooks).
- Seguranca Zero-Secrets: nenhuma credencial ou token no Arestrix; autenticacao e identidade delegadas integralmente ao Git Credential Manager no SO.
- Criacao automatica de repositorios privados com o nome do projeto.
- Aviso obrigatorio de governanca e privacidade para servicos comerciais de terceiros (GitHub/GitLab).
- Modal Fluent/Office (`GitAccountModal`), indicador no QAT e informacoes no Backstage.
- Bateria de testes: 764/764 verificacoes nativas C++ e 289/289 testes Vitest.

## Linha do tempo (git)

| Data | Marco |
|---|---|
| 10-03 | README em ingles; catalogo de agentes e sistema de plugins (Fase 4) |
| 10-04 | Migracao TS -> C++ nativo com WebUI |
| 10-05 | Renomeado para TaskForge; testes adaptados ao backend C++ |
| 10-06 | Fase 5 CUDA (`tf::cudaFlow`); Fase 6 export Graphviz DOT/SVG |
| 10-07 | Fase 7 padroes globais e modais; Fase 8 no Code Mode; Fase 9 sandbox/MicroVM; Fase 10 telemetria; Fase 11 batch/resume |
| 10-08 | Consolidacao do CppCore; Taskflow 4.1 CUDA; docs |
| 10-09 | Renomeado para Arestrix; Memoria Heterogenea, MCP, Workspaces, Ribbon estilo Office; importacao generica de projetos Pi |
| 10-10 | Topologia concorrente fork/join, ingestao de skills, editor markdown, catalogo GitHub; modais redimensionaveis, fonte SDF, icones Lucide |
| 10-11 | ID de projeto por SHA-256, restaurar ultimo projeto, Salvar Como com pacote `.arestrix`, autosave, vertices SVG, abas de padroes globais |
| 10-12 | Governanca de workspace, aba de skills pre-aprovadas, arquitetura tripartite Studio/Workspace/Projeto, exportador Taskflow C++20 com Monaco, i18n pt-BR/en-US (i18next), guia de novos idiomas |
| 10-13 | i18n profunda de toda a UI; READMEs bilingues; motor de execucao + simdjson + `third_party`; correcao do `.gitignore` |
| 10-18 | Sincronizacao Git por projeto (GitHub/privado), login seguro via GCM no SO, push automatico F5/shutdown, aviso comercial |

## Estado atual e pendencias

- `main` limpo e publicado; clone novo com submodulos compila e roda (`python CppCore/dev.py`).
- `SEGURANCA_DEPENDENCIAS.md`: atualizar vite/vitest e monaco-editor/dompurify.
- Avisos do Vite: `webui.js` sem `type="module"`; `fs`/`path` em `GenericPiProjectScanner.ts`.
- Futuro: criacao de aplicativos runner standalone (so Windows) depois que o Studio executar grafos completos.
