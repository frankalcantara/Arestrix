import type { IAssetOriginMetadata, IGitHubSkillItem } from '../types.ts';
import type { ISerializedTemplatePlugin } from '../templates/AgentTemplateTypes.ts';
import { LibraryAuditGate } from './LibraryAuditGate.ts';
import { builtinSkillToAsset, skillAssetId } from './adapters/skillAssetAdapter.ts';
import type { ILibraryAsset, ILibraryAssetAudit, ILibraryState, IMcpAssetPayload } from './types.ts';
import bundledAgentCatalog from './catalog/bundledAgentVertices.json';
import bundledMcpCatalog from './catalog/bundledMcpServers.json';

/**
 * Arestrix Studio - Catálogo Embutido da Biblioteca Arestrix
 *
 * Skills e vértices de agentes que acompanham o Studio. Eles são auditados e
 * hidratados localmente; nenhum asset é baixado da internet aqui.
 */

export const BUILTIN_LIBRARY_SKILLS: IGitHubSkillItem[] = [
    {
      id: 'redacao-tecnica-profunda',
      name: 'Redação Técnica e Conceitual',
      description: 'Produção de conteúdo denso, estruturado, sem redundâncias e com clareza expositiva.',
      repoId: 'library-arestrix',
      repoName: 'Biblioteca Arestrix',
      category: 'Redação & Conteúdo',
      license: 'MIT',
      tools: ['write', 'edit', 'read'],
      systemPrompt: `# DIRETRIZES DE REDAÇÃO TÉCNICA E CONCEITUAL
Você é um redator técnico de precisão.
- Desenvolva cada parágrafo com substância conceitual densa.
- Evite lugares-comuns, introduções vazias ou repetições de premissas.
- Use terminologia técnica exata e contextualizada.
- Estruture o documento com seções claras, encadeamento lógico e exemplos concretos.`,
      markdownContent: `---
name: "Redação Técnica e Conceitual"
description: "Produção de conteúdo denso, estruturado e com rigor acadêmico/técnico"
license: "MIT"
tools:
  - write
  - edit
  - read
---

# Redação Técnica e Conceitual

Esta habilidade capacita o agente a gerar textos de alta fidelidade técnica, documentações profundas e artigos com encadeamento causal impecável.

## Objetivos e Escopo
1. Exposição clara de conceitos complexos sem simplismo.
2. Organização hierárquica em seções e subseções balanceadas.
3. Integração de diagramas e tabelas conceituais quando aplicável.

## Regras de Execução
- **Tom**: Neutro, assertivo e fundamentado.
- **Formatação**: Markdown canônico, código em blocos delimitados e referências explícitas.
- **Validação**: Nenhum parágrafo deve conter menos de duas ideias estruturadas interligadas.`
    },
    {
      id: 'rigor-matematico-latex',
      name: 'Rigor Matemático & Fórmulas KaTeX',
      description: 'Demonstrações formais, validação de teoremas e formatação precisa de fórmulas matemáticas.',
      repoId: 'library-arestrix',
      repoName: 'Biblioteca Arestrix',
      category: 'Matemática & Ciência',
      license: 'Apache-2.0',
      tools: ['read', 'write'],
      systemPrompt: `# DIRETRIZES DE RIGOR MATEMÁTICO E FÓRMULAS
Você é um agente especializado em formalismo matemático e demonstrações analíticas.
- Apresente todas as hipóteses iniciais e definições com clareza axiomática.
- Renderize fórmulas matemáticas exclusivamente em sintaxe KaTeX/LaTeX: inline com $...$ ou bloco com $$...$$.
- Verifique a consistência dimensional e limites de validade de cada expressão.
- Demonstre teoremas passo a passo, justificando cada passagem lógica.`,
      markdownContent: `---
name: "Rigor Matemático & Fórmulas KaTeX"
description: "Modelagem matemática formal, derivações algébricas e demonstrações"
license: "Apache-2.0"
tools:
  - read
  - write
---

# Rigor Matemático e Fórmulas KaTeX

Capacita agentes a desenvolverem modelos analíticos formais, cálculos simbólicos e explicações matemáticas rigorosas.

## Diretrizes de Formatação
- Fórmulas inline: \`$f(x) = \\int_0^x e^{-t^2} dt$\`
- Blocos de equações:
  $$\\nabla \\times \\mathbf{B} = \\mu_0 \\mathbf{J} + \\mu_0 \\varepsilon_0 \\frac{\\partial \\mathbf{E}}{\\partial t}$$
- Convenção de variáveis: mantenha coerência tipográfica em todo o pipeline.`
    },
    {
      id: 'ilustracao-vetorial-svg',
      name: 'Criação e Validação Vetorial SVG',
      description: 'Geração de diagramas SVG puros, geométricos, acessíveis e com estrito respeito à proibição de sombras em texto.',
      repoId: 'library-arestrix',
      repoName: 'Biblioteca Arestrix',
      category: 'Design & Visual',
      license: 'Apache-2.0',
      tools: ['write', 'edit', 'validate_svg'],
      systemPrompt: `# DIRETRIZES DE CRIAÇÃO VETORIAL SVG
Você é um desenhista vetorial técnico.
- Gere código SVG puro, válido e autocontido com viewBox responsivo.
- REGRA CRÍTICA DE TIPOGRAFIA: NUNCA use filtros de sombra, drop-shadow, blur ou text-shadow em elementos <text>.
- Para dar destaque visual, use peso tipográfico (font-weight 700), cores de contraste ou badges <rect> de fundo limpos com cantos arredondados.
- Utilize cores da paleta corporativa moderna com contraste WCAG AA.`,
      markdownContent: `---
name: "Criação e Validação Vetorial SVG"
description: "Construção física de ilustrações vetoriais SVG nítidas e acessíveis"
license: "Apache-2.0"
tools:
  - write
  - edit
  - validate_svg
---

# Criação e Validação Vetorial SVG

Produção de artefatos visuais vetoriais para documentações técnicas e dashboards.

## Restrições Estritas de Qualidade Visual
1. **Zero Efeitos de Sombra em Texto**: Proibido \`feGaussianBlur\`, \`feDropShadow\` ou \`filter: drop-shadow\` sobre fontes.
2. **Escalabilidade**: Sempre utilize \`viewBox="0 0 W H"\` e \`width="100%"\`.
3. **Semântica**: Agrupe elementos lógicos em \`<g id="...">\` com títulos \`<title>\` acessíveis.`
    },
    {
      id: 'auditoria-codigo-cpp20',
      name: 'Auditoria de Código C++20 & Taskflow',
      description: 'Inspeção de concorrência, ausência de deadlocks, RAII e conformidade com C++20 Taskflow.',
      repoId: 'library-arestrix',
      repoName: 'Biblioteca Arestrix',
      category: 'Engenharia de Software',
      license: 'MIT',
      tools: ['read', 'write', 'edit', 'bash'],
      systemPrompt: `# DIRETRIZES DE AUDITORIA C++20 TASKFLOW
Você é um engenheiro sênior de sistemas C++20 concorrentes.
- Valide o uso seguro de taskflow (tf::Taskflow, tf::Executor, tf::Subflow).
- Garanta que todas as lambdas capturem referências estritamente válidas durante o lifetime de execução.
- Verifique a ausência de condições de corrida através de tf::CriticalSection ou mutexes std::osyncstream.
- Recomende o padrão RAII e tipos de valor imutáveis sempre que possível.`,
      markdownContent: `---
name: "Auditoria de Código C++20 & Taskflow"
description: "Revisão e endurecimento de algoritmos concorrentes em C++20"
license: "MIT"
tools:
  - read
  - write
  - edit
  - bash
---

# Auditoria de Código C++20 & Taskflow

Especialização em segurança de memória, paralelismo massivo e compilação limpa com MSVC / Clang.

## Pontos Críticos de Checagem
- Ciclos em grafos Taskflow: verificar se há branches com retorno sem nó de condição intermediário.
- Alocações dinâmicas excessivas dentro de loops críticos concorrentes.
- Tratamento explícito de exceções dentro de lambdas paralelas.`
    },
    {
      id: 'analista-dados-sql',
      name: 'Analista de Dados & Ingestão SQL',
      description: 'Normalização relacional, sanitização de dados tabulares e criação de esquemas SQLite WAL.',
      repoId: 'library-arestrix',
      repoName: 'Biblioteca Arestrix',
      category: 'Dados & Armazenamento',
      license: 'MIT',
      tools: ['read', 'write', 'bash'],
      systemPrompt: `# DIRETRIZES DE ANALISTA DE DADOS E SQL
Você é um especialista em arquitetura de dados relacionais e SQL SQLite/PostgreSQL.
- Projete esquemas normalizados na 3ª Forma Normal (3NF) com chaves primárias e estrangeiras explícitas.
- Gere scripts DDL idempotentes (CREATE TABLE IF NOT EXISTS) com índices para colunas de consulta frequente.
- Valide formatos de datas ISO 8601 e números antes da inserção.`,
      markdownContent: `---
name: "Analista de Dados & Ingestão SQL"
description: "Modelagem, higienização de datasets e consultas estruturadas"
license: "MIT"
tools:
  - read
  - write
  - bash
---

# Analista de Dados & Ingestão SQL

Capacidade de transformar dados brutos e planilhas em bases relacionais íntegras com consultas analíticas rápidas.`
    },
    {
      id: 'engenharia-prompt-xml',
      name: 'Engenharia de Prompt XML Semântico',
      description: 'Criação de prompts industriais estruturados com delimitação XML (<context>, <rules>, <output_format>).',
      repoId: 'library-arestrix',
      repoName: 'Biblioteca Arestrix',
      category: 'Engenharia de Prompt',
      license: 'MIT',
      tools: ['write', 'edit'],
      systemPrompt: `# DIRETRIZES DE ENGENHARIA DE PROMPT XML
Você é um engenheiro de prompts de ponta para modelos de linguagem avançados.
- Estruture sempre os prompts com delimitadores XML semânticos limpos.
- Inclua exemplos Few-Shot em seções <examples> com input e output esperados.
- Defina restrições negativas explícitas na tag <forbidden_actions>.`,
      markdownContent: `---
name: "Engenharia de Prompt XML Semântico"
description: "Formulação de prompts com isolamento estrito de contexto via tags XML"
license: "MIT"
tools:
  - write
  - edit
---

# Engenharia de Prompt XML Semântico

Padrão ouro para evitar vazamento de prompt, injeção de instruções e garantir determinismo nas respostas dos agentes.`
    }
  ];

