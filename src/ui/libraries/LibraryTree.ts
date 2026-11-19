import { globalEventBus } from '../../core/EventBus.ts';
import type { Graph } from '../../core/Graph.ts';
import { AddNodeCommand } from '../../core/history/GraphCommands.ts';
import { LibraryRegistry } from '../../core/libraries/LibraryRegistry.ts';
import { LibraryService } from '../../core/libraries/LibraryService.ts';
import type { ILibraryAsset, ILibraryEntry, LibraryAssetKind, LibraryAuditVerdict, LibraryScope } from '../../core/libraries/types.ts';
import type { INodeData, IPort } from '../../core/types.ts';
import { t } from '../../i18n/index.ts';

export interface ILibraryBrowserOptions {
  registry: LibraryRegistry;
  /** Grafo ativo: usado por "Inserir asset" e "Editar" para criar vértices. */
  graph?: Graph;
  /** Quando omitido, o navegador lista as bibliotecas de todos os escopos. */
  scope?: LibraryScope;
  /** Callback para fechar a Biblioteca / Backstage e retornar à tela principal do Studio. */
  onReturnToStudio?: () => void;
}

interface ILibraryBrowserState {
  selectedLibraryId?: string;
  selectedAssetId?: string;
  search: string;
  kind: 'all' | 'skill' | 'mcp' | 'agent-template' | 'pipeline';
  /** Bibliotecas cujas árvores estão abertas (ex.: Biblioteca Arestrix, Minha biblioteca). */
  expandedLibraries: Set<string>;
  /** Bibliotecas que já receberam expansão inicial dos domínios. */
  seededLibraries: Set<string>;
  /** Chaves dos nós da ontologia (libraryId + domínio e libraryId + domínio + subdomínio) abertos. */
  expandedGroups: Set<string>;
}

export type LibraryTreeContextTarget =
  | { kind: 'library'; libraryId: string }
  | { kind: 'domain'; libraryId: string; domain: string }
  | { kind: 'subdomain'; libraryId: string; domain: string; subdomain: string };

export type LibraryTreeContextAction = 'expand-all' | 'collapse-all';

interface IAssetSubdomainGroup {
  name: string;
  assets: ILibraryAsset[];
}

interface IAssetDomainGroup {
  name: string;
  total: number;
  subdomains: IAssetSubdomainGroup[];
}

type RestoreCapableLibraryService = LibraryService & {
  getInstallDefaultAsset?: (assetId: string) => ILibraryAsset | undefined;
  restoreBuiltinAsset?: (assetId: string) => Promise<ILibraryAsset | boolean | { ok: boolean; asset?: ILibraryAsset; reason?: string } | undefined>;
  resetBuiltinLibrary?: () => Promise<{ restored?: number; imported?: ILibraryAsset[]; ok?: boolean } | number | void>;
};

/**
 * Escopo efetivo de uma biblioteca. Necessário porque a página do Backstage
 * exibe todos os escopos ao mesmo tempo e as ações de importação/clonagem
 * precisam gravar no escopo da biblioteca selecionada.
 */
function libraryScope(options: ILibraryBrowserOptions, libraryId?: string): LibraryScope {
  if (libraryId) {
    const library = options.registry.list().find(entry => entry.id === libraryId);
    if (library?.isBuiltin || library?.scope === 'studio') return 'studio';
    if (library?.scope) return library.scope;
  }
  return options.scope ?? 'workspace';
}

const stateByRoot = new WeakMap<HTMLElement, ILibraryBrowserState>();

function esc(value: unknown): string {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
}

function lbl(key: string, fallback: string): string {
  return t(`properties_window.libraries.${key}`, fallback);
}

function lblv(key: string, fallback: string, vars: Record<string, unknown>): string {
  return t(`properties_window.libraries.${key}`, fallback, vars);
}

function badge(text: string, tone: 'neutral' | 'success' | 'warning' | 'accent' | 'danger'): string {
  return `<span class="properties-badge properties-badge--${tone}">${esc(text)}</span>`;
}

function verdictTone(verdict: LibraryAuditVerdict): 'neutral' | 'success' | 'warning' | 'danger' {
  if (verdict === 'APPROVE') return 'success';
  if (verdict === 'CAUTION') return 'warning';
  if (verdict === 'REJECT') return 'danger';
  return 'neutral';
}

function verdictLabel(verdict: LibraryAuditVerdict): string {
  return lbl(`audit_${verdict.toLowerCase()}`, verdict);
}

function kindLabel(kind: LibraryAssetKind): string {
  if (kind === 'skill') return lbl('kind_skill', 'Skill');
  if (kind === 'mcp') return lbl('kind_mcp', 'MCP');
  if (kind === 'agent-template') return lbl('kind_agent_template', 'Template de agente');
  if (kind === 'pipeline') return lbl('kind_pipeline', 'Pipeline');
  return lbl('kind_graph_template', 'Template de grafo');
}

function statusLabel(status: ILibraryAsset['status']): string {
  return lbl(`status_${status}`, status);
}

function assetDomain(asset: ILibraryAsset): string {
  if (asset.domain) return asset.domain.trim();
  if (asset.payload.kind === 'agent-template' || asset.payload.kind === 'pipeline') {
    return (asset.payload.template.domain || '').trim();
  }
  return '';
}

function assetSubdomain(asset: ILibraryAsset): string {
  if (asset.subdomain) return asset.subdomain.trim();
  if (asset.payload.kind === 'agent-template' || asset.payload.kind === 'pipeline') {
    return (asset.payload.template.subdomain || '').trim();
  }
  return '';
}

function isBundledAsset(asset: ILibraryAsset): boolean {
  return Boolean(asset.bundledInProduct || asset.origin?.bundledInProduct);
}

function bundledOriginText(asset: ILibraryAsset): string {
  const repo = asset.origin?.repo || asset.provenance.repo || '';
  const license = asset.origin?.license || asset.license || '';
  return [repo, license].filter(Boolean).join(' · ');
}

function safeHttpUrl(raw?: string): string {
  return raw && /^https?:\/\//i.test(raw) ? raw : '';
}

export function isBundledAssetModified(asset: ILibraryAsset, service: RestoreCapableLibraryService): boolean {
  // "Restaurar padrão de instalação" só existe para ativos de fábrica da
  // Biblioteca Arestrix. Cópias locais/derivadas (mesmo com origin copiado)
  // não têm padrão de instalação para restaurar.
  if (!asset.bundledInProduct || asset.libraryId !== 'library-arestrix' || asset.derivedFrom) {
    return false;
  }
  const installDefault = service.getInstallDefaultAsset?.(asset.id);
  if (installDefault) {
    return installDefault.audit.hash !== asset.audit.hash || installDefault.version !== asset.version;
  }
  return Boolean(asset.origin?.installHash && asset.audit.hash !== asset.origin.installHash);
}

/** Bibliotecas pequenas abrem a ontologia inteira; catálogos grandes abrem só os domínios. */
const AUTO_EXPAND_ASSET_LIMIT = 60;
const GROUP_KEY_SEPARATOR = '\u001f';

function compareGroupNames(a: string, b: string): number {
  if (!a) return b ? 1 : 0;
  if (!b) return -1;
  return a.localeCompare(b);
}

/** Chave estável de um nó da ontologia por biblioteca: libraryId + domínio ou + subdomínio. */
function groupKey(libraryId: string, domain: string, subdomain?: string): string {
  return subdomain === undefined
    ? `${libraryId}${GROUP_KEY_SEPARATOR}${domain}`
    : `${libraryId}${GROUP_KEY_SEPARATOR}${domain}${GROUP_KEY_SEPARATOR}${subdomain}`;
}

function parseGroupKey(key: string): { libraryId: string; domain: string; subdomain?: string } {
  const parts = key.split(GROUP_KEY_SEPARATOR);
  if (parts.length >= 3) {
    return { libraryId: parts[0], domain: parts[1], subdomain: parts.slice(2).join(GROUP_KEY_SEPARATOR) };
  }
  return { libraryId: parts[0] || '', domain: parts[1] || '' };
}

function groupKeyAttribute(key: string): string {
  return encodeURIComponent(key);
}

function readGroupKey(value: string | undefined): string {
  if (!value) return '';
  try {
    return decodeURIComponent(value);
  } catch {
    return value;
  }
}

function groupAssetsByTaxonomy(assets: ILibraryAsset[]): IAssetDomainGroup[] {
  const byDomain = new Map<string, Map<string, ILibraryAsset[]>>();
  for (const asset of assets) {
    const domain = assetDomain(asset);
    if (!byDomain.has(domain)) byDomain.set(domain, new Map());
    const bySubdomain = byDomain.get(domain)!;
    const subdomain = assetSubdomain(asset);
    if (!bySubdomain.has(subdomain)) bySubdomain.set(subdomain, []);
    bySubdomain.get(subdomain)!.push(asset);
  }

  return Array.from(byDomain.entries())
    .sort(([a], [b]) => compareGroupNames(a, b))
    .map(([domain, subdomains]) => {
      const grouped = Array.from(subdomains.entries())
        .sort(([a], [b]) => compareGroupNames(a, b))
        .map(([name, list]) => ({ name, assets: list }));
      return {
        name: domain,
        total: grouped.reduce((sum, group) => sum + group.assets.length, 0),
        subdomains: grouped
      };
    });
}

