import { Graph } from '../../core/Graph.ts';
import { ProjectManager } from '../../core/ProjectManager.ts';
import { globalEventBus } from '../../core/EventBus.ts';
import {
  IProjectManifest,
  IRecentProjectItem,
  IOpenableEntry,
  IOpenableListing,
  IOpenOptions,
  IOpenResult,
  OpenableEntryKind
} from '../../core/types.ts';
import { hashProjectName } from '../../core/hash.ts';
import { dirName, joinPath, pathCrumbs } from '../../core/paths.ts';
import { CodeModal } from '../CodeModal.ts';
import { GlobalDefaultsModal } from '../GlobalDefaultsModal.ts';
import { WorkspaceManager } from '../../core/workspace/WorkspaceManager.ts';
import { t, getLocale, setLocale } from '../../i18n/index.ts';

type OpenPlace = 'recent' | 'computer';
type OpenRecentTab = 'projects' | 'graphs';
export type RecencyGroup = 'today' | 'yesterday' | 'last_week' | 'last_month' | 'older';

const RECENCY_ORDER: RecencyGroup[] = ['today', 'yesterday', 'last_week', 'last_month', 'older'];
const DAY_MS = 24 * 60 * 60 * 1000;

const ENTRY_ICONS: Record<OpenableEntryKind, string> = {
  folder: '📁',
  project: '🗂️',
  package: '📦',
  manifest: '📋',
  json: '📊'
};

// Grupo de data da lista de recentes, como no Abrir do Office.
export function recencyGroup(iso: string | undefined, now: Date = new Date()): RecencyGroup {
  const time = iso ? new Date(iso).getTime() : NaN;
  if (Number.isNaN(time)) return 'older';
  const startOfToday = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime();
  if (time >= startOfToday) return 'today';
  if (time >= startOfToday - DAY_MS) return 'yesterday';
  if (time >= startOfToday - 7 * DAY_MS) return 'last_week';
  if (time >= startOfToday - 30 * DAY_MS) return 'last_month';
  return 'older';
}

export class BackstageView {
  private overlay?: HTMLElement;
  private graph: Graph;
  private projectManager: ProjectManager;
  private codeModal: CodeModal;
  private globalDefaultsModal: GlobalDefaultsModal;
  private activeSection: 'info' | 'new' | 'open' | 'saveas' | 'export' | 'options' = 'info';

  // Estado da Tela "Salvar Como" (Navegação de Discos e Pastas)
  private saveAsCurrentPath: string = '';
  private saveAsDrives: string[] = ['C:\\'];
  private saveAsSubdirectories: Array<{ name: string; path: string }> = [];
  private saveAsFileName: string = '';
  private saveAsFormat: 'arestrix' | 'json' = 'arestrix';

  // Estado da Tela "Abrir" (Recentes / Este Computador / Procurar)
  private openPlace: OpenPlace = 'recent';
  private openRecentTab: OpenRecentTab = 'projects';
  private openQuery: string = '';
  private openListing: IOpenableListing | null = null;
  private openDrives: string[] = [];

  constructor(graph: Graph, codeModal: CodeModal, globalDefaultsModal: GlobalDefaultsModal) {
    this.graph = graph;
    this.codeModal = codeModal;
    this.globalDefaultsModal = globalDefaultsModal;
    this.projectManager = ProjectManager.getInstance(globalEventBus);

    if (typeof document !== 'undefined') {
      this.overlay = document.createElement('div');
      this.overlay.className = 'office-backstage-overlay';
      document.body.appendChild(this.overlay);

      this.render();
      this.bindEvents();

      globalEventBus.on('project:changed', () => {
        if (this.isOpen()) {
          this.render();
        }
      });
      globalEventBus.on('project:loaded', () => {
        if (this.isOpen()) {
          this.render();
        }
      });
      globalEventBus.on('project:recents-updated', () => {
        if (this.isOpen()) {
          this.render();
        }
      });
      globalEventBus.on('locale:changed', () => {
        if (this.isOpen()) {
          this.render();
        }
      });
    }
  }

  public async initSaveAsState(): Promise<void> {
    if (!this.saveAsCurrentPath) {
      this.saveAsCurrentPath = await this.projectManager.getDefaultWorkspaceDirectory();
      if (!this.saveAsCurrentPath || this.saveAsCurrentPath === '.') {
        this.saveAsCurrentPath = 'C:/Projetos';
      }
    }
    this.saveAsCurrentPath = this.saveAsCurrentPath.replace(/\\/g, '/');
    this.saveAsDrives = await this.projectManager.listDrives();
    this.saveAsSubdirectories = await this.projectManager.listSubdirectories(this.saveAsCurrentPath);
    const proj = this.projectManager.getCurrentProject();
    if (!this.saveAsFileName) {
      this.saveAsFileName = (proj.name || 'projeto').toLowerCase().replace(/\s+/g, '_');
    }
  }

  // Este Computador comeca na pasta que contem o projeto ativo, onde costumam estar os projetos irmaos.
  private async loadComputerListing(): Promise<void> {
    if (this.openDrives.length === 0) {
      this.openDrives = await this.projectManager.listDrives();
    }
    if (this.openListing?.exists) {
      this.openListing = await this.projectManager.listOpenableEntries(this.openListing.path);
      return;
    }
    const root = this.projectManager.getCurrentProject().rootPath;
    const start = root && root !== '.' ? dirName(root) : '';
    let listing = await this.projectManager.listOpenableEntries(start);
    if (listing.available && !listing.exists && start) {
      listing = await this.projectManager.listOpenableEntries('');
    }
    this.openListing = listing;
  }

  private async prepareSection(): Promise<void> {
    if (this.activeSection === 'saveas') {
      await this.initSaveAsState();
    } else if (this.activeSection === 'open' && this.openPlace === 'computer') {
      await this.loadComputerListing();
    }
  }

  public async open(section: 'info' | 'new' | 'open' | 'saveas' | 'export' | 'options' = 'info'): Promise<void> {
    this.activeSection = section;
    try {
      await this.projectManager.init(true);
      await this.prepareSection();
    } catch (_) {}
    if (this.overlay) {
      this.render();
      this.overlay.classList.add('open');
    }
  }

  public close(): void {
    if (this.overlay) {
      this.overlay.classList.remove('open');
    }
  }

  public isOpen(): boolean {
    return this.overlay ? this.overlay.classList.contains('open') : false;
  }

  public getOverlay(): HTMLElement | undefined {
    return this.overlay;
  }

  private render(): void {
    if (!this.overlay) return;
    const proj: IProjectManifest = this.projectManager.getCurrentProject();
    const recentList: IRecentProjectItem[] = this.projectManager.listRecentProjects();

    this.overlay.innerHTML = `
      <div class="office-backstage-container">
        <!-- BARRA LATERAL OFFICE (AZUL FLUENT & IDENTIDADE OFICIAL ARESTRIX) -->
        <div class="office-backstage-sidebar">
          <div class="backstage-brand-header" style="padding: 16px 20px 12px 20px; display: flex; align-items: center; gap: 10px; border-bottom: 1px solid rgba(255,255,255,0.12); margin-bottom: 6px;">
            <div style="width: 28px; height: 28px; flex-shrink: 0;">
              <svg viewBox="0 0 124 124" width="28" height="28" fill="none" style="display:block;">
                <g stroke="#F4F2EE" stroke-width="14" stroke-linecap="round">
                  <line x1="62" y1="21" x2="22" y2="103"/>
                  <line x1="62" y1="21" x2="102" y2="103"/>
                </g>
                <line x1="37" y1="73" x2="87" y2="73" stroke="#E8590C" stroke-width="12" stroke-linecap="round"/>
                <circle cx="62" cy="21" r="11" fill="#F4F2EE"/>
                <circle cx="22" cy="103" r="11" fill="#F4F2EE"/>
                <circle cx="102" cy="103" r="11" fill="#F4F2EE"/>
              </svg>
            </div>
            <div>
              <div style="font-size: 15px; font-weight: 800; color: #F4F2EE; letter-spacing: -0.3px; line-height: 1;">Arestrix</div>
              <div style="font-size: 9px; font-weight: 700; color: #E8590C; letter-spacing: 1.2px; text-transform: uppercase; margin-top: 2px;">Studio 2.0</div>
            </div>
          </div>

          <button class="backstage-back-btn" id="btn-backstage-close" title="${t('backstage.sidebar.return')} (Esc)">
            <span class="backstage-back-icon">←</span>
            <span>${t('backstage.sidebar.return')}</span>
          </button>

          <ul class="backstage-nav-list">
            <li class="backstage-nav-item ${this.activeSection === 'info' ? 'active' : ''}" data-section="info">
              <span>ℹ️</span> ${t('backstage.sidebar.info')}
            </li>
            <li class="backstage-nav-item ${this.activeSection === 'new' ? 'active' : ''}" data-section="new">
              <span>📄</span> ${t('backstage.sidebar.new')}
            </li>
            <li class="backstage-nav-item ${this.activeSection === 'open' ? 'active' : ''}" data-section="open">
              <span>📂</span> ${t('backstage.sidebar.open')}
            </li>
            <li class="backstage-nav-item" id="btn-backstage-save">
              <span>💾</span> ${t('backstage.sidebar.save_file')}
            </li>
            <li class="backstage-nav-item ${this.activeSection === 'saveas' ? 'active' : ''}" data-section="saveas">
              <span>📥</span> ${t('backstage.sidebar.saveas')}
            </li>
            <li class="backstage-nav-item ${this.activeSection === 'export' ? 'active' : ''}" data-section="export">
              <span>📤</span> ${t('backstage.sidebar.export')}
            </li>
            <li class="backstage-nav-item" id="btn-backstage-github-repos">
              <span>🌐</span> ${t('github_repos.title')}
            </li>
            <li class="backstage-nav-item ${this.activeSection === 'options' ? 'active' : ''}" data-section="options">
              <span>⚙️</span> ${t('backstage.sidebar.options')}
            </li>
          </ul>
        </div>

        <!-- PAINEL PRINCIPAL DE CONTEÚDO -->
        <div class="office-backstage-main">
          ${this.renderSectionContent(proj, recentList)}
        </div>
      </div>
    `;

    this.bindInternalEvents();
  }