export interface IBundledAgentVertexEntry {
  id: string;
  name: string;
  description: string;
  domain: string;
  subdomain: string;
  category: string;
  icon: string;
  license: string;
  bundledInProduct: true;
  qualityScore?: number;
  selectionScore?: number;
  origin: IAssetOriginMetadata;
  template: ISerializedTemplatePlugin;
}

const DEFAULT_LIBRARY_ID = 'library-arestrix';

function cloneData<T>(value: T): T {
  if (value === undefined || value === null) return value;
  if (typeof structuredClone === 'function') {
    return structuredClone(value);
  }
  return JSON.parse(JSON.stringify(value)) as T;
}

const rawBundledCatalog = bundledAgentCatalog as unknown as {
  generatedAt?: string;
  items?: IBundledAgentVertexEntry[];
};

export const BUNDLED_AGENT_CATALOG_GENERATED_AT: string =
  rawBundledCatalog.generatedAt || '2026-11-18T00:00:00.000Z';

export const BUNDLED_AGENT_VERTICES: readonly IBundledAgentVertexEntry[] =
  Array.isArray(rawBundledCatalog.items) ? rawBundledCatalog.items : [];

const bundledByEntryId = new Map<string, IBundledAgentVertexEntry>();
const bundledByOriginAssetId = new Map<string, IBundledAgentVertexEntry>();
const bundledByTemplateId = new Map<string, IBundledAgentVertexEntry>();