function expandGroups(state: ILibraryBrowserState, libraryId: string, groups: IAssetDomainGroup[]): void {
  for (const group of groups) {
    state.expandedGroups.add(groupKey(libraryId, group.name));
    for (const subdomain of group.subdomains) {
      state.expandedGroups.add(groupKey(libraryId, group.name, subdomain.name));
    }
  }
}

function collapseLibraryGroups(state: ILibraryBrowserState, libraryId: string): void {
  const prefix = `${libraryId}${GROUP_KEY_SEPARATOR}`;
  for (const key of Array.from(state.expandedGroups)) {
    if (key.startsWith(prefix)) state.expandedGroups.delete(key);
  }
}

function seedExpandedGroups(state: ILibraryBrowserState, libraryId: string, assets: ILibraryAsset[]): void {
  collapseLibraryGroups(state, libraryId);
  const groups = groupAssetsByTaxonomy(assets);
  for (const group of groups) {
    state.expandedGroups.add(groupKey(libraryId, group.name));
  }
  if (assets.length <= AUTO_EXPAND_ASSET_LIMIT) {
    expandGroups(state, libraryId, groups);
  }
  state.seededLibraries.add(libraryId);
}

function toggleGroup(state: ILibraryBrowserState, key: string): void {
  if (state.expandedGroups.has(key)) {
    state.expandedGroups.delete(key);
  } else {
    state.expandedGroups.add(key);
  }
}

export function getTreeNodeContextActions(
  target: LibraryTreeContextTarget,
  groups: IAssetDomainGroup[],
  state: Pick<ILibraryBrowserState, 'expandedLibraries' | 'expandedGroups'>
): LibraryTreeContextAction[] {
  if (target.kind === 'library') {
    if (groups.length === 0) return [];
    const isOpen = state.expandedLibraries.has(target.libraryId);
    const allExpanded = isOpen && groups.every(group =>
      state.expandedGroups.has(groupKey(target.libraryId, group.name)) &&
      group.subdomains.every(sub => state.expandedGroups.has(groupKey(target.libraryId, group.name, sub.name)))
    );
    const actions: LibraryTreeContextAction[] = [];
    if (!allExpanded) actions.push('expand-all');
    if (isOpen) actions.push('collapse-all');
    return actions;
  }

  const group = groups.find(g => g.name === target.domain);
  if (!group) return [];

  if (target.kind === 'domain') {
    if (group.subdomains.length === 0) return [];
    const domainKey = groupKey(target.libraryId, group.name);
    const isOpen = state.expandedGroups.has(domainKey);
    const allExpanded = isOpen && group.subdomains.every(sub =>
      state.expandedGroups.has(groupKey(target.libraryId, group.name, sub.name))
    );
    const actions: LibraryTreeContextAction[] = [];
    if (!allExpanded) actions.push('expand-all');
    if (isOpen) actions.push('collapse-all');
    return actions;
  }

  const sub = group.subdomains.find(s => s.name === target.subdomain);
  if (!sub || sub.assets.length === 0) return [];
  const subKey = groupKey(target.libraryId, group.name, sub.name);
  return state.expandedGroups.has(subKey) ? ['collapse-all'] : ['expand-all'];
}

export function applyTreeNodeContextAction(
  target: LibraryTreeContextTarget,
  action: LibraryTreeContextAction,
  groups: IAssetDomainGroup[],
  state: Pick<ILibraryBrowserState, 'expandedLibraries' | 'seededLibraries' | 'expandedGroups'>
): void {
  if (target.kind === 'library') {
    state.seededLibraries.add(target.libraryId);
    if (action === 'expand-all') {
      state.expandedLibraries.add(target.libraryId);
      expandGroups(state as ILibraryBrowserState, target.libraryId, groups);
    } else {
      collapseLibraryGroups(state as ILibraryBrowserState, target.libraryId);
      state.expandedLibraries.delete(target.libraryId);
    }
    return;
  }

  const group = groups.find(g => g.name === target.domain);
  if (!group) return;

  if (target.kind === 'domain') {
    const domainKey = groupKey(target.libraryId, group.name);
    if (action === 'expand-all') {
      state.expandedLibraries.add(target.libraryId);
      state.expandedGroups.add(domainKey);
      for (const sub of group.subdomains) {
        state.expandedGroups.add(groupKey(target.libraryId, group.name, sub.name));
      }
    } else {
      for (const sub of group.subdomains) {
        state.expandedGroups.delete(groupKey(target.libraryId, group.name, sub.name));
      }
      state.expandedGroups.delete(domainKey);
    }
    return;
  }

  const subKey = groupKey(target.libraryId, target.domain, target.subdomain);
  if (action === 'expand-all') {
    state.expandedLibraries.add(target.libraryId);
    state.expandedGroups.add(groupKey(target.libraryId, target.domain));
    state.expandedGroups.add(subKey);
  } else {
    state.expandedGroups.delete(subKey);
  }
}

function defaultState(options: ILibraryBrowserOptions, registry: LibraryRegistry): ILibraryBrowserState {
  const libraries = registry.list(options.scope);
  const first = options.scope === 'workspace'
    ? (libraries.find(library => !library.isBuiltin) || libraries[0])
    : libraries[0];
  const state: ILibraryBrowserState = {
    selectedLibraryId: first?.id,
    selectedAssetId: undefined,
    search: '',
    kind: 'all',
    expandedLibraries: new Set<string>(),
    seededLibraries: new Set<string>(),
    expandedGroups: new Set<string>()
  };
  if (state.selectedLibraryId) {
    state.expandedLibraries.add(state.selectedLibraryId);
    seedExpandedGroups(state, state.selectedLibraryId, registry.listAssets(state.selectedLibraryId));
  }
  return state;
}

function filteredAssets(registry: LibraryRegistry, libraryId: string, state: ILibraryBrowserState): ILibraryAsset[] {
  const query = state.search.trim().toLowerCase();
  return registry.listAssets(libraryId)
    .filter(asset => state.kind === 'all' || asset.kind === state.kind)
    .filter(asset => !query
      || asset.name.toLowerCase().includes(query)
      || asset.description.toLowerCase().includes(query)
      || asset.category.toLowerCase().includes(query)
      || assetDomain(asset).toLowerCase().includes(query)
      || assetSubdomain(asset).toLowerCase().includes(query)
      || (asset.origin?.repo || '').toLowerCase().includes(query))
    .sort((a, b) => {
      const rank = (k: LibraryAssetKind): number => (k === 'skill' ? 0 : k === 'mcp' ? 1 : k === 'pipeline' ? 2 : 3);
      const kindDelta = rank(a.kind) - rank(b.kind);
      return kindDelta !== 0 ? kindDelta : a.name.localeCompare(b.name);
    });
}

function renderAssetRow(asset: ILibraryAsset, libraryId: string, state: ILibraryBrowserState): string {
  const bundled = isBundledAsset(asset);
  return `
    <button type="button" class="properties-library-asset ${asset.id === state.selectedAssetId ? 'active' : ''}" data-asset-select="${esc(asset.id)}" data-library-id="${esc(libraryId)}" title="${esc(asset.name)}">
      <span class="properties-library-asset-name">${esc(asset.name)}</span>
      ${bundled ? badge(lbl('badge_bundled', 'Nativo Arestrix'), 'success') : `<span class="properties-library-asset-meta">${esc(kindLabel(asset.kind))}</span>`}
      ${badge(verdictLabel(asset.audit.verdict), verdictTone(asset.audit.verdict))}
    </button>
  `;
}

function renderTaxonomyToggle(params: {
  attribute: 'data-library-domain-toggle' | 'data-library-subdomain-toggle';
  libraryId: string;
  key: string;
  label: string;
  count: number;
  expanded: boolean;
  level: 'domain' | 'subdomain';
}): string {
  const toggleClass = params.level === 'domain'
    ? 'properties-library-domain-toggle'
    : 'properties-library-subdomain-toggle';
  return `
    <button type="button" class="properties-library-taxon-toggle ${toggleClass}" ${params.attribute}="${esc(groupKeyAttribute(params.key))}" data-library-id="${esc(params.libraryId)}" aria-expanded="${params.expanded ? 'true' : 'false'}" title="${esc(`${params.label} (${params.count})`)}">
      <span class="properties-library-chevron" aria-hidden="true">${params.expanded ? '▾' : '▸'}</span>
      <span class="properties-library-taxon-name">${esc(params.label)}</span>
      <span class="properties-library-item-count">${params.count}</span>
    </button>
  `;
}

