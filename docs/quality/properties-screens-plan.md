# Plano de implementação — Propriedades do Studio, Workspace e Projeto

> **Status:** Fases 1 e 2 implementadas; Fase 3 concluída, Fase 4 praticamente concluída, Fase 5 em
> andamento e migração de entradas antigas iniciada. Em Studio, idioma, tema global (claro/escuro/
> sistema), canvas, modelo padrão, credenciais, AI Gateway e restauração de preferências já são
> editáveis. No Workspace, sandbox padrão, políticas de auditoria,
> reconstrução do `.workspace` e restauração de padrões já funcionam, e o `workspace.config.json` é
> lido/gravado em `.workspace/` sem perder propriedades no provisionamento. No Projeto, nome, versão,
> descrição, pasta de saída, defaults de modelo/sandbox/ferramentas/memória e ambiente já são
> editáveis, incluindo o caminho do banco SQLite. As Opções do Backstage, a faixa de opções, o alias do
> Assistente e a aba de configurações do Gerenciador de Projetos já apontam para as telas de
> Propriedades com deep link de categoria. Os modais de vértice de Sandbox, Modelo, Ferramentas e
> Memória já usam o shell compartilhado `BaseModal`. O tema global está implementado e segue em
> validação visual. Integrações avançadas, Git, Runner e os demais editores especializados ainda
> dependem das próximas fases.
>
> **Objetivo:** criar três telas de propriedades no padrão Office 365 (Studio, Workspace e Projeto),
> usando um único shell visual e uma única camada de componentes, sem duplicar editores já existentes.
>
> **Última revisão:** 2026-10-26.
>
> **Bibliotecas e GitHub:** o desenho específico de fontes, bibliotecas e assets está em
> [`libraries-plan.md`](libraries-plan.md), que substitui as referências ao `GitHubReposModal`.

## 1. Resumo executivo

O Arestrix já separa conceitualmente **Studio**, **Workspace** e **Projeto** na documentação e em
parte do código, mas a interface atual mistura esses escopos:

- `GlobalDefaultsModal` concentra credenciais do Studio, modelo do Studio, sandbox do Workspace,
  ferramentas, MCP e memória do Projeto no mesmo formulário.
- `BackstageView` (Options) repete idioma, pastas do workspace, governança, catálogos e ações de
  execução.
- `ProjectManagerModal` cobre apenas uma parte das propriedades do projeto.
- `workspace.config.json` é gravado pelo `WorkspaceManager`, mas não existe uma tela de leitura e
  edição completa desse arquivo.
- O sandbox é rotulado como Workspace, mas hoje é persistido em `globalDefaults` do projeto.
- O Workspace, na prática, é a pasta física do projeto ativo; não há um contêiner de múltiplos
  projetos.

A proposta é criar **um shell reutilizável de propriedades** e três configurações de escopo em cima
dele. As telas não serão três modais independentes: serão três instâncias do mesmo componente, com
seções registradas por escopo. Isso garante a mesma aparência, os mesmos atalhos, o mesmo rodapé e o
mesmo comportamento de salvamento.

## 2. Princípios de desenho

1. **Um shell, três escopos.** Nada de copiar HTML/CSS entre três modais.
2. **Uma propriedade, um dono.** Cada campo tem um único local de persistência e um único escopo
   responsável. As telas mostram o valor efetivo, mas só o dono pode editá-lo.
3. **Propriedades não são operações.** Rodar auditor, linter ou pipeline continua sendo ação do
   Backstage/ribbon, não um campo de propriedades.
4. **Editores complexos continuam especializados.** MCP, repositórios GitHub, modelos locais,
   credenciais Git, memória e Runner continuam em seus modais próprios; a tela de propriedades mostra
   resumo, status e o botão “Gerenciar…”.
5. **Segredos nunca aparecem.** A tela mostra “configurado / não configurado”, origem e botão para
   substituir/limpar. Valores reais continuam no `EnvironmentManager`/cofre do sistema.
6. **Compatibilidade primeiro.** Projetos existentes continuam abrindo com o mesmo comportamento;
   a migração é aditiva e reversível.

## 3. Experiência alvo

### 3.1 Estrutura visual

