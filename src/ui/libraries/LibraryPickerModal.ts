import { BaseModal } from '../modals/BaseModal.ts';
import { globalEventBus } from '../../core/EventBus.ts';
import { LibraryRegistry } from '../../core/libraries/LibraryRegistry.ts';
import { LibraryService } from '../../core/libraries/LibraryService.ts';
import { ProjectLibraryStore } from '../../core/libraries/ProjectLibraryStore.ts';
import type { ILibraryAsset, ILibraryEntry, LibraryAuditVerdict } from '../../core/libraries/types.ts';
import { t } from '../../i18n/index.ts';

export interface ILibraryPickerOptions {
  kind: 'skill' | 'mcp' | 'agent-template';
  projectId?: string;
  title?: string;
  subtitle?: string;
  onSelect: (asset: ILibraryAsset) => void | Promise<void>;
}

interface IPickerState {
  selectedLibraryId?: string;
  selectedAssetId?: string;
  search: string;
}

function lbl(key: string, fallback: string): string {
  return t(`library_picker.${key}`, fallback);
}

function verdictTone(verdict: LibraryAuditVerdict): 'neutral' | 'success' | 'warning' | 'danger' {
  if (verdict === 'APPROVE') return 'success';
  if (verdict === 'CAUTION') return 'warning';
  if (verdict === 'REJECT') return 'danger';
  return 'neutral';
}

function badge(text: string, tone: 'neutral' | 'success' | 'warning' | 'accent' | 'danger'): string {
  return `<span class="properties-badge properties-badge--${tone}">${text}</span>`;
}

export class LibraryPickerModal extends BaseModal {
  private readonly registry = LibraryRegistry.getInstance();
  private readonly service = LibraryService.getInstance();
  private readonly projectStore = new ProjectLibraryStore();
  private options?: ILibraryPickerOptions;
  private state: IPickerState = { search: '' };

  constructor() {
    super({
      id: 'library-picker-modal',
      title: t('library_picker.title', 'Adicionar da Biblioteca'),
      icon: '📚',
      minWidth: 860,
      minHeight: 560,
      dialogClass: 'library-picker-modal-dialog'
    });
  }

  public override open(options: ILibraryPickerOptions): void {
    this.options = options;
    this.state = { search: '' };

    // O catálogo de MCPs vive no Workspace; garante a biblioteca antes de abrir.
    if ((options.kind === 'mcp' || options.kind === 'agent-template')
      && !this.registry.list('workspace').some(library => !library.isBuiltin)) {
      this.service.ensureWorkspaceLibrary();
    }

    const libraries = this.availableLibraries();
    const withAssets = libraries.filter(library => this.assetsFor(library.id).length > 0);
    const preferred = options.kind === 'mcp'
      ? (withAssets.find(library => !library.isBuiltin) || libraries.find(library => !library.isBuiltin) || libraries[0])
      : (withAssets[0] || libraries[0]);
    this.state.selectedLibraryId = preferred?.id;
    this.state.selectedAssetId = this.firstSelectableAsset(preferred?.id)?.id;
    this.config.title = options.title || t('library_picker.title', 'Adicionar da Biblioteca');
    this.config.subtitle = options.subtitle
      || t('library_picker.subtitle', 'Escolha um asset auditado para incluir no agente.');
    super.open();
  }

  protected override renderBody(): string {
    return `
      <div style="padding: 14px 18px 18px;">
        <div class="properties-library-browser" style="margin-top: 0;">
          <aside class="properties-library-tree-pane">
            <div class="properties-library-toolbar" style="grid-template-columns: 1fr;">
              <input type="search" class="form-input" data-picker-search placeholder="${this.escapeHtml(lbl('search', 'Buscar assets...'))}" value="${this.escapeHtml(this.state.search)}" />
            </div>
            <div class="properties-library-tree" data-picker-tree>
              ${this.renderTree()}
            </div>
          </aside>
          <section class="properties-library-detail-pane" data-picker-detail>
            ${this.renderDetail()}
          </section>
        </div>
      </div>
    `;
  }