  private renderRecentProjectsTable(recentList: IRecentProjectItem[]): string {
    if (!recentList || recentList.length === 0) {
      return `
        <div style="padding: 20px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 8px; color: #64748b; font-size: 13px;">
          ${t('backstage.recent.empty')}
        </div>
      `;
    }

    return `
      <table class="backstage-mru-table" style="width: 100%; border-collapse: collapse;">
        <thead>
          <tr>
            <th style="text-align: left; padding: 10px 14px;">${t('backstage.recent.col_name')}</th>
            <th style="text-align: left; padding: 10px 14px;">${t('backstage.recent.col_path')}</th>
            <th style="text-align: left; padding: 10px 14px;">${t('backstage.recent.col_last_access')}</th>
            <th style="text-align: right; padding: 10px 14px;">${t('backstage.recent.col_action')}</th>
          </tr>
        </thead>
        <tbody>
          ${recentList.map(item => `
            <tr>
              <td style="padding: 10px 14px;">
                <strong style="color: #0f172a; font-size: 13px;">${this.escapeHtml(item.name)}</strong>
              </td>
              <td style="padding: 10px 14px;">
                <code style="color: #475569; font-size: 11.5px; background: #f1f5f9; padding: 2px 6px; border-radius: 4px;">${this.escapeHtml(item.rootPath)}</code>
              </td>
              <td style="padding: 10px 14px; font-size: 12px; color: #64748b;">
                ${item.lastOpened ? new Date(item.lastOpened).toLocaleString() : t('backstage.recent.recently')}
              </td>
              <td style="text-align: right; padding: 10px 14px;">
                ${this.renderRecentActionsSelect(item)}
              </td>
            </tr>
          `).join('')}
        </tbody>
      </table>
    `;
  }

  private renderRecentActionsSelect(item: IRecentProjectItem, extraClass = ''): string {
    return `
      <select class="recent-project-action-select backstage-project-action-select ${extraClass}" data-id="${this.escapeHtml(item.projectId)}" data-path="${this.escapeHtml(item.rootPath)}" data-name="${this.escapeHtml(item.name)}">
        <option value="" disabled selected>${t('backstage.recent.options')}</option>
        <option value="open">📂 ${t('backstage.recent.open')}</option>
        <option value="manage">⚙️ ${t('backstage.recent.manage')}</option>
        <option value="delete">🗑️ ${t('backstage.recent.delete')}</option>
      </select>
    `;
  }

  // --- Tela "Abrir" (inspirada no Abrir do Office): locais a esquerda, busca e lista a direita ---

  private renderOpenSection(proj: IProjectManifest, recentList: IRecentProjectItem[]): string {
    const isComputer = this.openPlace === 'computer';
    return `
      <h2 class="backstage-header-title">${t('backstage.open.title')}</h2>
      <p class="backstage-header-sub">${t('backstage.open.subtitle')}</p>

      <div class="backstage-open-layout">
        <nav class="backstage-open-places">
          ${this.renderOpenPlace('recent', '🕘', t('backstage.open.place_recent'))}
          <div class="backstage-open-places-sep"></div>
          ${this.renderOpenPlace('computer', '💻', t('backstage.open.place_computer'))}
          <div class="backstage-open-places-sep"></div>
          <button type="button" class="backstage-open-place" id="btn-open-browse" title="${t('backstage.open.place_browse_tip')}">
            <span class="backstage-open-place-icon">📂</span><span>${t('backstage.open.place_browse')}</span>
          </button>
          <input type="file" id="backstage-open-file-input" accept=".arestrix,.json" style="display: none;" />
        </nav>

        <section class="backstage-open-content">
          <label class="backstage-open-search">
            <span aria-hidden="true">🔍</span>
            <input type="search" id="backstage-open-search" value="${this.escapeHtml(this.openQuery)}" placeholder="${isComputer ? t('backstage.open.filter_folder_placeholder') : t('backstage.open.search_placeholder')}" />
          </label>
          ${isComputer ? this.renderOpenComputer() : this.renderOpenRecent(proj, recentList)}
          <div class="backstage-open-empty" id="backstage-open-no-matches" style="display: none;">${t('backstage.open.no_matches')}</div>
        </section>
      </div>
    `;
  }

  private renderOpenPlace(place: OpenPlace, icon: string, label: string): string {
    return `
      <button type="button" class="backstage-open-place ${this.openPlace === place ? 'active' : ''}" data-open-place="${place}">
        <span class="backstage-open-place-icon">${icon}</span><span>${label}</span>
      </button>
    `;
  }

  private renderOpenListHeader(metaLabel: string): string {
    return `
      <div class="backstage-open-list-header">
        <span>${t('backstage.open.col_name')}</span>
        <span>${metaLabel}</span>
      </div>
    `;
  }

  private renderOpenRow(attrs: string, icon: string, name: string, detail: string, meta: string, trailing = ''): string {
    return `
      <div class="backstage-open-row" role="button" tabindex="0" ${attrs}>
        <span class="backstage-open-row-icon">${icon}</span>
        <span class="backstage-open-row-text">
          <span class="backstage-open-row-name">${name}</span>
          <span class="backstage-open-row-detail">${detail}</span>
        </span>
        <span class="backstage-open-row-meta">${meta}</span>
        ${trailing}
      </div>
    `;
  }

  private renderOpenRecent(proj: IProjectManifest, recentList: IRecentProjectItem[]): string {
    const tab = this.openRecentTab;
    return `
      <div class="backstage-open-tabs" role="tablist">
        <button type="button" role="tab" class="backstage-open-tab ${tab === 'projects' ? 'active' : ''}" data-open-tab="projects" aria-selected="${tab === 'projects'}">${t('backstage.open.tab_projects')}</button>
        <button type="button" role="tab" class="backstage-open-tab ${tab === 'graphs' ? 'active' : ''}" data-open-tab="graphs" aria-selected="${tab === 'graphs'}">${t('backstage.open.tab_graphs')}</button>
      </div>
      ${tab === 'projects' ? this.renderOpenRecentProjects(recentList) : this.renderOpenGraphs(proj)}
    `;
  }

  private renderOpenRecentProjects(recentList: IRecentProjectItem[]): string {
    if (!recentList || recentList.length === 0) {
      return `<div class="backstage-open-empty">${t('backstage.recent.empty')}</div>`;
    }
    const now = new Date();
    const groups = new Map<RecencyGroup, IRecentProjectItem[]>();
    for (const item of recentList) {
      const group = recencyGroup(item.lastOpened, now);
      groups.set(group, [...(groups.get(group) || []), item]);
    }
    return `
      ${this.renderOpenListHeader(t('backstage.open.col_last_opened'))}
      <div class="backstage-open-list">
        ${RECENCY_ORDER.filter(g => groups.has(g)).map(g => `
          <div class="backstage-open-group">
            <div class="backstage-open-group-title">${t(`backstage.open.group_${g}`)}</div>
            ${groups.get(g)!.map(item => this.renderOpenRow(
              `data-open-action="recent" data-id="${this.escapeHtml(item.projectId)}" data-path="${this.escapeHtml(item.rootPath)}" data-name="${this.escapeHtml(item.name)}" data-search="${this.escapeHtml(`${item.name} ${item.rootPath}`.toLowerCase())}"`,
              ENTRY_ICONS.project,
              this.escapeHtml(item.name),
              this.escapeHtml(item.rootPath),
              item.lastOpened ? this.formatDateTime(item.lastOpened) : t('backstage.recent.recently'),
              this.renderRecentActionsSelect(item, 'backstage-open-row-actions')
            )).join('')}
          </div>
        `).join('')}
      </div>
    `;
  }