function renderAssetTree(libraryId: string, assets: ILibraryAsset[], state: ILibraryBrowserState): string {
  if (assets.length === 0) {
    return `<div class="properties-library-assets-empty">${esc(lbl('tree_empty_assets', 'Nenhum asset. Importe um catálogo ou crie um derivado.'))}</div>`;
  }

  return groupAssetsByTaxonomy(assets).map(group => {
    const domainKey = groupKey(libraryId, group.name);
    const domainExpanded = state.expandedGroups.has(domainKey);
    return `
      <div class="properties-library-taxon">
        ${renderTaxonomyToggle({
          attribute: 'data-library-domain-toggle',
          libraryId,
          key: domainKey,
          label: group.name || lbl('tree_uncategorized_domain', 'Sem domínio'),
          count: group.total,
          expanded: domainExpanded,
          level: 'domain'
        })}
        ${domainExpanded ? `
          <div class="properties-library-subtree">
            ${group.subdomains.map(subdomain => {
              const subdomainKey = groupKey(libraryId, group.name, subdomain.name);
              const subdomainExpanded = state.expandedGroups.has(subdomainKey);
              return `
                <div class="properties-library-taxon">
                  ${renderTaxonomyToggle({
                    attribute: 'data-library-subdomain-toggle',
                    libraryId,
                    key: subdomainKey,
                    label: subdomain.name || lbl('tree_uncategorized_subdomain', 'Sem subdomínio'),
                    count: subdomain.assets.length,
                    expanded: subdomainExpanded,
                    level: 'subdomain'
                  })}
                  ${subdomainExpanded ? `
                    <div class="properties-library-assets">
                      ${subdomain.assets.map(asset => renderAssetRow(asset, libraryId, state)).join('')}
                    </div>
                  ` : ''}
                </div>
              `;
            }).join('')}
          </div>
        ` : ''}
      </div>
    `;
  }).join('');
}

function renderTree(registry: LibraryRegistry, options: ILibraryBrowserOptions, state: ILibraryBrowserState): string {
  const libraries = registry.list(options.scope);
  if (libraries.length === 0) {
    return `<div class="properties-library-empty">${esc(lbl('tree_empty_libraries', 'Nenhuma biblioteca disponível.'))}</div>`;
  }

  if (!state.selectedLibraryId || !libraries.some(library => library.id === state.selectedLibraryId)) {
    state.selectedLibraryId = libraries[0].id;
    state.selectedAssetId = undefined;
  }

  return libraries.map(library => {
    const assets = filteredAssets(registry, library.id, state);
    const total = registry.listAssets(library.id).length;
    const isSelected = library.id === state.selectedLibraryId;
    const isExpanded = state.expandedLibraries.has(library.id);
    const isBuiltin = library.isBuiltin;
    const kind = isBuiltin ? lbl('kind_builtin', 'Embutida') : library.kind === 'repository' ? lbl('kind_repository', 'Repositório') : lbl('kind_local', 'Local');

    return `
      <div class="properties-library-group">
        <button type="button" class="properties-library-item ${isSelected ? 'active' : ''}" data-library-select="${esc(library.id)}" aria-expanded="${isExpanded ? 'true' : 'false'}">
          <span class="properties-library-chevron" aria-hidden="true">${isExpanded ? '▾' : '▸'}</span>
          <span class="properties-library-item-name">${esc(library.name)}</span>
          ${badge(kind, isBuiltin ? 'success' : 'neutral')}
          <span class="properties-library-item-count">${total}</span>
        </button>
        ${isExpanded ? `
          <div class="properties-library-tree-children">
            ${renderAssetTree(library.id, assets, state)}
          </div>
        ` : ''}
      </div>
    `;
  }).join('');
}

function renderAssetDetail(asset: ILibraryAsset, library: ILibraryEntry | undefined): string {
  const service = LibraryService.getInstance() as RestoreCapableLibraryService;
  const hasContent = asset.payload.kind === 'skill'
    ? asset.payload.markdownContent.length > 0
    : asset.payload.kind === 'mcp'
      ? Boolean(asset.payload.command || asset.payload.url || (asset.payload.args && asset.payload.args.length > 0))
      : true;
  const showDownload = Boolean(asset.remote) && (asset.audit.verdict === 'PENDING' || !hasContent || asset.status === 'disabled');
  const installDisabled = !hasContent || asset.audit.verdict === 'REJECT' || asset.audit.verdict === 'PENDING';
  const canInstall = asset.payload.kind === 'skill' || asset.payload.kind === 'mcp';
  const bundled = isBundledAsset(asset);
  const bundledOrigin = bundledOriginText(asset);
  const originUrl = safeHttpUrl(asset.origin?.url || asset.provenance.url);
  const originText = bundled && bundledOrigin
    ? bundledOrigin
    : [asset.provenance.sourceKind, asset.provenance.authority, asset.provenance.sourceId]
      .filter(Boolean)
      .join(' · ');
  // Dominio e subdominio ficam apenas na arvore; o detalhe mostra so a biblioteca.
  const breadcrumb = library?.name || '';
  const canRestoreBuiltin = isBundledAssetModified(asset, service);
  const riskScore = Math.max(0, Math.min(100, Number(asset.audit.riskScore) || 0));
  const tone = verdictTone(asset.audit.verdict);
  const auditModifier = tone === 'success' ? 'approve' : tone === 'warning' ? 'caution' : tone === 'danger' ? 'reject' : 'pending';
  const originHtml = originUrl
    ? `<a class="properties-library-origin-link" href="${esc(originUrl)}" target="_blank" rel="noopener noreferrer" title="${esc(originText || originUrl)}">${esc(originText || originUrl)}</a>`
    : `<span class="properties-library-origin-text" title="${esc(originText || '—')}">${esc(originText || '—')}</span>`;
  const findings = asset.audit.findings.length + asset.audit.warnings.length > 0
    ? `<ul class="properties-library-findings">
        ${asset.audit.findings.map(finding => `<li>${esc(finding)}</li>`).join('')}
        ${asset.audit.warnings.map(warning => `<li class="is-warning">${esc(warning)}</li>`).join('')}
      </ul>`
    : `<p class="properties-library-audit-hint">${esc(asset.audit.verdict === 'PENDING'
        ? lbl('audit_pending_download', 'Baixe o conteúdo para auditar antes de usar.')
        : lbl('audit_clean', 'Nenhum achado ou alerta de segurança.'))}</p>`;

  return `
    <header class="properties-library-detail-header">
      <div class="properties-library-detail-heading">
        <div class="properties-library-detail-title">${esc(asset.name)}</div>
        <div class="properties-library-detail-subtitle">
          ${bundled ? badge(lbl('badge_bundled', 'Nativo Arestrix'), 'success') : ''}
          ${badge(kindLabel(asset.kind), 'accent')}
          ${badge(statusLabel(asset.status), asset.status === 'installed' ? 'success' : 'neutral')}
          ${asset.remote ? badge(lbl('badge_remote', 'Remoto'), 'warning') : ''}
        </div>
        <div class="properties-library-detail-breadcrumb">${esc(breadcrumb)}</div>
      </div>
    </header>

    <p class="properties-library-description" title="${esc(asset.description)}">${esc(asset.description)}</p>

    <div class="properties-library-facts">
      <div class="properties-library-fact-line">
        <span class="properties-library-fact"><span class="properties-library-fact-label">${esc(lbl('meta_version', 'Versão'))}</span>${esc(asset.version || '—')}</span>
        <span class="properties-library-fact"><span class="properties-library-fact-label">${esc(lbl('meta_license', 'Licença'))}</span>${esc(asset.license || '—')}</span>
        <span class="properties-library-fact"><span class="properties-library-fact-label">${esc(lbl('meta_category', 'Categoria'))}</span>${esc(asset.category || '—')}</span>
      </div>
      <div class="properties-library-fact-line">
        <span class="properties-library-fact"><span class="properties-library-fact-label">${esc(lbl('meta_policy', 'Política'))}</span><code class="properties-code">${esc(asset.audit.policyVersion || '—')}</code></span>
        <span class="properties-library-fact properties-library-fact--origin"><span class="properties-library-fact-label">${esc(lbl('meta_origin', 'Origem'))}</span>${originHtml}</span>
      </div>
      ${asset.derivedFrom ? `
        <div class="properties-library-fact-line properties-library-fact-line--muted">
          <span>${esc(lbl('meta_derived_from', 'Derivado de'))} ${esc(asset.derivedFrom.originName)}</span>
        </div>
      ` : ''}
    </div>

    <section class="properties-library-audit properties-library-audit--${auditModifier}" aria-label="${esc(lbl('audit_title', 'Auditoria'))}">
      <div class="properties-library-audit-head">
        <div class="properties-library-audit-title">${esc(lbl('audit_title', 'Auditoria'))}</div>
        <div class="properties-library-audit-metrics">
          ${badge(verdictLabel(asset.audit.verdict), tone)}
          <span class="properties-library-risk" title="${esc(lbl('audit_risk', 'Risco'))}: ${riskScore}/100">
            <span class="properties-library-risk-meter"><span class="properties-library-risk-fill" style="width:${riskScore}%"></span></span>
            <span class="properties-library-risk-label"><strong>${riskScore}</strong>/100</span>
          </span>
          ${asset.audit.auditedAt
            ? `<span class="properties-library-audit-date">${esc(lbl('meta_audited_at', 'Auditado em'))} ${esc(new Date(asset.audit.auditedAt).toLocaleString())}</span>`
            : ''}
        </div>
      </div>
      ${findings}
    </section>

    <div class="properties-library-actions">
      ${showDownload ? `<button type="button" class="btn btn-primary-office" data-asset-action="download" data-asset-id="${esc(asset.id)}">${esc(lbl('action_download', 'Baixar e auditar'))}</button>` : ''}
      ${!showDownload && asset.audit.verdict !== 'REJECT' ? `<button type="button" class="btn btn-primary-office" data-asset-action="insert-asset" data-asset-id="${esc(asset.id)}">${esc(lbl('action_insert_asset', 'Inserir asset'))}</button>` : ''}
      ${canInstall ? `<button type="button" class="btn btn-secondary-office" data-asset-action="install" data-asset-id="${esc(asset.id)}" ${installDisabled ? 'disabled' : ''}>${esc(lbl('action_install', 'Instalar no Workspace'))}</button>` : ''}
      <button type="button" class="btn btn-secondary-office" data-asset-action="audit" data-asset-id="${esc(asset.id)}">${esc(lbl('action_audit', 'Reauditar'))}</button>
      <button type="button" class="btn btn-secondary-office" data-asset-action="edit" data-asset-id="${esc(asset.id)}">${esc(lbl('action_derive', 'Editar'))}</button>
      ${canRestoreBuiltin ? `<button type="button" class="btn btn-secondary-office" data-asset-action="restore-builtin" data-asset-id="${esc(asset.id)}">${esc(lbl('action_restore_builtin', 'Restaurar padrão de instalação'))}</button>` : ''}
      ${!(library?.isBuiltin || asset.libraryId === 'library-arestrix') ? `<button type="button" class="btn btn-secondary-office properties-library-action-danger" data-asset-action="remove" data-asset-id="${esc(asset.id)}">${esc(lbl('action_remove_asset', 'Remover asset'))}</button>` : ''}
    </div>
  `;
}

