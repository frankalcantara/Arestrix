# Plano de implementação — Bibliotecas de Assets (GitHub como fonte, não como tela)

> **Status:** análise concluída; plano aprovado em parte (decisões 1, 2, 3, 6, 8 e 11 registradas
> na seção 14). **Fases 1, 2, 3, 4, 5 e 6 implementadas em 2026-10-27.**
>
> **Fase 1 entregue:** catálogo embutido renomeado para “Biblioteca Arestrix”, URLs fictícias
> removidas, importação da biblioteca passando pelo Skill Guard, seção **Bibliotecas** em
> Studio/Workspace com criação de biblioteca e cadastro de fonte, e o evento
> `modal:open-github-repos` redirecionado para as Propriedades.
>
> **Fase 2 entregue:** modelo de assets, `LibraryStore`, `LibraryService`, `LibraryAuditGate`,
> adaptadores de skill/MCP, importação do catálogo embutido e do Workspace, derivados imutáveis
> com `derivedFrom`, instalação física em `.workspace/skills|mcps` e ação **Importar catálogo**
> nas Propriedades.
>
> **Fase 3 entregue:** árvore de bibliotecas/assets no shell, painel de detalhe com auditoria,
> ações de reauditar, duplicar/editar, instalar e remover; importar/exportar/clonar biblioteca;
> e seção **Bibliotecas do Projeto** para habilitar bibliotecas e fixar assets.
>
> **Fase 4 entregue (skills/MCPs):** `LibraryPickerModal`, aba de skills do agente lendo a
> biblioteca com reauditoria na inclusão, “Adicionar da biblioteca” no modal de ferramentas MCP
> (instala no Workspace antes de usar) e pins do Projeto filtrando/priorizando o seletor.
>
> **Fase 4.1 entregue:** templates de vértice como assets de biblioteca, auditoria de esquema,
> ação **Salvar na biblioteca** no catálogo de vértices, **Bibliotecas** no `TemplateModal` e
> detecção de atualização de derivados no vértice com confirmação e reauditoria.
>
> **Fase 5 entregue:** fonte GitHub real via endpoint C++ `fetch_library_source`, commit pin,
> leitura de manifesto/árvore sem baixar blobs, sincronização de candidatos como `PENDING`,
> download sob demanda em quarentena lógica, auditoria e promoção somente após o veredito.
> O frontend cadastra a fonte em Propriedades → Bibliotecas, sincroniza e baixa apenas o asset
> selecionado.
>
> **Fase 6 entregue:** `GitHubReposModal` e `GitHubSkillsRegistry` removidos; catálogo embutido
> passou a `BuiltinLibraryCatalog`; entradas de ribbon/Backstage/Defaults apontam diretamente para
> Propriedades → Bibliotecas; fontes legadas em `arestrix_github_skills_repos` são migradas para
> `ILibrarySource`; testes legados substituídos pelos testes de biblioteca.
>
> **Automação:** `npm run test:libraries` executa o E2E de frontend + testes C++ da fonte GitHub.
> `npm run test:libraries:live` inclui um manifesto real do GitHub.
>
> **Fase 7 (parcial):** `npm run homologation:libraries` executa o gate determinístico com mock
> GitHub local, gera relatório/matriz/logs e deixa 3 casos manuais pendentes (teclado/foco já
> aprovado). Ver
> [`libraries-homologation-plan.md`](./libraries-homologation-plan.md).
>
> **Objetivo:** substituir o uso atual do GitHub como “catálogo improvisado” por um conceito
> explícito de **Biblioteca**: um contêiner versionado, auditado e reutilizável de assets
> (skills, MCPs, templates de agente e de grafo), gerenciado exclusivamente nas telas de
> Propriedades e consumido por um seletor único na criação e edição de agentes e grafos.
>
> **Última revisão:** 2026-10-27.
>
> **Substitui, para o tema GitHub/bibliotecas:** a decisão 5 e as referências ao
> `GitHubReposModal` em [`properties-screens-plan.md`](properties-screens-plan.md).
> O restante daquele plano (shell, escopos, stores e herança) continua válido.

---

## 1. Resumo executivo

O Arestrix não usa o GitHub como fonte de dados. Ele usa o **nome** do GitHub como justificativa
para um catálogo de skills escrito à mão no código, exposto por um modal global separado das
Propriedades. O usuário final precisa:

1. descobrir que existe um modal “Repositórios do GitHub”;
2. cadastrar um repositório por caminho `org/repo`;
3. acreditar que aquele repositório fornece skills;
4. importar uma skill sem que ela passe pelo auditor;
5. editar a skill dentro do vértice, sem vínculo com a origem.

Nada disso funciona como uma biblioteca. Pior: **3 dos 4 repositórios “padrão” retornam 404**
(verificado em 2026-10-27), os assets são embutidos no código e a importação pela aba GitHub
**não executa a auditoria** que a importação pelo Workspace executa.

A proposta é separar três coisas que hoje estão misturadas:

| Conceito | Responsabilidade |
| :--- | :--- |
| **Fonte** (`LibrarySource`) | De onde vêm os assets: biblioteca embutida do Arestrix, repositório GitHub, pasta local. |
| **Biblioteca** (`Library`) | Coleção instalada e versionada de assets, com proveniência, licença e política de confiança. |
| **Asset** (`LibraryAsset`) | Item individual reutilizável: `skill`, `mcp`, `agent-template` ou `graph-template`. |

O GitHub passa a ser **um adaptador de fonte**, gerenciado em **Studio → Bibliotecas**. O usuário
não vê “repositórios” no fluxo de criação do agente; vê **bibliotecas** e escolhe um asset. Ao
editar, ele cria um **derivado** rastreável em **Minha biblioteca do Workspace**. A inclusão no
agente/grafo continua sendo um snapshot (o runtime atual não muda), mas agora com hash, veredito e
origem preservados.

---

## 2. Diagnóstico do uso atual do GitHub

### 2.1 Inventário de código