  private renderOpenGraphs(proj: IProjectManifest): string {
    const pipelines = proj.pipelines || [];
    if (pipelines.length === 0) {
      return `<div class="backstage-open-empty">${t('backstage.open.empty_graphs')}</div>`;
    }
    return `
      ${this.renderOpenListHeader(t('backstage.open.col_type'))}
      <div class="backstage-open-list">
        <div class="backstage-open-group">
          <div class="backstage-open-group-title">${t('backstage.open.graphs_group', { name: this.escapeHtml(proj.name) })}</div>
          ${pipelines.map(p => this.renderOpenRow(
            `data-open-action="pipeline" data-pipeline-file="${this.escapeHtml(p.file)}" data-search="${this.escapeHtml(`${p.name} ${p.file}`.toLowerCase())}"`,
            ENTRY_ICONS.json,
            this.escapeHtml(p.name),
            this.escapeHtml(joinPath(proj.rootPath, p.file)),
            p.isPrimary
              ? `<span class="badge-inherited" style="font-size: 10px; padding: 2px 6px; border-radius: 4px; font-weight: 700;">${t('backstage.open.badge_primary')}</span>`
              : t('backstage.open.badge_subflow')
          )).join('')}
        </div>
      </div>
    `;
  }

  private renderOpenComputer(): string {
    const listing = this.openListing;
    if (!listing || !listing.available) {
      return `<div class="backstage-open-empty">${t('backstage.open.desktop_only')}</div>`;
    }

    const drives = this.openDrives.map(drive => {
      const clean = drive.replace(/\\/g, '/');
      const active = listing.path.toUpperCase().startsWith(clean.replace(/\/$/, '').toUpperCase());
      return `<button type="button" class="backstage-open-drive ${active ? 'active' : ''}" data-open-navigate="${this.escapeHtml(clean)}">💾 ${this.escapeHtml(drive)}</button>`;
    }).join('');
    const crumbs = pathCrumbs(listing.path).map((c, i) => `
      ${i > 0 ? '<span class="backstage-open-crumb-sep">›</span>' : ''}
      <button type="button" class="backstage-open-crumb" data-open-navigate="${this.escapeHtml(c.path)}">${this.escapeHtml(c.label)}</button>
    `).join('');

    let body: string;
    if (!listing.exists) {
      body = `<div class="backstage-open-empty">${t('backstage.open.folder_missing', { path: this.escapeHtml(listing.path) })}</div>`;
    } else if (listing.entries.length === 0) {
      body = `<div class="backstage-open-empty">${t('backstage.open.empty_folder')}</div>`;
    } else {
      body = `
        ${this.renderOpenListHeader(t('backstage.open.col_modified'))}
        <div class="backstage-open-list">
          ${listing.entries.map(entry => this.renderDiskEntryRow(entry)).join('')}
        </div>
        ${listing.truncated ? `<div class="backstage-open-note">${t('backstage.open.truncated', { count: listing.entries.length })}</div>` : ''}
      `;
    }

    return `
      <div class="backstage-open-pathbar">
        <div class="backstage-open-drives">
          <span class="backstage-open-drives-label">${t('backstage.open.drives_label')}</span>
          ${drives}
        </div>
        <div class="backstage-open-location">
          <button type="button" class="backstage-open-nav-btn" id="btn-open-up" title="${t('backstage.open.up_tip')}" ${listing.parent ? '' : 'disabled'}>⬆ ${t('backstage.open.btn_up')}</button>
          <button type="button" class="backstage-open-nav-btn" id="btn-open-refresh">🔄 ${t('backstage.open.btn_refresh')}</button>
          <nav class="backstage-open-breadcrumb">${crumbs}</nav>
        </div>
      </div>
      ${body}
    `;
  }

  // Pasta comum navega; projeto, pacote, manifesto e JSON abrem. Pasta de projeto tem tambem o botao de entrar.
  private renderDiskEntryRow(entry: IOpenableEntry): string {
    const action = entry.kind === 'folder' ? 'navigate' : 'disk';
    const enter = entry.kind === 'project'
      ? `<button type="button" class="backstage-open-enter" data-open-navigate="${this.escapeHtml(entry.path)}" title="${t('backstage.open.enter_folder_tip')}">📁 ›</button>`
      : '';
    return this.renderOpenRow(
      `data-open-action="${action}" data-path="${this.escapeHtml(entry.path)}" data-search="${this.escapeHtml(entry.name.toLowerCase())}"`,
      ENTRY_ICONS[entry.kind] || ENTRY_ICONS.json,
      this.escapeHtml(entry.name),
      t(`backstage.open.kind_${entry.kind}`),
      entry.modified ? this.formatDateTime(entry.modified) : '',
      enter
    );
  }

  private formatDateTime(value: string | number): string {
    const date = new Date(value);
    if (Number.isNaN(date.getTime())) return '';
    try {
      return date.toLocaleString(getLocale(), { dateStyle: 'short', timeStyle: 'short' });
    } catch (_) {
      return date.toLocaleString();
    }
  }

