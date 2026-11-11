# Registro de Auditoria e Integridade de Testes (Test Audit Log)

> **Aplica-se a:** Arestrix Beta 2.3 (`2.3.0-beta.2`) · Última revisão: 2026-10-28

Este documento registra o histórico cronológico das rodadas de testes automatizados, verificações de segurança e auditorias de integridade do **Arestrix**, organizadas da **mais recente para a mais antiga**.

> O Beta 2.3 está registrado como pré-release. O instalador foi implementado; a matriz
> obrigatória e as validações pendentes estão em [Plano de testes do Beta 2.3](beta2.3-test-plan.md).

O Arestrix adota a filosofia de integridade estrita (*fail-closed*): nenhuma funcionalidade é considerada concluída ou pronta para ambiente corporativo sem aprovação completa em todas as categorias de validação.

---

## Estrutura das Categorias de Integridade

Para garantir clareza executiva e cobertura técnica aprofundada, as suítes de testes são consolidadas em **8 categorias fundamentais**:

1. **Segurança Corporativa, Chaves & Segredos (Zero-Secret & Sanitização)**: Detecção preventiva de segredos, isolamento de credenciais via DPAPI / Azure Key Vault, fail-closed e scrub de subprocessos.
2. **Motor Concorrente C++20, Pipeline DAG & Standalone Runner**: Compilação de grafos em Taskflow C++20, agendamento multi-thread, telemetria em tempo real (streaming de pensamento/thinking) e retomada de falhas em lote.
3. **Topologia de Grafos, Vértices, Arestas Bezier & Validação**: Detecção estrita de ciclos (DAG), tipagem de portas de entrada/saída, suavização de curvas Bezier, subfluxos aninhados e pilha de undo/redo.
4. **Agentes de IA, Herança de Parâmetros, Memória & Skills**: Herança determinística de modelos/sandboxes, memória persistente SQLite, isolamento entre skills locais pré-aprovadas e catálogos externos.
5. **Protocolo MCP (Model Context Protocol) & Conectores**: Conformidade com JSON-RPC 2.0, descoberta de esquemas dinâmicos, validação Zod e execução isolada de ferramentas.
6. **Interface de Usuário Office 365, Gerenciador de Janelas & Modais**: Ribbon fluida, janelas flutuantes com arrasto/redimensionamento/restauração, Backstage view e painéis de suporte.
7. **Tipografia Nítida (Zero Shadows), Renderização SVG & i18n**: Ausência estrita de sombras/glows em fontes, paridade bilíngue (pt-BR / en-US), exportação Graphviz WASM e renderização vetorial.
8. **Gerenciamento de Projetos, Integridade SHA-256 & Sincronização Git**: Salvamento atômico transacional com hash SHA-256, sincronização Git segura via SO e validação estrutural dos exemplos.

---

## Histórico Cronológico de Auditorias

### Rodada #007 - 2026-11-11 (validação pós-2.7.0: A/B live, LIB-M-01 e instalador)
- **Commit:** árvore de trabalho sobre `0027182` (2.7.0 + scripts de validação e documentação)
- **Status Geral:** `APROVADO NO ESCOPO AUTOMATIZADO; CONFIRMAÇÃO NATIVA E MÁQUINA LIMPA PENDENTES`
- **Ambiente de Teste:** Windows 11 (x64) | Inno Setup 6.7.3 | WebView2 157.0.4341.42 | VC++ 14.51.36247.00 | DeepSeek Chat (provedor real)

#### Cobertura desta rodada