| Arquivo | Papel atual | Problema |
| :--- | :--- | :--- |
| `src/core/skills/GitHubSkillsRegistry.ts` | “Registro” de repositórios e catálogo de skills embutidas | 100% hardcoded; `addRepository` só grava metadado e **nunca produz assets**; persistência em `localStorage`; singleton sem escopo. |
| `src/ui/modals/GitHubReposModal.ts` | Modal global de “Repositórios do GitHub” | Tela separada das Propriedades; gerencia fontes, não assets; sem árvore, sem auditoria, sem instalação. |
| `src/ui/modals/NodeSkillsModal.ts` | Aba 3 “Importar do Catálogo (GitHub Open Source)” | Importa direto para o nó **sem chamar `auditSkill`**; mistura catálogo, editor e importação; gestão de repositórios fica fora do modal. |
| `src/ui/properties/PropertiesData.ts` | Cartão “Integrações” no Studio e “Catálogos” no Workspace | Só mostra contagem e um botão “Gerenciar…” que abre o modal antigo. Não é gestão nas Propriedades. |
| `src/ui/properties/PropertiesController.ts` | Ação `studio.open-github` / `workspace.open-github` | Emite `modal:open-github-repos`; não há árvore nem edição no shell de Propriedades. |
| `src/ui/GlobalDefaultsModal.ts` | Botão “Repositórios GitHub…” | Duplica o ponto de entrada e reforça a ideia de configuração global solta. |
| `src/ui/ribbon/OfficeRibbon.ts` | Botão de ribbon + comando de busca | Terceiro ponto de entrada para o mesmo modal. |
| `src/ui/ribbon/BackstageView.ts` | Item de navegação “Repositórios do GitHub” | Quarto ponto de entrada. |
| `src/main.ts` | Instancia `GitHubReposModal` e escuta `modal:open-github-repos` | O modal vive fora do ciclo de Propriedades. |
| `src/core/workspace/WorkspaceManager.ts` + `ApprovedWorkspaceCatalog.ts` | Catálogo “pré-aprovado” paralelo | Segundo catálogo, hardcoded, sem relação com o catálogo GitHub; grava `.workspace/skills` e `.workspace/mcps`. |
| `src/ui/modals/McpServersModal.ts` | Cadastro de MCP | Não existe biblioteca de MCPs; grava direto no projeto ativo; sem licença, versão ou proveniência. |
| `src/core/skills/*` | SkillSpector, Cloudflare Audit, ledgers, hashers | Já é a base de segurança correta; só não é aplicada ao caminho GitHub. |
| `CppCore/src/main.cpp` | `save_mcp_server`, `write_workspace_asset`, `ensure_workspace_directories` | Backend já suporta instalação de assets e validação de segredos; falta o índice de biblioteca e a aquisição remota. |

### 2.2 Evidências objetivas

- **Não há chamada ao GitHub.** Busca por `api.github.com`, `raw.githubusercontent.com`, `octokit`
  ou `fetch` no catálogo não retorna nada. As skills são literais TypeScript.
- **Repositórios declarados não existem.** Em 2026-10-27:
  - `anthropics/prompt-eng-interactive-tutorial` → 200 (é um tutorial de prompt engineering,
    não um catálogo de skills);
  - `taskflow/agent-skills-community` → 404;
  - `openai/evals-and-skills` → 404;
  - `microsoft/autogen-skills` → 404.
- **Licença é declarada, não verificada.** O campo `license` é digitado no objeto; não há leitura
  de `LICENSE` nem do SPDX retornado pela API.
- **Adicionar repositório é um beco sem saída.** `GitHubSkillsRegistry.addRepository` apenas
  acrescenta o metadado; `searchSkills` continua filtrando somente `defaultSkills`, cujos
  `repoId` apontam para os quatro repositórios padrão. O usuário cadastra e não vê nada novo.
- **Importação GitHub não é auditada.** Em `NodeSkillsModal`, o handler
  `.btn-import-github-skill` converte e grava no nó; diferente de
  `.btn-import-workspace-skill`, ele não chama `workspaceManager.auditSkill` nem o
  `WorkspaceSkillAuditorAgent`.
- **Persistência fora do escopo.** `arestrix_github_skills_repos` fica em `localStorage`, não no
  `studio.config.json`, não entra em backup e não é compartilhável entre máquinas.
- **Dois catálogos concorrentes.** `GitHubSkillsRegistry` (7 skills) e
  `APPROVED_WORKSPACE_SKILLS` (catálogo do Workspace) descrevem assets parecidos com regras
  diferentes; o usuário vê “Workspace” e “GitHub” como se fossem produtos distintos.

### 2.3 Conclusão do diagnóstico

O problema não é “o GitHub é complexo”. É que **não existe modelo de biblioteca**. Sem ele:

- a origem do asset não é rastreável;
- a atualização de um asset não é gerenciável;
- a auditoria depende da porta de entrada, não do conteúdo;
- a edição destrói a proveniência;
- cada nova origem (GitHub, pasta local, marketplace) exige uma tela nova.

---

## 3. Conceito de Biblioteca

### 3.1 Definições

- **Fonte de biblioteca (`LibrarySource`)**: mecanismo de aquisição. Ex.: `builtin`, `github`,
  `workspace`, `local-folder`. Uma fonte descreve *como* ler assets, não *o que* está instalado.
- **Repositório**: uma fonte do tipo GitHub. **Não é uma biblioteca**; é matéria-prima. O mesmo
  repositório pode alimentar várias bibliotecas (por subpasta ou filtro), e uma biblioteca pode
  combinar assets de mais de uma fonte.
- **Biblioteca (`Library`)**: conjunto nomeado e versionado de assets com um escopo
  (`studio`, `workspace` ou `project`), política de confiança e estado de sincronização. Pode
  nascer **vazia**, **de um repositório**, **de um arquivo exportado** ou **de um clone**.
- **Asset (`LibraryAsset`)**: unidade reutilizável. No MVP: `skill` e `mcp`. Em seguida:
  `agent-template` e `graph-template` (reaproveitando o catálogo de vértices).
- **Instalação**: copiar um asset da fonte para o armazenamento local da biblioteca, com hash,
  licença, proveniência e veredito de auditoria.
- **Uso**: incluir uma **cópia snapshot** do asset no agente/grafo. O runtime continua lendo
  `INodeSkill` / `IMcpServerConfig`; nada muda no formato do pipeline.
- **Derivado (`derivedFrom`)**: cópia editável criada a partir de um asset de biblioteca. Guarda
  `libraryId`, `assetId`, `hash` e `policyVersion` de origem.

### 3.2 Ciclo de vida

```
Criação (vazia/fonte/importação/clone) → Descoberta → Pré-visualização → Auditoria
      → Instalação → Uso (snapshot) → Edição (derivado)
     ↑                                                       │
     └──────────── Sincronização explícita / diff / reauditoria ←────────────┘
```

Regras:

1. Asset de biblioteca é **imutável**; qualquer edição gera um derivado.
2. Nada entra na biblioteca sem auditoria e licença aprovada.
3. Atualização nunca é automática: mostra diff, commit de origem e novo veredito.
4. Excluir uma biblioteca não quebra agentes existentes (eles têm snapshot), mas marca o vínculo
   como “origem indisponível”.
5. Asset `REJECT` não pode ser instalado nem usado; `CAUTION` exige confirmação explícita e é
   bloqueado no modo estrito.

### 3.3 Escopos (uma propriedade, um dono)