  private renderSectionContent(proj: IProjectManifest, recentList: IRecentProjectItem[]): string {
    switch (this.activeSection) {
      case 'info':
        return `
          <h2 class="backstage-header-title">${t('backstage.info.title')}</h2>
          <p class="backstage-header-sub">${t('backstage.info.subtitle')}</p>

          <div class="backstage-cards-grid">
            <div class="backstage-info-card" style="min-width: 0; max-width: 100%; box-sizing: border-box; overflow: hidden;">
              <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; gap: 8px;">
                <h4 style="margin: 0; min-width: 0;">📁 ${t('backstage.info.card_ident')}</h4>
                <span class="badge-inherited" style="font-size: 9px; padding: 1px 6px; flex-shrink: 0;">📦 ${t('backstage.info.scope_proj')}</span>
              </div>
              <p style="word-break: break-word; overflow-wrap: anywhere; margin-bottom: 8px;"><strong>${t('backstage.info.name_label')}</strong> ${this.escapeHtml(proj.name)}</p>
              <p style="word-break: break-word; overflow-wrap: anywhere; margin-bottom: 8px;">
                <strong>${t('backstage.info.id_label')}</strong>
                <code style="display: block; margin-top: 4px; word-break: break-all; overflow-wrap: anywhere; white-space: pre-wrap; background: #f1f5f9; padding: 4px 8px; border-radius: 4px; color: #334155; font-size: 11px; border: 1px solid #e2e8f0; max-width: 100%; box-sizing: border-box;">${this.escapeHtml(proj.projectId)}</code>
              </p>
              <p style="word-break: break-word; overflow-wrap: anywhere; margin-bottom: 8px;"><strong>${t('backstage.info.version_label')}</strong> ${this.escapeHtml(proj.version)}</p>
              <p style="word-break: break-word; overflow-wrap: anywhere; margin-bottom: 8px;"><strong>${t('backstage.info.desc_label')}</strong> ${this.escapeHtml(proj.description || t('backstage.info.no_desc'))}</p>
            </div>

            <div class="backstage-info-card" style="min-width: 0; max-width: 100%; box-sizing: border-box; overflow: hidden;">
              <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; gap: 8px;">
                <h4 style="margin: 0; min-width: 0;">🛠️ ${t('backstage.info.card_storage')}</h4>
                <span class="badge-inherited" style="font-size: 9px; padding: 1px 6px; flex-shrink: 0;">🛠️ ${t('backstage.info.scope_workspace')}</span>
              </div>
              <p style="word-break: break-word; overflow-wrap: anywhere; margin-bottom: 8px;"><strong>${t('backstage.info.persist_type')}</strong> ${proj.database?.type || t('backstage.info.sqlite_default')}</p>
              <p style="word-break: break-word; overflow-wrap: anywhere; margin-bottom: 8px;">
                <strong>${t('backstage.info.db_file')}</strong>
                <code style="display: block; margin-top: 4px; word-break: break-all; overflow-wrap: anywhere; white-space: pre-wrap; background: #f1f5f9; padding: 4px 8px; border-radius: 4px; color: #334155; font-size: 11px; border: 1px solid #e2e8f0; max-width: 100%; box-sizing: border-box;">${this.escapeHtml(proj.database?.filePath || 'storage/database.sqlite')}</code>
              </p>
              <p style="word-break: break-word; overflow-wrap: anywhere; margin-bottom: 8px;">
                <strong>${t('backstage.info.phys_dir')}</strong>
                <code style="display: block; margin-top: 4px; word-break: break-all; overflow-wrap: anywhere; white-space: pre-wrap; background: #f1f5f9; padding: 4px 8px; border-radius: 4px; color: #334155; font-size: 11px; border: 1px solid #e2e8f0; max-width: 100%; box-sizing: border-box;">${this.escapeHtml(proj.rootPath)}</code>
              </p>
              <p style="word-break: break-word; overflow-wrap: anywhere; margin-bottom: 8px;">
                <strong>${t('backstage.info.output_dir')}</strong>
                <code style="display: block; margin-top: 4px; word-break: break-all; overflow-wrap: anywhere; white-space: pre-wrap; background: #f1f5f9; padding: 4px 8px; border-radius: 4px; color: #334155; font-size: 11px; border: 1px solid #e2e8f0; max-width: 100%; box-sizing: border-box;">${this.escapeHtml(proj.outputDir || 'Output')}</code>
              </p>
            </div>

            <div class="backstage-info-card" style="min-width: 0; max-width: 100%; box-sizing: border-box; overflow: hidden;">
              <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px; gap: 8px;">
                <h4 style="margin: 0; min-width: 0;">⚡ ${t('backstage.info.card_graph')}</h4>
                <span class="badge-inherited" style="font-size: 9px; padding: 1px 6px; flex-shrink: 0;">📦 ${t('backstage.info.scope_proj')}</span>
              </div>
              <p style="margin-bottom: 8px;"><strong>${t('backstage.info.active_nodes')}</strong> ${this.graph.getAllNodes().length}</p>
              <p style="margin-bottom: 8px;"><strong>${t('backstage.info.edges_links')}</strong> ${this.graph.getAllEdges().length}</p>
              <p style="margin-bottom: 8px;"><strong>${t('backstage.info.registered_pipelines')}</strong> ${proj.pipelines ? proj.pipelines.length : 0}</p>
            </div>
          </div>

          <div style="margin-top: 14px;">
            <h4 style="font-size: 14px; font-weight: 700; color: #0f172a; margin-bottom: 8px;">${t('backstage.info.quick_maintenance')}</h4>
            <div style="display: flex; gap: 10px;">
              <button class="tool-btn btn-secondary" id="btn-quick-sync-db" style="font-weight: 700;">🔄 ${t('backstage.info.btn_sync_db')}</button>
              <button class="tool-btn btn-secondary" id="btn-quick-clean-cache" style="font-weight: 700;">🧹 ${t('backstage.info.btn_clean_cache')}</button>
            </div>
          </div>

          <!-- LISTA DE PROJETOS RECENTES (ESTILO OFFICE) -->
          <div style="margin-top: 26px;">
            <h3 class="backstage-header-title" style="font-size: 16px; margin-bottom: 4px;">📂 ${t('backstage.info.recents_title')}</h3>
            <p class="backstage-header-sub" style="margin-bottom: 12px;">${t('backstage.info.recents_subtitle')}</p>
            ${this.renderRecentProjectsTable(recentList)}
          </div>
        `;

      case 'new':
        return `
          <h2 class="backstage-header-title">${t('backstage.new.title')}</h2>
          <p class="backstage-header-sub">${t('backstage.new.subtitle')}</p>

          <div style="max-width: 650px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 12px; padding: 24px; margin-bottom: 28px;">
            <div style="margin-bottom: 16px;">
              <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">${t('backstage.new.project_name_label')}</label>
              <input type="text" id="backstage-new-name" placeholder="${t('backstage.new.project_name_placeholder')}" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 6px; font-size: 13px; box-sizing: border-box;">
            </div>

            <div style="margin-bottom: 16px;">
              <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">${t('backstage.new.dir_storage_label')}</label>
              <input type="text" id="backstage-new-path" placeholder="${t('backstage.new.dir_placeholder')}" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 6px; font-size: 13px; box-sizing: border-box;">
              <span style="font-size: 11px; color: #64748b; margin-top: 4px; display: block;">${t('backstage.new.dir_hint')}</span>
            </div>

            <div style="margin-bottom: 20px;">
              <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">${t('backstage.new.desc_label')}</label>
              <textarea id="backstage-new-desc" rows="3" placeholder="${t('backstage.new.desc_placeholder')}" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 6px; font-size: 13px; box-sizing: border-box;"></textarea>
            </div>

            <div style="display: flex; gap: 12px;">
              <button class="tool-btn btn-action" id="btn-backstage-create-submit" style="background: #0f6cbd; color: #fff; font-weight: 700; padding: 10px 22px; height: 38px; font-size: 13px;">
                🚀 ${t('backstage.new.btn_create_submit')}
              </button>
            </div>
          </div>

          <!-- LISTA DE PROJETOS RECENTES (REPETIDA NA TELA NOVO PROJETO) -->
          <div>
            <h3 class="backstage-header-title" style="font-size: 16px; margin-bottom: 4px;">📂 ${t('backstage.new.recents_title')}</h3>
            <p class="backstage-header-sub" style="margin-bottom: 12px;">${t('backstage.new.recents_subtitle')}</p>
            ${this.renderRecentProjectsTable(recentList)}
          </div>
        `;

      case 'open':
        return this.renderOpenSection(proj, recentList);

      case 'saveas':
        return `
          <h2 class="backstage-header-title">${t('backstage.sidebar.saveas')}</h2>
          <p class="backstage-header-sub">${t('backstage.saveas.saveas_desc')}</p>

          <!-- BARRA DE DISCOS & LOCALIZAÇÃO RÁPIDA -->
          <div style="background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 12px; padding: 18px 22px; margin-bottom: 22px;">
            <div style="display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 12px; margin-bottom: 14px;">
              <div style="display: flex; align-items: center; gap: 8px;">
                <span style="font-size: 12px; font-weight: 700; color: #475569; text-transform: uppercase; letter-spacing: 0.5px;">${t('backstage.saveas.drives')}</span>
                <div class="saveas-drives-list" style="display: flex; gap: 8px; flex-wrap: wrap;">
                  ${this.saveAsDrives.map(drive => {
                    const cleanDrive = drive.replace(/\\/g, '/');
                    const isCurrent = this.saveAsCurrentPath.toUpperCase().startsWith(cleanDrive.replace(/\/$/, '').toUpperCase());
                    return `
                      <button class="tool-btn btn-saveas-drive ${isCurrent ? 'active-drive' : ''}" data-drive="${this.escapeHtml(cleanDrive)}" style="font-weight: 700; padding: 6px 14px; font-size: 12px; border-radius: 6px; ${isCurrent ? 'background: #0f6cbd; color: #ffffff; border: 1px solid #0f6cbd;' : 'background: #ffffff; color: #0f172a; border: 1px solid #cbd5e1;'}">
                        💾 ${this.escapeHtml(drive)}
                      </button>
                    `;
                  }).join('')}
                </div>
              </div>

              <button class="tool-btn btn-secondary" id="btn-saveas-browse-native" style="font-weight: 700; padding: 6px 16px; font-size: 12px; height: 32px; display: inline-flex; align-items: center; gap: 6px;">
                <span>📂</span> ${t('backstage.saveas.btn_browse_windows')}
              </button>
            </div>

            <!-- BARRA DE NAVEGAÇÃO DE DIRETÓRIO (BREADCRUMB & SUBIR NÍVEL) -->
            <div style="display: flex; align-items: center; gap: 10px; background: #ffffff; border: 1px solid #cbd5e1; border-radius: 8px; padding: 6px 12px;">
              <button class="tool-btn btn-secondary" id="btn-saveas-up-dir" title="${t('backstage.saveas.up_tip')}" style="padding: 4px 10px; font-size: 12px; font-weight: 700; height: 28px;">
                📁 ⬆ ${t('backstage.saveas.btn_up')}
              </button>
              <button class="tool-btn btn-secondary" id="btn-saveas-refresh-dir" title="${t('backstage.saveas.refresh_tip')}" style="padding: 4px 10px; font-size: 12px; font-weight: 700; height: 28px;">
                🔄 ${t('backstage.saveas.btn_refresh')}
              </button>

              <div style="flex: 1; display: flex; align-items: center; gap: 4px; overflow-x: auto; font-family: 'IBM Plex Mono', monospace; font-size: 12px; color: #334155; padding: 2px 4px;">
                <span style="color: #64748b; font-weight: 700;">${t('backstage.saveas.location_prefix')}</span>
                <span style="font-weight: 600; color: #0f172a;" id="saveas-current-path-display">${this.escapeHtml(this.saveAsCurrentPath)}</span>
              </div>
            </div>
          </div>

          <!-- ÁREA DE SUBPASTAS PARA NAVEGAÇÃO INTERATIVA -->
          <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 12px; padding: 18px 22px; margin-bottom: 22px;">
            <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px;">
              <h4 style="font-size: 13px; font-weight: 700; color: #0f172a; text-transform: uppercase; letter-spacing: 0.5px;">${t('backstage.saveas.subfolders_in_dir')}</h4>
              <span style="font-size: 11.5px; color: #64748b;">${t('backstage.saveas.subfolders_count', { count: this.saveAsSubdirectories.length })}</span>
            </div>

            <div class="saveas-folders-grid" style="display: grid; grid-template-columns: repeat(auto-fill, minmax(210px, 1fr)); gap: 10px; max-height: 240px; overflow-y: auto; padding: 4px;">
              ${this.saveAsSubdirectories.length > 0 ? this.saveAsSubdirectories.map(sub => `
                <div class="saveas-folder-card" data-path="${this.escapeHtml(sub.path)}" style="display: flex; align-items: center; gap: 8px; padding: 10px 14px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 8px; cursor: pointer; transition: all 0.15s ease;">
                  <span style="font-size: 18px;">📁</span>
                  <span style="font-size: 12.5px; font-weight: 600; color: #1e293b; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;">${this.escapeHtml(sub.name)}</span>
                </div>
              `).join('') : `
                <div style="grid-column: 1 / -1; padding: 24px; text-align: center; color: #64748b; font-size: 12.5px; background: #f8fafc; border-radius: 8px;">
                  ${t('backstage.saveas.empty_subfolders')}
                </div>
              `}
            </div>
          </div>

          <!-- PAINEL DE SALVAMENTO (ESTILO DIÁLOGO OFFICE SAVE AS) -->
          <div style="background: #f8fafc; border: 1px solid #cbd5e1; border-radius: 12px; padding: 20px 24px;">
            <h4 style="font-size: 14px; font-weight: 700; color: #0f172a; margin-bottom: 14px;">${t('backstage.saveas.pkg_props_title')}</h4>

            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 16px; margin-bottom: 16px;">
              <div>
                <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">${t('backstage.saveas.filename_field')}</label>
                <input type="text" id="saveas-input-filename" value="${this.escapeHtml(this.saveAsFileName)}" placeholder="${t('backstage.saveas.filename_placeholder')}" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 6px; font-size: 13px; font-weight: 600; color: #0f172a; box-sizing: border-box;" />
              </div>

              <div>
                <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">${t('backstage.saveas.filetype_field')}</label>
                <select id="saveas-select-format" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 6px; font-size: 13px; font-weight: 600; color: #0f172a; box-sizing: border-box; background: #ffffff;">
                  <option value="arestrix" ${this.saveAsFormat === 'arestrix' ? 'selected' : ''}>${t('backstage.saveas.format_arestrix')}</option>
                  <option value="json" ${this.saveAsFormat === 'json' ? 'selected' : ''}>${t('backstage.saveas.format_json')}</option>
                </select>
              </div>
            </div>

            <div style="display: flex; align-items: center; justify-content: space-between; flex-wrap: wrap; gap: 12px; padding-top: 10px; border-top: 1px solid #e2e8f0;">
              <div style="font-size: 12px; color: #475569;">
                <span>${t('backstage.saveas.final_dest')}</span>
                <code id="saveas-path-preview" style="background: #e2e8f0; padding: 3px 8px; border-radius: 4px; font-family: 'IBM Plex Mono', monospace; font-size: 11.5px; color: #0f172a;">${this.escapeHtml(this.saveAsCurrentPath)}/${this.escapeHtml(this.saveAsFileName)}.${this.saveAsFormat}</code>
              </div>

              <button class="tool-btn btn-action" id="btn-saveas-submit" style="background: #0f6cbd; color: #ffffff; font-weight: 700; padding: 10px 26px; height: 38px; font-size: 13px; display: inline-flex; align-items: center; gap: 8px;">
                <span>💾</span> ${t('backstage.saveas.btn_save_package')}
              </button>
            </div>
          </div>
        `;

      case 'export':
        return `
          <h2 class="backstage-header-title">${t('backstage.export.title')}</h2>
          <p class="backstage-header-sub">${t('backstage.export.export_sub')}</p>

          <div class="backstage-cards-grid">
            <div class="backstage-info-card">
              <h4>💻 ${t('backstage.export.cpp_title')}</h4>
              <p>${t('backstage.export.cpp_desc')}</p>
              <button class="tool-btn btn-action" id="btn-backstage-open-codemodal" style="background: #0f6cbd; color: white; font-weight: 700;">${t('backstage.export.btn_see_cpp')}</button>
            </div>

            <div class="backstage-info-card">
              <h4>🛠️ ${t('backstage.export.cmake_title')}</h4>
              <p>${t('backstage.export.cmake_desc')}</p>
              <button class="tool-btn btn-secondary" id="btn-backstage-export-cmake" style="font-weight: 700;">${t('backstage.export.btn_gen_cmake')}</button>
            </div>

            <div class="backstage-info-card">
              <h4>🖼️ ${t('backstage.export.svg_title')}</h4>
              <p>${t('backstage.export.svg_desc')}</p>
              <button class="tool-btn btn-secondary" id="btn-backstage-export-svg" style="font-weight: 700;">${t('backstage.export.btn_export_svg')}</button>
            </div>
          </div>
        `;

      case 'options': {
        const wm = WorkspaceManager.getInstance();
        const dirs = wm.getWorkspaceDirectories();
        const approvedSkills = wm.getApprovedSkills();
        const approvedMcps = wm.getApprovedMcps();
        const currentLocale = getLocale();

        return `
          <h2 class="backstage-header-title">${t('backstage.options.title')}</h2>
          <p class="backstage-header-sub">${t('backstage.options.subtitle')}</p>

          <!-- 0. IDIOMA DO STUDIO (POLIGLOTA / I18N) -->
          <div class="backstage-info-card" style="margin-bottom: 24px; background: #ffffff; border: 1px solid #cbd5e1; border-radius: 8px; padding: 16px 20px; box-sizing: border-box;">
            <div style="display: flex; justify-content: space-between; align-items: center; min-height: 36px;">
              <div style="display: flex; align-items: center; gap: 10px; line-height: 1;">
                <span style="font-size: 20px; line-height: 1; display: inline-flex; align-items: center;">🌐</span>
                <span style="font-size: 14.5px; font-weight: 700; color: #0f172a; line-height: 1; display: inline-flex; align-items: center;">${t('backstage.options.language_group')}</span>
              </div>
              <div style="display: flex; align-items: center; gap: 12px; line-height: 1;">
                <span style="font-size: 12.5px; color: #64748b; font-weight: 600; line-height: 1; display: inline-flex; align-items: center;">${t('backstage.options.current_lang_label')}</span>
                <select id="backstage-language-select" class="backstage-language-dropdown" style="min-width: 220px; height: 34px; padding: 4px 10px; font-size: 12.5px; font-weight: 600; color: #1e293b; background-color: #ffffff; border: 1px solid #cbd5e1; border-radius: 6px; cursor: pointer; outline: none;">
                  <option value="pt-BR" ${currentLocale === 'pt-BR' ? 'selected' : ''}>BR Português (Brasil)</option>
                  <option value="en-US" ${currentLocale === 'en-US' ? 'selected' : ''}>US English (United States)</option>
                </select>
              </div>
            </div>
          </div>

          <!-- 1. IDENTIFICAÇÃO E PASTAS FÍSICAS DO WORKSPACE NA MÁQUINA DO USUÁRIO -->
          <div class="backstage-cards-grid" style="margin-bottom: 24px;">
            <div class="backstage-info-card">
              <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
                <h4 style="margin: 0;">📁 ${t('backstage.options.folders_title')}</h4>
                <span class="badge-inherited" style="font-size: 9px; padding: 1px 6px;">🛠️ ${t('backstage.options.scope_workspace_badge')}</span>
              </div>
              <p><strong>${t('backstage.options.root_path_label')}</strong> <code>${this.escapeHtml(dirs.root)}</code></p>
              <p><strong>${t('backstage.options.skills_path_label')}</strong> <code>${this.escapeHtml(dirs.skills)}</code></p>
              <p><strong>${t('backstage.options.mcps_path_label')}</strong> <code>${this.escapeHtml(dirs.mcps)}</code></p>
              <p><strong>${t('backstage.options.agents_path_label')}</strong> <code>${this.escapeHtml(dirs.agents)}</code></p>
              <div style="margin-top: 14px;">
                <button class="tool-btn btn-action" id="btn-sync-workspace-disk" style="background: #0f6cbd; color: #ffffff; font-weight: 700; padding: 8px 16px; font-size: 12px; display: inline-flex; align-items: center; gap: 6px;">
                  <span>📥</span> ${t('backstage.options.btn_sync_disk')}
                </button>
              </div>
            </div>

            <div class="backstage-info-card">
              <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
                <h4 style="margin: 0;">🛡️ ${t('backstage.options.gov_title')}</h4>
                <span class="badge-inherited" style="font-size: 9px; padding: 1px 6px;">🏢 ${t('backstage.options.scope_global_badge')}</span>
              </div>
              <p><strong>${t('backstage.options.audit_auto_label')}</strong> <span class="badge-inherited" style="font-weight: 700;">${t('backstage.options.audit_active')}</span></p>
              <p><strong>${t('backstage.options.typo_design_label')}</strong> <span style="color: #0f766e; font-weight: 700;">${t('backstage.options.typo_design_val')}</span></p>
              <p><strong>${t('backstage.options.iso_mode_label')}</strong> ${t('backstage.options.iso_mode_val')}</p>
              <div style="margin-top: 14px;">
                <button class="tool-btn btn-secondary" id="btn-open-global-defaults-modal" style="font-weight: 700; padding: 8px 16px; font-size: 12px;">
                  ⚙️ ${t('backstage.options.btn_adv_pref')}
                </button>
              </div>
            </div>
          </div>

          <!-- 2. MOTORES DE EXECUÇÃO DO WORKSPACE (AGENTES, GRAFOS, PROJETOS ATRELADOS A EVENTOS) -->
          <div style="margin-bottom: 28px;">
            <h3 class="backstage-header-title" style="font-size: 16px; margin-bottom: 4px;">⚡ ${t('backstage.options.integrated_exec_title')}</h3>
            <p class="backstage-header-sub" style="margin-bottom: 14px;">${t('backstage.options.integrated_exec_sub')}</p>

            <div class="backstage-cards-grid">
              <div class="backstage-info-card" style="display: flex; flex-direction: column; justify-content: space-between;">
                <div>
                  <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
                    <h4 style="margin: 0;">🤖 ${t('backstage.options.auditor_title')}</h4>
                    <span class="badge-inherited" style="font-size: 9px; padding: 1px 6px;">🛠️ ${t('backstage.options.scope_workspace_badge')}</span>
                  </div>
                  <p style="font-size: 12px; color: #475569; margin-bottom: 8px;">
                    ${t('backstage.options.auditor_desc')}
                  </p>
                </div>
                <button class="tool-btn btn-action" id="btn-run-workspace-auditor" style="background: #107c41; color: #ffffff; font-weight: 700; padding: 8px 14px; font-size: 12px; margin-top: 10px;">
                  ▶ ${t('backstage.options.btn_run_auditor')}
                </button>
              </div>

              <div class="backstage-info-card" style="display: flex; flex-direction: column; justify-content: space-between;">
                <div>
                  <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
                    <h4 style="margin: 0;">📊 ${t('backstage.options.linter_title')}</h4>
                    <span class="badge-inherited" style="font-size: 9px; padding: 1px 6px;">📦 ${t('backstage.info.scope_project')}</span>
                  </div>
                  <p style="font-size: 12px; color: #475569; margin-bottom: 8px;">
                    ${t('backstage.options.linter_desc')}
                  </p>
                </div>
                <button class="tool-btn btn-secondary" id="btn-run-workspace-linter" style="font-weight: 700; padding: 8px 14px; font-size: 12px; margin-top: 10px;">
                  ▶ ${t('backstage.options.btn_run_linter')}
                </button>
              </div>

              <div class="backstage-info-card" style="display: flex; flex-direction: column; justify-content: space-between;">
                <div>
                  <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
                    <h4 style="margin: 0;">🚀 ${t('backstage.options.proj_pipeline_title')}</h4>
                    <span class="badge-inherited" style="font-size: 9px; padding: 1px 6px;">📦 ${t('backstage.info.scope_project')}</span>
                  </div>
                  <p style="font-size: 12px; color: #475569; margin-bottom: 8px;">
                    ${t('backstage.options.proj_pipeline_desc')}
                  </p>
                </div>
                <button class="tool-btn btn-action" id="btn-run-workspace-project" style="background: #0f6cbd; color: #ffffff; font-weight: 700; padding: 8px 14px; font-size: 12px; margin-top: 10px;">
                  ▶ ${t('backstage.options.btn_run_proj_pipeline')}
                </button>
              </div>
            </div>

            <div id="workspace-execution-output" style="margin-top: 14px; display: none;"></div>
          </div>

          <!-- 3. CATÁLOGO DE RECURSOS PRÉ-APROVADOS NO WORKSPACE -->
          <div style="margin-bottom: 28px;">
            <h3 class="backstage-header-title" style="font-size: 16px; margin-bottom: 4px;">🛡️ ${t('backstage.options.preapproved_cat_title')} <span class="badge-inherited" style="font-size: 10px; margin-left: 6px;">🛠️ ${t('backstage.options.scope_workspace_badge')}</span></h3>
            <p class="backstage-header-sub" style="margin-bottom: 14px;">${t('backstage.options.preapproved_cat_sub')}</p>

            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 16px;">
              <!-- Habilidades Pré-aprovadas -->
              <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 16px;">
                <h4 style="margin: 0 0 10px 0; font-size: 13px; font-weight: 700; color: #0f172a;">
                  📜 ${t('backstage.options.skills_count_heading', { count: approvedSkills.length })}
                </h4>
                <div style="max-height: 220px; overflow-y: auto; display: flex; flex-direction: column; gap: 8px;">
                  ${approvedSkills.map(s => `
                    <div style="padding: 8px 10px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 6px;">
                      <div style="display: flex; justify-content: space-between; align-items: center;">
                        <strong style="font-size: 12px; color: #0f6cbd;">${this.escapeHtml(s.name)}</strong>
                        <span style="font-size: 10px; background: #e8f5e9; color: #1e7e34; padding: 1px 6px; border-radius: 3px; font-weight: 700;">${t('backstage.options.skill_approved_tag')}</span>
                      </div>
                      <div style="font-size: 11px; color: #64748b; margin-top: 2px;">${this.escapeHtml(s.description)}</div>
                    </div>
                  `).join('')}
                </div>
              </div>

              <!-- MCPs Pré-aprovados -->
              <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 16px;">
                <h4 style="margin: 0 0 10px 0; font-size: 13px; font-weight: 700; color: #0f172a;">
                  🔌 ${t('backstage.options.mcps_count_heading', { count: approvedMcps.length })}
                </h4>
                <div style="max-height: 220px; overflow-y: auto; display: flex; flex-direction: column; gap: 8px;">
                  ${approvedMcps.map(m => `
                    <div style="padding: 8px 10px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 6px;">
                      <div style="display: flex; justify-content: space-between; align-items: center;">
                        <strong style="font-size: 12px; color: #334155;">${this.escapeHtml(m.name)}</strong>
                        <span style="font-size: 10px; background: #e0f2fe; color: #0369a1; padding: 1px 6px; border-radius: 3px; font-weight: 700;">${this.escapeHtml(m.transport.toUpperCase())}</span>
                      </div>
                      <div style="font-size: 11px; color: #64748b; margin-top: 2px;">${this.escapeHtml(m.description)}</div>
                    </div>
                  `).join('')}
                </div>
              </div>
            </div>
          </div>
        `;
      }
    }
  }