| Área | Evidência |
| :--- | :--- |
| **A/B v2 real** | `scripts/e2e_memory_ab_live.py`: 20 tarefas × 3 repetições × 2 braços (120 execuções), conhecimento congelado, 0 falhas de infraestrutura e 0 tarefas inválidas; aprovação 60/60 nos dois braços; ON 377.378 tokens e 10,95 s contra OFF 146.220 tokens e 6,46 s |
| **A/B v2 de valor** | `--memory-dependent` (token `AB-7731`, só no conhecimento curado): 120 execuções; ON 60/60 (1,000) × OFF 0/60 (0,000), delta 1,000 com IC95 [1,000, 1,000]; ON 405.161 tokens e 11,56 s contra OFF 151.539 tokens e 7,12 s; `tests/fixtures/memory-ab-live-value-report.json` |
| **Tracy F1** | `ARESTRIX_ENABLE_TRACY` e `dev.py --tracy`; build OFF e ON com `4.259/4.259` verificações; cache final em `OFF`; zona `agent.run` em `rt/Profiling.hpp` |
| **MemoryGate** | `off`/`shadow`/`enforce`; shadow registrou 19 decisões com `applied=false`; com Jev (`jev-latest`, System One): paridade ON 1,000 com 1,39× tokens e 1,01× latência, valor ON 1,000 com 1,51×/1,03×, gate ~270 ms; DeepSeek ficou em 1,34×/1,16× e 1,47×/1,19×; confirmação de 3 repetições pendente por latência do agente |
| **Cenários de memória** | Propriedades do Projeto → Memória: Consultar (`gate=brief`), Consultar e Decidir (padrão, `gate=enforce` com Jev) e Aprender com aprovação; memória ligada por padrão; “Personalizado” derivado ao alterar campo avançado; sem botão de configuração avançada |
| **Exemplos migrados** | 14 projetos/pipelines com memória ligada no cenário Consultar e Decidir; exemplos 04/05 mantêm `learn` com gate `shadow`; `e2e_examples.py` passou 14/14 |
| **Diagnóstico do custo ON** | 20/20 traces do ensaio fizeram `memory_search` (1,8 busca/trace); o brief injetado tem ~451 caracteres. O custo vem das buscas do agente, não do brief |
| **Bibliotecas LIB-M-01** | `scripts/capture-libraries-homologation.mjs`: temas claro/escuro, 1024/1800 px, árvore com PENDING/APPROVE/REJECT, sem overflow e canvas escuro; capturas em `docs/assets/screenshots/libraries-*.webp` |
| **Instalador 2.7.0** | `arestrix-2.7.0-windows-x64-setup.exe` (62.866.760 bytes, SHA-256 `f4067105…f3f80a`); instalação silenciosa, `arestrix-cli`/`arestrix-memory` 2.7.0, `project validate`, abertura do Studio, upgrade 2.1.0-beta.1 → 2.7.0 preservando projeto do usuário e desinstalação completa |
| **Pacote portátil 2.7.0** | `arestrix-2.7.0-windows-x64.zip` (90.100.073 bytes, 4.912 arquivos), SHA-256 `bb88bee8…e327f33` |

#### Pendências para promoção

- [ ] Confirmar LIB-M-01 na aplicação nativa (a evidência desta rodada é da camada web).
- [ ] LIB-M-03 (instalação limpa/upgrade preservando bibliotecas) e LIB-M-04 (offline após cache).
- [ ] Instalação em máquina realmente limpa (apenas runtime VC++) e Intune/SCCM.
- [ ] Medição dos interruptores experimentais (`pathServing`, `traversal`, `bridges`, `routing`) com um conjunto de tarefas próprio.
- [ ] Confirmação de 3 repetições do `memory.gate` com Jev/local ou janela de menor latência.
- [ ] Pacote Linux e matriz de CI Linux/macOS.

### Rodada #006 - 2026-11-10 (pré-release 2.7.0, correções da memória)
- **Commit:** árvore de validação pré-release `2.7.0` (hash do merge registrado no GitHub)
- **Status Geral:** `APROVADO NO ESCOPO AUTOMATIZADO; INSTALADOR E 4 CASOS MANUAIS DE BIBLIOTECAS PENDENTES`
- **Ambiente de Teste:** Windows 11 (x64) | Node.js v24.11.1 | Vitest v5.0.5 | Vite v8.3.5 | TypeScript v7.0.2 | Visual Studio 2026 / MSVC C++20 | Python 3.11.9
- **Métricas Globais:** 76 arquivos de teste | 617 testes Vitest | 4.223/4.223 verificações C++ | tutorial E2E + 14 exemplos | execução live com DeepSeek | 0 falhas automatizadas

#### Cobertura desta rodada