```
┌ Propriedades do Projeto ─────────────────────────────────────────────┐
│ [ícone] Projeto: AgentComplexOrchestrator                 [Projeto]  │
│         Studio  >  Workspace  >  [Projeto]  >  Vértice              │
├──────────────────┬───────────────────────────────────────────────────┤
│ Geral            │  Identidade do projeto                            │
│ Pipelines        │  ┌─────────────────────────────────────────────┐  │
│ Modelo e IA      │  │ Nome, ID, versão, descrição, caminho…       │  │
│ Ferramentas      │  └─────────────────────────────────────────────┘  │
│ Memória          │                                                   │
│ Ambiente         │  Modelo e IA                                      │
│ Git              │  ┌─────────────────────────────────────────────┐  │
│ Runner           │  │ Provedor, modelo, skill, prompt, override…  │  │
│ Avançado         │  └─────────────────────────────────────────────┘  │
├──────────────────┴───────────────────────────────────────────────────┤
│ [Restaurar padrões]                         [Cancelar]  [Salvar]      │
└──────────────────────────────────────────────────────────────────────┘
```

- **Title bar:** título do escopo, nome da entidade, badge de escopo e breadcrumb de herança
  (`Studio > Workspace > Projeto > Vértice`).
- **Navegação lateral:** categorias com ícones SVG do catálogo já existente (`RibbonIcons.ts`).
  Em larguras menores que 900 px, vira um seletor no topo.
- **Área de conteúdo:** cartões de seção com título, descrição, badge de herança e campos.
- **Rodapé:** “Restaurar padrões” à esquerda; “Cancelar” e “Salvar” à direita. “Aplicar” pode entrar
  na fase 2 se houver necessidade de salvar sem fechar.
- **Busca de propriedades:** campo no topo da navegação para filtrar categorias/campos. Fica para a
  fase 2, mas o layout já deve reservar espaço.
- **Estado sujo:** fechar com alterações pendentes pede confirmação; `Esc` cancela a edição atual.
- **Tema:** todos os componentes usam os tokens de `office-modals.css`; claro e escuro precisam ser
  validados juntos.

### 3.2 Pontos de entrada

- **Backstage > Propriedades** com três cartões: Studio, Workspace, Projeto.
- **QAT/indicador do projeto:** menu de contexto com “Propriedades do Projeto”.
- **Ribbon contextual do vértice:** “Propriedades do Vértice” continua abrindo os modais de nó;
  numa fase posterior eles passam a usar o mesmo shell.
- **Compatibilidade:** os botões atuais (`ribbon-btn-pipeline-defaults`, opções do Backstage e
  configurações do Project Manager) redirecionam para a tela/aba correspondente.

## 4. Matriz de propriedade e escopo

Esta matriz é o contrato principal. Antes de codificar, ela precisa ser revisada e congelada.

| Área | Studio | Workspace | Projeto | Vértice |
| :--- | :--- | :--- | :--- | :--- |
| Idioma, aparência, tema, densidade | Dono | — | — | — |
| Chaves de API, AI Gateway, cofre | Dono | Lê | Lê | Lê |
| Modelo padrão do Studio / runtime local | Dono | Herda | Herda | Herda |
| Repositórios GitHub de skills autorizados | Dono | Usa no catálogo | Usa | Usa |
| Pastas do Workspace, sync físico | — | Dono | Referencia | — |
| Catálogo aprovado de Skills/MCPs | — | Dono | Seleciona subconjunto | Seleciona/usa |
| Política de auditoria, SkillSpector, segurança | Política global | Dono da configuração | Herda/override | Herda/override |
| Sandbox padrão (isolamento, rede, memória, timeout, mounts) | — | Dono do padrão | Override | Override |
| Allowlist HTTP | — | Dono do padrão | Override | — |
| Memória do Workspace | — | Dono do banco | Lê/promove | — |
| Metadados do Projeto | — | — | Dono | — |
| Pipelines do Projeto | — | — | Dono | — |
| Modelo, prompt e skill do Projeto | Fallback | Fallback | Dono/override | Override |
| Ferramentas e scripts do Projeto | Fallback | Fallback | Dono/override | Override |
| Memória do Projeto | — | — | Dono | Override |
| Variáveis de ambiente do Projeto | — | — | Dono | — |
| Banco de dados do Projeto | — | — | Dono | — |
| Git do Projeto (remoto, automação) | Identidade/conta | — | Dono | — |
| Runner e distribuição | — | — | Dono | — |
| Runner/assinatura/cofre de segredos | — | — | Dono | — |
| Modelo, ferramentas, sandbox, memória e skills do vértice | — | — | Herda | Dono/override |

### 4.1 Decisão sobre o Workspace

Hoje o `WorkspaceManager` usa `project.rootPath` como raiz do workspace. Para o MVP, vamos manter
essa semântica:

- **Workspace = pasta física do projeto ativo**; seus arquivos são `.workspace/*` e
  `workspace.config.json`.
- **Projeto = `arestrix.project.json` + `pipelines/*.json` + configurações do projeto.**

Isso permite separar as telas e os arquivos sem introduzir um modelo de “workspace com vários
projetos”. Um workspace multi-projeto é uma evolução separada e fora do escopo deste plano.

Consequência: a tela de Workspace mostra a pasta ativa e deixa claro que ela é compartilhada pelo
projeto atual. Se o multi-projeto for necessário, a navegação e o `WorkspaceManager` precisarão de
uma nova fase.

### 4.2 Herança

A resolução de valores deve ser explícita:

```
Studio → Workspace → Projeto → Vértice
```

- Cada valor resolvido carrega `origem`, `valor` e, quando aplicável, `sobrescritoEm`.
- A UI mostra “Herdado do Studio/Workspace/Projeto” e um botão “Restaurar herança”.
- Campos não herdáveis (IDs, caminhos, segredos, repositório Git, assinatura do Runner) são marcados
  como tal.
- O botão “Restaurar padrões” restaura apenas o escopo atual; nunca apaga valores de outro escopo.

## 5. Arquitetura proposta

### 5.1 Componentes de UI

```
src/ui/properties/
  PropertiesWindow.ts          // shell único; herda BaseModal
  PropertyTypes.ts             // PropertyScope, IPropertyDefinition, IPropertySource…
  PropertyRegistry.ts          // seções por escopo
  components/
    PropertyNav.ts
    PropertySection.ts
    PropertyField.ts
    InheritanceControl.ts
    SecretField.ts
    PropertyFooter.ts
  scopes/
    StudioProperties.ts
    WorkspaceProperties.ts
    ProjectProperties.ts
  sections/
    studio/…
    workspace/…
    project/…
```

- `PropertiesWindow` deve **estender `BaseModal`** para reaproveitar title bar, controles de janela,
  ARIA, `ModalWindowManager` e tokens.
- `PropertiesWindow` adiciona o layout de duas colunas via classe `properties-dialog`; o corpo precisa
  de uma opção para remover o padding padrão do `modal-body-scrollable` e permitir rolagem apenas no
  painel direito.
- `PropertyRegistry` define quais seções aparecem em cada escopo. As seções podem ser compartilhadas
  (ex.: cartão de modelo, cartão de sandbox) com regras de edição diferentes por escopo.

### 5.2 Domínio e persistência

```
src/core/settings/
  types.ts
  SettingsResolver.ts
  PropertyRepository.ts
  StudioSettingsStore.ts
  WorkspaceSettingsStore.ts
  ProjectSettingsStore.ts
```

Responsabilidades:

- **`StudioSettingsStore`:** lê/grava configurações não secretas em
  `%APPDATA%\Arestrix\studio.config.json` (ou equivalente por plataforma). Segredos continuam via
  `EnvironmentManager`.
- **`WorkspaceSettingsStore`:** lê/grava `workspace.config.json`; reaproveita
  `WorkspaceManager` para diretórios, catálogos e provisionamento.
- **`ProjectSettingsStore`:** lê/grava `arestrix.project.json` via `ProjectManager` e
  `save_project`; atualiza metadados, `globalDefaults`, ambiente, Git, banco e Runner.
- **`SettingsResolver`:** resolve o valor efetivo e a origem para uma chave, sem conhecer HTML.
- **`PropertyRepository`:** interface comum de load/save/patch por escopo, com fallback local para
  testes e modo navegador.

Contratos iniciais (rascunho):

```ts
type PropertyScope = 'studio' | 'workspace' | 'project' | 'vertex';

interface IPropertyOrigin {
  scope: PropertyScope;
  source: string;          // arquivo ou variável de ambiente
  inheritedFrom?: PropertyScope;
  readOnly?: boolean;
}

interface IResolvedValue<T> {
  value: T;
  origin: IPropertyOrigin;
  overriddenAt?: PropertyScope;
}

interface IPropertySection {
  id: string;
  title: string;
  description?: string;
  icon: string;
  scopes: PropertyScope[];
  order: number;
  load(context: IPropertyContext): Promise<unknown>;
  save?(context: IPropertyContext, draft: unknown): Promise<void>;
  validate?(draft: unknown): IPropertyIssue[];
  render(context: IPropertyContext, state: unknown): string | HTMLElement;
}
```

### 5.3 Backend necessário