function renderLibraryDetail(
  registry: LibraryRegistry,
  options: ILibraryBrowserOptions,
  state: ILibraryBrowserState
): string {
  const libraries = registry.list(options.scope);
  const library = libraries.find(item => item.id === state.selectedLibraryId) || libraries[0];
  if (!library) {
    return `<div class="properties-library-empty">${esc(lbl('detail_no_library', 'Selecione uma biblioteca para ver os detalhes.'))}</div>`;
  }
  state.selectedLibraryId = library.id;

  const assets = registry.listAssets(library.id);
  if (state.selectedAssetId) {
    const asset = assets.find(item => item.id === state.selectedAssetId);
    if (asset) return renderAssetDetail(asset, library);
    state.selectedAssetId = undefined;
  }

  const skills = assets.filter(asset => asset.kind === 'skill').length;
  const mcps = assets.filter(asset => asset.kind === 'mcp').length;
  const approved = assets.filter(asset => asset.audit.verdict === 'APPROVE').length;
  const source = library.sourceId ? registry.getSource(library.sourceId) : undefined;
  const isGitHubSource = source?.kind === 'github' && Boolean(source.github);
  const importLabel = libraryScope(options, library.id) === 'studio'
    ? lbl('action_import_builtin', 'Importar catálogo embutido')
    : lbl('action_import_workspace', 'Importar catálogo do Workspace');

  return `
    <header class="properties-library-detail-header">
      <div class="properties-library-detail-heading">
        <div class="properties-library-detail-title">${esc(library.name)}</div>
        <div class="properties-library-detail-subtitle">
          ${badge(
            library.isBuiltin
              ? lbl('kind_builtin', 'Embutida')
              : library.kind === 'repository'
                ? lbl('kind_repository', 'Repositório')
                : lbl('kind_local', 'Local'),
            library.isBuiltin ? 'success' : library.kind === 'repository' ? 'warning' : 'neutral'
          )}
          ${badge(library.scope === 'studio' ? lbl('scope_studio', 'Studio') : lbl('scope_workspace', 'Workspace'), 'accent')}
        </div>
      </div>
    </header>
    <p class="properties-library-description">${esc(library.description)}</p>
    ${isGitHubSource ? `
      <div class="properties-library-facts">
        <div class="properties-library-fact-line">
          <span class="properties-library-fact"><span class="properties-library-fact-label">${esc(lbl('source_repo', 'Repositório'))}</span><code class="properties-code">${esc(source!.github!.repo)}</code></span>
          <span class="properties-library-fact"><span class="properties-library-fact-label">${esc(lbl('source_ref', 'Referência'))}</span>${esc(source!.github!.ref)}</span>
        </div>
        <div class="properties-library-fact-line">
          <span class="properties-library-fact"><span class="properties-library-fact-label">${esc(lbl('source_commit', 'Commit'))}</span><code class="properties-code">${esc((source!.github!.commit || '—').slice(0, 16))}</code></span>
          <span class="properties-library-fact"><span class="properties-library-fact-label">${esc(lbl('source_last_sync', 'Última sincronização'))}</span>${esc(source!.lastSyncAt ? new Date(source!.lastSyncAt).toLocaleString() : lbl('source_never_synced', 'Nunca sincronizada'))}</span>
        </div>
      </div>
    ` : ''}
    <div class="properties-library-summary">
      <span class="properties-library-stat"><strong>${assets.length}</strong>${esc(lbl('summary_assets', 'assets'))}</span>
      <span class="properties-library-stat"><strong>${skills}</strong>${esc(lbl('summary_skills', 'skills'))}</span>
      <span class="properties-library-stat"><strong>${mcps}</strong>${esc(lbl('summary_mcps', 'MCPs'))}</span>
      <span class="properties-library-stat"><strong>${approved}</strong>${esc(lbl('summary_approved', 'aprovados'))}</span>
    </div>
    ${assets.length === 0
      ? `<p class="properties-library-empty">${esc(isGitHubSource
          ? lbl('detail_empty_remote', 'Esta biblioteca ainda não foi sincronizada. Use Sincronizar GitHub para ler o manifesto (sem baixar assets).')
          : lbl('detail_empty_library', 'Esta biblioteca ainda não tem assets. Importe um catálogo ou crie um derivado.'))}</p>`
      : `<div class="properties-library-overview-hint">
          <div class="properties-library-overview-title">${esc(lbl('detail_pick_asset_title', 'Selecione um ativo na árvore'))}</div>
          <p>${esc(lbl('detail_pick_asset_desc', 'Escolha um domínio e um subdomínio para ver a auditoria, a origem e as ações do ativo.'))}</p>
        </div>`}
    <div class="properties-library-actions properties-library-actions--library">
      ${isGitHubSource
        ? `<button type="button" class="btn btn-primary-office" data-library-action="sync" data-library-id="${esc(library.id)}">${esc(lbl('action_sync', 'Sincronizar GitHub'))}</button>`
        : `<button type="button" class="btn btn-primary-office" data-library-action="import" data-library-id="${esc(library.id)}">${esc(importLabel)}</button>`}
      <button type="button" class="btn btn-secondary-office" data-library-action="import-file" data-library-id="${esc(library.id)}">${esc(lbl('action_import_file', 'Importar arquivo'))}</button>
      <button type="button" class="btn btn-secondary-office" data-library-action="export" data-library-id="${esc(library.id)}">${esc(lbl('action_export', 'Exportar'))}</button>
      <button type="button" class="btn btn-secondary-office" data-library-action="clone" data-library-id="${esc(library.id)}">${esc(lbl('action_clone', 'Clonar'))}</button>
      ${(library.id === 'library-arestrix' || library.isBuiltin) ? `<button type="button" class="btn btn-secondary-office" data-library-action="reset-builtin" data-library-id="${esc(library.id)}">${esc(lbl('action_reset_builtin', 'Restaurar padrão de instalação'))}</button>` : ''}
      ${library.isBuiltin ? '' : `<button type="button" class="btn btn-secondary-office properties-library-action-danger" data-library-action="remove" data-library-id="${esc(library.id)}">${esc(lbl('action_remove_library', 'Remover biblioteca'))}</button>`}
    </div>
  `;
}