for (const entry of BUNDLED_AGENT_VERTICES) {
  if (!entry || !entry.id || !entry.template) continue;
  bundledByEntryId.set(entry.id, entry);
  if (entry.origin?.assetId) {
    bundledByOriginAssetId.set(entry.origin.assetId, entry);
  }
  if (entry.template.id && !bundledByTemplateId.has(entry.template.id)) {
    bundledByTemplateId.set(entry.template.id, entry);
  }
  if (entry.origin?.templateId && !bundledByTemplateId.has(entry.origin.templateId)) {
    bundledByTemplateId.set(entry.origin.templateId, entry);
  }
}

/**
 * Busca um vértice de agente embutido por `entry.id`, `origin.assetId`,
 * `template.id` ou `origin.templateId`, retornando sempre um clone imutável.
 */
export function getBundledAgentVertexById(idOrTemplateId: string): IBundledAgentVertexEntry | undefined {
  if (!idOrTemplateId) return undefined;
  const hit =
    bundledByEntryId.get(idOrTemplateId) ||
    bundledByOriginAssetId.get(idOrTemplateId) ||
    bundledByTemplateId.get(idOrTemplateId);
  return hit ? cloneData(hit) : undefined;
}

function buildBundledAgentAssetId(entry: IBundledAgentVertexEntry, libraryId: string): string {
  if (libraryId === DEFAULT_LIBRARY_ID) return entry.id;
  if (entry.id.startsWith(`asset-${DEFAULT_LIBRARY_ID}-`)) {
    return entry.id.replace(`asset-${DEFAULT_LIBRARY_ID}-`, `asset-${libraryId}-`);
  }
  return `asset-${libraryId}-agent-${entry.template.id}`;
}