| Área | Evidência |
| :--- | :--- |
| **Gate oficial de release** | `release_check.py` (sem `--skip-e2e`): frontend, build C++20, CTest, Vitest, version, assets, secrets, skills, licenças, gateway smoke, tutorial e 14 exemplos; 26 artefatos e 131 componentes no SBOM |
| **Frontend** | `npm test`: 76 arquivos / 617 testes; `npm run build` e `tsc` sem erros |
| **Nativo C++20** | `arestrix_tests`: 4.223/4.223 verificações, incluindo a nova cobertura de linhagem e backfill |
| **Memória determinística** | baseline 3× (7.314 tokens, 15 chamadas), `e2e_memory`, remoção, fixture v4, A/B 120 execuções (ON=OFF=1,00), avaliação, estresse 8×500 com 0 falhas e doctor 0, instituição e `memory_fix5_baseline` |
| **Memória live (DeepSeek)** | `--with-epochs`: `ORION-42` nos braços on/compactado/epochs, `UNKNOWN` no off, razão de cache 1,021053; redução de prompt: 28.110 tokens, razão 0,1308 |
| **Bibliotecas + GitHub** | `homologation:libraries:live`: 35 testes Vitest, 46/46 C++ mock, 2/2 smoke live; 4 casos manuais pendentes |
| **CLI e doctor** | `doctor`, `memory doctor/health/report`, `utility status|seed|rebuild|rekey`, `audit-credit --report/--run`, `validator-audit`, `proposals`, `state`, `paths`, `bridges`, `embeddings`, `consolidate` e `check` |
| **Pacote portátil (ensaio 2.6.0)** | `arestrix-2.6.0-windows-x64.zip`: 89.894.688 bytes, 4.912 arquivos, SHA-256 `1a737029…ea3d7a8`; o pacote 2.7.0 é gerado pelo workflow de deploy |

#### Achado e correção

O `doctor` de um projeto que mistura ingestão de fontes com lições e propostas de agentes acusava linhagem órfã. A gravação de `memory_lineage` passou a cobrir arestas de lição, propostas e playbooks a partir de claim, episódio ou agente, e `backfillLineage` reconstrói esses vínculos em bancos antigos. O projeto E2E misto passou de 11 órfãos para 13 vínculos e 0 órfãos.

#### Pendências para promoção

- [ ] Instalador Windows: o Inno Setup 6 não está instalado neste ambiente; o pacote portátil foi validado, o `setup.exe` não.
- [ ] Quatro casos manuais da matriz de Bibliotecas + GitHub.
- [ ] Instalação limpa, upgrade e desinstalação em máquina Windows.

### Rodada #005 — 2026-10-28 (release 2.3.0-beta.2)
- **Commit:** árvore de release `2.3.0-beta.2` (hash registrado no GitHub após o push)
- **Status Geral:** `APROVADO NO ESCOPO AUTOMATIZADO (GATE COMPLETO COM E2E)`
- **Ambiente de Teste:** Windows 11 (x64) | Node.js v24.11.1 | Vitest v5.0.5 | Vite v8.3.5 | TypeScript v7.0.2 | Visual Studio 2026 / MSVC C++20
- **Métricas Globais:** 72 arquivos de teste | 558 testes aprovados | CTest 1/1 | tutorial E2E + 12 exemplos | 0 falhas

#### Cobertura desta rodada

| Área | Evidência |
| :--- | :--- |
| **Stack de build e testes** | `npm run build` (Vite 8.3.5 + TypeScript 7.0.2) e `npm test` (Vitest 5.0.5) |
| **Canvas, ícones e Monaco** | `svg-node-view.spec.ts`, `node-view-typography.spec.ts`, `node-action-tooltips.spec.ts`, `code-modal-monaco.spec.ts` |
| **Referências de skills** | `check:skill-references` e `skill-validation-policy.spec.ts` (NVIDIA `2b9f451`, Cloudflare `c1c8a8c`) |
| **Submódulos** | monaco-editor `dbf1945` e simdjson `9c00353` (v5.0.5), com `check-submodule-updates` |
| **Nativos C++20** | CTest `arestrix_tests` 1/1 (31,24 s) |
| **E2E** | tutorial SVG (`e2e_tutorial.py`) e 12 exemplos (`e2e_examples.py`) |

#### Verificações complementares

- `py scripts/release_check.py`: gate completo com E2E aprovado; 26 artefatos e 131 componentes no SBOM.
- `npm test`: 72 arquivos / 558 testes, 0 falhas.
- `npm run build`: concluído; `dist` espelhado em `CppCore/dist`.
- `npm run check:version`, `check:local-assets`, `check:secrets` (116 arquivos), `check:skill-references` e
  licenças de terceiros: todos aprovados.