| Escopo | Dono de | Consome |
| :--- | :--- | :--- |
| **Studio** | Fontes autorizadas (incluindo GitHub), política de confiança, licenças permitidas, token por variável de ambiente | — |
| **Workspace** | Bibliotecas instaladas, cache, índice de assets, ledger de auditoria e **Minha biblioteca** (derivados) | Fontes do Studio |
| **Projeto** | Bibliotecas habilitadas e assets *pinned*; nunca é dono de derivados | Bibliotecas do Workspace |
| **Vértice/Grafo** | Snapshot usado e `derivedFrom` | Workspace/Projeto |

O Project não “gerencia GitHub” nem é o destino padrão de edição. Ele apenas habilita bibliotecas
do Workspace e fixa versões. **A liberação de um repositório como biblioteca acontece no
Workspace**, que é o ponto de governança mais importante do fluxo.

---

## 4. Modelo de dados proposto

Arquivo novo: `src/core/libraries/types.ts`.

```ts
export type LibraryScope = 'studio' | 'workspace' | 'project';
export type LibrarySourceKind = 'builtin' | 'github' | 'local-folder' | 'workspace';
export type LibraryAssetKind = 'skill' | 'mcp' | 'agent-template' | 'graph-template';
export type LibraryTrust = 'official' | 'verified' | 'community' | 'local';
export type LibraryAssetStatus = 'available' | 'installed' | 'pinned' | 'disabled';
export type LibraryAuditVerdict = 'APPROVE' | 'CAUTION' | 'REJECT' | 'PENDING';

export interface ILibrarySource {
  id: string;
  kind: LibrarySourceKind;
  scope: LibraryScope;
  name: string;
  enabled: boolean;
  trust: LibraryTrust;
  /** GitHub: repositório, ref e commit resolvido (pin). */
  github?: {
    repo: string;            // "org/repo"
    ref: string;             // branch/tag solicitada
    commit?: string;         // SHA resolvido e congelado
    subPath?: string;        // pasta opcional dentro do repo
    licenseSpdx?: string;    // lida da API, não digitada
    private?: boolean;
  };
  licenseAllowlist: string[]; // ex.: ["MIT", "Apache-2.0", "BSD-3-Clause", "ISC"]
  lastSyncAt?: string;
  lastSyncError?: string;
}

export interface ILibrary {
  id: string;
  scope: LibraryScope;
  name: string;
  description?: string;
  enabled: boolean;
  trust: LibraryTrust;
  origin: {
    kind: 'empty' | 'source' | 'import' | 'clone';
    sourceIds: string[];      // vazio em biblioteca local vazia; 1+ em biblioteca curada
    clonedFrom?: string;      // libraryId de origem
    importedFrom?: string;    // arquivo .arestrix-library
  };
  assetCount: number;
  createdAt: string;
  installedAt: string;
  lastSyncAt?: string;
}

export interface ILibraryProvenance {
  sourceKind: LibrarySourceKind;
  sourceId: string;
  repo?: string;
  path?: string;
  commit?: string;
  url?: string;
  author?: string;
  license: string;
  importedAt: string;
  importedBy: 'user' | 'system';
}

export interface ILibraryAudit {
  verdict: LibraryAuditVerdict;
  riskScore: number;
  policyVersion: string;
  auditedAt?: string;
  findings: string[];
  warnings: string[];
  hash: string;
  ledgerEntryId?: string;
}

export interface ILibraryAsset {
  id: string;              // estável: hash(sourceId + path + kind), nunca o nome
  libraryId: string;
  kind: LibraryAssetKind;
  name: string;
  description: string;
  category: string;
  version: string;
  status: LibraryAssetStatus;
  provenance: ILibraryProvenance;
  audit: ILibraryAudit;
  payload: ISkillAssetPayload | IMcpAssetPayload | IAgentTemplatePayload | IGraphTemplatePayload;
}

export interface ISkillAssetPayload {
  kind: 'skill';
  markdownContent: string;   // SKILL.md completo
  systemPrompt: string;
  tools: string[];
}

export interface IMcpAssetPayload {
  kind: 'mcp';
  transport: 'stdio' | 'sse';
  command?: string;
  args?: string[];
  url?: string;
  env?: Record<string, string>;     // somente ${VAR}
  headers?: Record<string, string>; // somente ${VAR}
}
```

### 4.1 Derivação em `INodeSkill`

Estender o tipo existente (aditivo, compatível):

```ts
export interface INodeSkill {
  // ...campos atuais...
  derivedFrom?: {
    libraryId: string;
    assetId: string;
    hash: string;
    policyVersion: string;
    originName: string;
  };
  audit?: {
    verdict: LibraryAuditVerdict;
    riskScore: number;
    hash?: string;
    policyVersion?: string;
    auditedAt?: string;
  };
}
```

`INodeSkill` continua sendo o que o runtime consome. O agente não passa a depender de um arquivo
de biblioteca em tempo de execução.

---

## 5. Arquitetura

### 5.1 Frontend

```
src/core/libraries/
  types.ts
  LibraryRegistry.ts        // estado em memória + eventos library:*
  LibraryStore.ts           // load/save por escopo (studio/workspace/project)
  LibraryService.ts         // instalar, remover, habilitar, fixar, derivar, sincronizar
  LibraryAuditGate.ts       // adapta SkillSpector/Cloudflare/ledgers para assets de biblioteca
  LibrarySourceResolver.ts  // resolve qual fonte/adaptador usar
  sources/
    LibrarySourceAdapter.ts // interface comum
    BuiltinSourceAdapter.ts // catálogo embutido (migra o GitHubSkillsRegistry)
    GitHubSourceAdapter.ts  // REST read-only, commit pin, cache, licença
    WorkspaceSourceAdapter.ts
    LocalFolderSourceAdapter.ts (fase posterior)
  adapters/
    skillAssetAdapter.ts
    mcpAssetAdapter.ts
    agentTemplateAdapter.ts (fase posterior)
    graphTemplateAdapter.ts (fase posterior)

src/ui/libraries/
  LibraryTree.ts            // árvore fonte → categoria → asset
  LibraryAssetPanel.ts      // detalhe, licença, proveniência, auditoria, ações
  LibraryPicker.ts          // seletor usável no agente/grafo
  LibraryPropertiesSection.ts // integra a árvore ao shell de Propriedades
  LibraryDialogs.ts         // criar biblioteca, adicionar repositório, importar, diff
```

`LibraryRegistry` e `LibraryService` não conhecem HTML. `LibraryTree` não conhece persistência.

### 5.2 Persistência

Manter o padrão dos stores existentes (backend nativo + fallback local para testes/navegador):

