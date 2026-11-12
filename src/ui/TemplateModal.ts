import { AgentTemplateRegistry } from '../core/templates/AgentTemplateRegistry.ts';
import { IAgentTemplate } from '../core/templates/AgentTemplateTypes.ts';
import { Graph } from '../core/Graph.ts';
import { Viewport } from '../view/Viewport.ts';
import { AddNodeCommand, AddEdgeCommand, CompoundCommand } from '../core/history/GraphCommands.ts';
import { INodeData, IEdgeData, defaultCallSettings, defaultCallVertexSettings } from '../core/types.ts';
import { ModalWindowManager, IModalWindowController } from './modals/ModalWindowManager.ts';
import { t, onLocaleChange } from '../i18n/index.ts';
import { LibraryPickerModal } from './libraries/LibraryPickerModal.ts';
import { LibraryService } from '../core/libraries/LibraryService.ts';
import { globalEventBus } from '../core/EventBus.ts';

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
  private libraryPicker?: LibraryPickerModal;
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
            ${this.activeCategory === 'Agentes' ? `
              <button id="btn-save-pipeline-library" class="btn btn-secondary-pill" title="${t('template_modal.btn_save_pipeline')}">
                ${t('template_modal.btn_save_pipeline')}
              </button>
              <button id="btn-import-pipeline-library" class="btn btn-secondary-pill" title="${t('template_modal.btn_import_pipeline')}">
                ${t('template_modal.btn_import_pipeline')}
              </button>
              <input type="file" id="pipeline-file-input" accept=".json,application/json" style="display: none;" />
            ` : ''}
          </div>
          <div class="template-action-right">
            <button id="btn-library-templates" class="btn btn-secondary-pill" title="${t('template_modal.btn_library_tip', 'Inserir um template salvo em uma biblioteca')}">
              ${t('template_modal.btn_library', 'Bibliotecas')}
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

        <!-- Grid de Cards de Templates -->
        <div class="template-modal-body">
          <div class="templates-grid">
            ${filtered.length === 0 ? `
              <div class="templates-empty">
                <div class="empty-icon">🔍</div>
                <div class="empty-title">${t('template_modal.empty_title')}</div>
                <div class="empty-desc">${this.activeCategory === 'Agentes'
                  ? t('template_modal.empty_agents_desc', 'Salve um vértice agente como template ou adicione um agente da biblioteca.')
                  : t('template_modal.empty_desc')}</div>
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

    return `
      <div class="template-card" data-template-id="${tpl.id}">
        <div class="template-card-header">
          <div class="template-icon-badge">${tpl.icon}</div>
          <div class="template-info">
            <div class="template-name">${this.escapeHtml(tpl.name)}</div>
            <div class="template-badges">
              <span class="inspector-badge badge-${tpl.taskType}">${tpl.taskType.toUpperCase()}</span>
              ${tpl.assetKind === 'pipeline' ? `<span class="plugin-custom-badge">${t('template_modal.pipeline_badge', 'PIPELINE')}</span>` : ''}
              ${tpl.taskType === 'agent' ? `<span class="plugin-custom-badge">${t('template_modal.preset_badge', 'PRÉ-CONFIGURADO')}</span>` : ''}
              ${tpl.isCustomPlugin && tpl.assetKind !== 'pipeline' ? `<span class="plugin-custom-badge">${t('template_modal.dynamic_plugin_badge')}</span>` : ''}
            </div>
          </div>
        </div>

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
          this.render();
        }
      });
    });

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

    // Inserir template vindo da biblioteca
    this.overlay.querySelector('#btn-library-templates')?.addEventListener('click', () => {
      if (!this.libraryPicker) this.libraryPicker = new LibraryPickerModal();
      this.libraryPicker.open({
        kind: 'agent-template',
        projectId: this.graph.getProjectId(),
        title: t('template_modal.library_title', 'Inserir template da biblioteca'),
        subtitle: t('template_modal.library_subtitle', 'O template será registrado no catálogo e inserido no grafo.'),
        onSelect: async (asset) => {
          if (asset.payload.kind !== 'agent-template') return;
          const result = this.registry.importTemplatesFromJSON(JSON.stringify(asset.payload.template));
          if (result.success === 0) {
            alert(t('template_modal.library_insert_failed', 'Não foi possível importar o template: {{errors}}', { errors: result.errors.join('; ') }));
            return;
          }
          this.activeCategory = this.registry.getTemplate(asset.payload.template.id)?.category || 'Agentes';
          this.render();
          const importedTemplate = this.registry.getTemplate(asset.payload.template.id);
          if (importedTemplate?.assetKind === 'pipeline') this.insertPipelineAsAgent(importedTemplate);
          else this.insertTemplateNode(asset.payload.template.id);
          this.hide();
          globalEventBus.emit('notification:show', {
            severity: 'success',
            title: t('template_modal.library_title', 'Inserir template da biblioteca'),
            message: t('template_modal.library_inserted', 'Template "{{name}}" inserido no grafo.', { name: asset.name })
          });
        }
      });
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

  /**
   * Pipeline importado como agente: um único vértice visível (tipo Call com
   * apresentação de agente) executa um subflow interno oculto.
   */
  private insertPipelineAsAgent(tpl: IAgentTemplate): void {
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
        { id: callPortId, name: 'call', direction: 'output', type: 'call' }
      ],
      parameters: {},
      callSettings: defaultCallVertexSettings(),
      templateId: tpl.id,
      presetId: tpl.id,
      presetLocked: true,
      presentation: 'agent',
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