/**
 * Converte um item do catálogo embutido de agentes em um `ILibraryAsset`
 * pronto para o `LibraryRegistry`, com cópias isoladas de `origin` e `template`.
 */
export function bundledAgentVertexToLibraryAsset(
  entry: IBundledAgentVertexEntry,
  libraryId: string = DEFAULT_LIBRARY_ID
): ILibraryAsset {
  const originCopy = cloneData(entry.origin);
  originCopy.bundledInProduct = true;
  originCopy.sourceKind = 'builtin';

  const templateCopy = cloneData(entry.template);
  templateCopy.domain = entry.domain;
  templateCopy.subdomain = entry.subdomain;
  templateCopy.bundledInProduct = true;
  templateCopy.origin = cloneData(originCopy);
  templateCopy.presetLocked = templateCopy.presetLocked !== false;

  const version = originCopy.installVersion || templateCopy.version || '1.0.0';

  return {
    id: buildBundledAgentAssetId(entry, libraryId),
    libraryId,
    kind: 'agent-template',
    name: entry.name,
    description: entry.description,
    category: entry.category || 'Agentes',
    domain: entry.domain,
    subdomain: entry.subdomain,
    icon: entry.icon,
    bundledInProduct: true,
    origin: originCopy,
    qualityScore: entry.qualityScore,
    selectionScore: entry.selectionScore,
    version,
    license: entry.license || originCopy.license || 'MIT',
    status: 'available',
    provenance: {
      sourceKind: 'builtin',
      sourceId: libraryId,
      repo: originCopy.repo,
      path: originCopy.path,
      url: originCopy.url,
      authority: 'Arestrix',
      importedAt: BUNDLED_AGENT_CATALOG_GENERATED_AT
    },
    audit: {
      verdict: 'APPROVE',
      riskScore: 0,
      policyVersion: 'bundled-v1',
      auditedAt: BUNDLED_AGENT_CATALOG_GENERATED_AT,
      hash: originCopy.installHash,
      findings: [],
      warnings: []
    },
    payload: {
      kind: 'agent-template',
      template: templateCopy
    },
    createdAt: BUNDLED_AGENT_CATALOG_GENERATED_AT,
    updatedAt: BUNDLED_AGENT_CATALOG_GENERATED_AT
  };
}