  protected override renderFooter(): string {
    const asset = this.selectedAsset();
    const canSelect = Boolean(asset && asset.audit.verdict !== 'REJECT' && asset.audit.verdict !== 'PENDING');
    return `
      <div class="modal-footer office-modal-footer">
        <button type="button" class="btn btn-secondary-office" id="btn-library-picker-cancel">${t('common.close', 'Fechar')}</button>
        <div style="display: flex; gap: 8px; margin-left: auto;">
          ${this.options?.kind === 'skill' ? `<button type="button" class="btn btn-secondary-office" id="btn-library-picker-new">${this.escapeHtml(lbl('create_new', 'Criar novo'))}</button>` : ''}
          <button type="button" class="btn btn-secondary-office" id="btn-library-picker-derive" ${asset ? '' : 'disabled'}>${this.escapeHtml(lbl('derive', 'Duplicar e editar'))}</button>
          <button type="button" class="btn btn-primary-office" id="btn-library-picker-select" ${canSelect ? '' : 'disabled'}>${this.escapeHtml(lbl('select', 'Incluir'))}</button>
        </div>
      </div>
    `;
  }

  protected override bindEvents(): void {
    super.bindEvents();
    const search = this.dialog.querySelector<HTMLInputElement>('[data-picker-search]');
    search?.addEventListener('input', () => {
      this.state.search = search.value;
      this.rerender();
    });

    this.dialog.removeEventListener('click', this.handleDelegatedClick);
    this.dialog.addEventListener('click', this.handleDelegatedClick);

    this.dialog.querySelector('#btn-library-picker-cancel')?.addEventListener('click', () => this.hide());
    this.dialog.querySelector('#btn-library-picker-select')?.addEventListener('click', () => { void this.select(false); });
    this.dialog.querySelector('#btn-library-picker-derive')?.addEventListener('click', () => { void this.select(true); });
    this.dialog.querySelector('#btn-library-picker-new')?.addEventListener('click', () => { void this.createNew(); });
  }

  private handleDelegatedClick = (event: MouseEvent): void => {
    const target = event.target as HTMLElement | null;
    if (!target) return;

    const importButton = target.closest<HTMLElement>('[data-picker-import]');
    if (importButton) {
      void this.importCatalog(importButton.dataset.pickerImport || '');
      return;
    }

    const downloadButton = target.closest<HTMLElement>('[data-picker-download]');
    if (downloadButton) {
      void this.downloadAsset(downloadButton.dataset.pickerDownload || '');
      return;
    }

    const library = target.closest<HTMLElement>('[data-picker-library]');
    if (library) {
      const id = library.dataset.pickerLibrary;
      if (id) {
        this.state.selectedLibraryId = id;
        this.state.selectedAssetId = this.firstSelectableAsset(id)?.id;
        this.rerender();
      }
      return;
    }

    const asset = target.closest<HTMLElement>('[data-picker-asset]');
    if (asset) {
      const id = asset.dataset.pickerAsset;
      if (id) {
        this.state.selectedAssetId = id;
        this.rerender();
      }
    }
  };

  private availableLibraries(): ILibraryEntry[] {
    const all = this.registry.list();
    const projectId = this.options?.projectId;
    let result: ILibraryEntry[];

    if (!projectId) {
      result = all.filter(library => library.isBuiltin || library.scope === 'workspace' || library.scope === 'studio');
    } else {
      const settings = this.projectStore.load(projectId);
      const enabled = new Set(settings.enabledLibraryIds);
      result = settings.enabledLibraryIds.length === 0 && settings.pinnedAssetIds.length === 0
        ? all.filter(library => library.isBuiltin || library.scope === 'workspace')
        : all.filter(library => library.isBuiltin || enabled.has(library.id));
    }

    // Mantém visível a biblioteca do asset selecionado mesmo que ela não esteja
    // habilitada no projeto (ex.: derivado recém-criado).
    const selected = this.state.selectedLibraryId
      ? all.find(library => library.id === this.state.selectedLibraryId)
      : undefined;
    if (selected && !result.some(library => library.id === selected.id)) {
      result = [...result, selected];
    }
    return result;
  }

