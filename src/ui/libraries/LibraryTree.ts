import { globalEventBus } from '../../core/EventBus.ts';
import { LibraryRegistry } from '../../core/libraries/LibraryRegistry.ts';
import { LibraryService } from '../../core/libraries/LibraryService.ts';
import type { ILibraryAsset, ILibraryEntry, LibraryAssetKind, LibraryAuditVerdict, LibraryScope } from '../../core/libraries/types.ts';
import { t } from '../../i18n/index.ts';

export interface ILibraryBrowserOptions {
  registry: LibraryRegistry;
  scope: LibraryScope;
}

interface ILibraryBrowserState {
  selectedLibraryId?: string;
  selectedAssetId?: string;
  search: string;
  kind: 'all' | 'skill' | 'mcp' | 'agent-template';
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
  return lbl('kind_graph_template', 'Template de grafo');
}

function statusLabel(status: ILibraryAsset['status']): string {
  return lbl(`status_${status}`, status);
}

function defaultState(options: ILibraryBrowserOptions, registry: LibraryRegistry): ILibraryBrowserState {
  const libraries = registry.list(options.scope);
  const first = options.scope === 'workspace'
    ? (libraries.find(library => !library.isBuiltin) || libraries[0])
    : libraries[0];
  return {
    selectedLibraryId: first?.id,
    selectedAssetId: undefined,
    search: '',
    kind: 'all'
  };
}

function filteredAssets(registry: LibraryRegistry, libraryId: string, state: ILibraryBrowserState): ILibraryAsset[] {
  const query = state.search.trim().toLowerCase();
  return registry.listAssets(libraryId)
    .filter(asset => state.kind === 'all' || asset.kind === state.kind)
    .filter(asset => !query
      || asset.name.toLowerCase().includes(query)
      || asset.description.toLowerCase().includes(query)
      || asset.category.toLowerCase().includes(query))
    .sort((a, b) => a.name.localeCompare(b.name));
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
    const isBuiltin = library.isBuiltin;
    const kind = isBuiltin ? lbl('kind_builtin', 'Embutida') : library.kind === 'repository' ? lbl('kind_repository', 'Repositório') : lbl('kind_local', 'Local');

    return `
      <div class="properties-library-group">
        <button type="button" class="properties-library-item ${isSelected ? 'active' : ''}" data-library-select="${esc(library.id)}">
          <span class="properties-library-item-name">${esc(library.name)}</span>
          ${badge(kind, isBuiltin ? 'success' : 'neutral')}
          <span class="properties-library-item-count">${total}</span>
        </button>
        ${isSelected ? `
          <div class="properties-library-assets">
            ${assets.length === 0
              ? `<div class="properties-library-assets-empty">${esc(lbl('tree_empty_assets', 'Nenhum asset. Importe um catálogo ou crie um derivado.'))}</div>`
              : assets.map(asset => `
                <button type="button" class="properties-library-asset ${asset.id === state.selectedAssetId ? 'active' : ''}" data-asset-select="${esc(asset.id)}">
                  <span class="properties-library-asset-name">${esc(asset.name)}</span>
                  <span class="properties-library-asset-meta">${esc(kindLabel(asset.kind))}</span>
                  ${badge(verdictLabel(asset.audit.verdict), verdictTone(asset.audit.verdict))}
                </button>
              `).join('')}
          </div>
        ` : ''}
      </div>
    `;
  }).join('');
}

function renderMetaRow(label: string, value: string): string {
  return `
    <div class="properties-library-meta-item">
      <span class="properties-library-meta-label">${esc(label)}</span>
      <span class="properties-library-meta-value">${value}</span>
    </div>
  `;
}

