import { AgentTemplateRegistry } from '../core/templates/AgentTemplateRegistry.ts';
import { IAgentTemplate } from '../core/templates/AgentTemplateTypes.ts';
import { Graph } from '../core/Graph.ts';
import { Viewport } from '../view/Viewport.ts';
import { AddNodeCommand, AddEdgeCommand, CompoundCommand } from '../core/history/GraphCommands.ts';
import { INodeData, IEdgeData, defaultCallSettings, defaultCallVertexSettings } from '../core/types.ts';
import { ModalWindowManager, IModalWindowController } from './modals/ModalWindowManager.ts';
import { t, onLocaleChange } from '../i18n/index.ts';
import { BackstageView } from './ribbon/BackstageView.ts';
import { LibraryService } from '../core/libraries/LibraryService.ts';
import type { ILibraryAsset } from '../core/libraries/types.ts';
import type { IAssetOriginMetadata } from '../core/types.ts';
import { globalEventBus } from '../core/EventBus.ts';
import { selectProjectPipelineFiles } from '../core/templates/ProjectPipelineImport.ts';

type RestoreCapableLibraryService = LibraryService & {
  resetBuiltinLibrary?: () => Promise<{ restored?: number; imported?: ILibraryAsset[]; ok?: boolean } | number | void>;
};

const GRID_RENDER_LIMIT = 60;

const CATEGORY_MAP: { id: string; labelKey: string }[] = [
  { id: 'Todos', labelKey: 'template_modal.cat_all' },
  { id: 'Agentes', labelKey: 'template_modal.cat_agents' },
  { id: 'IA & Inferência', labelKey: 'template_modal.cat_ai' },
  { id: 'Concorrência & Filas', labelKey: 'template_modal.cat_concurrency' },
  { id: 'Armazenamento & Dados', labelKey: 'template_modal.cat_storage' },
  { id: 'Visão & Processamento', labelKey: 'template_modal.cat_vision' },
  { id: 'Síntese & Orquestração', labelKey: 'template_modal.cat_synthesis' },
  { id: 'Plugins Customizados', labelKey: 'template_modal.cat_custom' }
];

export class TemplateModal {
  private overlay: HTMLElement;
  private graph: Graph;
  private viewport: Viewport;
  private registry: AgentTemplateRegistry;
  private windowController?: IModalWindowController;
  private activeCategory: string = 'Todos';
  private searchQuery: string = '';
  private activeDomain: string = '';
  private activeSubdomain: string = '';
  private showAllTemplates: boolean = false;
  private readonly libraryService = LibraryService.getInstance();

  constructor(graph: Graph, viewport: Viewport) {
    this.graph = graph;
    this.viewport = viewport;
    this.registry = AgentTemplateRegistry.getInstance();

    this.overlay = document.createElement('div');
    this.overlay.className = 'modal-overlay hidden';
    document.body.appendChild(this.overlay);

    this.render();

    onLocaleChange(() => {
      if (!this.overlay.classList.contains('hidden')) {
        this.render();
      }
    });
  }

  public show(): void {
    this.activeCategory = 'Todos';
    this.searchQuery = '';
    this.activeDomain = '';
    this.activeSubdomain = '';
    this.showAllTemplates = false;
    this.render();
    this.overlay.classList.remove('hidden');
    this.windowController?.bringToFront();
  }

  public hide(): void {
    this.overlay.classList.add('hidden');
  }

  private categoryTemplates(allTemplates: IAgentTemplate[]): IAgentTemplate[] {
    return this.activeCategory === 'Todos'
      ? allTemplates
      : allTemplates.filter(tpl => tpl.category === this.activeCategory);
  }

  private filteredTemplates(allTemplates: IAgentTemplate[]): IAgentTemplate[] {
    const q = this.searchQuery.trim().toLowerCase();
    return this.categoryTemplates(allTemplates)
      .filter(tpl => !this.activeDomain || (tpl.domain || '').trim() === this.activeDomain)
      .filter(tpl => !this.activeSubdomain || (tpl.subdomain || '').trim() === this.activeSubdomain)
      .filter(tpl => !q
        || tpl.name.toLowerCase().includes(q)
        || tpl.description.toLowerCase().includes(q)
        || (tpl.domain || '').toLowerCase().includes(q)
        || (tpl.subdomain || '').toLowerCase().includes(q)
        || (tpl.origin?.repo || '').toLowerCase().includes(q));
  }

  private collectDomains(templates: IAgentTemplate[]): string[] {
    const set = new Set<string>();
    for (const tpl of templates) {
      const d = (tpl.domain || '').trim();
      if (d) set.add(d);
    }
    return Array.from(set).sort((a, b) => a.localeCompare(b));
  }

  private collectSubdomains(templates: IAgentTemplate[], domain: string): string[] {
    const set = new Set<string>();
    for (const tpl of templates) {
      if (domain && (tpl.domain || '').trim() !== domain) continue;
      const s = (tpl.subdomain || '').trim();
      if (s) set.add(s);
    }
    return Array.from(set).sort((a, b) => a.localeCompare(b));
  }