```
%APPDATA%\Arestrix\libraries.json                  # Studio: fontes, política, sem segredos
<workspace>/.workspace/libraries.json              # Workspace: bibliotecas instaladas + índices
<workspace>/.workspace/libraries.lock.json         # commit/tree/hash/policyVersion por asset
<workspace>/.workspace/library-cache/<src>/<commit>/
<workspace>/.workspace/skills/<id>/SKILL.md        # destino compatível com o runtime atual
<workspace>/.workspace/mcps/<id>.json              # destino compatível com o runtime atual
<project>/arestrix.project.json                    # opcional: libraries.enabled / libraries.pinned
```

Compatibilidade:

- `.workspace/skills` e `.workspace/mcps` continuam sendo os caminhos que o runtime lê.
- O índice de biblioteca é **aditivo**: quem não o conhece continua funcionando.
- Nenhum segredo é gravado. Token GitHub só por `ARESTRIX_GITHUB_TOKEN` ou `GITHUB_TOKEN`.
- `localStorage` permanece apenas como fallback de testes; nunca como fonte de verdade no desktop.

### 5.3 Backend C++ necessário

Reutilizar o que já existe (`write_workspace_asset`, `read_workspace_asset`,
`save_mcp_server`, `ensure_workspace_directories`) e acrescentar:

| Endpoint | Função |
| :--- | :--- |
| `get_studio_library_config` / `save_studio_library_config` | Ler/gravar `%APPDATA%\Arestrix\libraries.json`. |
| `fetch_library_source` | Baixar metadados/arquivos de uma fonte GitHub com HTTP nativo, timeout, limite de tamanho e allowlist de hosts. Retorna manifesto + conteúdo para auditoria. |
| `read_library_index` / `write_library_index` | Ler/gravar `.workspace/libraries.json` e o lock. |

**Decisão tomada:** a aquisição é feita **no C++**, via `fetch_library_source`. O desktop já
concentra HTTP, proxy, certificados e política de rede, e isso evita depender de CORS do WebView.
O frontend recebe apenas manifesto + conteúdo já baixado, para auditoria e exibição.

### 5.4 Eventos

```
library:source-added        library:source-synced       library:source-removed
library:created             library:updated             library:removed
library:asset-installed     library:asset-removed       library:asset-updated
library:asset-audited       library:asset-rejected
library:changed             (refresh de árvores e seletores)
```

Emitir `library:changed` após qualquer mutação; `NodeSkillsModal` e `LibraryPicker` escutam.

---

## 6. UX nas Propriedades (gestão de fontes e bibliotecas)

Toda a gestão fica no shell existente, em três seções novas. A árvore ocupa a área de conteúdo,
com painel de detalhe à direita (mesmo padrão visual do `PropertiesWindow`).

### 6.1 Studio → Bibliotecas

```
┌ Bibliotecas do Studio ───────────────────────────────────────────────────────┐
│ [+ Nova biblioteca] [+ Adicionar repositório] [Sincronizar] [Política]      │
├──────────────────────────────┬───────────────────────────────────────────────┤
│ ▾ Arestrix (embutida)        │  Redação Técnica e Conceitual                 │
│   ▾ Skills (7)               │  Skill · v1.0.0 · MIT                         │
│     • Redação Técnica        │  Origem: builtin/redacao-tecnica              │
│     • Rigor Matemático       │  Auditoria: APPROVE · risco 0/100             │
│   ▾ MCPs (2)                 │  Política: f3d1bfef… · SHA-256: 9a2c…         │
│ ▾ GitHub: org/repo           │  Licença detectada: MIT (SPDX)                │
│   ▾ Skills (12)              │                                               │
│   ▾ MCPs (3)                 │  [Reauditar] [Duplicar] [Instalar no Workspace]│
│ ▸ Workspace (instaladas)     │                                               │
└──────────────────────────────┴───────────────────────────────────────────────┘
```

Ações de biblioteca e de fonte — as duas coisas são distintas e continuam disponíveis:

- **Nova biblioteca**: abre um assistente com quatro origens:
  1. **Biblioteca vazia** (local): nome, descrição, escopo inicial (`Studio` ou `Workspace`) e
     licença padrão. O usuário adiciona assets manualmente depois.
  2. **A partir de um repositório**: escolhe uma fonte GitHub já cadastrada — ou cadastra uma
     nova ali mesmo —, uma `ref`, uma subpasta e quais assets importar. A biblioteca nasce no
     escopo escolhido.
  3. **Importar arquivo**: `.arestrix-library` ou JSON de biblioteca, com validação de lock,
     hash e licença.
  4. **Clonar biblioteca**: copia nome, política e assets de uma biblioteca existente.
- **Adicionar repositório**: cadastra somente a **fonte** (`org/repo` ou URL, `ref`, subpasta,
  licença permitida). Não cria biblioteca automaticamente; a fonte fica disponível para
  “Nova biblioteca → A partir de um repositório” e para sincronização.
- **Adicionar assets**: inclui novos assets em uma biblioteca existente, por upload manual,
  por importação de arquivo ou por seleção de outra fonte.
- **Importar catálogo**: importa as skills da biblioteca embutida (Studio) ou o catálogo
  homologado do Workspace, sempre com auditoria antes do upsert.
- **Sincronizar**: resolve o commit e lê a árvore, mostrando *quantos* assets novos/alterados/
  removidos. Não baixa conteúdo: o download acontece sob demanda ao selecionar o asset no projeto,
  seguido de quarentena, validação e confirmação antes de instalar.
- **Detalhe do asset**: nome, tipo, versão, categoria, licença, commit, caminho, hash, veredito,
  achados, “Usado por” e diff quando houver atualização.
- **Remover fonte**: só remove a fonte e o cache; assets já usados permanecem nos agentes.

### 6.2 Workspace → Bibliotecas

Árvore das bibliotecas instaladas, com:

- barra de ações **`[+ Nova biblioteca] [+ Adicionar repositório] [+ Adicionar assets]
  [Importar .arestrix-library]`**;
- **liberação de um repositório como biblioteca**: aprovar a fonte, instalar o catálogo e
  habilitá-lo para o Workspace (ação principal desta tela);
- **criação de biblioteca** com as mesmas quatro origens do Studio (vazia, de repositório,
  arquivo ou clone), com escopo padrão `Workspace`;
- **adição de assets** a uma biblioteca existente: manual, por arquivo ou por seleção de fonte;
- estado da instalação (`installed`, `pinned`, `disabled`);
- veredito atual e botão “Reauditar”;
- **Minha biblioteca**: derivados criados pelo usuário (edição de assets), com `derivedFrom`
  visível e opção de “Promover à biblioteca”;
- “Reconstruir índices” (não sobrescreve assets editados);
- “Exportar biblioteca” (`.arestrix-library` ou JSON assinado por hash);
- “Abrir pasta no Explorer”;
- contagem de “Usado por” (vértices e pipelines).