  private hasActiveModal(): boolean {
    if (typeof document === 'undefined') return false;
    const modalOverlays = document.querySelectorAll('.modal-overlay');
    for (let i = 0; i < modalOverlays.length; i++) {
      const el = modalOverlays[i] as HTMLElement;
      if (el.classList ? !el.classList.contains('hidden') : !el.className?.includes('hidden')) {
        return true;
      }
    }
    return false;
  }

  private bindEvents(): void {
    window.addEventListener('keydown', (e: KeyboardEvent) => {
      if (e.key === 'Escape' && this.isOpen()) {
        if (this.hasActiveModal()) {
          return;
        }
        this.close();
      }
    });
  }

  private bindInternalEvents(): void {
    if (!this.overlay) return;

    this.overlay.querySelector('#btn-backstage-close')?.addEventListener('click', () => {
      this.close();
    });

    this.overlay.querySelectorAll<HTMLElement>('.backstage-nav-item[data-section]').forEach(item => {
      item.addEventListener('click', async () => {
        const sec = item.dataset.section as any;
        if (sec) {
          this.activeSection = sec;
          try {
            await this.projectManager.init(true);
            await this.prepareSection();
          } catch (_) {}
          this.render();
        }
      });
    });

    // Seletor de Idioma nas Opções do Studio (Dropdown Select)
    const langSelect = this.overlay.querySelector('#backstage-language-select') as HTMLSelectElement;
    langSelect?.addEventListener('change', async (e) => {
      e.stopPropagation();
      const val = langSelect.value as any;
      if (val === 'pt-BR' || val === 'en-US') {
        await setLocale(val);
      }
    });

    // Controles da Tela "Salvar Como"
    if (this.activeSection === 'saveas') {
      // 1. Botões de Unidades de Disco
      this.overlay.querySelectorAll<HTMLButtonElement>('.btn-saveas-drive').forEach(btn => {
        btn.addEventListener('click', async () => {
          const drive = btn.dataset.drive;
          if (drive) {
            this.saveAsCurrentPath = drive.replace(/\\/g, '/');
            this.saveAsSubdirectories = await this.projectManager.listSubdirectories(this.saveAsCurrentPath);
            this.render();
          }
        });
      });

      // 2. Navegação em Pastas
      this.overlay.querySelectorAll<HTMLElement>('.saveas-folder-card').forEach(card => {
        card.addEventListener('click', async () => {
          const p = card.dataset.path;
          if (p) {
            this.saveAsCurrentPath = p.replace(/\\/g, '/');
            this.saveAsSubdirectories = await this.projectManager.listSubdirectories(this.saveAsCurrentPath);
            this.render();
          }
        });
      });

      // 3. Subir Nível
      this.overlay.querySelector('#btn-saveas-up-dir')?.addEventListener('click', async () => {
        const cur = this.saveAsCurrentPath.replace(/\\/g, '/').replace(/\/$/, '');
        const lastSlash = cur.lastIndexOf('/');
        if (lastSlash > 0) {
          this.saveAsCurrentPath = cur.substring(0, lastSlash);
          if (/^[a-zA-Z]:$/.test(this.saveAsCurrentPath)) {
            this.saveAsCurrentPath += '/';
          }
        } else {
          this.saveAsCurrentPath = 'C:/';
        }
        this.saveAsSubdirectories = await this.projectManager.listSubdirectories(this.saveAsCurrentPath);
        this.render();
      });

      // 4. Atualizar
      this.overlay.querySelector('#btn-saveas-refresh-dir')?.addEventListener('click', async () => {
        this.saveAsSubdirectories = await this.projectManager.listSubdirectories(this.saveAsCurrentPath);
        this.render();
      });

      // 5. Procurar Nativo
      this.overlay.querySelector('#btn-saveas-browse-native')?.addEventListener('click', async () => {
        try {
          if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
            const picked = await (window as any).webui.call('pick_folder');
            if (picked && picked.trim()) {
              this.saveAsCurrentPath = picked.trim().replace(/\\/g, '/');
              this.saveAsSubdirectories = await this.projectManager.listSubdirectories(this.saveAsCurrentPath);
              this.render();
            }
          }
        } catch (_) {}
      });

      // 6. Atualização de Nome do Arquivo
      const nameInput = this.overlay.querySelector('#saveas-input-filename') as HTMLInputElement;
      nameInput?.addEventListener('input', () => {
        this.saveAsFileName = nameInput.value;
        const preview = this.overlay?.querySelector('#saveas-path-preview');
        if (preview) {
          preview.textContent = `${this.saveAsCurrentPath}/${this.saveAsFileName}.${this.saveAsFormat}`;
        }
      });

      // 7. Atualização do Formato
      const formatSelect = this.overlay.querySelector('#saveas-select-format') as HTMLSelectElement;
      formatSelect?.addEventListener('change', () => {
        this.saveAsFormat = formatSelect.value as any;
        const preview = this.overlay?.querySelector('#saveas-path-preview');
        if (preview) {
          preview.textContent = `${this.saveAsCurrentPath}/${this.saveAsFileName}.${this.saveAsFormat}`;
        }
      });

      // 8. Executar Salvamento do Arquivo
      this.overlay.querySelector('#btn-saveas-submit')?.addEventListener('click', async () => {
        const fileName = (this.saveAsFileName || 'projeto').trim();
        if (!fileName) {
          if (typeof alert !== 'undefined') alert(t('backstage.alerts.enter_filename'));
          return;
        }

        if (this.saveAsFormat === 'arestrix') {
          const res = await this.projectManager.saveProjectAsArestrix(this.saveAsCurrentPath, fileName, this.graph);
          if (res && res.success) {
            if (typeof alert !== 'undefined') {
              alert(t('backstage.alerts.pkg_saved', { path: res.filePath }));
            }
            this.render();
          } else {
            if (typeof alert !== 'undefined') alert(t('backstage.alerts.pkg_error'));
          }
        } else {
          const ok = await this.projectManager.saveProject(this.graph);
          if (ok) {
            if (typeof alert !== 'undefined') alert(t('backstage.alerts.json_saved'));
            this.render();
          }
        }
      });
    }

