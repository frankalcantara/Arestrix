# Registro de Auditoria e Integridade de Testes (Test Audit Log)

> **Aplica-se a:** Arestrix Beta 1.5 (`1.5.0-beta.1`) · Última revisão: 2026-10-19

Este documento registra o histórico cronológico das rodadas de testes automatizados, verificações de segurança e auditorias de integridade do **Arestrix**, organizadas da **mais recente para a mais antiga**.

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