export function renderLibraryBrowser(options: ILibraryBrowserOptions): string {
  const state = defaultState(options, options.registry);
  return `
    <div class="properties-library-browser" data-library-browser>
      <aside class="properties-library-tree-pane">
        <div class="properties-library-toolbar">
          <input type="search" class="form-input" data-library-search placeholder="${esc(lbl('tree_search', 'Buscar assets...'))}" value="${esc(state.search)}" />
          <select class="form-select" data-library-filter>
            <option value="all">${esc(lbl('tree_filter_all', 'Todos'))}</option>
            <option value="skill">${esc(lbl('tree_filter_skills', 'Skills'))}</option>
            <option value="mcp">${esc(lbl('tree_filter_mcps', 'MCPs'))}</option>
            <option value="agent-template">${esc(lbl('tree_filter_templates', 'Templates'))}</option>
            <option value="pipeline">${esc(lbl('tree_filter_pipelines', 'Pipelines'))}</option>
          </select>
        </div>
        <div class="properties-library-tree" data-library-tree>
          ${renderTree(options.registry, options, state)}
        </div>
      </aside>
      <section class="properties-library-detail-pane" data-library-detail>
        ${renderLibraryDetail(options.registry, options, state)}
      </section>
    </div>
  `;
}

function notify(severity: 'success' | 'warning' | 'error' | 'info', message: string): void {
  globalEventBus.emit('notification:show', {
    severity,
    title: t('properties_window.libraries.title', 'Bibliotecas'),
    message
  });
}