O botão “Adicionar repositório” no Workspace abre o mesmo assistente do Studio (a fonte continua
tendo um único dono, o Studio) e retorna ao fluxo de criação de biblioteca, para o usuário não
precisar trocar de tela.

### 6.3 Projeto → Bibliotecas

- Habilitar/desabilitar bibliotecas já liberadas no Workspace para este projeto.
- Fixar versões (pins) por asset.
- Aviso quando a origem sumiu ou quando existe versão mais nova.
- Nenhum derivado nasce aqui: editar um asset cria em **Minha biblioteca do Workspace**; o
  projeto apenas referencia e fixa.

### 6.4 Remoção dos pontos de entrada antigos

| Entrada atual | Novo comportamento |
| :--- | :--- |
| Ribbon “Repositórios GitHub” | Deep link para `properties:open { scope:'studio', section:'libraries' }`. |
| Backstage “Repositórios do GitHub” | Mesmo deep link. |
| GlobalDefaults “Repositórios GitHub…” | Mesmo deep link. |
| `modal:open-github-repos` | Alias legado do deep link acima. |
| `GitHubReposModal` | Removido após duas releases de convivência. |
| Aba GitHub do `NodeSkillsModal` | Substituída por “Bibliotecas” (seletor). |

---

## 7. UX em agentes e grafos

### 7.1 Seletor único de bibliotecas

Um componente `LibraryPicker`, aberto por:

- `NodeSkillsModal` → aba **Bibliotecas**;
- `NodeToolsModal` → “Adicionar MCP da biblioteca”;
- `TemplateModal` (Biblioteca de Vértices) → “Bibliotecas” como fonte de templates;
- menu de contexto do grafo → “Inserir da biblioteca”.

Layout:

```
┌ Adicionar da Biblioteca ────────────────────────────────────────────────────┐
│ [Todas] [Skills] [MCPs] [Agentes] [Grafos]     🔎 buscar…                    │
├──────────────────────────────┬───────────────────────────────────────────────┤
│ ▾ Arestrix                   │  Rigor Matemático & Fórmulas KaTeX           │
│   ▾ Skills                   │  🛡️ APPROVE · risco 0 · MIT · v1.0.0          │
│     • Rigor Matemático       │  Origem: builtin/rigor-matematico             │
│     • SVG Vetorial           │  Ferramentas: read, write                     │
│ ▾ GitHub: org/repo           │                                               │
│   ▾ Skills                   │  [Incluir no agente] [Duplicar e editar]      │
│ ▸ Meu projeto                │  [Ver detalhes]                               │
└──────────────────────────────┴───────────────────────────────────────────────┘
```

Regras:

- Assets `REJECT` aparecem desabilitados com o motivo.
- `CAUTION` mostra confirmação explícita; no modo estrito, fica bloqueado.
- “Incluir no agente” copia o snapshot e registra `derivedFrom`.
- “Duplicar e editar” cria um derivado em **Minha biblioteca do Workspace** e abre o editor.
- “Criar novo” cria um asset local em **Minha biblioteca do Workspace**, já com front matter
  canônico e auditoria pendente.
- Nenhum botão de “configurar repositório” aparece aqui; há apenas “Gerenciar bibliotecas”,
  que abre as Propriedades.

### 7.2 Atualização de derivados

Quando a biblioteca de origem muda:

- o vértice mostra um badge “Atualização disponível”;
- o usuário vê o diff (`SKILL.md`/prompt/ferramentas);
- “Aplicar atualização” substitui o snapshot e reaudita;
- “Manter minha versão” mantém o derivado e registra a decisão.

Nada é atualizado silenciosamente.

### 7.3 Grafos

Para `agent-template` e `graph-template`:

- “Inserir no grafo” instancia o template como nós/arestas (reaproveita o `TemplateModal`);
- “Duplicar e editar” cria um template em **Minha biblioteca do Workspace**;
- o template carrega `libraryId`, `assetId`, `hash` e `verdict` no metadado do grafo/template.

---

## 8. Segurança

### 8.1 Gate único de entrada

Todo asset, independentemente da origem, passa por `LibraryAuditGate`:

| Tipo | Verificações |
| :--- | :--- |
| `skill` | Front matter, licença, ferramentas, hash SHA-256, `SkillSpectorRunner`, `CloudflareSkillAudit`, `SkillLedgerService`. |
| `mcp` | Estrutura, id válido, transporte, metacaracteres de shell, HTTPS-only, segredos literais, `McpSecurityScanner`, `McpLedgerService`. |
| `agent-template` / `graph-template` | Esquema do template, ferramentas/modelo/sandbox herdados, sem scripts embutidos, hash e ledger. |

Regras:

1. `REJECT` bloqueia instalação e uso.
2. `CAUTION` exige confirmação; no modo estrito, bloqueia.
3. O veredito é cacheado por `(hash, policyVersion)`. Mudou a política ou o conteúdo, volta a
   `PENDING` e precisa de reauditoria.
4. O veredito e o hash são gravados no asset e no snapshot do nó.
5. A importação pela biblioteca **não pode** ter caminho alternativo sem auditoria (corrige a
   falha atual da aba GitHub).

### 8.2 Aquisição GitHub

- Somente leitura (REST `GET`), hosts exatos: `api.github.com`, `raw.githubusercontent.com`.
- **Sincronização de fonte:** lê apenas metadados e a árvore do repositório (caminhos, `blob sha`,
  tamanho e licença). **Nenhum conteúdo de asset é baixado nessa etapa.**
- **Download sob demanda:** o conteúdo de um asset só é baixado depois que ele é **selecionado no
  projeto** (ou quando o usuário pede uma pré-visualização explícita).
- **Quarentena e validação:** todo download vai para uma área temporária; o asset só é promovido à
  biblioteca/instalação depois da auditoria. `REJECT` descarta o conteúdo baixado; `CAUTION` exige
  confirmação; `APPROVE` segue para instalação.
- Acesso **anônimo por padrão**: somente repositórios públicos, 60 requisições/h.
- **PAT opcional** por `ARESTRIX_GITHUB_TOKEN`/`GITHUB_TOKEN`: 5.000 requisições/h e repositórios
  privados; escopo mínimo, nunca persistido, nunca logado.
- Commit pin: toda importação resolve e congela um SHA; atualizar é uma ação explícita.
- Caminhos: normalizar, rejeitar `..`, absolutos, barra invertida, NUL e caminhos > 4096.
- Somente blobs regulares; rejeitar symlink, submodule e arquivo executável.
- Limites: `SKILL.md` ≤ 256 KB, MCP ≤ 64 KB, biblioteca total ≤ 5 MB, árvore ≤ 10.000 entradas.
- UTF-8 válido; front matter obrigatório em skills; JSON válido em MCPs.
- Licença: ler SPDX do repositório e do arquivo; se não estiver na allowlist, bloquear.
- Nunca executar scripts do repositório durante a importação.
- Rate limit: cache por `blob sha` + `ETag`; mensagem clara quando a cota anônima acabar.
- Funcionamento offline: cache local permite abrir, auditar e usar o que já foi instalado.