O frontend já tem `save_project` e `write_workspace_asset`, mas faltam operações explícitas para
configurações de escopo:

- `get_studio_settings` / `save_studio_settings` (arquivo de configuração do usuário, sem segredos).
- `get_workspace_settings` / `save_workspace_settings` (ou `read_workspace_asset` com o mesmo guard
  de caminho já usado em `write_workspace_asset`).
- Manter `get_current_project` / `save_project` para o Projeto.
- Reutilizar `get_env_var`, `set_env_var`, `clear_env_var` para credenciais.

As chamadas novas precisam de testes C++ e de fallback no frontend para o modo navegador/testes.

## 6. Conteúdo das três telas

### 6.1 Propriedades do Studio

Categorias sugeridas:

1. **Geral:** versão do Studio, instalação, pasta de configuração, projetos recentes, canal de
   atualização.
2. **Aparência e idioma:** idioma, tema (claro/escuro/sistema), densidade e preferências de canvas.
3. **Credenciais e Gateway:** status das chaves por provedor, AI Gateway (URL, chave virtual,
   fail-closed), teste de conexão e orçamento.
4. **Modelo padrão do Studio:** provedor, modelo, endpoint local, descoberta de modelos.
5. **Integrações:** conta GitHub, repositórios de skills autorizados, runtime local de modelos.
6. **Governança:** versão da política SkillSpector, retenção de auditoria, raízes de confiança.
7. **Avançado:** logs, proxy, diretório de dados, limpeza de cache e restauração de padrões.

### 6.2 Propriedades do Workspace

1. **Geral:** raiz, nome/ID do workspace, arquivo `workspace.config.json`, último sync.
2. **Pastas e armazenamento:** skills, MCPs, agentes, grafos, memória, banco e exports.
3. **Catálogo de Skills:** skills aprovadas, proveniência, versão, ferramentas, status de auditoria.
4. **Catálogo MCP:** servidores aprovados, transporte, ferramentas, status e teste.
5. **Segurança e auditoria:** auto-verificação, modo estrito, histórico e política.
6. **Sandbox padrão:** isolamento, rede, memória, timeout e mounts.
7. **Memória do Workspace:** banco, retenção, embeddings e consolidação.
8. **Avançado:** reconstruir `.workspace/`, exportar/importar configuração, reset do workspace.

### 6.3 Propriedades do Projeto

1. **Geral:** ID, nome, versão, descrição, raiz, saída, timestamps, caminho do manifesto.
2. **Pipelines:** lista, pipeline principal, arquivos, descrição; link para o gerenciador de
   pipelines.
3. **Modelo e IA:** override de provedor/modelo/skill/prompt; herdar do Studio.
4. **Ferramentas e Skills:** ferramentas nativas, scripts, MCPs e skills do projeto.
5. **Memória:** modos do projeto e do vértice, conhecimento, retenção, embeddings e escopo.
6. **Ambiente (.env):** variáveis, referências `${VAR}`, importar/exportar, validação de segredos.
7. **Banco de dados:** SQLite/Postgres, caminho/URL e teste de conexão.
8. **Git:** provedor, servidor, usuário, repositório, branch, auto-commit, push temporizado e último
   sync. A identidade do SO e a conta ficam em Studio; o projeto só guarda o remoto e a automação.
9. **Runner e distribuição:** enabled, título, versão, saída, parâmetros, assinatura, cofre e
   empacotamento.
10. **Avançado:** exportar manifesto, validar projeto, restaurar overrides e abrir pasta.

## 7. Migração e convivência

### 7.1 Estratégia strangler

Não reescrever `GlobalDefaultsModal`, `BackstageView` e `ProjectManagerModal` de uma vez.

1. Criar o shell e as telas novas lendo os stores atuais.
2. Redirecionar os botões existentes para as novas telas.
3. Manter os modais antigos como aliases temporários:
   - `GlobalDefaultsModal` → abre `ProjectProperties` na categoria correspondente.
   - Opções do Backstage → botões “Abrir propriedades do Studio/Workspace/Projeto”.
   - Project Manager → aba “Configurações” vira link para Propriedades do Projeto.
4. Remover o código duplicado só depois de duas releases de validação.

### 7.2 Dados existentes

- **Projetos atuais:** `globalDefaults` continua sendo lido como override do Projeto. Nada é apagado
  no primeiro momento.
- **Workspace:** `workspace.config.json` passa a ser lido; se não existir, é criado a partir dos
  defaults atuais.