export function getBundledAgentAssets(libraryId: string = DEFAULT_LIBRARY_ID): ILibraryAsset[] {
  return BUNDLED_AGENT_VERTICES.map(entry => bundledAgentVertexToLibraryAsset(entry, libraryId));
}

export interface IBundledMcpServerEntry {
  id: string;
  serverId: string;
  name: string;
  description: string;
  domain: string;
  subdomain: string;
  category: string;
  icon: string;
  version: string;
  license: string;
  bundledInProduct: true;
  qualityScore: number;
  selectionScore: number;
  defaultTool?: string;
  cppAdaptations?: string[];
  origin: IAssetOriginMetadata;
  audit: Omit<ILibraryAssetAudit, 'auditedAt'> & { auditedAt?: string };
  payload: IMcpAssetPayload;
}

interface IBundledMcpCatalogFile {
  format: string;
  version: string;
  generatedAt: string;
  totalCount: number;
  items: IBundledMcpServerEntry[];
}

const rawBundledMcpCatalog = bundledMcpCatalog as unknown as IBundledMcpCatalogFile;

export const BUNDLED_MCP_CATALOG_GENERATED_AT: string =
  rawBundledMcpCatalog.generatedAt || BUNDLED_AGENT_CATALOG_GENERATED_AT;

export const BUNDLED_MCP_SERVERS: readonly IBundledMcpServerEntry[] =
  Array.isArray(rawBundledMcpCatalog.items) ? rawBundledMcpCatalog.items : [];

const bundledMcpById = new Map<string, IBundledMcpServerEntry>();
const bundledMcpByServerId = new Map<string, IBundledMcpServerEntry>();

for (const entry of BUNDLED_MCP_SERVERS) {
  if (!entry || !entry.id || !entry.serverId || !entry.payload) continue;
  bundledMcpById.set(entry.id, entry);
  bundledMcpByServerId.set(entry.serverId, entry);
}

export function getBundledMcpServerById(idOrServerId: string): IBundledMcpServerEntry | undefined {
  if (!idOrServerId) return undefined;
  const hit = bundledMcpById.get(idOrServerId) || bundledMcpByServerId.get(idOrServerId);
  return hit ? cloneData(hit) : undefined;
}

function buildBundledMcpAssetId(entry: IBundledMcpServerEntry, libraryId: string): string {
  if (libraryId === DEFAULT_LIBRARY_ID) return entry.id;
  return `asset-${libraryId}-${entry.serverId}`.slice(0, 64);
}

export function bundledMcpToLibraryAsset(
  entry: IBundledMcpServerEntry,
  libraryId: string = DEFAULT_LIBRARY_ID
): ILibraryAsset {
  const originCopy = cloneData(entry.origin);
  originCopy.bundledInProduct = true;
  originCopy.sourceKind = 'builtin';
  originCopy.libraryId = libraryId;

  const payloadCopy = cloneData(entry.payload);
  const assetId = buildBundledMcpAssetId(entry, libraryId);
  originCopy.assetId = assetId;

  return {
    id: assetId,
    libraryId,
    kind: 'mcp',
    name: entry.name,
    description: entry.description,
    category: entry.category || entry.subdomain || 'MCP',
    domain: entry.domain,
    subdomain: entry.subdomain,
    icon: entry.icon || '🔌',
    bundledInProduct: true,
    origin: originCopy,
    qualityScore: entry.qualityScore,
    selectionScore: entry.selectionScore,
    version: entry.version || '1.0.0',
    license: entry.license || originCopy.license || 'MIT',
    status: 'available',
    provenance: {
      sourceKind: 'builtin',
      sourceId: libraryId,
      repo: originCopy.repo,
      path: originCopy.path,
      url: originCopy.url,
      authority: 'Arestrix',
      importedAt: BUNDLED_MCP_CATALOG_GENERATED_AT
    },
    audit: {
      verdict: entry.audit.verdict,
      riskScore: entry.audit.riskScore,
      policyVersion: entry.audit.policyVersion || 'mcp-scanner-v1',
      auditedAt: entry.audit.auditedAt || BUNDLED_MCP_CATALOG_GENERATED_AT,
      hash: originCopy.installHash,
      findings: [...(entry.audit.findings || [])],
      warnings: [...(entry.audit.warnings || [])]
    },
    payload: payloadCopy,
    createdAt: BUNDLED_MCP_CATALOG_GENERATED_AT,
    updatedAt: BUNDLED_MCP_CATALOG_GENERATED_AT
  };
}