- Gateway smoke: `200 -> 402`, métricas, `/v1/usage` e impacto da memória.
- Primeira execução do gate reprovou apenas por resíduos ignorados de runs de 26/10 em
  `examples/10-subflow-chamavel` (`.arestrix/` e `workspace/`); os resíduos foram removidos e a
  execução seguinte passou integralmente.

#### Pendências para promoção

- [ ] Instalação limpa, upgrade e desinstalação em máquina Windows.
- [ ] Matriz manual 2.1-2.10 do [Plano de testes do Beta 2.3](beta2.3-test-plan.md).

### Rodada #004 — 2026-10-27 (release 2.3.0-beta.1)
- **Commit:** árvore de release `2.3.0-beta.1` (hash registrado no GitHub após o push)
- **Status Geral:** `APROVADO NO ESCOPO AUTOMATIZADO; 4 CASOS MANUAIS PENDENTES; TECLADO/FOCO APROVADO`
- **Ambiente de Teste:** Windows 11 (x64) | Node.js v24.11.1 | Vitest v4.1.12 | Visual Studio 2026 / MSVC C++20
- **Métricas Globais:** 71 arquivos de teste | 543 testes aprovados | 46/46 verificações nativas focadas | 0 falhas

#### Cobertura desta rodada

| Área | Evidência |
| :--- | :--- |
| **Bibliotecas, assets, derivados, templates e pins** | `library-e2e.spec.ts`, `library-registry.spec.ts`, `library-tree.spec.ts`, `libraries-properties.spec.ts`, `project-library-store.spec.ts` |
| **Fonte GitHub, licença e sincronização sem blobs** | `library-github.spec.ts`, `github_source_*` no C++, smoke live do GitHub |
| **Auditoria adversarial de skill/MCP** | `library-audit-gate.spec.ts` |
| **Menu Arquivo / Backstage** | `open-screen.spec.ts`, `office-ribbon-interface.spec.ts`, `workspace-governance.spec.ts` |
| **Tema e canvas** | `app-theme.spec.ts`, `canvas-preferences.spec.ts` |

#### Homologação automatizada

- `npm run homologation:libraries:live`: `status=passed-automated`.
- 25 casos na matriz: 21 automatizados verdes (incluindo smoke live) e 4 manuais; LIB-M-02
  (teclado/foco) aprovado manualmente, restando 3 pendentes.
- Frontend: 8 arquivos / 35 testes; C++ focado: 46/46 verificações.
- Evidências: `release/homologation/2026-10-28T00-57-55-889Z-6bf95810/`.

#### Verificações complementares

- `py CppCore/dev.py --full`: frontend, build nativo C++20, CTest, Vitest, version, local assets,
  secrets, skill references, licenças de terceiros e gateway smoke aprovados.
- `npx tsc --noEmit`: 0 erros.
- `npm test`: 71 arquivos / 543 testes, 0 falhas.
- `npm run build`: concluído; `dist` espelhado em `CppCore/dist`.
- `npm run check:secrets`: 116 arquivos de exemplo examinados, nenhum segredo.
- Revisão visual do menu Arquivo em 1600×1000: Abrir, Informações, Novo, Salvar, Exportar e Opções.
- `npm run capture:properties`: capturas `file-menu-info/new/save/export/options.webp`, `libraries-sources.webp` e telas de Propriedades atualizadas.

#### Pendências para promoção

- [ ] Tema claro/escuro e larguras 1024/1800 px nas Bibliotecas (correção do canvas aplicada; revalidar).
- [ ] Instalação limpa e upgrade preservando projetos/bibliotecas.
- [ ] Uso offline após sincronização/cache.
- [ ] Anexar capturas/logs manuais e repetir o gate em árvore limpa no commit candidato.