    if (!this.overlay) return;

    // Salvar Workspace
    this.overlay.querySelector('#btn-backstage-save')?.addEventListener('click', async () => {
      await this.projectManager.saveProject(this.graph);
      if (typeof alert !== 'undefined') alert(t('backstage.alerts.workspace_saved'));
      this.close();
    });

    // Opções do Studio & Governança do Workspace
    this.overlay.querySelector('#btn-sync-workspace-disk')?.addEventListener('click', async () => {
      const wm = WorkspaceManager.getInstance();
      await wm.installApprovedAssetsToDisk();
      const out = this.overlay?.querySelector('#workspace-execution-output') as HTMLElement;
      if (out) {
        out.style.display = 'block';
        out.innerHTML = `
          <div style="padding: 12px 16px; background: #e8f5e9; border: 1px solid #c8e6c9; border-radius: 6px; font-size: 12px; color: #1e7e34;">
            <strong>✅ ${t('backstage.alerts.sync_success_title')}</strong><br>
            ${t('backstage.alerts.sync_success_desc')}<br>
            • <code>${wm.getWorkspaceDirectories().skills}</code> (${wm.getApprovedSkills().length} ${t('backstage.options.skills_path_label').replace(':', '')})<br>
            • <code>${wm.getWorkspaceDirectories().mcps}</code> (${wm.getApprovedMcps().length} ${t('backstage.options.mcps_path_label').replace(':', '')})<br>
            • <code>${wm.getWorkspaceDirectories().agents}</code> (${t('backstage.options.agents_path_label').replace(':', '')})
          </div>
        `;
      }
    });