### 8.3 Segredos

- Nenhum token, header ou chave em `libraries.json`, índice, cache ou logs.
- MCPs seguem a regra atual: `env`/`headers` só com `${VAR}`.
- Testes de vazamento no CI (padrões OpenAI, GitHub, AWS, Google, Slack, Bearer, PEM).

### 8.4 Cadeia de suprimentos

- `libraries.lock.json` registra `source`, `commit`, `tree`, `path`, `sha256`, `policyVersion` e
  `verdict`.
- Atualização exige diff e reauditoria; nunca “latest”.
- Exportação/importação de biblioteca preserva o lock, para reprodutibilidade.

---

## 9. Adaptador GitHub

### 9.1 Fluxo de sincronização

A sincronização é executada pelo backend C++ (`fetch_library_source`), com timeout, limite de
tamanho, allowlist de hosts e cache. O frontend nunca acessa a rede diretamente.

1. `GET /repos/{org}/{repo}` → licença SPDX, `default_branch`, `private`, `archived`.
2. Resolver `ref` → commit (`GET /repos/{org}/{repo}/commits/{ref}`).
3. `GET /repos/{org}/{repo}/git/trees/{commit}?recursive=1` → candidatos.
4. Filtrar por convenção:
   - `skills/**/SKILL.md`;
   - `mcps/*.json` ou `**/mcp*.json` (configurável por fonte).
5. Persistir apenas o **manifesto de candidatos**: caminho, `blob sha`, tamanho, licença e tipo
   provável. **Nenhum blob é baixado nesta etapa.**
6. Mostrar o catálogo com o que já está instalado, o que é novo, o que mudou e o que saiu.
7. Quando o usuário **seleciona um asset no projeto** (ou pede pré-visualização), baixar somente
   aquele blob para a **quarentena** temporária.
8. Validar e auditar o conteúdo na quarentena.
9. Promover à biblioteca/instalação apenas `APPROVE` (ou `CAUTION` com confirmação). `REJECT`
   descarta o download e mantém o candidato como bloqueado.

O mesmo fluxo vale para pré-visualização: ela baixa para a quarentena, mostra o conteúdo e não
promove nada sem confirmação.

### 9.2 Interface do adaptador

```ts
export interface ILibrarySourceAdapter {
  kind: LibrarySourceKind;
  validateSource(source: ILibrarySource): Promise<ILibrarySourceValidation>;
  listAssets(source: ILibrarySource): Promise<ILibraryAssetCandidate[]>;
  readAsset(source: ILibrarySource, candidate: ILibraryAssetCandidate): Promise<ILibraryAssetPayload>;
  resolveRevision(source: ILibrarySource): Promise<string>;
}
```

O `LibraryService` não sabe se a origem é GitHub, pasta local ou embutida.
`listAssets` retorna apenas metadados; `readAsset` é chamado somente após a seleção no projeto e
grava o conteúdo em quarentena antes da validação.

---

## 10. Migração (estrangler)

### 10.1 Dados existentes

- `arestrix_github_skills_repos` → importar uma vez para `libraries.json` do Studio como fontes
  `github` desabilitadas, com aviso “fonte não verificada”; remover a chave antiga só após
  duas releases.
- `GitHubSkillsRegistry.defaultSkills` → migrar para `BuiltinSourceAdapter` (biblioteca
  “Arestrix”), mantendo IDs para não quebrar snapshots existentes.
- `APPROVED_WORKSPACE_SKILLS` / `APPROVED_WORKSPACE_MCPS` → virar a biblioteca de Workspace
  “Arestrix Workspace”, sem duplicar conteúdo.
- `INodeSkill` existentes continuam válidos; recebem `derivedFrom` apenas quando reimportados.
- Nenhum pipeline é reescrito.

### 10.2 Ordem de corte

1. Criar o modelo e a árvore nas Propriedades lendo os dados atuais (somente leitura).
2. Migrar o catálogo embutido para `BuiltinSourceAdapter`.
3. Trocar o seletor do agente para `LibraryPicker` (fonte embutida + Workspace).
4. Ligar o adaptador GitHub real e o gate de auditoria.
5. Redirecionar entradas antigas.
6. Remover `GitHubReposModal`, `GitHubSkillsRegistry` e a aba GitHub do nó.
7. Atualizar docs, i18n, testes e changelog.

---

## 11. Fases de implementação

### Fase 0 — Contrato e decisões (2–3 dias)

- Congelar modelo, escopos, nomes e semântica de derivado.
- ADR curto: “Bibliotecas são o único caminho para assets externos”.
- Definir licença allowlist e política `CAUTION`/`REJECT`.
- **Saída:** tipos aprovados; nenhuma UI.

### Fase 1 — Correção de confiança no que existe (2–3 dias) — **implementada**

- Renomear o catálogo embutido para “Biblioteca Arestrix”.
- Remover URLs e alegações de repositórios inexistentes.
- Aplicar auditoria na importação hoje feita pela aba GitHub (correção de segurança).
- Marcar `GitHubReposModal` como legado e apontar para Propriedades.
- Adicionar a seção **Bibliotecas** em Studio/Workspace com criação de biblioteca e cadastro de fonte.
- **Critério:** nenhuma tela promete GitHub sem GitHub; importação não auditada deixa de existir.

### Fase 2 — Domínio, stores e gate (5–7 dias) — **implementada**

- `types.ts`, `LibraryRegistry` (com assets), `LibraryStore`, `LibraryService`, `LibraryAuditGate`.
- Operações de criação: `createLibrary` (vazia ou de fonte) e importação de catálogo.
  Importação de arquivo `.arestrix-library` e clone entram com a UI da Fase 3.
- Adaptadores `builtin` e `workspace` para skills e MCPs.
- Persistência Studio/Workspace/Projeto com fallback local; espelho nativo em `.workspace/libraries.json`.
- **Minha biblioteca** no Workspace (derivados) com promoção explícita.
- Importação do catálogo embutido e do catálogo do Workspace, sempre auditada.
- **Critério:** importar, auditar, instalar, usar, editar e remover sem UI nova.

### Fase 3 — Árvore nas Propriedades (5–7 dias) — **implementada**

- `LibraryTree` + `LibraryAssetPanel` + binder no shell de Propriedades.
- Criação de biblioteca vazia ou de repositório; importar arquivo e clonar pela árvore.
- **Adicionar repositório** e **importar catálogo/arquivo** disponíveis em Studio e Workspace.
- Árvore com busca, filtro por tipo, detalhe de proveniência/auditoria e ações de reauditar,
  duplicar/editar, instalar e remover.