### Rodada #003 — 2026-10-27 (Fase 7 — Bibliotecas + GitHub)
- **Commit:** `6bf95810` (árvore de trabalho; evidências em `release/homologation/2026-10-28T00-12-45-264Z-6bf95810/`)
- **Status Geral:** `APROVADO NO ESCOPO AUTOMATIZADO; 3 CASOS MANUAIS PENDENTES; TECLADO/FOCO APROVADO`
- **Ambiente de Teste:** Windows 11 (x64) | Node.js v24.11.1 | Vitest v4.1.12 | Visual Studio 2026 / MSVC C++20
- **Métricas Globais:** 71 arquivos de teste | 543 testes aprovados | 1.862/1.862 verificações C++ | 0 falhas

#### Cobertura desta rodada

| Área | Evidência |
| :--- | :--- |
| **Bibliotecas, assets, derivados, templates e pins** | `library-e2e.spec.ts`, `library-registry.spec.ts`, `library-tree.spec.ts`, `libraries-properties.spec.ts`, `project-library-store.spec.ts` |
| **Fonte GitHub, licença e sincronização sem blobs** | `library-github.spec.ts`, `github_source_*` no C++ |
| **Auditoria adversarial de skill/MCP** | `library-audit-gate.spec.ts` |
| **Mock GitHub determinístico** | `scripts/mock-github-server.mjs` + `github_source_mock_manifest_and_asset` |
| **Migração legada** | `library-registry.spec.ts` (`arestrix_github_skills_repos`) |

#### Homologação automatizada

- `npm run homologation:libraries:live`: `status=passed-automated`.
- 25 casos na matriz: 21 automatizados verdes (incluindo smoke live) e 4 manuais no momento da
  execução; LIB-M-02 foi aprovado depois, restando 3 pendentes.
- Frontend: 8 arquivos / 35 testes; C++ focado: 46/46 verificações.
- Relatório, matriz, ambiente, logs e requisições do mock gravados em
  `release/homologation/<runId>/`.
- Override de teste restrito a `ARESTRIX_TEST_MODE=1` + `127.0.0.1`/`localhost`; bases de
  produção inalteradas.

#### Verificações complementares

- `npx tsc --noEmit`: 0 erros.
- `npm test`: 71 arquivos / 543 testes, 0 falhas.
- Suíte C++ completa: 1.862/1.862 verificações.
- `npm run build`: concluído; `dist` espelhado em `CppCore/dist`.
- Smoke live do GitHub (`octocat/Hello-World`, `master`): aprovado.

#### Atualização manual — 2026-10-27

- **LIB-M-02 (teclado/foco):** aprovado manualmente; navegação e foco funcionam.
- **LIB-M-01 (tema/tamanhos):** o tema escuro não alterava o fundo do canvas e os vértices
  permaneciam brancos. Correção aplicada: o canvas acompanha o tema resolvido e o corpo do vértice
  usa cinza grafite (`#1f2937`) com título claro. Revalidação visual pendente.
- **LIB-M-03/04 (instalação e offline):** adiados para outra oportunidade.

#### Pendências para promoção

- [ ] Tema claro/escuro e larguras 1024/1800 px (correção aplicada; revalidar).
- [ ] Instalação limpa e upgrade preservando projetos/bibliotecas.
- [ ] Uso offline após sincronização/cache.
- [ ] Anexar capturas/logs manuais e repetir com árvore limpa no commit candidato.

### Rodada #002 — 2026-10-27 (release 2.2.0-beta.1)
- **Commit:** árvore de release `2.2.0-beta.1` (hash registrado no GitHub após o push)
- **Status Geral:** `APROVADO`
- **Ambiente de Teste:** Windows 11 (x64) | Node.js v24 | Vitest v4.1.12 | Visual Studio 2026 / MSVC C++20
- **Métricas Globais:** 64 arquivos de teste | 507 testes aprovados | 0 falhas

#### Cobertura desta rodada

| Área | Evidência |
| :--- | :--- |
| **Propriedades Studio/Workspace/Projeto** | `properties-data.spec.ts`, `settings-stores.spec.ts`, `settings-resolver.spec.ts` |
| **Preferências e tema global** | `app-theme.spec.ts`, `settings-stores.spec.ts` |
| **Projeto e migração de telas** | `project-creation-hash.spec.ts`, `office-ribbon-interface.spec.ts` |
| **Workspace e provisionamento** | `workspace-governance.spec.ts` |
| **Modais de vértice** | `skill-editor-modal.spec.ts`, `workspace-governance.spec.ts` |