  private renderDomainOptions(templates: IAgentTemplate[]): string {
    const domains = this.collectDomains(templates);
    return [
      `<option value="">${this.escapeHtml(t('template_modal.filter_domain_all', 'Todos os domínios'))}</option>`,
      ...domains.map(d => `<option value="${this.escapeHtml(d)}" ${d === this.activeDomain ? 'selected' : ''}>${this.escapeHtml(d)}</option>`)
    ].join('');
  }

  private renderSubdomainOptions(templates: IAgentTemplate[]): string {
    const subdomains = this.collectSubdomains(templates, this.activeDomain);
    return [
      `<option value="">${this.escapeHtml(t('template_modal.filter_subdomain_all', 'Todos os subdomínios'))}</option>`,
      ...subdomains.map(s => `<option value="${this.escapeHtml(s)}" ${s === this.activeSubdomain ? 'selected' : ''}>${this.escapeHtml(s)}</option>`)
    ].join('');
  }

  private renderGridMarkup(filtered: IAgentTemplate[]): string {
    if (filtered.length === 0) {
      return `
        <div class="templates-grid">
          <div class="templates-empty">
            <div class="empty-icon">🔍</div>
            <div class="empty-title">${t('template_modal.empty_title')}</div>
            <div class="empty-desc">${this.activeCategory === 'Agentes'
              ? t('template_modal.empty_agents_desc', 'Salve um vértice agente como template ou adicione um agente da biblioteca.')
              : t('template_modal.empty_desc')}</div>
          </div>
        </div>
      `;
    }
    const visible = this.showAllTemplates || filtered.length <= GRID_RENDER_LIMIT
      ? filtered
      : filtered.slice(0, GRID_RENDER_LIMIT);
    const hasMore = filtered.length > visible.length;
    return `
      <div class="templates-grid">
        ${visible.map(tpl => this.renderTemplateCard(tpl)).join('')}
      </div>
      ${hasMore ? `
        <div style="display: flex; align-items: center; justify-content: center; gap: 10px; margin-top: 12px;">
          <span style="font-size: 12px; color: #616161;">${this.escapeHtml(t('template_modal.showing_subset', 'Mostrando {{shown}} de {{total}} templates', { shown: visible.length, total: filtered.length }))}</span>
          <button type="button" id="btn-show-all-templates" class="btn btn-secondary-pill">${this.escapeHtml(t('template_modal.show_all', 'Mostrar todos ({{count}})', { count: filtered.length }))}</button>
        </div>
      ` : ''}
    `;
  }