  private assetsFor(libraryId?: string): ILibraryAsset[] {
    if (!libraryId || !this.options) return [];
    const kind = this.options.kind;
    const query = this.state.search.trim().toLowerCase();
    const pinned = this.options.projectId
      ? new Set(this.projectStore.load(this.options.projectId).pinnedAssetIds)
      : new Set<string>();

    return this.registry.listAssets(libraryId)
      .filter(asset => asset.kind === kind)
      .filter(asset => !query
        || asset.name.toLowerCase().includes(query)
        || asset.description.toLowerCase().includes(query)
        || asset.category.toLowerCase().includes(query))
      .sort((a, b) => {
        const pinnedDelta = Number(pinned.has(b.id)) - Number(pinned.has(a.id));
        return pinnedDelta !== 0 ? pinnedDelta : a.name.localeCompare(b.name);
      });
  }

  private firstSelectableAsset(libraryId?: string): ILibraryAsset | undefined {
    return this.assetsFor(libraryId).find(asset =>
      asset.audit.verdict !== 'REJECT' && asset.audit.verdict !== 'PENDING'
    );
  }

  private selectedAsset(): ILibraryAsset | undefined {
    return this.state.selectedAssetId
      ? this.registry.getAsset(this.state.selectedAssetId)
      : undefined;
  }

  private renderTree(): string {
    const libraries = this.availableLibraries();
    if (libraries.length === 0) {
      return `<div class="properties-library-empty">${this.escapeHtml(lbl('no_libraries', 'Nenhuma biblioteca liberada. Importe um catálogo nas Propriedades.'))}</div>`;
    }
    if (!this.state.selectedLibraryId || !libraries.some(library => library.id === this.state.selectedLibraryId)) {
      this.state.selectedLibraryId = libraries[0].id;
      this.state.selectedAssetId = this.firstSelectableAsset(libraries[0].id)?.id;
    }

    return libraries.map(library => {
      const assets = this.assetsFor(library.id);
      const selected = library.id === this.state.selectedLibraryId;
      return `
        <div class="properties-library-group">
          <button type="button" class="properties-library-item ${selected ? 'active' : ''}" data-picker-library="${this.escapeHtml(library.id)}">
            <span class="properties-library-item-name">${this.escapeHtml(library.name)}</span>
            ${badge(library.isBuiltin ? lbl('kind_builtin', 'Embutida') : lbl('kind_local', 'Local'), library.isBuiltin ? 'success' : 'neutral')}
            <span class="properties-library-item-count">${assets.length}</span>
          </button>
          ${selected ? `
            <div class="properties-library-assets">
              ${assets.length === 0
                ? `<div class="properties-library-assets-empty">${this.escapeHtml(lbl('empty_library', 'Nenhum asset deste tipo. Importe um catálogo.'))}</div>`
                : assets.map(asset => `
                  <button type="button" class="properties-library-asset ${asset.id === this.state.selectedAssetId ? 'active' : ''}" data-picker-asset="${this.escapeHtml(asset.id)}">
                    <span class="properties-library-asset-name">${this.escapeHtml(asset.name)}</span>
                    ${badge(this.escapeHtml(asset.audit.verdict), verdictTone(asset.audit.verdict))}
                  </button>
                `).join('')}
            </div>
          ` : ''}
        </div>
      `;
    }).join('');
  }