export function getBundledMcpAssets(libraryId: string = DEFAULT_LIBRARY_ID): ILibraryAsset[] {
  return BUNDLED_MCP_SERVERS.map(entry => bundledMcpToLibraryAsset(entry, libraryId));
}

let cachedDefaultSkillAssets: ILibraryAsset[] | null = null;
const skillByRawId = new Map<string, IGitHubSkillItem>();
for (const skill of BUILTIN_LIBRARY_SKILLS) {
  skillByRawId.set(skill.id, skill);
}

function buildBuiltinSkillAsset(item: IGitHubSkillItem, libraryId: string): ILibraryAsset {
  const gate = LibraryAuditGate.getInstance();
  const audit = gate.auditSkill({
    id: item.id,
    name: item.name,
    description: item.description,
    markdownContent: item.markdownContent,
    systemPrompt: item.systemPrompt,
    tools: item.tools,
    license: item.license
  });
  const asset = builtinSkillToAsset(item, libraryId, audit);
  asset.bundledInProduct = true;
  asset.provenance.importedAt = BUNDLED_AGENT_CATALOG_GENERATED_AT;
  asset.createdAt = BUNDLED_AGENT_CATALOG_GENERATED_AT;
  asset.updatedAt = BUNDLED_AGENT_CATALOG_GENERATED_AT;
  return asset;
}

export function getBundledSkillAssets(libraryId: string = DEFAULT_LIBRARY_ID): ILibraryAsset[] {
  if (libraryId === DEFAULT_LIBRARY_ID) {
    if (!cachedDefaultSkillAssets) {
      cachedDefaultSkillAssets = BUILTIN_LIBRARY_SKILLS.map(item => buildBuiltinSkillAsset(item, DEFAULT_LIBRARY_ID));
    }
    return cachedDefaultSkillAssets.map(asset => cloneData(asset));
  }
  return BUILTIN_LIBRARY_SKILLS.map(item => buildBuiltinSkillAsset(item, libraryId));
}

export function getBundledFactoryAssets(libraryId: string = DEFAULT_LIBRARY_ID): ILibraryAsset[] {
  return [
    ...getBundledSkillAssets(libraryId),
    ...getBundledAgentAssets(libraryId),
    ...getBundledMcpAssets(libraryId)
  ];
}

export function getBundledFactoryAssetById(
  idOrTemplateId: string,
  libraryId: string = DEFAULT_LIBRARY_ID
): ILibraryAsset | null {
  if (!idOrTemplateId) return null;

  const skillAssets = getBundledSkillAssets(libraryId);
  const directSkill = skillAssets.find(asset =>
    asset.id === idOrTemplateId ||
    asset.id === skillAssetId(libraryId, idOrTemplateId)
  );
  if (directSkill) return directSkill;

  const entry = getBundledAgentVertexById(idOrTemplateId);
  if (entry) return bundledAgentVertexToLibraryAsset(entry, libraryId);

  const mcpEntry = getBundledMcpServerById(idOrTemplateId);
  if (mcpEntry) return bundledMcpToLibraryAsset(mcpEntry, libraryId);

  return null;
}