  private render(): void {
    const allTemplates = this.registry.getAllTemplates();
    const catTemplates = this.categoryTemplates(allTemplates);
    const filtered = this.filteredTemplates(allTemplates);

    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: t('template_modal.title'),
      subtitle: t('template_modal.subtitle'),
      icon: '🧩',
      closeBtnId: 'btn-close-template-modal'
    });

    this.overlay.innerHTML = `
      <div class="modal-dialog template-modal-dialog">
        ${titleBarHtml}

        <!-- Barra de Ações Rápidas -->
        <div class="template-action-bar">
          <div class="template-action-left">
            <button id="btn-preset-pipeline" class="btn btn-preset-action" title="${t('template_modal.btn_preset_tip')}">
              ${t('template_modal.btn_preset')}
            </button>
            ${this.activeCategory === 'Agentes' ? `
              <button id="btn-save-pipeline-library" class="btn btn-secondary-pill" title="${t('template_modal.btn_save_pipeline')}">
                ${t('template_modal.btn_save_pipeline')}
              </button>
              <button id="btn-import-pipeline-library" class="btn btn-secondary-pill" title="${t('template_modal.btn_import_pipeline')}">
                ${t('template_modal.btn_import_pipeline')}
              </button>
              <button id="btn-publish-pipeline-version" class="btn btn-secondary-pill" title="${t('template_modal.btn_publish_version_tip', 'Publicar o pipeline atual como nova versão de um asset da biblioteca')}">
                ${t('template_modal.btn_publish_version', 'Publicar versão')}
              </button>
              <button id="btn-import-project-agent" class="btn btn-secondary-pill" title="${t('template_modal.btn_import_project')}">
                ${t('template_modal.btn_import_project')}
              </button>
              <input type="file" id="pipeline-file-input" accept=".json,application/json" style="display: none;" />
              <input type="file" id="project-folder-input" webkitdirectory directory multiple style="display: none;" />
            ` : ''}
          </div>
          <div class="template-action-right">
            <button id="btn-library-templates" class="btn btn-secondary-pill" title="${t('template_modal.btn_library_tip', 'Inserir um template salvo em uma biblioteca')}">
              ${t('template_modal.btn_library', 'Bibliotecas')}
            </button>
            ${this.activeCategory === 'Agentes' ? `
              <button id="btn-library-pipelines" class="btn btn-secondary-pill" title="${t('template_modal.btn_library_pipelines', 'Inserir um pipeline da biblioteca como agente')}">
                ${t('template_modal.btn_library_pipelines', 'Pipelines')}
              </button>
            ` : ''}
            <button id="btn-restore-builtin-templates" class="btn btn-secondary-pill" title="${t('template_modal.btn_restore_builtin', 'Restaurar padrão de instalação')}">
              ${t('template_modal.btn_restore_builtin', 'Restaurar padrão de instalação')}
            </button>
            <button id="btn-import-plugin" class="btn btn-secondary-pill" title="${t('template_modal.btn_import_tip')}">
              ${t('template_modal.btn_import')}
            </button>
            <button id="btn-export-catalog" class="btn btn-secondary-pill" title="${t('template_modal.btn_export_tip')}">
              ${t('template_modal.btn_export')}
            </button>
            <input type="file" id="plugin-file-input" accept=".json" style="display: none;" />
          </div>
        </div>

        <!-- Filtros por Categoria -->
        <div class="template-category-tabs">
          ${CATEGORY_MAP.map(catItem => {
            const count = catItem.id === 'Todos' 
              ? allTemplates.length 
              : allTemplates.filter(tpl => tpl.category === catItem.id).length;
            const activeClass = this.activeCategory === catItem.id ? 'active' : '';
            return `<button class="category-pill ${activeClass}" data-cat="${catItem.id}">
              ${t(catItem.labelKey)} <span class="cat-count">(${count})</span>
            </button>`;
          }).join('')}
        </div>

        <!-- Busca e Taxonomia (Domínio → Subdomínio) -->
        <div class="template-filter-bar" id="template-taxonomy-bar">
          <input type="search" id="template-search-input" class="form-input" placeholder="${this.escapeHtml(t('template_modal.search_placeholder', 'Buscar templates por nome, domínio ou descrição...'))}" value="${this.escapeHtml(this.searchQuery)}" />
          <select id="template-domain-select" class="form-select">
            ${this.renderDomainOptions(catTemplates)}
          </select>
          <select id="template-subdomain-select" class="form-select">
            ${this.renderSubdomainOptions(catTemplates)}
          </select>
        </div>

        <!-- Grid de Cards de Templates -->
        <div class="template-modal-body" id="templates-grid-container">
          ${this.renderGridMarkup(filtered)}
        </div>

        <div class="modal-footer">
          <div class="footer-info">
            ${t('template_modal.footer_hint')}
          </div>
          <button id="btn-close-modal-bottom" class="btn btn-secondary">${t('code_modal.btn_close')}</button>
        </div>
      </div>
    `;

    const dialog = this.overlay.querySelector('.modal-dialog') as HTMLElement;
    if (dialog) {
      this.windowController = ModalWindowManager.attach({
        dialog,
        minWidth: 640,
        minHeight: 480,
        onClose: () => this.hide()
      });
    }

    this.bindEvents();
  }

  private memoryModeLabel(mode?: string): string {
    switch (mode) {
      case 'consult': return t('template_modal.memory_consult', 'Consultar');
      case 'learn': return t('template_modal.memory_learn', 'Aprender');
      case 'off': return t('template_modal.memory_off', 'Desligada');
      default: return t('template_modal.memory_inherit', 'Herdar');
    }
  }

  private renderTemplateCard(tpl: IAgentTemplate): string {
    const paramKeys = Object.keys(tpl.parameters);
    const paramSummary = paramKeys.length > 0 
      ? paramKeys.slice(0, 3).join(', ') + (paramKeys.length > 3 ? '...' : '')
      : t('template_modal.none');

    const isBundled = Boolean(tpl.bundledInProduct || tpl.origin?.bundledInProduct);
    const originText = [tpl.origin?.repo, tpl.origin?.license].filter(Boolean).join(' · ');

    return `
      <div class="template-card" data-template-id="${tpl.id}">
        <div class="template-card-header">
          <div class="template-icon-badge">${tpl.icon}</div>
          <div class="template-info">
            <div class="template-name">${this.escapeHtml(tpl.name)}</div>
            <div class="template-badges">
              <span class="inspector-badge badge-${tpl.taskType}">${tpl.taskType.toUpperCase()}</span>
              ${isBundled ? `<span class="properties-badge properties-badge--success">${t('template_modal.bundled_badge', 'Nativo Arestrix')}</span>` : ''}
              ${tpl.assetKind === 'pipeline' ? `<span class="plugin-custom-badge">${t('template_modal.pipeline_badge', 'PIPELINE')}</span>` : ''}
              ${tpl.taskType === 'agent' ? `<span class="plugin-custom-badge">${t('template_modal.preset_badge', 'PRÉ-CONFIGURADO')}</span>` : ''}
              ${tpl.isCustomPlugin && !isBundled && tpl.assetKind !== 'pipeline' ? `<span class="plugin-custom-badge">${t('template_modal.dynamic_plugin_badge')}</span>` : ''}
            </div>
          </div>
        </div>

        ${(tpl.domain || tpl.subdomain || originText) ? `
          <div style="font-size: 11px; color: #616161; margin-bottom: 6px; display: flex; flex-wrap: wrap; gap: 6px;">
            ${tpl.domain ? `<span>${this.escapeHtml(tpl.domain)}${tpl.subdomain ? ` → ${this.escapeHtml(tpl.subdomain)}` : ''}</span>` : ''}
            ${originText ? `<span>(${this.escapeHtml(originText)})</span>` : ''}
          </div>
        ` : ''}

        <p class="template-desc">${this.escapeHtml(tpl.description)}</p>

        ${tpl.taskType === 'agent' ? `
          <div class="template-meta-row">
            <div class="meta-item">
              <span class="meta-label">${t('template_modal.meta_skills', 'Skills:')}</span>
              <span class="meta-value">${tpl.agentSettings?.skills?.length ?? 0}</span>
            </div>
            <div class="meta-item">
              <span class="meta-label">${t('template_modal.meta_memory', 'Memória:')}</span>
              <span class="meta-value">${this.escapeHtml(this.memoryModeLabel(tpl.agentSettings?.memory?.mode))}</span>
            </div>
            <div class="meta-item">
              <span class="meta-label">${t('template_modal.meta_tools', 'Ferramentas:')}</span>
              <span class="meta-value">${tpl.agentSettings?.codeMode?.allowedTools?.length ?? 0}</span>
            </div>
          </div>
        ` : ''}

        <div class="template-meta-row">
          <div class="meta-item">
            <span class="meta-label">${t('template_modal.meta_inputs')}</span>
            <span class="meta-value">${t('template_modal.ports_count', { count: tpl.inputs.length })}</span>
          </div>
          <div class="meta-item">
            <span class="meta-label">${t('template_modal.meta_outputs')}</span>
            <span class="meta-value">${t('template_modal.ports_count', { count: tpl.outputs.length })}</span>
          </div>
          <div class="meta-item">
            <span class="meta-label">${t('template_modal.meta_params')}</span>
            <span class="meta-value" title="${paramSummary}">${t('template_modal.configs_count', { count: paramKeys.length })}</span>
          </div>
        </div>

        <div class="template-card-footer">
          ${(tpl.taskType !== 'agent' && tpl.assetKind !== 'pipeline') ? `
            <button class="btn-toggle-code" data-code-id="${tpl.id}">
              ${t('template_modal.view_cpp')}
            </button>
          ` : ''}
          ${(tpl.isCustomPlugin || tpl.taskType === 'agent') ? `
            <button class="btn btn-secondary btn-save-library" data-save-library-id="${tpl.id}">
              ${t('template_modal.btn_save_library', 'Salvar na biblioteca')}
            </button>
          ` : ''}
          ${tpl.isCustomPlugin ? `
            <button class="btn btn-secondary btn-delete-template" data-delete-id="${tpl.id}">
              ${t('template_modal.btn_delete', 'Excluir')}
            </button>
          ` : ''}
          <button class="btn btn-primary btn-insert-node" data-insert-id="${tpl.id}">
            ${t('template_modal.btn_insert')}
          </button>
        </div>
      </div>
    `;
  }


  private showCodePreview(tpl: IAgentTemplate): void {
    let code: string;
    try {
      code = tpl.generateLambdaBody(this.getDefaultParams(tpl), tpl.name);
    } catch (err: any) {
      code = `// ${err?.message ?? err}`;
    }

    const layer = document.createElement('div');
    layer.className = 'modal-overlay template-code-viewer';
    layer.innerHTML = `
      <div class="modal-dialog template-code-viewer-dialog">
        <div class="template-code-viewer-header">
          <strong>${this.escapeHtml(tpl.name)}</strong>
          <span>C++20</span>
        </div>
        <pre class="code-font template-code-viewer-body">${this.escapeHtml(code)}</pre>
        <div class="modal-footer">
          <button class="btn btn-secondary" data-act="copy">${t('template_modal.btn_copy_code', 'Copiar')}</button>
          <button class="btn btn-primary" data-act="close">${t('code_modal.btn_close')}</button>
        </div>
      </div>
    `;
    const close = () => { layer.remove(); document.removeEventListener('keydown', onKey, true); };
    const onKey = (ev: KeyboardEvent) => {
      if (ev.key === 'Escape') { ev.stopPropagation(); close(); }
    };
    document.addEventListener('keydown', onKey, true);
    layer.addEventListener('click', (ev) => { if (ev.target === layer) close(); });
    layer.querySelector('[data-act="close"]')?.addEventListener('click', close);
    layer.querySelector('[data-act="copy"]')?.addEventListener('click', async (ev) => {
      const btn = ev.currentTarget as HTMLElement;
      try {
        await navigator.clipboard.writeText(code);
        btn.textContent = t('template_modal.msg_copied', 'Copiado!');
      } catch {
        // sem permissão de área de transferência: o texto continua selecionável
      }
    });
    document.body.appendChild(layer);
  }

  private getDefaultParams(tpl: IAgentTemplate): Record<string, any> {
    const res: Record<string, any> = {};
    Object.entries(tpl.parameters).forEach(([k, def]) => {
      res[k] = def.defaultValue;
    });
    return res;
  }

  private bindEvents(): void {
    // Fechar
    this.overlay.querySelector('#btn-close-template-modal')?.addEventListener('click', () => this.hide());
    this.overlay.querySelector('#btn-close-modal-bottom')?.addEventListener('click', () => this.hide());
    this.overlay.addEventListener('click', (e) => {
      if (e.target === this.overlay) this.hide();
    });

    // Filtros de Categoria
    this.overlay.querySelectorAll('.category-pill').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const cat = (e.currentTarget as HTMLElement).getAttribute('data-cat');
        if (cat) {
          this.activeCategory = cat;
          this.activeDomain = '';
          this.activeSubdomain = '';
          this.showAllTemplates = false;
          this.render();
        }
      });
    });

    const searchInput = this.overlay.querySelector('#template-search-input') as HTMLInputElement | null;
    searchInput?.addEventListener('input', () => {
      this.searchQuery = searchInput.value;
      this.showAllTemplates = false;
      this.updateGridAndTaxonomy();
    });

    const domainSelect = this.overlay.querySelector('#template-domain-select') as HTMLSelectElement | null;
    domainSelect?.addEventListener('change', () => {
      this.activeDomain = domainSelect.value;
      this.activeSubdomain = '';
      this.showAllTemplates = false;
      this.updateGridAndTaxonomy();
    });

    const subdomainSelect = this.overlay.querySelector('#template-subdomain-select') as HTMLSelectElement | null;
    subdomainSelect?.addEventListener('change', () => {
      this.activeSubdomain = subdomainSelect.value;
      this.showAllTemplates = false;
      this.updateGridAndTaxonomy();
    });

    this.overlay.querySelector('#btn-restore-builtin-templates')?.addEventListener('click', () => {
      const confirmed = typeof window === 'undefined' || typeof window.confirm !== 'function'
        ? true
        : window.confirm(t('template_modal.confirm_restore_builtin', 'Restaurar a Biblioteca Arestrix para o padrão de instalação?'));
      if (!confirmed) return;
      const restoreService = this.libraryService as RestoreCapableLibraryService;
      void Promise.resolve(restoreService.resetBuiltinLibrary?.()).then((result) => {
        globalEventBus.emit('notification:show', {
          severity: result !== undefined ? 'success' : 'warning',
          title: t('template_modal.btn_restore_builtin', 'Restaurar padrão de instalação'),
          message: result !== undefined
            ? t('template_modal.restore_builtin_success', 'Padrão de instalação restaurado.')
            : t('template_modal.restore_builtin_failed', 'Não foi possível restaurar o padrão de instalação.')
        });
        this.render();
      });
    });

    this.bindGridEvents();

    // Inserir template vindo da biblioteca
    this.overlay.querySelector('#btn-library-templates')?.addEventListener('click', () => {
      this.hide();
      const backstage = BackstageView.getInstance();
      if (backstage) {
        void backstage.open('library');
      }
    });

    // Inserir pipeline vindo da biblioteca como agente
    this.overlay.querySelector('#btn-library-pipelines')?.addEventListener('click', () => {
      this.hide();
      const backstage = BackstageView.getInstance();
      if (backstage) {
        void backstage.open('library');
      }
    });

    this.bindRemainingTemplateEvents();
  }

  private bindGridEvents(): void {
    // Inserir nó individual
    this.overlay.querySelectorAll('.btn-insert-node').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const tplId = (e.currentTarget as HTMLElement).getAttribute('data-insert-id');
        if (tplId) {
          const tpl = this.registry.getTemplate(tplId);
          if (tpl?.assetKind === 'pipeline') this.insertPipelineAsAgent(tpl);
          else this.insertTemplateNode(tplId);
          this.hide();
        }
      });
    });

    this.overlay.querySelector('#btn-show-all-templates')?.addEventListener('click', () => {
      this.showAllTemplates = true;
      this.updateGridAndTaxonomy();
    });

    // Salvar template customizado na biblioteca
    this.overlay.querySelectorAll('.btn-save-library').forEach(btn => {
      btn.addEventListener('click', () => {
        const id = (btn as HTMLElement).getAttribute('data-save-library-id');
        if (!id) return;
        const json = this.registry.exportTemplateAsJSON(id);
        if (!json) return;
        void this.libraryService.saveTemplateToLibrary(JSON.parse(json), { scope: 'workspace' }).then(asset => {
          globalEventBus.emit('notification:show', {
            severity: asset ? 'success' : 'warning',
            title: t('template_modal.btn_save_library', 'Salvar na biblioteca'),
            message: asset
              ? t('template_modal.library_saved', 'Template salvo na biblioteca com auditoria.')
              : t('template_modal.library_save_failed', 'Não foi possível salvar o template na biblioteca.')
          });
        });
      });
    });

    // Excluir template customizado
    this.overlay.querySelectorAll('.btn-delete-template').forEach(btn => {
      btn.addEventListener('click', () => {
        const id = (btn as HTMLElement).getAttribute('data-delete-id');
        const tpl = id ? this.registry.getTemplate(id) : undefined;
        if (!id || !tpl) return;
        const confirmed = typeof window === 'undefined' || typeof window.confirm !== 'function'
          ? true
          : window.confirm(t('template_modal.confirm_delete', 'Excluir o template "{{name}}"?', { name: tpl.name }));
        if (!confirmed) return;
        this.registry.unregisterTemplate(id);
        this.render();
      });
    });

    // Ver o C++20 do template em janela somente leitura
    this.overlay.querySelectorAll('.btn-toggle-code').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const tplId = (e.currentTarget as HTMLElement).getAttribute('data-code-id');
        const tpl = tplId ? this.registry.getTemplate(tplId) : undefined;
        if (tpl) this.showCodePreview(tpl);
      });
    });
  }

  private updateGridAndTaxonomy(): void {
    const allTemplates = this.registry.getAllTemplates();
    const catTemplates = this.categoryTemplates(allTemplates);
    const domainSelect = this.overlay.querySelector('#template-domain-select') as HTMLSelectElement | null;
    const subdomainSelect = this.overlay.querySelector('#template-subdomain-select') as HTMLSelectElement | null;
    if (domainSelect) {
      domainSelect.innerHTML = this.renderDomainOptions(catTemplates);
      domainSelect.value = this.activeDomain;
    }
    if (subdomainSelect) {
      subdomainSelect.innerHTML = this.renderSubdomainOptions(catTemplates);
      subdomainSelect.value = this.activeSubdomain;
    }
    const container = this.overlay.querySelector('#templates-grid-container') as HTMLElement | null;
    if (container) {
      container.innerHTML = this.renderGridMarkup(this.filteredTemplates(allTemplates));
      this.bindGridEvents();
    }
  }

  private bindRemainingTemplateEvents(): void {

    // Preset Arquitetura Completa
    this.overlay.querySelector('#btn-preset-pipeline')?.addEventListener('click', () => {
      const confirmed = typeof window === 'undefined' || typeof window.confirm !== 'function'
        ? true
        : window.confirm(t('template_modal.confirm_preset', 'O grafo atual será substituído pela arquitetura completa de agentes. Continuar?'));
      if (!confirmed) return;
      this.instantiateCompleteMultiAgentPreset();
      this.hide();
    });

    // Salvar o pipeline atual como ativo composto (apresentado como agente)
    this.overlay.querySelector('#btn-save-pipeline-library')?.addEventListener('click', () => {
      try {
        const template = this.registry.saveGraphAsPipelineTemplate(this.graph);
        const json = this.registry.exportTemplateAsJSON(template.id);
        if (!json) return;
        void this.libraryService.saveTemplateToLibrary(JSON.parse(json), { scope: 'workspace' }).then(asset => {
          globalEventBus.emit('notification:show', {
            severity: asset ? 'success' : 'warning',
            title: t('template_modal.btn_save_pipeline', 'Salvar pipeline atual na biblioteca'),
            message: asset
              ? t('template_modal.pipeline_saved', 'Pipeline salvo na biblioteca como agente visual.')
              : t('template_modal.pipeline_save_failed', 'Não foi possível salvar o pipeline na biblioteca.')
          });
          if (asset) this.render();
        });
      } catch (error: any) {
        globalEventBus.emit('notification:show', {
          severity: 'error',
          title: t('template_modal.btn_save_pipeline', 'Salvar pipeline atual na biblioteca'),
          message: t('template_modal.pipeline_pack_failed', 'Não foi possível empacotar o pipeline: {{error}}', {
            error: error?.message || String(error)
          })
        });
      }
    });

    // Importar um pipeline de outro projeto como ativo composto
    const pipelineInput = this.overlay.querySelector('#pipeline-file-input') as HTMLInputElement | null;
    this.overlay.querySelector('#btn-import-pipeline-library')?.addEventListener('click', () => {
      pipelineInput?.click();
    });
    pipelineInput?.addEventListener('change', () => {
      const file = pipelineInput.files?.[0];
      if (!file) return;
      const reader = new FileReader();
      reader.onload = async (event) => {
        try {
          const text = String(event.target?.result || '');
          const schema = JSON.parse(text);
          const template = this.registry.importGraphAsPipelineTemplate(schema, file.name.replace(/\.json$/i, ''));
          const json = this.registry.exportTemplateAsJSON(template.id);
          const asset = json
            ? await this.libraryService.saveTemplateToLibrary(JSON.parse(json), { scope: 'workspace' })
            : null;
          if (!asset) throw new Error('auditoria rejeitou o pipeline');
          globalEventBus.emit('notification:show', {
            severity: 'success',
            title: t('template_modal.btn_import_pipeline', 'Importar pipeline (.json)'),
            message: t('template_modal.pipeline_imported', 'Pipeline importado e salvo na biblioteca.')
          });
          this.render();
        } catch (error: any) {
          globalEventBus.emit('notification:show', {
            severity: 'error',
            title: t('template_modal.btn_import_pipeline', 'Importar pipeline (.json)'),
            message: t('template_modal.pipeline_import_failed', 'Não foi possível importar o pipeline: {{error}}', {
              error: error?.message || String(error)
            })
          });
        } finally {
          pipelineInput.value = '';
        }
      };
      reader.readAsText(file);
    });

    // Publicar o pipeline atual como nova versão de um asset da biblioteca
    this.overlay.querySelector('#btn-publish-pipeline-version')?.addEventListener('click', () => {
      this.hide();
      const backstage = BackstageView.getInstance();
      if (backstage) {
        void backstage.open('library');
      }
    });

    // Importar a pasta de outro projeto, usando pipelines/main.json
    const projectInput = this.overlay.querySelector('#project-folder-input') as HTMLInputElement | null;
    this.overlay.querySelector('#btn-import-project-agent')?.addEventListener('click', () => {
      projectInput?.click();
    });
    projectInput?.addEventListener('change', () => {
      void (async () => {
        try {
          const files = Array.from(projectInput.files || []);
          const selection = selectProjectPipelineFiles(files);
          if (!selection.pipeline) {
            throw new Error('a pasta não contém pipelines/main.json nem outro pipeline .json');
          }
          let projectName = selection.rootName;
          if (selection.manifest) {
            try {
              const manifest = JSON.parse(await selection.manifest.text());
              projectName = manifest.name || manifest.projectName || projectName;
            } catch {
              // manifesto inválido não impede usar o pipeline; o nome da pasta basta
            }
          }
          const schema = JSON.parse(await selection.pipeline.text());
          const template = this.registry.importGraphAsPipelineTemplate(schema, projectName);
          const json = this.registry.exportTemplateAsJSON(template.id);
          const asset = json
            ? await this.libraryService.saveTemplateToLibrary(JSON.parse(json), { scope: 'workspace' })
            : null;
          if (!asset) throw new Error('auditoria rejeitou o projeto');
          globalEventBus.emit('notification:show', {
            severity: 'success',
            title: t('template_modal.btn_import_project', 'Importar projeto (pasta)'),
            message: t('template_modal.project_imported', 'Projeto importado como agente visual e salvo na biblioteca.')
          });
          this.render();
        } catch (error: any) {
          globalEventBus.emit('notification:show', {
            severity: 'error',
            title: t('template_modal.btn_import_project', 'Importar projeto (pasta)'),
            message: t('template_modal.project_import_failed', 'Não foi possível importar o projeto: {{error}}', {
              error: error?.message || String(error)
            })
          });
        } finally {
          projectInput.value = '';
        }
      })();
    });

    // Exportar Catálogo
    this.overlay.querySelector('#btn-export-catalog')?.addEventListener('click', () => {
      const json = this.registry.exportAllTemplatesAsJSON();
      const blob = new Blob([json], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = 'arestrix_agent_templates.json';
      a.click();
      URL.revokeObjectURL(url);
    });

    // Importar Plugin
    const fileInput = this.overlay.querySelector('#plugin-file-input') as HTMLInputElement;
    this.overlay.querySelector('#btn-import-plugin')?.addEventListener('click', () => {
      fileInput?.click();
    });

    fileInput?.addEventListener('change', () => {
      const file = fileInput.files?.[0];
      if (file) {
        const reader = new FileReader();
        reader.onload = (e) => {
          const content = e.target?.result as string;
          if (content) {
            const res = this.registry.importTemplatesFromJSON(content);
            if (res.errors.length > 0) {
              alert(`Avisos ao importar:\n\n${res.errors.join('\n')}`);
            } else {
              alert(`✅ Sucesso! ${res.success} plugin(s) de agentes carregado(s) com sucesso.`);
            }
            this.activeCategory = 'Plugins Customizados';
            this.render();
          }
        };
        reader.readAsText(file);
      }
    });
  }

  private insertTemplateNode(
    templateId: string,
    source?: INodeData['librarySource'],
    origin?: IAssetOriginMetadata
  ): void {
    const center = this.viewport.getCameraCenter();
    const existingCount = this.graph.getAllNodes().length;
    const offset = (existingCount % 5) * 40;
    const x = Math.round((center.x + offset) / 10) * 10;
    const y = Math.round((center.y - offset) / 10) * 10;

    const node = this.registry.instantiateNode(templateId, undefined, x, y);
    const tpl = this.registry.getTemplate(templateId);
    if (source) {
      node.librarySource = { ...source };
    } else if (tpl?.bundledInProduct || tpl?.origin?.bundledInProduct) {
      node.librarySource = {
        libraryId: tpl.origin?.libraryId || 'library-arestrix',
        assetId: tpl.origin?.assetId || `asset-library-arestrix-agent-${tpl.id}`,
        hash: tpl.origin?.installHash || '',
        version: tpl.origin?.installVersion || tpl.version || '1.0.0',
        bundledInProduct: true,
        originRepo: tpl.origin?.repo,
        originUrl: tpl.origin?.url,
        originLicense: tpl.origin?.license
      };
    }
    const resolvedOrigin = origin || tpl?.origin;
    if (resolvedOrigin) {
      node.origin = JSON.parse(JSON.stringify(resolvedOrigin));
    }
    const cmd = new AddNodeCommand(this.graph, node);
    this.graph.commandManager.execute(cmd);

    // Seleciona o nó inserido
    this.viewport.selectNode(node.id);
  }

  /**
   * Pipeline importado como agente: um único vértice visível (tipo Call com
   * apresentação de agente) executa um subflow interno oculto.
   */
  private insertPipelineAsAgent(
    tpl: IAgentTemplate,
    source?: INodeData['librarySource'],
    origin?: IAssetOriginMetadata
  ): void {
    if (!tpl.childGraph) return;
    const center = this.viewport.getCameraCenter();
    const stamp = `${Date.now().toString(36)}${Math.random().toString(36).slice(2, 5)}`;
    const callId = `pipeline_agent_${tpl.id}_${stamp}`;
    const targetId = `pipeline_internal_${tpl.id}_${stamp}`;
    const x = Math.round(center.x / 10) * 10;
    const y = Math.round(center.y / 10) * 10;

    const callInputId = `${callId}_in`;
    const callOutputId = `${callId}_out`;
    const callPortId = `${callId}_out_call`;
    const targetInputId = `${targetId}_in`;
    const targetOutputId = `${targetId}_out`;

    const visible: INodeData = {
      id: callId,
      label: tpl.name,
      type: 'call',
      position: { x, y, z: 0 },
      inputs: [{ id: callInputId, name: 'in', direction: 'input', type: 'flow' }],
      outputs: [
        { id: callOutputId, name: 'out', direction: 'output', type: 'flow' },
        ...(tpl.pipelineInterface?.outputs || []).map((output, index) => ({
          id: `${callId}_out_data_${index}`,
          name: output.name,
          direction: 'output' as const,
          type: 'data' as const,
          dataType: 'std::string'
        })),
        { id: callPortId, name: 'call', direction: 'output', type: 'call' }
      ],
      parameters: {},
      callSettings: defaultCallVertexSettings(),
      templateId: tpl.id,
      presetId: tpl.id,
      presetLocked: true,
      presentation: 'agent',
      pipelineInterface: tpl.pipelineInterface ? JSON.parse(JSON.stringify(tpl.pipelineInterface)) : undefined,
      librarySource: source ? { ...source } : undefined,
      origin: (origin || tpl.origin) ? JSON.parse(JSON.stringify(origin || tpl.origin)) : undefined,
      requiredHeaders: tpl.requiredHeaders
    };

    const internal: INodeData = {
      id: targetId,
      label: `${tpl.name} · pipeline`,
      type: 'subflow',
      position: { x: x + 600, y: y + 240, z: 0 },
      inputs: [{ id: targetInputId, name: 'in', direction: 'input', type: 'flow' }],
      outputs: [{ id: targetOutputId, name: 'out', direction: 'output', type: 'flow' }],
      parameters: {},
      childGraph: JSON.parse(JSON.stringify(tpl.childGraph)),
      pipelineInterface: tpl.pipelineInterface ? JSON.parse(JSON.stringify(tpl.pipelineInterface)) : undefined,
      hidden: true,
      requiredHeaders: tpl.requiredHeaders
    };

    const edge: IEdgeData = {
      id: `edge_${callId}_${callPortId}_to_${targetId}_${targetInputId}`,
      sourceNodeId: callId,
      sourcePortId: callPortId,
      targetNodeId: targetId,
      targetPortId: targetInputId,
      kind: 'call',
      callSettings: defaultCallSettings()
    };

    this.graph.commandManager.execute(new CompoundCommand(
      t('template_modal.pipeline_inserted', 'Pipeline "{{name}}" inserido como agente visual.', { name: tpl.name }),
      [
        new AddNodeCommand(this.graph, visible),
        new AddNodeCommand(this.graph, internal),
        new AddEdgeCommand(this.graph, edge)
      ]
    ));
    this.viewport.selectNode(visible.id);
    globalEventBus.emit('notification:show', {
      severity: 'success',
      title: t('template_modal.pipeline_badge', 'PIPELINE'),
      message: t('template_modal.pipeline_inserted', 'Pipeline "{{name}}" inserido como agente visual.', { name: tpl.name })
    });
  }

  public instantiateCompleteMultiAgentPreset(): void {
    this.graph.clear();

    // 1. Agente de Fila Concorrente (Produtor/Consumidor)
    const nQueue = this.registry.instantiateNode('concurrent_queue_worker', '1. Fila_Buffer_ThreadSafe', -500, 60);
    this.graph.addNode(nQueue);

    // 2. Agente de Visão (Decomposição em Subflow)
    const nVision = this.registry.instantiateNode('vision_pipeline_subflow', '2. Visao_Subflow_Paralelo', -150, 60);
    this.graph.addNode(nVision);

    // 3. Agente LLM (Decisor Estocástico com Ramo 0 e Ramo 1)
    const nLLM = this.registry.instantiateNode('llm_decision_agent', '3. LLM_Prompt_Decisor', 200, 60);
    this.graph.addNode(nLLM);

    // 4. Agente SQLite (Persistência com CriticalSection)
    const nSQLite = this.registry.instantiateNode('sqlite_persistence_agent', '4. SQLite_Persistencia_ACID', 580, 140);
    this.graph.addNode(nSQLite);

    // 5. Agente de Consenso (Módulo Encapsulado)
    const nConsensus = this.registry.instantiateNode('consensus_module_agent', '5. Modulo_Consenso_Final', 920, 140);
    this.graph.addNode(nConsensus);

    // Conexões:
    // Fila -> Visão
    this.graph.createEdge(nQueue.id, nQueue.outputs[0].id, nVision.id, nVision.inputs[0].id);

    // Visão -> LLM
    this.graph.createEdge(nVision.id, nVision.outputs[0].id, nLLM.id, nLLM.inputs[0].id);

    // LLM Ramo 0 (Aprovado) -> SQLite
    this.graph.createEdge(nLLM.id, nLLM.outputs[0].id, nSQLite.id, nSQLite.inputs[0].id);

    // LLM Ramo 1 (Re-tentativa / Feedback Loop) -> Volta para Fila Concorrente!
    this.graph.createEdge(nLLM.id, nLLM.outputs[1].id, nQueue.id, nQueue.inputs[0].id);

    // SQLite -> Módulo de Consenso
    this.graph.createEdge(nSQLite.id, nSQLite.outputs[0].id, nConsensus.id, nConsensus.inputs[0].id);

    setTimeout(() => {
      this.viewport.zoomToFit();
    }, 150);
  }

  private escapeHtml(text: string): string {
    return text
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
}