  private renderDetail(): string {
    const asset = this.selectedAsset();
    const library = this.state.selectedLibraryId ? this.registry.get(this.state.selectedLibraryId) : undefined;
    if (!asset) {
      const canImportCatalog = this.options?.kind !== 'agent-template';
      return `
        <div class="properties-library-empty">
          ${this.escapeHtml(this.options?.kind === 'agent-template'
            ? lbl('empty_template_detail', 'Nenhum template nesta biblioteca. Salve um template do catálogo de vértices na biblioteca.')
            : lbl('empty_detail', 'Selecione um asset na árvore. Se a biblioteca estiver vazia, importe o catálogo nas Propriedades.'))}
          ${canImportCatalog ? `<div class="properties-library-actions">
            <button type="button" class="btn btn-secondary-office" data-picker-import="${this.escapeHtml(library?.id || '')}">${this.escapeHtml(lbl('import_catalog', 'Importar catálogo'))}</button>
          </div>` : ''}
        </div>
      `;
    }

    const payload = asset.payload.kind === 'skill'
      ? `${asset.payload.tools.length} ${lbl('tools', 'ferramentas')}`
      : asset.payload.kind === 'agent-template'
        ? `Template · ${asset.payload.template.taskType}`
        : `${asset.payload.transport.toUpperCase()} · ${asset.payload.command || asset.payload.url || ''}`;
    const canSelect = asset.audit.verdict !== 'REJECT' && asset.audit.verdict !== 'PENDING';
    const needsDownload = Boolean(asset.remote) && (
      asset.audit.verdict === 'PENDING'
      || asset.status === 'disabled'
      || (asset.payload.kind === 'skill' && !asset.payload.markdownContent)
    );

    return `
      <div class="properties-library-detail-header">
        <div>
          <div class="properties-library-detail-title">${this.escapeHtml(asset.name)}</div>
          <div class="properties-library-detail-subtitle">
            ${badge(asset.kind === 'skill' ? 'Skill' : asset.kind === 'agent-template' ? lbl('kind_template', 'Template') : 'MCP', 'accent')}
            ${badge(this.escapeHtml(asset.audit.verdict), verdictTone(asset.audit.verdict))}
            ${badge(asset.status === 'installed' ? lbl('installed', 'Instalado') : lbl('available', 'Disponível'), asset.status === 'installed' ? 'success' : 'neutral')}
          </div>
        </div>
        <div class="properties-library-detail-breadcrumb">${this.escapeHtml(library?.name || '')}</div>
      </div>
      <p class="properties-library-description">${this.escapeHtml(asset.description)}</p>
      <div class="properties-library-meta">
        <div class="properties-library-meta-item"><span class="properties-library-meta-label">${lbl('source', 'Origem')}</span><span class="properties-library-meta-value">${this.escapeHtml(asset.provenance.sourceKind)} · ${this.escapeHtml(asset.provenance.authority || '')}</span></div>
        <div class="properties-library-meta-item"><span class="properties-library-meta-label">${lbl('license', 'Licença')}</span><span class="properties-library-meta-value">${this.escapeHtml(asset.license)}</span></div>
        <div class="properties-library-meta-item"><span class="properties-library-meta-label">${lbl('content', 'Conteúdo')}</span><span class="properties-library-meta-value">${this.escapeHtml(payload)}</span></div>
        <div class="properties-library-meta-item"><span class="properties-library-meta-label">${lbl('risk', 'Risco')}</span><span class="properties-library-meta-value">${asset.audit.riskScore}/100</span></div>
        ${asset.remote ? `<div class="properties-library-meta-item"><span class="properties-library-meta-label">${lbl('remote_path', 'Caminho remoto')}</span><span class="properties-library-meta-value">${this.escapeHtml(asset.remote.path)}</span></div>` : ''}
      </div>
      ${needsDownload ? `
        <div class="properties-library-actions">
          <button type="button" class="btn btn-primary-office" data-picker-download="${this.escapeHtml(asset.id)}">${this.escapeHtml(lbl('download', 'Baixar e auditar'))}</button>
        </div>
        <p class="properties-hint">${this.escapeHtml(lbl('download_hint', 'O conteúdo só é baixado agora, depois da seleção, e passa por auditoria antes de ser incluído.'))}</p>
      ` : ''}
      ${canSelect
        ? `<p class="properties-hint">${this.escapeHtml(lbl('select_hint', 'Incluir copia um snapshot auditado para o agente. O asset original permanece imutável.'))}</p>`
        : `<p class="properties-note properties-note--warning">${this.escapeHtml(lbl('blocked', 'Este asset não pode ser incluído enquanto estiver rejeitado ou pendente.'))}</p>`}
      ${asset.audit.findings.length + asset.audit.warnings.length > 0
        ? `<ul class="properties-library-findings">
            ${asset.audit.findings.map(finding => `<li>${this.escapeHtml(finding)}</li>`).join('')}
            ${asset.audit.warnings.map(warning => `<li class="is-warning">${this.escapeHtml(warning)}</li>`).join('')}
          </ul>`
        : ''}
    `;
  }