    this.overlay.querySelector('#btn-run-workspace-auditor')?.addEventListener('click', async () => {
      const wm = WorkspaceManager.getInstance();
      const res = await wm.runAgent('agent-workspace-skill-auditor');
      const out = this.overlay?.querySelector('#workspace-execution-output') as HTMLElement;
      if (out) {
        out.style.display = 'block';
        out.innerHTML = `
          <div style="padding: 12px 16px; background: #f0fdf4; border: 1px solid #bbf7d0; border-radius: 6px; font-size: 12px; color: #166534;">
            <strong>🛡️ ${res.summary}</strong><br>
            Status: <strong>${res.status.toUpperCase()}</strong> | Timestamp: ${new Date(res.timestamp).toLocaleTimeString()}<br>
            ${t('backstage.alerts.auditor_ok_desc')}
          </div>
        `;
      }
    });

    this.overlay.querySelector('#btn-run-workspace-linter')?.addEventListener('click', async () => {
      const wm = WorkspaceManager.getInstance();
      const res = await wm.runGraph(this.graph);
      const out = this.overlay?.querySelector('#workspace-execution-output') as HTMLElement;
      if (out) {
        out.style.display = 'block';
        out.innerHTML = `
          <div style="padding: 12px 16px; background: #eff6ff; border: 1px solid #bfdbfe; border-radius: 6px; font-size: 12px; color: #1e40af;">
            <strong>📊 ${res.summary}</strong><br>
            ${t('backstage.alerts.linter_nodes_edges', { nodes: res.details?.nodeCount, edges: res.details?.edgeCount })}<br>
            ${t('backstage.alerts.linter_no_errors')}
          </div>
        `;
      }
    });

    this.overlay.querySelector('#btn-run-workspace-project')?.addEventListener('click', async () => {
      const wm = WorkspaceManager.getInstance();
      const cur = this.projectManager.getCurrentProject();
      const res = await wm.runProject(cur.projectId, this.graph);
      const out = this.overlay?.querySelector('#workspace-execution-output') as HTMLElement;
      if (out) {
        out.style.display = 'block';
        out.innerHTML = `
          <div style="padding: 12px 16px; background: #faf5ff; border: 1px solid #e9d5ff; border-radius: 6px; font-size: 12px; color: #6b21a8;">
            <strong>🚀 ${res.summary}</strong><br>
            ${t('backstage.alerts.pipeline_init_desc')}
          </div>
        `;
      }
    });

    this.overlay.querySelector('#btn-open-global-defaults-modal')?.addEventListener('click', () => {
      this.globalDefaultsModal.show();
    });

    // Repositórios do GitHub (Abre diretamente sobre a BackstageView)
    this.overlay.querySelector('#btn-backstage-github-repos')?.addEventListener('click', () => {
      globalEventBus.emit('modal:open-github-repos');
    });

    // Criar Projeto
    this.overlay.querySelector('#btn-backstage-create-submit')?.addEventListener('click', async () => {
      if (!this.overlay) return;
      const nameInput = this.overlay.querySelector('#backstage-new-name') as HTMLInputElement;
      const pathInput = this.overlay.querySelector('#backstage-new-path') as HTMLInputElement;
      const descInput = this.overlay.querySelector('#backstage-new-desc') as HTMLTextAreaElement;

      const pName = nameInput?.value.trim() || 'Novo Projeto';
      const pId = hashProjectName(pName);
      const pDesc = descInput?.value.trim() || '';
      const customPath = pathInput?.value.trim();
      const effectivePath = customPath || `./projects/${pName}`;

      await this.projectManager.createProject({
        projectId: pId,
        name: pName,
        rootPath: effectivePath,
        description: pDesc,
        pipelines: [
          {
            id: 'main',
            name: 'Pipeline Principal',
            file: 'pipelines/main.json',
            isPrimary: true
          }
        ]
      }, this.graph);

      if (typeof alert !== 'undefined') alert(t('backstage.alerts.project_created', { name: pName }));
      this.close();
    });