- Importar/exportar/clonar biblioteca.
- **Bibliotecas do Projeto**: habilitar bibliotecas e fixar assets (pins locais até a Fase 4).
- Studio, Workspace e Projeto com ações e deep links.
- i18n completo pt-BR/en-US e tema claro/escuro.
- **Critério:** criar uma biblioteca nova, cadastrar um repositório e adicionar assets sem sair
  das Propriedades; o modal antigo não é necessário.

### Fase 4 — Agentes e grafos (4–5 dias) — **implementada (skills/MCPs)**

- `LibraryPickerModal` para escolher assets com busca, veredito e ações de incluir, derivar e criar.
- `NodeSkillsModal`: a aba de biblioteca lista assets reais, reaudita na inclusão e grava
  `derivedFrom`/`audit` no snapshot do vértice.
- `NodeToolsModal`: **Adicionar da biblioteca** instala o MCP no Workspace e atualiza o seletor.
- Pins do Projeto filtram as bibliotecas disponíveis e priorizam assets fixados no seletor.
- **Diff de atualização:** o vértice detecta quando o asset da biblioteca mudou, avisa e permite
  aplicar a versão reauditada (com confirmação).
- **Templates de vértice:** `agent-template` como asset, com auditoria de esquema, salvamento pelo
  catálogo e inserção no grafo via `TemplateModal → Bibliotecas`.
- **Critério:** criar um agente do zero usando só bibliotecas, sem tocar em GitHub.

### Fase 5 — Fonte GitHub real (5–7 dias) — **implementada**

- Endpoint C++ `fetch_library_source` com allowlist de hosts, commit pin, licença SPDX, limite de
  tamanho, cache curto e leitura anônima ou com PAT por variável de ambiente.
- `GitHubSourceAdapter` no frontend; o navegador/WebView nunca acessa o GitHub diretamente.
- **Sincronização sem download**: apenas manifesto/árvore; assets entram como `PENDING`.
- **Download sob demanda**: o blob é baixado ao selecionar/baixar o asset, auditado e promovido
  somente após o veredito; `REJECT` descarta o conteúdo.
- UI: cadastro da fonte com referência, **Sincronizar GitHub**, **Baixar e auditar** e download
  direto no seletor do agente.
- **Critério:** importar um repositório real, ver os assets na árvore, selecionar, baixar, auditar,
  instalar e usar offline depois. Nenhum blob é baixado antes da seleção.

### Fase 6 — Migração e limpeza (3–4 dias) — **implementada**

- `GitHubReposModal` e `GitHubSkillsRegistry` removidos; catálogo embutido em
  `BuiltinLibraryCatalog`.
- Entradas de ribbon, Backstage, Defaults e modal do vértice apontam diretamente para
  `properties:open { scope:'studio', section:'libraries' }`.
- Migração de `arestrix_github_skills_repos` para `ILibrarySource` + biblioteca de fonte.
- Testes legados substituídos pelos testes de biblioteca e pelo E2E.
- **Critério:** um único caminho de gestão e um único caminho de uso.

### Fase 6.1 — Automação do fluxo completo — **implementada**

- `npm run test:libraries`: Vitest E2E (source → sync → download → audit → install → derive →
  template → pins) + testes C++ filtrados da fonte GitHub.
- `npm run test:libraries:live`: inclui manifesto real via `fetch_library_source`.
- `--no-native` para ambientes sem binário C++.
- **Critério:** um comando valida o ciclo principal de ponta a ponta.

### Fase 7 — Homologação (3–5 dias) — **automação concluída; manual pendente**

- `npm run homologation:libraries`: gate determinístico com Vitest + C++ contra mock GitHub local
  (`ARESTRIX_TEST_MODE=1`, bases apenas em loopback), relatório, matriz, ambiente e logs.
- `npm run homologation:libraries:live`: acrescenta o smoke real do GitHub.
- Teclado/foco aprovado em 2026-10-27. Pendentes: revalidar tema/tamanhos após a correção de
  canvas/vértices, instalação limpa/upgrade e offline.
- **Critério:** gate de release aprovado. O status `passed-automated` não promove o Beta 2.3
  enquanto os 3 casos manuais restantes e o gate de release não estiverem verdes.
- Detalhes e matriz em [`libraries-homologation-plan.md`](./libraries-homologation-plan.md).

**Ordem de grandeza:** 29–41 dias de desenvolvimento, sem retrabalho de homologação.

---

## 12. Plano de testes

### Unitários

- `library-registry.spec.ts`: resolução por escopo, habilitação, pins, eventos.
- `library-store.spec.ts`: load/save, schema, corrupção, fallback, sem segredos.
- `library-audit-gate.spec.ts`: skill/MCP/template, `APPROVE`/`CAUTION`/`REJECT`, cache por
  `(hash, policyVersion)`, reauditoria após mudança de política.
- `library-service.spec.ts`: instalar, derivar, atualizar, remover, “usado por”.
- `github-source-adapter.spec.ts` (mock HTTP): árvore, licença, limites, path traversal,
  symlink/submodule, rate limit, commit pin.
- `library-migration.spec.ts`: `arestrix_github_skills_repos`, IDs legados, snapshots.
- Paridade i18n pt-BR/en-US.

### Integração

- Abrir Studio/Workspace/Projeto → Bibliotecas, adicionar repositório, sincronizar, criar
  biblioteca (vazia e a partir do repositório), adicionar assets e instalar.
- Criar biblioteca vazia, adicionar skill manual e usar no agente.
- Clonar e importar biblioteca validando lock, hash e licença.
- Importar skill → ver veredito → incluir no agente → executar pipeline.
- Editar derivado → reauditar → ver `derivedFrom` e o item em **Minha biblioteca do Workspace**.
- Atualizar biblioteca → diff → aplicar → reauditar.
- Desabilitar/remover biblioteca → agente continua abrindo.
- Modo estrito bloqueia `CAUTION`; `REJECT` nunca aparece habilitado.
- Offline após sincronização.
- Sincronizar uma fonte não baixa blobs; selecionar um asset no projeto baixa apenas aquele asset
  para a quarentena e só instala depois do veredito.
- `REJECT` na quarentena descarta o conteúdo e não cria asset instalado.
- Deep links antigos (ribbon, backstage, defaults) caem na seção correta.

### Segurança

- Skill maliciosa (injeção, esteganografia, exfiltração, trifeta letal) → `REJECT`.
- MCP com shell metachar, HTTP sem TLS, header secreto literal → bloqueado.
- Repositório com `../../`, symlink, submodule, binário, arquivo > limite → rejeitado.
- Token GitHub não aparece em JSON, log, HTML ou cache.
- Licença desconhecida ou fora da allowlist → bloqueada.
- Atualização de conteúdo invalida o veredito anterior.