#### Verificações complementares
- `npx tsc --noEmit`: 0 erros.
- `npm run build`: concluído.
- Build nativo C++20 (`arestrix`) verificado sem trabalho pendente.
- Capturas de tela geradas por `npm run capture:screenshots` para o hub de Propriedades, escopos Studio/Workspace/Projeto, defaults, tema escuro e modais de vértice.
- Revisão visual em 1600×1000 e 1024×768.

### Rodada #001 — 2026-10-18 22:30 UTC-3
- **Commit:** `cf0e9b8` (Branch: `main`)
- **Status Geral:** `APROVADO (100% de Sucesso)`
- **Ambiente de Teste:** Windows 11 Enterprise (x64) | Node.js v20+ | Vitest v4.1.12 | MSVC v143 (C++20)
- **Métricas Globais:** 42 arquivos de teste executados | 300 testes unitários e de integração aprovados | 0 falhas | 0 alertas de tipo

#### Desempenho por Categoria

| Categoria de Integridade | Suítes / Arquivos Cobertos | Testes | Status |
| :--- | :--- | :---: | :---: |
| **1. Segurança Corporativa & Zero-Secret** | `check-secrets.spec.ts`, `env-sanitize.spec.ts`, `workspace-governance.spec.ts` | 24 | **PASS** |
| **2. Motor Concorrente C++20 & Runner** | `runner.spec.ts`, `pipeline-runner.spec.ts`, `batch-resume-engine.spec.ts`, `agent-telemetry.spec.ts` | 32 | **PASS** |
| **3. Topologia de Grafos & Validação DAG** | `graph-validation.spec.ts`, `bezier-edges-and-layout.spec.ts`, `switch-vertex.spec.ts`, `subflow-hierarchy.spec.ts`, `interaction-connect.spec.ts`, `history-undo-redo.spec.ts` | 46 | **PASS** |
| **4. Agentes de IA, Memória & Skills** | `agent-memory-config.spec.ts`, `agent-settings-inheritance.spec.ts`, `agent-templates.spec.ts`, `skill-editor-modal.spec.ts`, `github-skills-separation.spec.ts`, `skills-and-concurrency.spec.ts` | 48 | **PASS** |
| **5. Protocolo MCP & Conectores** | `mcp-protocol.spec.ts`, `mcp-vertex.spec.ts`, `mcp-tool-picker.spec.ts` | 26 | **PASS** |
| **6. Interface Office 365 & Gerenciador Janelas** | `office-ribbon-interface.spec.ts`, `office-workspace-interface.spec.ts`, `modal-window-manager.spec.ts`, `open-screen.spec.ts`, `assistant-panel.spec.ts`, `global-defaults-modal.spec.ts`, `code-modal-monaco.spec.ts` | 58 | **PASS** |
| **7. Tipografia Nítida & Internacionalização** | `i18n.spec.ts`, `node-view-typography.spec.ts`, `svg-node-view.spec.ts`, `node-action-tooltips.spec.ts`, `graphviz-export.spec.ts` | 34 | **PASS** |
| **8. Gestão de Projetos & Sincronização Git** | `project-management.spec.ts`, `project-creation-hash.spec.ts`, `save-as-arestrix.spec.ts`, `git-sync.spec.ts`, `generic-pi-import.spec.ts`, `pi-modal-tabs.spec.ts`, `pi-project-importer.spec.ts`, `examples-valid.spec.ts` | 32 | **PASS** |
| **Total Consolidado** | **42 arquivos de especificação** | **300** | **100% OK** |

#### Verificações Complementares de Segurança & Build
- **Varredura Pré-Commit de Segredos (`check:secrets`)**: 62 arquivos examinados no workspace. Nenhuma credencial em texto plano, token privado, certificado ou dado sensível detectado.
- **Checagem Estática de Tipagem (`tsc --noEmit`)**: 0 erros de compilação em TypeScript com strict mode ativado.
- **Higienização de Interface**: Ícone do Git no Titlebar em estado neutro cinza ardósia sem marca de check; remoção completa de ícones e emojis de botões e abas fora da Ribbon; ausência absoluta de sombras e filtros em textos (Zero Shadows).
- **Compilação e Espelhamento de Frontend**: Sucesso em `vite build` gerando bundle standalone e espelhando para `CppCore/dist`.