- **Studio:** novo arquivo de configuração; se não existir, usa os defaults e as variáveis de
  ambiente atuais.
- **Sandbox:** o valor atual do projeto continua como override; o padrão do Workspace é semeado com
  `DEFAULT_PIPELINE_DEFAULTS.sandbox` e pode ser editado depois.
- **Segredos:** nunca migrar valores para JSON; apenas manter as referências de ambiente.

### 7.3 Ordem de migração

1. Projeto (menor risco; usa `save_project` existente).
2. Workspace (config já é gravado; falta leitura).
3. Studio (arquivo/backends novos; maior risco).
4. Remoção dos trechos duplicados do Backstage/GlobalDefaults.
5. Convergência dos modais de vértice.

## 8. Fases de implementação

### Fase 0 — Contrato de escopo e decisões (2–3 dias)

- Congelar a matriz de propriedades e herança.
- Decidir a semântica do Workspace (recomendado: pasta física do projeto ativo).
- Definir caminho do `studio.config.json` e nomes dos endpoints.
- Criar um ADR curto em `docs/quality/` ou `docs/en|pt-BR/`.
- **Saída:** matriz aprovada, sem código de UI.

### Fase 1 — Shell compartilhado (4–6 dias)

- `PropertiesWindow` estendendo `BaseModal`.
- Layout de navegação lateral + conteúdo + rodapé.
- Componentes de seção, campo, herança, segredo, estado sujo e validação.
- Tokens claro/escuro e responsividade.
- Três telas vazias (placeholders) abrindo pelo Backstage.
- **Critério:** três telas com a mesma cara, sem persistência ainda.

### Fase 2 — Stores, resolver e backend (5–7 dias)

- `StudioSettingsStore`, `WorkspaceSettingsStore`, `ProjectSettingsStore`.
- `SettingsResolver` com origem/herança.
- Endpoints C++ e fallback local.
- Versionamento de schema e migração inicial.
- **Critério:** load/save de cada escopo comprovado por testes, sem perda de dados.

### Fase 3 — Propriedades do Studio (3–4 dias)

- Idioma, aparência, modelo padrão, credenciais/gateway, integrações e governança.
- Reutilizar `EnvironmentManager`, `LocalModelsModal` e a seção **Bibliotecas** do shell
  (`libraries-plan.md`), sem abrir o modal legado de repositórios.
- **Critério:** alterações persistem, segredos não aparecem e o Studio continua iniciando.

### Fase 4 — Propriedades do Workspace (4–5 dias)

- Pastas, catálogos de skills/MCP, governança, sandbox padrão e memória do workspace.
- Ações “Gerenciar…” para os modais especializados.
- **Critério:** `workspace.config.json` é lido/gravado; diretórios e catálogos refletem o estado real.

### Fase 5 — Propriedades do Projeto (4–6 dias)

- Metadados, pipelines, modelo/tools/memory, ambiente, banco, Git e Runner.
- Integração com `ProjectManager`, `Graph`, `GitSyncManager` e `RunnerManager`.
- **Critério:** projeto salva, recarrega e mantém overrides; nenhum campo duplicado.

### Fase 6 — Migração e limpeza (3–4 dias)

- Redirecionar entradas antigas.
- Remover duplicações do Backstage e GlobalDefaults.
- Atualizar i18n, docs, testes e changelog.
- **Critério:** não existe mais uma segunda tela editando a mesma propriedade.

### Fase 7 — Convergência dos modais de vértice (4–6 dias, recomendado)

- Migrar `NodeModelModal`, `NodeToolsModal`, `NodeSandboxModal`, `NodeMemoryModal` e
  `NodeSkillsModal` para o mesmo shell de propriedades.
- Manter os toggles de herança por seção e o vínculo com o vértice.
- **Critério:** “todas as propriedades” têm a mesma linguagem visual, inclusive as do vértice.

### Fase 8 — Homologação (4–5 dias)

- Testes unitários, integração, E2E e visuais.
- Matriz clara/escuro e 1024/1800 px.
- Migração de projeto existente, modo navegador e máquina limpa.
- **Critério:** gate de release aprovado.

**Ordem de grandeza:** 30–40 dias de desenvolvimento, sem contar retrabalho e validação
multi-máquina. As fases 1–2 podem andar em paralelo com a fase 0 depois da matriz congelada.

## 9. Plano de testes

### Unitários

- `settings-resolver.spec.ts`: precedência Studio → Workspace → Projeto → Vértice, origem e reset.
- `studio-settings-store.spec.ts`, `workspace-settings-store.spec.ts`,
  `project-settings-store.spec.ts`: load/save, schema, campos ausentes, corrupção e fallback.