function getFactoryIdentityMap(): Map<string, { hash: string; name: string; version: string }> {
  const map = new Map<string, { hash: string; name: string; version: string }>();
  for (const skill of getBundledSkillAssets(DEFAULT_LIBRARY_ID)) {
    map.set(skill.id, { hash: skill.audit.hash, name: skill.name, version: skill.version });
  }
  for (const entry of BUNDLED_AGENT_VERTICES) {
    map.set(entry.id, {
      hash: entry.origin.installHash,
      name: entry.name,
      version: entry.origin.installVersion || entry.template.version || '1.0.0'
    });
  }
  for (const entry of BUNDLED_MCP_SERVERS) {
    map.set(entry.id, {
      hash: entry.origin.installHash,
      name: entry.name,
      version: entry.version || '1.0.0'
    });
  }
  return map;
}

let cachedFactoryIdentityMap: Map<string, { hash: string; name: string; version: string }> | null = null;

function factoryIdentityMap(): Map<string, { hash: string; name: string; version: string }> {
  if (!cachedFactoryIdentityMap) {
    cachedFactoryIdentityMap = getFactoryIdentityMap();
  }
  return cachedFactoryIdentityMap;
}

export function isUnmodifiedFactoryBundledAsset(asset: ILibraryAsset): boolean {
  if (!asset.bundledInProduct || asset.libraryId !== DEFAULT_LIBRARY_ID || asset.derivedFrom) {
    return false;
  }
  const factory = factoryIdentityMap().get(asset.id);
  if (!factory) return false;
  return (
    asset.status === 'available' &&
    asset.audit?.hash === factory.hash &&
    asset.name === factory.name &&
    asset.version === factory.version
  );
}

/**
 * Compacta um snapshot de biblioteca para persistência em `localStorage`/Workspace,
 * removendo payloads completos dos assets embutidos de fábrica sem modificar
 * assets customizados, derivados ou alterados pelo usuário.
 */
export function compactLibraryStateForStorage(state: ILibraryState): ILibraryState {
  const identityMap = factoryIdentityMap();
  const compactedAssets: ILibraryAsset[] = [];

  for (const asset of state.assets || []) {
    if (asset.bundledInProduct && asset.libraryId === DEFAULT_LIBRARY_ID && !asset.derivedFrom) {
      const factory = identityMap.get(asset.id);
      if (
        factory &&
        asset.audit?.hash === factory.hash &&
        asset.name === factory.name &&
        asset.version === factory.version
      ) {
        // Asset de fábrica não modificado: não precisa gravar payload (~6 MB total).
        if (asset.status === 'available') {
          continue;
        }
        // Apenas status alterado (installed/pinned/disabled): grava stub leve sem payload pesado.
        compactedAssets.push({
          id: asset.id,
          libraryId: asset.libraryId,
          kind: asset.kind,
          name: asset.name,
          description: '',
          category: asset.category,
          version: asset.version,
          license: asset.license,
          status: asset.status,
          bundledInProduct: true,
          provenance: {
            sourceKind: 'builtin',
            sourceId: asset.libraryId,
            importedAt: asset.provenance?.importedAt || BUNDLED_AGENT_CATALOG_GENERATED_AT
          },
          audit: {
            verdict: asset.audit.verdict,
            riskScore: asset.audit.riskScore,
            policyVersion: asset.audit.policyVersion,
            hash: factory.hash,
            findings: [],
            warnings: []
          },
          payload: asset.kind === 'skill'
            ? { kind: 'skill', markdownContent: '', systemPrompt: '', tools: [] }
            : asset.kind === 'mcp'
              ? { kind: 'mcp', transport: 'stdio', command: '', args: [], url: '', cachedTools: [] }
              : {
                  kind: 'agent-template',
                  template: {
                    id: asset.origin?.templateId || '',
                    name: asset.name,
                    taskType: 'agent',
                    description: '',
                    inputs: [],
                    outputs: [],
                    lambdaBodyTemplate: ''
                  }
                },
          createdAt: asset.createdAt,
          updatedAt: asset.updatedAt
        });
        continue;
      }
    }
    compactedAssets.push({ ...asset });
  }

  return {
    ...state,
    libraries: (state.libraries || []).map(lib => ({ ...lib })),
    assets: compactedAssets,
    sources: state.sources ? state.sources.map(src => ({ ...src })) : undefined
  };
}