### Visuais e manuais

- Claro/escuro via `html[data-theme="dark"]`.
- 1024 px e 1800 px; árvore colapsável e painel rolável.
- Upgrade preservando projetos e bibliotecas.
- Exportar/importar biblioteca com lock.
- Máquina limpa, sem token e sem rede.

---

### Automação do fluxo completo

- `npm run test:libraries`:
  - roda `tests/library-e2e.spec.ts` e os testes de adaptador, gate, registry, árvore,
    Propriedades, pins e i18n;
  - roda os testes C++ da fonte GitHub com filtro `github_source`;
  - cobre source → sync sem download → download sob demanda → auditoria → instalação →
    derivado → template → pins.
- `npm run test:libraries:live`: adiciona um manifesto real do GitHub ao teste C++.
- `--no-native`: pula a etapa C++ em ambientes sem binário compilado.
- Requisito para a etapa nativa: `build/arestrix_tests` compilado.

## 13. Riscos e mitigação

| Risco | Impacto | Mitigação |
| :--- | :--- | :--- |
| Catálogo atual é fictício | Perda de confiança | Fase 1 corrige rótulos e remove URLs 404; testes de integridade de fonte |
| Importação GitHub sem auditoria | Risco de segurança | Gate único `LibraryAuditGate`; teste que falha se o caminho não auditar |
| Dois catálogos concorrentes | Divergência | `BuiltinSourceAdapter` + Workspace como fontes do mesmo modelo |
| GitHub fora do ar / rate limit | Bloqueio de uso | Cache local, commit pin, mensagem clara e modo offline |
| Conteúdo remoto malicioso | Execução indevida | Somente declarativo, sem execução, auditoria obrigatória, limites e allowlist |
| Download precoce de blobs | Exposição/consumo desnecessário | Sincronização só lê manifesto; download sob demanda, em quarentena, após seleção no projeto |
| Escopo Studio/Workspace/Projeto confuso | Duplicidade | “Uma propriedade, um dono”; Project só habilita e fixa |
| Migração quebrar pipelines | Regressão | Snapshot preservado; índice aditivo; testes de migração |
| Testes antigos acoplados a strings do modal | Churn alto | Fase 1 mantém compatibilidade; Fase 6 atualiza testes junto com a remoção |
| Aquisição C++ com rede limitada/proxy | Bloqueio de sincronização | Timeout, cache, mensagem clara e modo offline |
| Atualização silenciosa | Deriva de comportamento | Diff obrigatório e reauditoria |

---

## 14. Decisões tomadas e pendências

### 14.1 Decididas

| # | Tema | Decisão |
| :--- | :--- | :--- |
| 1 | Escopo do MVP | **Skills + MCPs**. `agent-template` e `graph-template` entram na fase 4. |
| 2 | Aquisição GitHub | **Backend C++** (`fetch_library_source`). O frontend não acessa a rede. |
| 3 | Autenticação GitHub | **Anônima para repositórios públicos + PAT opcional por variável de ambiente** (`ARESTRIX_GITHUB_TOKEN`/`GITHUB_TOKEN`), para repositórios privados e limites maiores. GitHub App fica fora do escopo. |
| 6 | Destino de “Minha biblioteca” | **Workspace por padrão.** O Projeto apenas habilita bibliotecas e fixa versões. Liberar um repositório como biblioteca é uma ação do Workspace. |
| 8 | Remoção do código antigo | `GitHubReposModal` e `GitHubSkillsRegistry` ficam como shim/alias até a **fase 6**. |
| 11 | Download de assets | **Sob demanda.** A sincronização lê apenas manifesto/árvore; o blob é baixado só após a seleção no projeto, em quarentena, e promovido apenas depois da validação. |

### 14.2 Pendentes

4. **Confiança padrão de fonte GitHub:** `community` (exige aprovação por asset) ou `verified`
   após auditoria? *Recomendação:* `community` até o usuário aprovar a biblioteca no Workspace.
5. **Imutabilidade:** asset sempre imutável com derivado, ou edição in-place em “Minha biblioteca”?
   *Recomendação:* imutável; edição sempre derivada.
7. **Atualização automática:** nunca, ou “avisar e aplicar em lote”?
   *Recomendação:* nunca automática; aviso + diff + aplicação manual.
9. **Arquivo de configuração do Studio:** `libraries.json` separado ou dentro de
   `studio.config.json`? *Recomendação:* separado, para não misturar preferências e catálogo.
10. **Repositórios privados:** suportar na v1 ou depois?
    *Recomendação:* públicos no MVP; privados na fase 5.1, somente com PAT de escopo mínimo.

---

## 15. Fora de escopo

- Marketplace público de bibliotecas com upload/assinatura de terceiros.
- Execução remota de skills ou MCPs sem instalação local.
- Sincronização bidirecional (publicar derivados de volta no GitHub).
- Múltiplos workspaces por instalação.
- Substituir o runtime de skills/MCPs; o snapshot continua sendo o contrato.
- Reescrever o `TemplateModal` inteiro; apenas alimentá-lo com a fonte de biblioteca.

---

## 16. Entregáveis

1. ADR e matriz de escopos de biblioteca.
2. `src/core/libraries/*` com modelo, store, serviço, gate e adaptadores.
3. Seções “Bibliotecas” em Studio, Workspace e Projeto no shell existente.
4. `LibraryPicker` integrado ao agente, às ferramentas e ao catálogo de vértices.
5. Adaptador GitHub real, com commit pin, licença, cache e diff.
6. Migração aditiva e remoção das entradas/modais antigos.
7. Testes unitários, de integração, de segurança, visuais e de migração.
8. Documentação pt-BR/en-US, i18n e changelog.

---

## 17. Critérios de aceite (resumo)

- Nenhuma tela do produto afirma “GitHub” sem que exista uma fonte GitHub real configurada.
- Toda gestão de fonte, biblioteca e asset ocorre nas Propriedades.
- O usuário consegue **cadastrar novos repositórios** e **criar bibliotecas novas** (vazias, a
  partir de repositório, importadas ou clonadas) sem sair das Propriedades.
- Nenhum asset entra em agente/grafo sem hash, licença, proveniência e veredito.
- O usuário consegue criar um agente completo escolhendo assets de bibliotecas, sem saber o que
  é um repositório Git.
- Editar um asset nunca destrói a origem; derivados nascem em **Minha biblioteca do Workspace** e
  o Projeto apenas os referencia e fixa.
- Atualizar nunca é silencioso.
- Nenhum blob é baixado antes da seleção explícita no projeto; `REJECT` descarta o download em
  quarentena.
- O produto funciona offline com o que já foi instalado.
- Nenhum segredo é gravado ou exibido.