function renderAssetDetail(asset: ILibraryAsset, library: ILibraryEntry | undefined): string {
  const hasContent = asset.payload.kind === 'skill'
    ? asset.payload.markdownContent.length > 0
    : asset.payload.kind === 'mcp'
      ? Boolean(asset.payload.command || asset.payload.url || (asset.payload.args && asset.payload.args.length > 0))
      : true;
  const showDownload = Boolean(asset.remote) && (asset.audit.verdict === 'PENDING' || !hasContent || asset.status === 'disabled');
  const installDisabled = !hasContent || asset.audit.verdict === 'REJECT' || asset.audit.verdict === 'PENDING';
  const canInstall = asset.payload.kind === 'skill' || asset.payload.kind === 'mcp';
  const origin = [asset.provenance.sourceKind, asset.provenance.authority, asset.provenance.sourceId]
    .filter(Boolean)
    .join(' · ');

  return `
    <div class="properties-library-detail-header">
      <div>
        <div class="properties-library-detail-title">${esc(asset.name)}</div>
        <div class="properties-library-detail-subtitle">
          ${badge(kindLabel(asset.kind), 'accent')}
          ${badge(verdictLabel(asset.audit.verdict), verdictTone(asset.audit.verdict))}
          ${badge(statusLabel(asset.status), asset.status === 'installed' ? 'success' : 'neutral')}
          ${asset.remote ? badge(lbl('badge_remote', 'Remoto'), 'warning') : ''}
        </div>
      </div>
      <div class="properties-library-detail-breadcrumb">${esc(library?.name || '')}</div>
    </div>

    <p class="properties-library-description">${esc(asset.description)}</p>

    <div class="properties-library-meta">
      ${renderMetaRow(lbl('meta_version', 'Versão'), esc(asset.version))}
      ${renderMetaRow(lbl('meta_license', 'Licença'), esc(asset.license))}
      ${renderMetaRow(lbl('meta_category', 'Categoria'), esc(asset.category))}
      ${renderMetaRow(lbl('meta_hash', 'Hash SHA-256'), `<code class="properties-code">${esc(asset.audit.hash ? asset.audit.hash.slice(0, 20) + '…' : '—')}</code>`)}
      ${renderMetaRow(lbl('meta_policy', 'Política'), `<code class="properties-code">${esc(asset.audit.policyVersion || '—')}</code>`)}
      ${renderMetaRow(lbl('meta_origin', 'Origem'), esc(origin || '—'))}
      ${asset.remote ? renderMetaRow(lbl('meta_remote_path', 'Caminho remoto'), `<code class="properties-code">${esc(asset.remote.path)}</code>`) : ''}
      ${asset.remote ? renderMetaRow(lbl('meta_blob', 'Blob SHA'), `<code class="properties-code">${esc(asset.remote.blobSha.slice(0, 16))}…</code>`) : ''}
      ${asset.derivedFrom ? renderMetaRow(lbl('meta_derived_from', 'Derivado de'), esc(asset.derivedFrom.originName)) : ''}
    </div>

    <div class="properties-library-audit">
      <div class="properties-library-subtitle">${esc(lbl('audit_title', 'Auditoria'))}</div>
      <div class="properties-library-audit-line">
        ${badge(verdictLabel(asset.audit.verdict), verdictTone(asset.audit.verdict))}
        <span>${esc(lbl('audit_risk', 'Risco'))}: <strong>${esc(asset.audit.riskScore)}/100</strong></span>
        ${asset.audit.auditedAt ? `<span class="properties-muted">${esc(lbl('meta_audited_at', 'Auditado em'))}: ${esc(new Date(asset.audit.auditedAt).toLocaleString())}</span>` : ''}
      </div>
      ${asset.audit.findings.length + asset.audit.warnings.length > 0
        ? `<ul class="properties-library-findings">
            ${asset.audit.findings.map(finding => `<li>${esc(finding)}</li>`).join('')}
            ${asset.audit.warnings.map(warning => `<li class="is-warning">${esc(warning)}</li>`).join('')}
          </ul>`
        : `<p class="properties-hint">${esc(asset.audit.verdict === 'PENDING'
            ? lbl('audit_pending_download', 'Baixe o conteúdo para auditar antes de usar.')
            : lbl('audit_clean', 'Nenhum achado ou alerta de segurança.'))}</p>`}
    </div>

    <div class="properties-library-actions">
      ${showDownload ? `<button type="button" class="btn btn-primary-office" data-asset-action="download" data-asset-id="${esc(asset.id)}">${esc(lbl('action_download', 'Baixar e auditar'))}</button>` : ''}
      <button type="button" class="btn btn-secondary-office" data-asset-action="audit" data-asset-id="${esc(asset.id)}">${esc(lbl('action_audit', 'Reauditar'))}</button>
      <button type="button" class="btn btn-secondary-office" data-asset-action="derive" data-asset-id="${esc(asset.id)}">${esc(lbl('action_derive', 'Duplicar e editar'))}</button>
      ${canInstall ? `<button type="button" class="btn btn-primary-office" data-asset-action="install" data-asset-id="${esc(asset.id)}" ${installDisabled ? 'disabled' : ''}>${esc(lbl('action_install', 'Instalar no Workspace'))}</button>` : ''}
      <button type="button" class="btn btn-secondary-office" data-asset-action="remove" data-asset-id="${esc(asset.id)}">${esc(lbl('action_remove_asset', 'Remover asset'))}</button>
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
  const importLabel = options.scope === 'studio'
    ? lbl('action_import_builtin', 'Importar catálogo embutido')
    : lbl('action_import_workspace', 'Importar catálogo do Workspace');

  return `
    <div class="properties-library-detail-header">
      <div>
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
    </div>
    <p class="properties-library-description">${esc(library.description)}</p>
    ${isGitHubSource ? `
      <div class="properties-library-meta">
        ${renderMetaRow(lbl('source_repo', 'Repositório'), `<code class="properties-code">${esc(source!.github!.repo)}</code>`)}
        ${renderMetaRow(lbl('source_ref', 'Referência'), esc(source!.github!.ref))}
        ${renderMetaRow(lbl('source_commit', 'Commit'), `<code class="properties-code">${esc((source!.github!.commit || '—').slice(0, 16))}</code>`)}
        ${renderMetaRow(lbl('source_last_sync', 'Última sincronização'), esc(source!.lastSyncAt ? new Date(source!.lastSyncAt).toLocaleString() : lbl('source_never_synced', 'Nunca sincronizada')))}
      </div>
    ` : ''}
    <div class="properties-library-summary">
      <div><strong>${assets.length}</strong><span>${esc(lbl('summary_assets', 'assets'))}</span></div>
      <div><strong>${skills}</strong><span>${esc(lbl('summary_skills', 'skills'))}</span></div>
      <div><strong>${mcps}</strong><span>${esc(lbl('summary_mcps', 'MCPs'))}</span></div>
      <div><strong>${approved}</strong><span>${esc(lbl('summary_approved', 'aprovados'))}</span></div>
    </div>
    ${assets.length === 0 ? `<p class="properties-library-empty">${esc(isGitHubSource
      ? lbl('detail_empty_remote', 'Esta biblioteca ainda não foi sincronizada. Use Sincronizar GitHub para ler o manifesto (sem baixar assets).')
      : lbl('detail_empty_library', 'Esta biblioteca ainda não tem assets. Importe um catálogo ou crie um derivado.'))}</p>` : ''}
    <div class="properties-library-actions">
      ${isGitHubSource
        ? `<button type="button" class="btn btn-primary-office" data-library-action="sync" data-library-id="${esc(library.id)}">${esc(lbl('action_sync', 'Sincronizar GitHub'))}</button>`
        : `<button type="button" class="btn btn-primary-office" data-library-action="import" data-library-id="${esc(library.id)}">${esc(importLabel)}</button>`}
      <button type="button" class="btn btn-secondary-office" data-library-action="import-file" data-library-id="${esc(library.id)}">${esc(lbl('action_import_file', 'Importar arquivo'))}</button>
      <button type="button" class="btn btn-secondary-office" data-library-action="export" data-library-id="${esc(library.id)}">${esc(lbl('action_export', 'Exportar'))}</button>
      <button type="button" class="btn btn-secondary-office" data-library-action="clone" data-library-id="${esc(library.id)}">${esc(lbl('action_clone', 'Clonar'))}</button>
      ${library.isBuiltin ? '' : `<button type="button" class="btn btn-secondary-office" data-library-action="remove" data-library-id="${esc(library.id)}">${esc(lbl('action_remove_library', 'Remover biblioteca'))}</button>`}
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
  if (root.dataset.libraryBrowserBound === 'true') return;
  root.dataset.libraryBrowserBound = 'true';

  const service = LibraryService.getInstance();
  const state = stateByRoot.get(root) || defaultState(options, options.registry);
  stateByRoot.set(root, state);

  const tree = root.querySelector<HTMLElement>('[data-library-tree]');
  const detail = root.querySelector<HTMLElement>('[data-library-detail]');
  const search = root.querySelector<HTMLInputElement>('[data-library-search]');
  const filter = root.querySelector<HTMLSelectElement>('[data-library-filter]');

  const rerender = (): void => {
    if (tree) tree.innerHTML = renderTree(options.registry, options, state);
    if (detail) detail.innerHTML = renderLibraryDetail(options.registry, options, state);
  };

  search?.addEventListener('input', () => {
    state.search = search.value;
    rerender();
  });

  filter?.addEventListener('change', () => {
    state.kind = filter.value === 'skill' || filter.value === 'mcp' || filter.value === 'agent-template'
      ? filter.value
      : 'all';
    rerender();
  });

  root.addEventListener('click', (event) => {
    const target = event.target as HTMLElement | null;
    if (!target) return;

    const librarySelect = target.closest<HTMLElement>('[data-library-select]');
    if (librarySelect) {
      const id = librarySelect.dataset.librarySelect;
      if (id) {
        state.selectedLibraryId = id;
        state.selectedAssetId = undefined;
        rerender();
      }
      return;
    }

    const assetSelect = target.closest<HTMLElement>('[data-asset-select]');
    if (assetSelect) {
      const id = assetSelect.dataset.assetSelect;
      if (id) {
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

  if (action === 'derive') {
    const derived = await service.deriveAsset(assetId);
    if (derived) {
      state.selectedLibraryId = derived.libraryId;
      state.selectedAssetId = derived.id;
      notify('success', lbl('msg_derived', 'Derivado criado em Minha biblioteca: {{name}}.').replace('{{name}}', derived.name));
    } else {
      notify('error', lbl('msg_derive_failed', 'Não foi possível criar o derivado.'));
    }
    rerender();
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

  if (action === 'remove') {
    if (!confirmAction(lbl('msg_remove_asset_confirm', 'Remover este asset da biblioteca?'))) return;
    options.registry.removeAsset(assetId);
    await service.persist('workspace');
    state.selectedAssetId = undefined;
    notify('success', lbl('msg_removed', 'Asset removido da biblioteca.'));
    rerender();
  }
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
    const result = options.scope === 'studio'
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
        const result = await service.importLibrary(text, { scope: options.scope });
        if (result) {
          state.selectedLibraryId = result.library.id;
          state.selectedAssetId = undefined;
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
    const result = await service.cloneLibrary(libraryId, name, options.scope);
    if (result) {
      state.selectedLibraryId = result.library.id;
      state.selectedAssetId = undefined;
      notify('success', lbl('msg_clone_success', 'Biblioteca clonada: {{name}}.').replace('{{name}}', result.library.name));
    } else {
      notify('error', lbl('msg_clone_failed', 'Não foi possível clonar a biblioteca.'));
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
    notify('success', lbl('msg_library_removed', 'Biblioteca removida.'));
    rerender();
  }
}
