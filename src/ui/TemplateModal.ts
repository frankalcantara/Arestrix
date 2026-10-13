import { AgentTemplateRegistry } from '../core/templates/AgentTemplateRegistry.ts';
import { IAgentTemplate } from '../core/templates/AgentTemplateTypes.ts';
import { Graph } from '../core/Graph.ts';
import { Viewport } from '../view/Viewport.ts';
import { AddNodeCommand } from '../core/history/GraphCommands.ts';
import { ModalWindowManager, IModalWindowController } from './modals/ModalWindowManager.ts';
import { t, onLocaleChange } from '../i18n/index.ts';

const CATEGORY_MAP: { id: string; labelKey: string }[] = [
  { id: 'Todos', labelKey: 'template_modal.cat_all' },
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
    this.render();
    this.overlay.classList.remove('hidden');
    this.windowController?.bringToFront();
  }

  public hide(): void {
    this.overlay.classList.add('hidden');
  }

  private render(): void {
    const allTemplates = this.registry.getAllTemplates();
    const filtered = this.activeCategory === 'Todos'
      ? allTemplates
      : allTemplates.filter(tpl => tpl.category === this.activeCategory);

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
          </div>
          <div class="template-action-right">
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

        <!-- Grid de Cards de Templates -->
        <div class="template-modal-body">
          <div class="templates-grid">
            ${filtered.length === 0 ? `
              <div class="templates-empty">
                <div class="empty-icon">🔍</div>
                <div class="empty-title">${t('template_modal.empty_title')}</div>
                <div class="empty-desc">${t('template_modal.empty_desc')}</div>
              </div>
            ` : filtered.map(tpl => this.renderTemplateCard(tpl)).join('')}
          </div>
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

  private renderTemplateCard(tpl: IAgentTemplate): string {
    const paramKeys = Object.keys(tpl.parameters);
    const paramSummary = paramKeys.length > 0 
      ? paramKeys.slice(0, 3).join(', ') + (paramKeys.length > 3 ? '...' : '')
      : t('template_modal.none');

    return `
      <div class="template-card" data-template-id="${tpl.id}">
        <div class="template-card-header">
          <div class="template-icon-badge">${tpl.icon}</div>
          <div class="template-info">
            <div class="template-name">${this.escapeHtml(tpl.name)}</div>
            <div class="template-badges">
              <span class="inspector-badge badge-${tpl.taskType}">${tpl.taskType.toUpperCase()}</span>
              ${tpl.isCustomPlugin ? `<span class="plugin-custom-badge">${t('template_modal.dynamic_plugin_badge')}</span>` : ''}
            </div>
          </div>
        </div>

        <p class="template-desc">${this.escapeHtml(tpl.description)}</p>

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
          <button class="btn-toggle-code" data-code-id="${tpl.id}">
            ${t('template_modal.view_cpp')}
          </button>
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
          this.render();
        }
      });
    });

    // Inserir nó individual
    this.overlay.querySelectorAll('.btn-insert-node').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const tplId = (e.currentTarget as HTMLElement).getAttribute('data-insert-id');
        if (tplId) {
          this.insertTemplateNode(tplId);
          this.hide();
        }
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

    // Preset Arquitetura Completa
    this.overlay.querySelector('#btn-preset-pipeline')?.addEventListener('click', () => {
      this.instantiateCompleteMultiAgentPreset();
      this.hide();
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

  private insertTemplateNode(templateId: string): void {
    const center = this.viewport.getCameraCenter();
    const existingCount = this.graph.getAllNodes().length;
    const offset = (existingCount % 5) * 40;
    const x = Math.round((center.x + offset) / 10) * 10;
    const y = Math.round((center.y - offset) / 10) * 10;

    const node = this.registry.instantiateNode(templateId, undefined, x, y);
    const cmd = new AddNodeCommand(this.graph, node);
    this.graph.commandManager.execute(cmd);

    // Seleciona o nó inserido
    this.viewport.selectNode(node.id);
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