function downloadFile(name: string, content: string): void {
  if (typeof document === 'undefined' || typeof URL === 'undefined') return;
  try {
    const blob = new Blob([content], { type: 'application/json;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    const anchor = document.createElement('a');
    anchor.href = url;
    anchor.download = name;
    anchor.rel = 'noopener';
    document.body.appendChild(anchor);
    anchor.click();
    anchor.remove();
    URL.revokeObjectURL(url);
  } catch {
    notify('error', lbl('msg_export_failed', 'Não foi possível exportar a biblioteca.'));
  }
}

function promptText(message: string, fallback: string): string | null {
  if (typeof window === 'undefined' || typeof window.prompt !== 'function') return null;
  return window.prompt(message, fallback);
}

function confirmAction(message: string): boolean {
  if (typeof window === 'undefined' || typeof window.confirm !== 'function') return false;
  return window.confirm(message);
}

export function bindLibraryBrowser(root: HTMLElement, options: ILibraryBrowserOptions): void {
  if (root.getAttribute('data-library-browser-bound') === 'true') return;
  root.setAttribute('data-library-browser-bound', 'true');

  const service = LibraryService.getInstance();
  const state = stateByRoot.get(root) || defaultState(options, options.registry);
  stateByRoot.set(root, state);

  const tree = root.querySelector<HTMLElement>('[data-library-tree]');
  const detail = root.querySelector<HTMLElement>('[data-library-detail]');
  const search = root.querySelector<HTMLInputElement>('[data-library-search]');
  const filter = root.querySelector<HTMLSelectElement>('[data-library-filter]');

  /** Expande as bibliotecas e grupos que sobreviveram ao filtro, para os resultados ficarem visíveis. */
  const expandVisibleGroups = (): void => {
    for (const library of options.registry.list(options.scope)) {
      const visible = filteredAssets(options.registry, library.id, state);
      if (visible.length > 0) {
        state.expandedLibraries.add(library.id);
        expandGroups(state, library.id, groupAssetsByTaxonomy(visible));
      }
    }
  };

  let activeMenu: HTMLElement | null = null;
  let activeMenuCleanup: (() => void) | null = null;

  const closeContextMenu = (): void => {
    if (activeMenuCleanup) {
      activeMenuCleanup();
      activeMenuCleanup = null;
    }
    if (activeMenu) {
      activeMenu.remove();
      activeMenu = null;
    }
  };

  const openContextMenu = (
    event: MouseEvent,
    target: LibraryTreeContextTarget,
    groups: IAssetDomainGroup[],
    actions: LibraryTreeContextAction[]
  ): void => {
    closeContextMenu();
    if (actions.length === 0 || typeof document === 'undefined') return;

    const menu = document.createElement('div');
    menu.className = 'properties-library-context-menu';
    menu.setAttribute('role', 'menu');
    menu.innerHTML = actions.map(action => {
      const label = action === 'expand-all'
        ? lbl('tree_expand_all', 'Expandir todos')
        : lbl('tree_collapse_all', 'Recolher todos');
      return `<button type="button" class="properties-library-context-item" role="menuitem" data-tree-context-action="${action}">${esc(label)}</button>`;
    }).join('');

    menu.style.visibility = 'hidden';
    document.body.appendChild(menu);

    const rect = menu.getBoundingClientRect();
    const maxX = Math.max(8, (window.innerWidth || 1024) - rect.width - 8);
    const maxY = Math.max(8, (window.innerHeight || 768) - rect.height - 8);
    menu.style.left = `${Math.min(Math.max(8, event.clientX), maxX)}px`;
    menu.style.top = `${Math.min(Math.max(8, event.clientY), maxY)}px`;
    menu.style.visibility = 'visible';

    menu.addEventListener('click', (clickEvent) => {
      const btn = (clickEvent.target as HTMLElement | null)?.closest<HTMLButtonElement>('[data-tree-context-action]');
      const action = btn?.dataset.treeContextAction as LibraryTreeContextAction | undefined;
      closeContextMenu();
      if (!action) return;
      applyTreeNodeContextAction(target, action, groups, state);
      renderTreeInto();
    });

    const onOutsidePointer = (ev: Event) => {
      const el = ev.target as Node | null;
      if (el && menu.contains(el)) return;
      closeContextMenu();
    };
    const onKeyDown = (ev: KeyboardEvent) => {
      if (ev.key === 'Escape') closeContextMenu();
    };
    const onScroll = () => closeContextMenu();

    document.addEventListener('mousedown', onOutsidePointer, true);
    document.addEventListener('keydown', onKeyDown, true);
    tree?.addEventListener('scroll', onScroll, { passive: true });

    activeMenu = menu;
    activeMenuCleanup = () => {
      document.removeEventListener('mousedown', onOutsidePointer, true);
      document.removeEventListener('keydown', onKeyDown, true);
      tree?.removeEventListener('scroll', onScroll);
    };
  };

  const renderTreeInto = (): void => {
    closeContextMenu();
    if (!tree) return;
    const scrollTop = tree.scrollTop;
    tree.innerHTML = renderTree(options.registry, options, state);
    tree.scrollTop = scrollTop;
  };

  const rerender = (): void => {
    renderTreeInto();
    if (detail) detail.innerHTML = renderLibraryDetail(options.registry, options, state);
  };

  search?.addEventListener('input', () => {
    state.search = search.value;
    if (state.search.trim() || state.kind !== 'all') expandVisibleGroups();
    renderTreeInto();
  });

  filter?.addEventListener('change', () => {
    state.kind = filter.value === 'skill' || filter.value === 'mcp' || filter.value === 'agent-template' || filter.value === 'pipeline'
      ? filter.value
      : 'all';
    if (state.search.trim() || state.kind !== 'all') expandVisibleGroups();
    renderTreeInto();
  });

  tree?.addEventListener('contextmenu', (event) => {
    const el = event.target as HTMLElement | null;
    if (!el) return;

    const librarySelect = el.closest<HTMLElement>('[data-library-select]');
    const domainToggle = el.closest<HTMLElement>('[data-library-domain-toggle]');
    const subdomainToggle = el.closest<HTMLElement>('[data-library-subdomain-toggle]');

    let contextTarget: LibraryTreeContextTarget | null = null;
    if (librarySelect?.dataset.librarySelect) {
      contextTarget = { kind: 'library', libraryId: librarySelect.dataset.librarySelect };
    } else if (domainToggle?.dataset.libraryDomainToggle) {
      const parsed = parseGroupKey(readGroupKey(domainToggle.dataset.libraryDomainToggle));
      if (parsed.libraryId) {
        contextTarget = { kind: 'domain', libraryId: parsed.libraryId, domain: parsed.domain };
      }
    } else if (subdomainToggle?.dataset.librarySubdomainToggle) {
      const parsed = parseGroupKey(readGroupKey(subdomainToggle.dataset.librarySubdomainToggle));
      if (parsed.libraryId && parsed.subdomain !== undefined) {
        contextTarget = {
          kind: 'subdomain',
          libraryId: parsed.libraryId,
          domain: parsed.domain,
          subdomain: parsed.subdomain
        };
      }
    }

    if (!contextTarget) {
      closeContextMenu();
      return;
    }

    const groups = groupAssetsByTaxonomy(filteredAssets(options.registry, contextTarget.libraryId, state));
    const actions = getTreeNodeContextActions(contextTarget, groups, state);
    if (actions.length === 0) {
      closeContextMenu();
      return;
    }

    event.preventDefault();
    event.stopPropagation();
    openContextMenu(event, contextTarget, groups, actions);
  });

  root.addEventListener('click', (event) => {
    const target = event.target as HTMLElement | null;
    if (!target) return;

    const domainToggle = target.closest<HTMLElement>('[data-library-domain-toggle]');
    if (domainToggle) {
      toggleGroup(state, readGroupKey(domainToggle.dataset.libraryDomainToggle));
      renderTreeInto();
      return;
    }

    const subdomainToggle = target.closest<HTMLElement>('[data-library-subdomain-toggle]');
    if (subdomainToggle) {
      toggleGroup(state, readGroupKey(subdomainToggle.dataset.librarySubdomainToggle));
      renderTreeInto();
      return;
    }

    const librarySelect = target.closest<HTMLElement>('[data-library-select]');
    if (librarySelect) {
      const id = librarySelect.dataset.librarySelect;
      if (id) {
        const wasExpanded = state.expandedLibraries.has(id);
        state.selectedLibraryId = id;
        state.selectedAssetId = undefined;
        if (wasExpanded) {
          state.expandedLibraries.delete(id);
        } else {
          state.expandedLibraries.add(id);
          if (!state.seededLibraries.has(id)) {
            seedExpandedGroups(state, id, options.registry.listAssets(id));
          }
          if (state.search.trim() || state.kind !== 'all') {
            expandGroups(state, id, groupAssetsByTaxonomy(filteredAssets(options.registry, id, state)));
          }
        }
        rerender();
      }
      return;
    }

    const assetSelect = target.closest<HTMLElement>('[data-asset-select]');
    if (assetSelect) {
      const id = assetSelect.dataset.assetSelect;
      const libId = assetSelect.dataset.libraryId;
      if (id) {
        if (libId) state.selectedLibraryId = libId;
        state.selectedAssetId = id;
        rerender();
      }
      return;
    }

    const assetAction = target.closest<HTMLButtonElement>('[data-asset-action]');
    if (assetAction) {
      const assetId = assetAction.dataset.assetId;
      const action = assetAction.dataset.assetAction;
      if (!assetId || !action) return;
      assetAction.disabled = true;
      void handleAssetAction(service, options, state, action, assetId, rerender);
      return;
    }

    const libraryAction = target.closest<HTMLButtonElement>('[data-library-action]');
    if (libraryAction) {
      const libraryId = libraryAction.dataset.libraryId;
      const action = libraryAction.dataset.libraryAction;
      if (!libraryId || !action) return;
      libraryAction.disabled = true;
      void handleLibraryAction(service, options, state, action, libraryId, rerender);
    }
  });
}

async function handleAssetAction(
  service: LibraryService,
  options: ILibraryBrowserOptions,
  state: ILibraryBrowserState,
  action: string,
  assetId: string,
  rerender: () => void
): Promise<void> {
  if (action === 'audit') {
    const result = await service.reauditAsset(assetId);
    if (result.ok && result.asset) {
      notify(
        result.asset.audit.verdict === 'REJECT' ? 'warning' : 'success',
        lbl('msg_audit_result', 'Auditoria concluída: {{verdict}} (risco {{risk}}/100).')
          .replace('{{verdict}}', verdictLabel(result.asset.audit.verdict))
          .replace('{{risk}}', String(result.asset.audit.riskScore))
      );
    } else {
      notify('error', result.reason || lbl('msg_audit_failed', 'Não foi possível reauditar o asset.'));
    }
    rerender();
    return;
  }

  if (action === 'edit') {
    try {
      await editLibraryAsset(service, options, state, assetId, rerender);
    } finally {
      // Reabilita o botão e reflete a cópia/estado novo mesmo quando o editor
      // abre por cima da página ou quando a ação termina sem criar vértice.
      rerender();
    }
    return;
  }

  if (action === 'insert-asset' || action === 'insert-mcp-vertex') {
    const source = options.registry.getAsset(assetId);
    if (!source) {
      notify('error', lbl('msg_edit_failed', 'Não foi possível inserir o ativo no grafo.'));
      rerender();
      return;
    }
    if (source.audit.verdict === 'REJECT') {
      notify('warning', lbl('msg_insert_rejected', 'Ativo reprovado na auditoria não pode ser inserido no grafo.'));
      rerender();
      return;
    }
    if (!options.graph) {
      notify('warning', lbl('msg_edit_no_graph', 'Abra um projeto para inserir o ativo no grafo.'));
      rerender();
      return;
    }

    // Regra de imutabilidade: ativos da Biblioteca Arestrix são sempre copiados
    // primeiro para "Minha biblioteca" antes de instalar ou inserir no grafo.
    const workingAsset = await ensureCopyInOwnLibrary(service, options, state, source, rerender);
    if (!workingAsset) {
      rerender();
      return;
    }

    if ((workingAsset.payload.kind === 'mcp' || workingAsset.payload.kind === 'skill') && workingAsset.status !== 'installed') {
      const installed = await service.installAsset(workingAsset.id, { allowCaution: true });
      if (!installed.ok) {
        notify('warning', installed.reason || lbl('msg_install_failed', 'Não foi possível instalar o asset no Workspace.'));
        rerender();
        return;
      }
    }
    const latestAsset = options.registry.getAsset(workingAsset.id) || workingAsset;
    const cleanLabel = source.name.replace(/\s*\(c[oó]pia\)$/i, '').trim() || source.name;
    const node = buildNodeFromLibraryAsset(latestAsset, options.graph, cleanLabel);
    if (!node) {
      notify('error', lbl('msg_edit_failed', 'Não foi possível criar o vértice a partir do ativo.'));
      rerender();
      return;
    }
    options.graph.commandManager.execute(new AddNodeCommand(options.graph, node));
    globalEventBus.emit('node:selected', node);
    notify('success', lbl('msg_asset_inserted', 'Vértice "{{label}}" inserido no grafo.')
      .replace('{{label}}', node.label));
    rerender();
    options.onReturnToStudio?.();
    return;
  }

  if (action === 'install') {
    const result = await service.installAsset(assetId, { allowCaution: true });
    if (result.ok) {
      notify('success', lbl('msg_installed', 'Asset instalado no Workspace.'));
    } else {
      notify('warning', result.reason || lbl('msg_install_failed', 'Não foi possível instalar o asset.'));
    }
    rerender();
    return;
  }

  if (action === 'download') {
    const first = await service.downloadGitHubAsset(assetId);
    if (!first.ok && first.needsConfirmation) {
      if (confirmAction(lbl('msg_download_caution_confirm', 'O asset remoto tem alertas de segurança. Baixar e usar mesmo assim?'))) {
        const second = await service.downloadGitHubAsset(assetId, { allowCaution: true });
        notify(
          second.ok ? 'warning' : 'error',
          second.ok
            ? lbl('msg_downloaded_caution', 'Asset baixado com alertas e disponível para revisão.')
            : second.reason || lbl('msg_download_failed', 'Não foi possível baixar o asset.')
        );
      }
    } else if (first.ok) {
      notify('success', lbl('msg_downloaded', 'Asset baixado e auditado com sucesso.'));
    } else {
      notify('warning', first.reason || lbl('msg_download_failed', 'Não foi possível baixar o asset.'));
    }
    rerender();
    return;
  }

  if (action === 'restore-builtin') {
    const restoreService = service as RestoreCapableLibraryService;
    const restored = await restoreService.restoreBuiltinAsset?.(assetId);
    const ok = Boolean(restored && (typeof restored !== 'object' || !('ok' in restored) || restored.ok));
    if (ok) {
      notify('success', lbl('msg_restore_builtin_success', 'Asset restaurado para o padrão de instalação.'));
    } else {
      notify('warning', lbl('msg_restore_builtin_failed', 'Não foi possível restaurar o padrão de instalação.'));
    }
    rerender();
    return;
  }

  if (action === 'remove') {
    const targetAsset = options.registry.getAsset(assetId);
    if (!targetAsset) return;
    if (targetAsset.libraryId === 'library-arestrix' || options.registry.get(targetAsset.libraryId)?.isBuiltin) {
      notify('warning', lbl('msg_builtin_readonly', 'Ativos da Biblioteca Arestrix são somente leitura e não podem ser removidos ou editados.'));
      rerender();
      return;
    }
    if (!confirmAction(lbl('msg_remove_asset_confirm', 'Remover este asset da biblioteca?'))) return;
    options.registry.removeAsset(assetId);
    await service.persist('workspace');
    state.selectedAssetId = undefined;
    notify('success', lbl('msg_removed', 'Asset removido da biblioteca.'));
    rerender();
  }
}

function libraryAssetNodePosition(graph: Pick<Graph, 'getAllNodes'>): { x: number; y: number; z: number } {
  const nodes = graph.getAllNodes();
  let rightmost: INodeData | undefined;
  for (const node of nodes) {
    if (!rightmost || node.position.x > rightmost.position.x) rightmost = node;
  }
  return {
    x: rightmost ? rightmost.position.x + 320 : 0,
    y: rightmost ? rightmost.position.y : 0,
    z: 0
  };
}

function libraryNodeId(seed: string): string {
  const suffix = `${Date.now().toString(36)}${Math.random().toString(36).slice(2, 6)}`;
  return `${seed || 'asset'}_${suffix}`;
}

function applyLambdaTemplate(body: string, label: string, parameters: Record<string, unknown>): string {
  let out = (body || '').split('${label}').join(label);
  for (const [key, value] of Object.entries(parameters)) {
    out = out.split(`\${params.${key}}`).join(String(value));
  }
  return out;
}

type NodeLibrarySource = NonNullable<INodeData['librarySource']>;

function nodeLibrarySource(asset: ILibraryAsset): NodeLibrarySource {
  return {
    libraryId: asset.libraryId,
    assetId: asset.id,
    hash: asset.audit.hash,
    version: asset.version,
    bundledInProduct: Boolean(asset.bundledInProduct),
    originRepo: asset.origin?.repo,
    originUrl: asset.origin?.url,
    originLicense: asset.origin?.license
  };
}

/**
 * Cria um vértice livre no grafo a partir de qualquer ativo da biblioteca
 * (MCP, skill, template de agente ou pipeline), pronto para ser conectado na tela do Studio.
 */
export function buildNodeFromLibraryAsset(
  asset: ILibraryAsset,
  graph: Pick<Graph, 'getAllNodes'>,
  labelOverride?: string
): INodeData | null {
  if (asset.payload.kind === 'skill') {
    const id = libraryNodeId(asset.id);
    const label = labelOverride || asset.name;
    return {
      id,
      label,
      type: 'agent',
      position: libraryAssetNodePosition(graph),
      inputs: [{ id: `${id}_in_0`, name: 'in', direction: 'input', type: 'flow' }],
      outputs: [{ id: `${id}_out_0`, name: 'out', direction: 'output', type: 'flow' }],
      parameters: {},
      customLambdaBody: '',
      agentSettings: {
        inheritSandbox: true,
        inheritModel: true,
        inheritCodeMode: true,
        inheritMemory: true,
        activeSkillId: asset.id,
        skills: [
          {
            id: asset.id,
            name: asset.name,
            description: asset.description,
            markdownContent: asset.payload.markdownContent,
            systemPrompt: asset.payload.systemPrompt,
            tools: [...(asset.payload.tools || [])],
            license: asset.license,
            updatedAt: asset.updatedAt,
            sourceUrl: asset.provenance?.url,
            audit: {
              hash: asset.audit.hash,
              verdict: asset.audit.verdict,
              riskScore: asset.audit.riskScore,
              policyVersion: asset.audit.policyVersion,
              auditedAt: asset.audit.auditedAt || asset.updatedAt
            }
          }
        ]
      },
      presetLocked: false,
      origin: asset.origin,
      librarySource: nodeLibrarySource(asset)
    };
  }
  const node = buildEditableNodeFromAsset(asset, graph, labelOverride || asset.name);
  if (!node) return null;
  node.label = labelOverride || asset.name;
  return node;
}

/**
 * Cria o vértice editável a partir de um asset de biblioteca. O asset original
 * continua imutável; o vértice nasce como cópia (presetLocked=false) e carrega
 * a proveniência da biblioteca para o botão "Atualizar da biblioteca".
 */
export function buildEditableNodeFromAsset(
  asset: ILibraryAsset,
  graph: Pick<Graph, 'getAllNodes'>,
  labelBase?: string
): INodeData | null {
  if (asset.payload.kind === 'mcp') {
    const id = libraryNodeId(asset.origin?.templateId || asset.id);
    const label = `${labelBase || asset.name} (editável)`;
    const firstTool = asset.payload.cachedTools?.[0]?.name || '';
    return {
      id,
      label,
      type: 'mcp',
      position: libraryAssetNodePosition(graph),
      inputs: [{ id: `${id}_in_0`, name: 'in', direction: 'input', type: 'flow' }],
      outputs: [{ id: `${id}_out_0`, name: 'out', direction: 'output', type: 'flow' }],
      parameters: {},
      customLambdaBody: '',
      mcpSettings: {
        server: asset.id,
        tool: firstTool,
        arguments: {},
        source: { kind: 'previous' },
        output: {}
      },
      presetId: asset.origin?.templateId || asset.id,
      presetLocked: false,
      origin: asset.origin,
      librarySource: nodeLibrarySource(asset)
    };
  }
  if (asset.payload.kind !== 'agent-template' && asset.payload.kind !== 'pipeline') return null;
  const template = asset.payload.template;
  if (!template || !template.taskType) return null;

  const id = libraryNodeId(template.id || asset.id);
  const label = `${labelBase || asset.name} (editável)`;
  const parameters: Record<string, string | number | boolean> = {};
  for (const [key, definition] of Object.entries(template.parameters || {})) {
    if (definition && typeof definition === 'object' && 'defaultValue' in definition &&
        definition.defaultValue !== undefined) {
      parameters[key] = definition.defaultValue;
    }
  }

  const inputs: IPort[] = (template.inputs || []).map((port, index) => ({
    id: `${id}_in_${index}`,
    name: port.name,
    direction: 'input',
    type: port.type,
    dataType: port.dataType
  }));
  const outputs: IPort[] = (template.outputs || []).map((port, index) => ({
    id: `${id}_out_${index}`,
    name: port.name,
    direction: 'output',
    type: port.type,
    conditionBranchIndex: port.conditionBranchIndex !== undefined ? port.conditionBranchIndex : index
  }));
  if (template.taskType === 'condition') {
    while (outputs.length < 2) {
      const index = outputs.length;
      outputs.push({
        id: `${id}_out_${index}`,
        name: index === 0 ? 'ramo 0' : `ramo ${index}`,
        direction: 'output',
        type: 'flow',
        conditionBranchIndex: index
      });
    }
  }

  let lambdaBody = applyLambdaTemplate(template.lambdaBodyTemplate || '', label, parameters);
  if (!lambdaBody && template.taskType !== 'agent' && template.assetKind !== 'pipeline') {
    lambdaBody = `// ${label}\nstd::cout << "${label.replace(/"/g, '\\"')}\\n";`;
  }

  return {
    id,
    label,
    type: template.taskType,
    position: libraryAssetNodePosition(graph),
    inputs,
    outputs,
    parameters,
    customLambdaBody: lambdaBody,
    templateId: template.id,
    requiredHeaders: template.requiredHeaders ? [...template.requiredHeaders] : undefined,
    agentSettings: template.agentSettings
      ? JSON.parse(JSON.stringify(template.agentSettings))
      : { inheritSandbox: true, inheritModel: true, inheritCodeMode: true, inheritMemory: true },
    presetId: template.id,
    presetLocked: false,
    presentation: template.presentation,
    childGraph: template.childGraph ? JSON.parse(JSON.stringify(template.childGraph)) : undefined,
    pipelineInterface: template.pipelineInterface
      ? JSON.parse(JSON.stringify(template.pipelineInterface))
      : undefined,
    origin: template.origin ?? asset.origin,
    librarySource: nodeLibrarySource(asset)
  };
}

function openSkillAssetEditor(
  service: LibraryService,
  asset: ILibraryAsset,
  rerender: () => void
): void {
  if (asset.payload.kind !== 'skill') return;
  const payload = asset.payload;
  globalEventBus.emit('node:open-skill-editor', {
    path: `.workspace/library-edits/${asset.id}/SKILL.md`,
    initialContent: payload.markdownContent,
    nodeLabel: asset.name,
    onSave: (_path: string, content: string) => {
      void service.updateDerivedAsset(asset.id, {
        kind: 'skill',
        markdownContent: content,
        systemPrompt: payload.systemPrompt,
        tools: [...payload.tools]
      }).then(result => {
        if (result.ok) {
          notify('success', lbl('msg_edited', 'Ativo atualizado.'));
        } else {
          notify('warning', result.reason || lbl('msg_edit_failed', 'Não foi possível atualizar o ativo.'));
        }
        rerender();
      });
    }
  });
}

/**
 * Garante que qualquer ativo da Biblioteca Arestrix (ou outra biblioteca somente
 * leitura) seja copiado primeiro para "Minha biblioteca" antes de editar ou inserir.
 * Nunca editamos nem mutamos os ativos originais da Biblioteca Arestrix.
 */
export async function ensureCopyInOwnLibrary(
  service: LibraryService,
  options: ILibraryBrowserOptions,
  state: ILibraryBrowserState,
  source: ILibraryAsset,
  rerender: () => void
): Promise<ILibraryAsset | null> {
  const ownLibrary = service.ensureOwnLibrary('workspace');
  if (!ownLibrary) {
    notify('error', lbl('msg_derive_failed', 'Não foi possível preparar Minha biblioteca.'));
    return null;
  }
  const isAlreadyInOwnLibrary = source.libraryId === ownLibrary.id;
  const isBuiltinSource = source.libraryId === 'library-arestrix' || Boolean(options.registry.get(source.libraryId)?.isBuiltin);

  let working: ILibraryAsset | undefined = isAlreadyInOwnLibrary
    ? source
    : (!isBuiltinSource && source.derivedFrom
        ? source
        : options.registry.listAssets(ownLibrary.id).find(candidate =>
            candidate.derivedFrom?.libraryId === source.libraryId &&
            candidate.derivedFrom?.assetId === source.id
          ));

  if (!working) {
    const derived = await service.deriveAsset(source.id, { libraryId: ownLibrary.id });
    if (!derived) {
      notify('error', lbl('msg_derive_failed', 'Não foi possível copiar o ativo para Minha biblioteca.'));
      return null;
    }
    working = derived;
    notify('success', lbl('msg_derived', 'Cópia criada em Minha biblioteca: {{name}}.')
      .replace('{{name}}', derived.name));
  }

  if (working.id !== state.selectedAssetId || working.libraryId !== state.selectedLibraryId) {
    state.selectedLibraryId = working.libraryId;
    state.selectedAssetId = working.id;
    state.expandedLibraries.add(working.libraryId);
    state.expandedGroups.add(groupKey(working.libraryId, assetDomain(working)));
    state.expandedGroups.add(groupKey(working.libraryId, assetDomain(working), assetSubdomain(working)));
    rerender();
  }
  return working;
}

/**
 * "Editar" mantém o asset original imutável: copia primeiro para Minha
 * biblioteca (quando ainda não estiver lá), abre o editor próprio do tipo do
 * ativo e, para vértices, insere a cópia editável no grafo antes de abrir o modal.
 */
async function editLibraryAsset(
  service: LibraryService,
  options: ILibraryBrowserOptions,
  state: ILibraryBrowserState,
  assetId: string,
  rerender: () => void
): Promise<void> {
  const source = options.registry.getAsset(assetId);
  if (!source) {
    notify('error', lbl('msg_edit_failed', 'Não foi possível atualizar o ativo.'));
    return;
  }
  const isVertex = source.payload.kind === 'agent-template' || source.payload.kind === 'pipeline';
  if (isVertex && !options.graph) {
    notify('warning', lbl('msg_edit_no_graph', 'Abra um projeto para criar o vértice editável.'));
    return;
  }

  const editable = await ensureCopyInOwnLibrary(service, options, state, source, rerender);
  if (!editable) return;

  if (editable.payload.kind === 'skill') {
    openSkillAssetEditor(service, editable, rerender);
    return;
  }
  if (editable.payload.kind === 'mcp') {
    if (options.graph) {
      await service.installAsset(editable.id, { allowCaution: true });
      const baseName = source.name.replace(/\s*\(c[oó]pia\)$/i, '').trim() || source.name;
      const node = buildEditableNodeFromAsset(editable, options.graph, baseName);
      if (node) {
        options.graph.commandManager.execute(new AddNodeCommand(options.graph, node));
        globalEventBus.emit('node:selected', node);
        options.onReturnToStudio?.();
        notify('success', lbl('msg_edit_vertex', 'Vértice MCP editável "{{label}}" criado no grafo.')
          .replace('{{label}}', node.label));
        return;
      }
    }
    notify('info', lbl('msg_edit_mcp',
      'Cópia criada em Minha biblioteca. Edite o servidor MCP em Workspace > Servidores MCP.'));
    return;
  }
  if (!isVertex || !options.graph) {
    notify('info', lbl('msg_edited', 'Ativo atualizado.'));
    return;
  }

  const baseName = source.name.replace(/\s*\(c[oó]pia\)$/i, '').trim() || source.name;
  const node = buildEditableNodeFromAsset(editable, options.graph, baseName);
  if (!node) {
    notify('error', lbl('msg_edit_failed', 'Não foi possível atualizar o ativo.'));
    return;
  }
  options.graph.commandManager.execute(new AddNodeCommand(options.graph, node));
  globalEventBus.emit('node:selected', node);
  options.onReturnToStudio?.();
  globalEventBus.emit('node:open-model-modal', { nodeId: node.id, tab: 'prompt' });
  notify('success', lbl('msg_edit_vertex', 'Vértice editável "{{label}}" criado no grafo.')
    .replace('{{label}}', node.label));
}

async function handleLibraryAction(
  service: LibraryService,
  options: ILibraryBrowserOptions,
  state: ILibraryBrowserState,
  action: string,
  libraryId: string,
  rerender: () => void
): Promise<void> {
  const library = options.registry.get(libraryId);
  if (!library) return;

  if (action === 'sync') {
    if (!library.sourceId) return;
    const result = await service.syncGitHubSource({ sourceId: library.sourceId, libraryId });
    notify(
      result.ok ? (result.removed > 0 ? 'warning' : 'success') : 'error',
      result.ok
        ? lblv('msg_sync_result', 'GitHub sincronizado em {{commit}}: {{added}} novos, {{updated}} atualizados, {{removed}} removidos.', {
            commit: result.commit.slice(0, 10),
            added: result.added,
            updated: result.updated,
            removed: result.removed
          })
        : result.error || lbl('msg_sync_failed', 'Não foi possível sincronizar a fonte.')
    );
    rerender();
    return;
  }

  if (action === 'import') {
    const result = libraryScope(options, libraryId) === 'studio'
      ? await service.importBuiltinSkills({ libraryId })
      : await service.importWorkspaceCatalog({ libraryId });
    notify(
      result.rejected.length > 0 ? 'warning' : 'success',
      lbl('msg_import_result', '{{imported}} assets importados e auditados; {{rejected}} rejeitados.')
        .replace('{{imported}}', String(result.imported.length))
        .replace('{{rejected}}', String(result.rejected.length))
    );
    rerender();
    return;
  }

  if (action === 'export') {
    const json = service.exportLibrary(libraryId);
    if (json) {
      downloadFile(`${library.name.replace(/[^\w.-]+/g, '-').toLowerCase()}.arestrix-library.json`, json);
      notify('success', lbl('msg_exported', 'Biblioteca exportada.'));
    } else {
      notify('error', lbl('msg_export_failed', 'Não foi possível exportar a biblioteca.'));
    }
    rerender();
    return;
  }

  if (action === 'import-file') {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json,.arestrix-library,application/json';
    input.addEventListener('change', () => {
      const file = input.files?.[0];
      if (!file) return;
      void file.text().then(async text => {
        const result = await service.importLibrary(text, { scope: libraryScope(options, state.selectedLibraryId) });
        if (result) {
          state.selectedLibraryId = result.library.id;
          state.selectedAssetId = undefined;
          state.expandedLibraries.add(result.library.id);
          seedExpandedGroups(state, result.library.id, options.registry.listAssets(result.library.id));
          notify(
            result.rejected.length > 0 ? 'warning' : 'success',
            lbl('msg_import_file_result', 'Biblioteca importada: {{imported}} assets; {{rejected}} rejeitados.')
              .replace('{{imported}}', String(result.imported.length))
              .replace('{{rejected}}', String(result.rejected.length))
          );
        } else {
          notify('error', lbl('msg_import_file_failed', 'Arquivo de biblioteca inválido.'));
        }
        rerender();
      });
    });
    input.click();
    return;
  }

  if (action === 'clone') {
    const name = promptText(lbl('msg_clone_prompt', 'Nome da nova biblioteca:'), `${library.name} (clone)`);
    if (!name) return;
    const result = await service.cloneLibrary(libraryId, name, libraryScope(options, libraryId));
    if (result) {
      state.selectedLibraryId = result.library.id;
      state.selectedAssetId = undefined;
      state.expandedLibraries.add(result.library.id);
      seedExpandedGroups(state, result.library.id, options.registry.listAssets(result.library.id));
      notify('success', lbl('msg_clone_success', 'Biblioteca clonada: {{name}}.').replace('{{name}}', result.library.name));
    } else {
      notify('error', lbl('msg_clone_failed', 'Não foi possível clonar a biblioteca.'));
    }
    rerender();
    return;
  }

  if (action === 'reset-builtin') {
    if (!confirmAction(lbl('msg_reset_builtin_confirm', 'Restaurar todos os itens nativos da Biblioteca Arestrix para o padrão de instalação?'))) {
      rerender();
      return;
    }
    const restoreService = service as RestoreCapableLibraryService;
    const result = await restoreService.resetBuiltinLibrary?.();
    if (result !== undefined) {
      notify('success', lbl('msg_reset_builtin_success', 'Biblioteca Arestrix restaurada para o padrão de instalação.'));
    } else {
      notify('warning', lbl('msg_restore_builtin_failed', 'Não foi possível restaurar o padrão de instalação.'));
    }
    rerender();
    return;
  }

  if (action === 'remove') {
    if (!confirmAction(lbl('msg_remove_library_confirm', 'Remover esta biblioteca e seus assets?'))) return;
    options.registry.remove(libraryId);
    await service.persist('workspace');
    const next = options.registry.list(options.scope)[0];
    state.selectedLibraryId = next?.id;
    state.selectedAssetId = undefined;
    if (next) {
      state.expandedLibraries.add(next.id);
      seedExpandedGroups(state, next.id, options.registry.listAssets(next.id));
    }
    notify('success', lbl('msg_library_removed', 'Biblioteca removida.'));
    rerender();
  }
}