  private rerender(): void {
    const tree = this.dialog.querySelector<HTMLElement>('[data-picker-tree]');
    const detail = this.dialog.querySelector<HTMLElement>('[data-picker-detail]');
    if (tree) tree.innerHTML = this.renderTree();
    if (detail) detail.innerHTML = this.renderDetail();
    this.updateFooter();
  }

  private updateFooter(): void {
    const asset = this.selectedAsset();
    const canSelect = Boolean(asset && asset.audit.verdict !== 'REJECT' && asset.audit.verdict !== 'PENDING');
    const select = this.dialog.querySelector<HTMLButtonElement>('#btn-library-picker-select');
    const derive = this.dialog.querySelector<HTMLButtonElement>('#btn-library-picker-derive');
    if (select) select.disabled = !canSelect;
    if (derive) derive.disabled = !asset;
  }

  private async select(derive: boolean): Promise<void> {
    const selected = this.selectedAsset();
    if (!selected || !this.options) return;

    let asset = selected;
    if (derive) {
      const derived = await this.service.deriveAsset(selected.id);
      if (!derived) {
        this.notify('error', lbl('derive_failed', 'Não foi possível criar o derivado.'));
        return;
      }
      asset = derived;
    }

    await this.options.onSelect(asset);
    this.hide();
  }

  private async importCatalog(libraryId: string): Promise<void> {
    if (!libraryId) return;
    const library = this.registry.get(libraryId);
    if (!library) return;

    const result = library.scope === 'studio'
      ? await this.service.importBuiltinSkills({ libraryId })
      : await this.service.importWorkspaceCatalog({ libraryId });

    this.notify(
      result.rejected.length > 0 ? 'warning' : 'success',
      lbl('import_result', '{{imported}} assets importados; {{rejected}} rejeitados.')
        .replace('{{imported}}', String(result.imported.length))
        .replace('{{rejected}}', String(result.rejected.length))
    );
    this.state.selectedAssetId = this.firstSelectableAsset(libraryId)?.id;
    this.rerender();
  }

  private async downloadAsset(assetId: string): Promise<void> {
    if (!assetId) return;
    const first = await this.service.downloadGitHubAsset(assetId);
    if (!first.ok && first.needsConfirmation) {
      const ok = typeof window !== 'undefined' && typeof window.confirm === 'function'
        && window.confirm(lbl('download_caution_confirm', 'O asset remoto tem alertas de segurança. Baixar e usar mesmo assim?'));
      if (!ok) return;
      const second = await this.service.downloadGitHubAsset(assetId, { allowCaution: true });
      this.notify(
        second.ok ? 'warning' : 'error',
        second.ok
          ? lbl('downloaded_caution', 'Asset baixado com alertas e disponível para revisão.')
          : second.reason || lbl('download_failed', 'Não foi possível baixar o asset.')
      );
    } else if (first.ok) {
      this.notify('success', lbl('downloaded', 'Asset baixado e auditado com sucesso.'));
    } else {
      this.notify('warning', first.reason || lbl('download_failed', 'Não foi possível baixar o asset.'));
    }
    this.state.selectedAssetId = assetId;
    this.rerender();
  }

  private async createNew(): Promise<void> {
    if (!this.options || this.options.kind !== 'skill') return;
    const library = this.state.selectedLibraryId ? this.registry.get(this.state.selectedLibraryId) : undefined;
    const scope = library?.scope === 'studio' ? 'studio' : 'workspace';
    const asset = await this.service.createBlankSkill({
      libraryId: library && !library.isBuiltin ? library.id : undefined,
      scope
    });
    if (!asset) {
      this.notify('error', lbl('create_failed', 'Não foi possível criar a habilidade.'));
      return;
    }
    this.state.selectedLibraryId = asset.libraryId;
    this.state.selectedAssetId = asset.id;
    await this.options.onSelect(asset);
    this.hide();
  }

  private notify(severity: 'success' | 'warning' | 'error', message: string): void {
    globalEventBus.emit('notification:show', {
      severity,
      title: t('library_picker.title', 'Adicionar da Biblioteca'),
      message
    });
  }
}