    // Ações de projeto recente no Backstage (Dropdown Estilo Office)
    this.overlay.querySelectorAll<HTMLSelectElement>('.backstage-project-action-select').forEach(select => {
      select.addEventListener('change', async () => {
        const action = select.value;
        const pId = select.dataset.id || '';
        const pPath = select.dataset.path || '';
        const pName = select.dataset.name || 'Projeto';
        const pTarget = pPath || pId;

        // Restaura dropdown para o placeholder
        select.value = '';

        if (action === 'open') {
          await this.openRecentProject(pTarget, pName);
        } else if (action === 'manage') {
          if (pTarget) {
            const cur = this.projectManager.getCurrentProject();
            if (cur.projectId !== pId && cur.rootPath !== pPath) {
              await this.projectManager.loadProject(pTarget, this.graph);
            }
          }
          this.close();
          globalEventBus.emit('modal:open-project-manager', 'settings');
        } else if (action === 'delete') {
          const confirmed = typeof confirm !== 'undefined'
            ? confirm(t('backstage.alerts.delete_confirm', { name: pName }))
            : true;

          if (confirmed) {
            await this.projectManager.deleteProject(pId || pPath, true, this.graph);
            if (typeof alert !== 'undefined') alert(t('backstage.alerts.project_deleted', { name: pName }));
            this.render();
          }
        }
      });
    });

    if (this.activeSection === 'open') {
      this.bindOpenSectionEvents();
    }

    // Exportações (Abre diretamente sobre a BackstageView)
    this.overlay.querySelector('#btn-backstage-open-codemodal')?.addEventListener('click', () => {
      this.codeModal.show('cpp');
    });

    this.overlay.querySelector('#btn-backstage-export-cmake')?.addEventListener('click', () => {
      this.codeModal.show('cmake');
    });

    this.overlay.querySelector('#btn-backstage-export-svg')?.addEventListener('click', () => {
      globalEventBus.emit('ribbon:export-svg');
    });

    this.overlay.querySelector('#btn-quick-sync-db')?.addEventListener('click', () => {
      if (typeof alert !== 'undefined') alert(t('backstage.alerts.db_synced'));
    });

    this.overlay.querySelector('#btn-quick-clean-cache')?.addEventListener('click', () => {
      if (typeof alert !== 'undefined') alert(t('backstage.alerts.cache_cleared'));
    });
  }

  private bindOpenSectionEvents(): void {
    const overlay = this.overlay;
    if (!overlay) return;

    overlay.querySelectorAll<HTMLElement>('[data-open-place]').forEach(btn => {
      btn.addEventListener('click', async () => {
        const place: OpenPlace = btn.dataset.openPlace === 'computer' ? 'computer' : 'recent';
        if (place === this.openPlace) return;
        this.openPlace = place;
        this.openQuery = '';
        if (place === 'computer') {
          await this.loadComputerListing();
        }
        this.render();
      });
    });

    overlay.querySelectorAll<HTMLElement>('[data-open-tab]').forEach(btn => {
      btn.addEventListener('click', () => {
        this.openRecentTab = btn.dataset.openTab === 'graphs' ? 'graphs' : 'projects';
        this.render();
      });
    });

    const search = overlay.querySelector('#backstage-open-search') as HTMLInputElement | null;
    search?.addEventListener('input', () => {
      this.openQuery = search.value;
      this.applyOpenFilter();
    });
    this.applyOpenFilter();

    // Unidades, migalhas do caminho e o botao de entrar numa pasta de projeto
    overlay.querySelectorAll<HTMLElement>('[data-open-navigate]').forEach(btn => {
      btn.addEventListener('click', async (e) => {
        e.stopPropagation();
        await this.navigateComputer(btn.dataset.openNavigate || '');
      });
    });
    overlay.querySelector('#btn-open-up')?.addEventListener('click', async () => {
      if (this.openListing?.parent) await this.navigateComputer(this.openListing.parent);
    });
    overlay.querySelector('#btn-open-refresh')?.addEventListener('click', async () => {
      await this.navigateComputer(this.openListing?.path || '');
    });

    overlay.querySelectorAll<HTMLElement>('.backstage-open-row').forEach(row => {
      row.addEventListener('keydown', (e: KeyboardEvent) => {
        if ((e.key === 'Enter' || e.key === ' ') && e.target === row) {
          e.preventDefault();
          row.click();
        }
      });
      row.addEventListener('click', async () => {
        const path = row.dataset.path || '';
        switch (row.dataset.openAction) {
          case 'recent':
            await this.openRecentProject(path || row.dataset.id || '', row.dataset.name || '');
            break;
          case 'pipeline':
            await this.projectManager.loadPipeline(this.graph);
            this.notify(t('backstage.alerts.canvas_graph_loaded'));
            this.close();
            break;
          case 'navigate':
            await this.navigateComputer(path);
            break;
          case 'disk':
            await this.openPathFromDisk(path);
            break;
        }
      });
    });
    // O seletor de acoes dentro da linha nao abre o projeto ao ser clicado.
    overlay.querySelectorAll<HTMLElement>('.backstage-open-row select').forEach(select => {
      select.addEventListener('click', e => e.stopPropagation());
    });

    overlay.querySelector('#btn-open-browse')?.addEventListener('click', () => this.browseForFile());
    const fileInput = overlay.querySelector('#backstage-open-file-input') as HTMLInputElement | null;
    fileInput?.addEventListener('change', async () => {
      const file = fileInput.files?.[0];
      if (!file) return;
      const bytes = new Uint8Array(await file.arrayBuffer());
      fileInput.value = '';
      const res = await this.projectManager.openFromBytes(bytes, file.name, this.graph, this.openOptions());
      this.reportOpenResult(res, file.name);
    });
  }

  // Filtra as linhas visiveis sem re-renderizar, para nao tirar o foco da caixa de busca.
  private applyOpenFilter(): void {
    const overlay = this.overlay;
    if (!overlay) return;
    const query = this.openQuery.trim().toLowerCase();
    const rows = Array.from(overlay.querySelectorAll<HTMLElement>('.backstage-open-row'));
    let visible = 0;
    rows.forEach(row => {
      const match = !query || (row.dataset.search || '').includes(query);
      row.style.display = match ? '' : 'none';
      if (match) visible++;
    });
    overlay.querySelectorAll<HTMLElement>('.backstage-open-group').forEach(group => {
      const anyVisible = Array.from(group.querySelectorAll<HTMLElement>('.backstage-open-row')).some(r => r.style.display !== 'none');
      group.style.display = anyVisible ? '' : 'none';
    });
    const noMatches = overlay.querySelector('#backstage-open-no-matches') as HTMLElement | null;
    if (noMatches) {
      noMatches.style.display = query && rows.length > 0 && visible === 0 ? '' : 'none';
    }
  }

  private async navigateComputer(path: string): Promise<void> {
    this.openQuery = '';
    this.openListing = await this.projectManager.listOpenableEntries(path);
    this.render();
  }

  private async browseForFile(): Promise<void> {
    if (!this.projectManager.hasNativeBridge()) {
      (this.overlay?.querySelector('#backstage-open-file-input') as HTMLInputElement | null)?.click();
      return;
    }
    const picked = await this.projectManager.pickFile({
      title: t('backstage.open.picker_title'),
      filters: [
        { name: `${t('backstage.open.filter_all')} (*.arestrix, *.json)`, pattern: '*.arestrix;*.json' },
        { name: `${t('backstage.open.filter_package')} (*.arestrix)`, pattern: '*.arestrix' },
        { name: `${t('backstage.open.filter_json')} (*.json)`, pattern: '*.json' }
      ],
      initialDir: this.openListing?.exists ? this.openListing.path : undefined
    });
    if (picked) {
      await this.openPathFromDisk(picked);
    }
  }

  private async openRecentProject(target: string, name: string): Promise<void> {
    if (!target) return;
    const ok = await this.projectManager.loadProject(target, this.graph);
    if (!ok) {
      this.notify(t('backstage.open.alert_open_fail', { path: target }));
      return;
    }
    this.notify(t('backstage.alerts.project_loaded', { name: name || this.projectManager.getCurrentProject().name }));
    this.close();
  }

  private async openPathFromDisk(path: string): Promise<void> {
    const res = await this.projectManager.openFromDisk(path, this.graph, this.openOptions());
    this.reportOpenResult(res, path);
  }

  private openOptions(): IOpenOptions {
    return {
      confirmLooseGraph: (fileName: string) => typeof confirm !== 'undefined' && confirm(t('backstage.open.confirm_graph_import', {
        name: fileName,
        project: this.projectManager.getCurrentProject().name
      }))
    };
  }

  private reportOpenResult(res: IOpenResult, path: string): void {
    if (res.ok) {
      if (res.kind === 'package') {
        this.notify(t('backstage.open.alert_package_extracted', { path: res.path }));
      } else if (res.kind === 'graph') {
        this.notify(t('backstage.alerts.canvas_graph_loaded'));
      } else {
        this.notify(t('backstage.alerts.project_loaded', { name: this.projectManager.getCurrentProject().name }));
      }
      this.close();
      return;
    }
    if (res.reason === 'cancelled') return;
    this.notify(res.reason === 'unsupported'
      ? t('backstage.open.alert_manifest_desktop_only')
      : t('backstage.open.alert_open_fail', { path }));
  }

  private notify(message: string): void {
    if (typeof alert !== 'undefined') alert(message);
  }

  private escapeHtml(str?: string): string {
    if (!str) return '';
    return String(str)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }
}