- `properties-window.spec.ts`: navegação, dirty state, validação, cancelar, salvar e teclado.
- `properties-registry.spec.ts`: nenhuma propriedade registrada em dois donos.
- `properties-secrets.spec.ts`: nenhum valor secreto em HTML, log ou JSON.
- Paridade i18n pt-BR/en-US.

### Integração

- Abrir as três telas pelo Backstage e pelos atalhos antigos.
- Alterar idioma, modelo do Studio, sandbox do Workspace e metadados do Projeto; recarregar.
- Projeto existente com `globalDefaults`: abrir, editar e salvar sem perder campos.
- Alternar projeto e conferir que as propriedades do escopo ativo mudaram.
- Workspace sem `workspace.config.json`: criar/seed e continuar.
- Git/Runner/banco: salvar configuração e validar no `ProjectManager`.
- Segredos: substituir/limpar chave e confirmar que o valor não aparece.

### Visuais

- Claro e escuro via `html[data-theme="dark"]`.
- 1024 px e 1800 px.
- Navegação lateral colapsada, rodapé fixo, conteúdo rolável e foco visível.
- Nenhum overflow horizontal em campos longos e caminhos.

### Manual / release

- Upgrade de instalação preservando projetos e configurações.
- Projeto criado antes desta mudança, aberto na versão nova.
- Backup/restauração de `studio.config.json` e `workspace.config.json`.
- Reset de cada escopo sem afetar os demais.

## 10. Riscos e mitigação

| Risco | Impacto | Mitigação |
| :--- | :--- | :--- |
| Workspace e Projeto compartilham a mesma raiz | Confusão de escopo | MVP com separação lógica por arquivos; multi-projeto fora do escopo |
| `GlobalDefaultsModal` é um monólito de ~95 KB | Regressão alta | Strangler, aliases e testes de migração |
| Backstage Options duplica configurações | Divergência | Uma propriedade, um dono; remover duplicatas na fase 6 |
| Falta endpoint de leitura do Workspace/Studio | Bloqueio | Implementar na fase 2, com fallback local |
| Segredos em JSON/Git | Segurança | Nunca persistir valores; só status/referências; testes de vazamento |
| Salvamento concorrente com autosave/Git | Corrida | Patch por seção, fila de salvamento e eventos `properties:saved` |
| Tema escuro global ainda não existe | Aparência inconsistente | Tokens prontos; tema é propriedade do Studio, mas switch global entra depois |
| i18n com chaves duplicadas/raw | UI quebrada | Namespace `properties.*` e teste de paridade |
| Escopo de convergência dos nós crescer | Atraso | Fase 7 separada e opcional após as três telas |

## 11. Decisões pendentes antes de codificar

1. **Workspace:** pasta física do projeto ativo (recomendado) ou contêiner multi-projeto?
2. **Tema:** a tela do Studio já ganha o seletor claro/escuro/sistema ou apenas guarda a preferência
   até o tema global existir?
3. **Salvar:** somente `Salvar/Cancelar` no MVP ou `Aplicar` sem fechar?
4. **Allowlist HTTP:** padrão do Workspace com override do Projeto (recomendado) ou só Projeto?
5. **Catálogos complexos:** resumo + “Gerenciar…” (recomendado) ou edição inline de MCP/GitHub?
6. **Modais de vértice:** entram na mesma entrega ou na fase 7?
7. **Caminho do `studio.config.json`:** `%APPDATA%\Arestrix` (recomendado) ou pasta de instalação?
8. **Histórico de alterações:** auditoria de mudanças de propriedades no MVP ou depois?

## 12. Fora de escopo

- Workspace com múltiplos projetos e troca de workspace.
- Implementação do tema escuro global do aplicativo.
- Reescrita dos editores de MCP, GitHub, Git, memória, modelos locais e Runner.
- Migração automática de valores secretos para arquivos.
- Alteração do formato de `arestrix.project.json` que quebre compatibilidade.

## 13. Entregáveis

1. Matriz de propriedades aprovada e ADR de escopo.
2. `PropertiesWindow` + componentes compartilhados.
3. Três telas: Studio, Workspace e Projeto.
4. Stores e `SettingsResolver` com testes.
5. Endpoints de leitura/gravação de configuração.
6. Migração aditiva e aliases das entradas antigas.
7. Documentação de arquitetura, interface e changelog.
8. Matriz de testes e evidências de homologação.
