import { Graph } from '../core/Graph.ts';
import { INodeData } from '../core/types.ts';
import { EventBus } from '../core/EventBus.ts';
import { RemoveNodeCommand, UpdateNodeCommand } from '../core/history/GraphCommands.ts';
import { AgentTemplateRegistry } from '../core/templates/AgentTemplateRegistry.ts';
import { t } from '../i18n/index.ts';

/**
 * Arestrix Studio - Microsoft Office Task Pane (Painel de Tarefas / Formatar Agente)
 * Painel dinâmico, redimensionável por splitter, recolhível e destacável em janela flutuante.
 */
export class Inspector {
  private container: HTMLElement;
  private graph: Graph;
  private bus: EventBus;
  private currentNodeId: string | null = null;
  private registry: AgentTemplateRegistry;
  private activeTab: 'general' | 'lambda' | 'agent' | 'ports' = 'general';

  // Estados de layout dinâmico (não-fixo)
  private isCollapsed: boolean = false;
  private isFloating: boolean = false;
  private panelWidth: number = 360;
  private toggleStripEl!: HTMLElement;
  private splitterEl!: HTMLElement;

  constructor(parent: HTMLElement, graph: Graph, bus: EventBus) {
    this.graph = graph;
    this.bus = bus;
    this.registry = AgentTemplateRegistry.getInstance();

    // 1. Splitter físico permanente entre o Canvas e o Painel de Tarefas
    this.splitterEl = document.createElement('div');
    this.splitterEl.className = 'office-task-pane-splitter';
    this.splitterEl.title = t('inspector.controls.splitter_tip');
    this.splitterEl.innerHTML = `<div class="splitter-grip-v"></div>`;
    parent.appendChild(this.splitterEl);

    // 2. Contêiner principal do Task Pane
    this.container = document.createElement('aside');
    this.container.className = 'inspector-panel office-task-pane';
    this.container.style.width = `${this.panelWidth}px`;
    parent.appendChild(this.container);

    // 3. Fita lateral de reabertura quando recolhido
    this.toggleStripEl = document.createElement('div');
    this.toggleStripEl.className = 'office-task-pane-toggle-strip';
    this.toggleStripEl.style.display = 'none';
    this.toggleStripEl.title = t('inspector.controls.toggle_strip_tip');
    this.toggleStripEl.innerHTML = `
      <span class="strip-icon">⚙️</span>
      <span class="strip-text">${t('inspector.controls.toggle_strip_label')}</span>
    `;
    this.toggleStripEl.addEventListener('click', () => this.toggleCollapse(false));
    parent.appendChild(this.toggleStripEl);

    this.initSplitterDrag();
    this.renderEmpty();
    this.bindEvents();
  }

  private bindEvents(): void {
    this.bus.on('node:selected', (node: INodeData) => {
      this.currentNodeId = node.id;
      if (this.isCollapsed) {
        this.toggleCollapse(false);
      }
      this.renderNode(node);
    });

    this.bus.on('node:deselected', () => {
      this.currentNodeId = null;
      this.renderEmpty();
    });

    this.bus.on('node:removed', ({ id }: { id: string }) => {
      if (this.currentNodeId === id) {
        this.currentNodeId = null;
        this.renderEmpty();
      }
    });

    this.bus.on('node:updated', (node: INodeData) => {
      if (this.currentNodeId === node.id) {
        const activeEl = document.activeElement;
        if (!activeEl || !this.container.contains(activeEl)) {
          this.renderNode(node);
        }
      }
    });

    const onGraphChange = () => {
      if (!this.currentNodeId) {
        this.renderEmpty();
      }
    };
    this.bus.on('node:added', onGraphChange);
    this.bus.on('edge:added', onGraphChange);
    this.bus.on('edge:removed', onGraphChange);
    this.bus.on('graph:loaded', onGraphChange);

    this.bus.on('locale:changed', () => {
      if (this.currentNodeId) {
        const node = this.graph.getNode(this.currentNodeId);
        if (node) this.renderNode(node);
        else this.renderEmpty();
      } else {
        this.renderEmpty();
      }
    });
  }

  /**
   * Alterna entre modo Painel Ancorado (Docked) e Janela Flutuante (Floating Tool Window)
   */
  public toggleFloat(): void {
    this.isFloating = !this.isFloating;
    if (this.isFloating) {
      this.isCollapsed = false;
      this.container.classList.remove('collapsed');
      this.container.classList.add('is-floating');
      this.toggleStripEl.style.display = 'none';
      this.splitterEl.style.display = 'none';

      // Posiciona a janela flutuante no canto superior direito do workspace
      const defaultLeft = Math.max(20, window.innerWidth - this.panelWidth - 30);
      const defaultTop = 110;
      this.container.style.left = `${defaultLeft}px`;
      this.container.style.top = `${defaultTop}px`;
      this.container.style.width = `${this.panelWidth}px`;
      this.container.style.height = '580px';
    } else {
      this.container.classList.remove('is-floating');
      this.container.style.left = '';
      this.container.style.top = '';
      this.container.style.height = '100%';
      this.container.style.width = `${this.panelWidth}px`;
      this.splitterEl.style.display = 'flex';
    }

    if (this.currentNodeId) {
      const node = this.graph.getNode(this.currentNodeId);
      if (node) this.renderNode(node);
      else this.renderEmpty();
    } else {
      this.renderEmpty();
    }
  }

  /**
   * Recolhe ou expande o painel de propriedades
   */
  public toggleCollapse(force?: boolean): void {
    if (this.isFloating) {
      this.toggleFloat();
      return;
    }

    this.isCollapsed = force !== undefined ? force : !this.isCollapsed;
    if (this.isCollapsed) {
      this.container.classList.add('collapsed');
      this.container.style.width = '0px';
      this.splitterEl.style.display = 'none';
      this.toggleStripEl.style.display = 'flex';
    } else {
      this.container.classList.remove('collapsed');
      this.container.style.width = `${this.panelWidth}px`;
      this.splitterEl.style.display = 'flex';
      this.toggleStripEl.style.display = 'none';
    }
  }

  /**
   * Gerencia arraste do splitter físico para redimensionamento da largura do painel
   */
  private initSplitterDrag(): void {
    let startX = 0;
    let startWidth = 0;
    let isDragging = false;

    const onPointerMove = (e: PointerEvent) => {
      if (!isDragging) return;
      const dx = startX - e.clientX; // Arrastar para a esquerda expande
      const newWidth = Math.max(260, Math.min(window.innerWidth - 300, startWidth + dx));
      this.setWidth(newWidth);
    };

    const onPointerUp = (e: PointerEvent) => {
      if (!isDragging) return;
      isDragging = false;
      this.splitterEl.classList.remove('resizing');
      document.body.style.cursor = '';
      try {
        this.splitterEl.releasePointerCapture(e.pointerId);
      } catch {
        // Ignora caso não suporte capture
      }
      window.removeEventListener('pointermove', onPointerMove);
      window.removeEventListener('pointerup', onPointerUp);
      window.removeEventListener('pointercancel', onPointerUp);
    };

    this.splitterEl.addEventListener('pointerdown', (e: PointerEvent) => {
      if (this.isFloating || this.isCollapsed) return;
      e.preventDefault();
      isDragging = true;
      startX = e.clientX;
      startWidth = this.container.offsetWidth || this.panelWidth;
      this.splitterEl.classList.add('resizing');
      document.body.style.cursor = 'col-resize';
      try {
        this.splitterEl.setPointerCapture(e.pointerId);
      } catch {
        // Fallback para listeners na janela
      }
      window.addEventListener('pointermove', onPointerMove);
      window.addEventListener('pointerup', onPointerUp);
      window.addEventListener('pointercancel', onPointerUp);
    });

    this.splitterEl.addEventListener('dblclick', () => {
      if (this.isFloating) return;
      if (this.panelWidth > 450) {
        this.setWidth(360);
      } else {
        this.setWidth(560);
      }
    });
  }

  /**
   * Altera a largura do painel em pixels
   */
  public setWidth(newWidth: number): void {
    this.panelWidth = Math.max(260, Math.min(window.innerWidth - 300, Math.round(newWidth)));
    this.container.style.width = `${this.panelWidth}px`;
    const widthVal = this.container.querySelector('.tp-width-val');
    if (widthVal) {
      widthVal.textContent = `${this.panelWidth}px`;
    }
  }

  public getWidth(): number {
    return this.panelWidth;
  }

  /**
   * Alterna entre presets rápidos de largura
   */
  public cycleWidth(): void {
    const presets = [340, 480, 640, 820];
    const currentIndex = presets.findIndex(p => Math.abs(p - this.panelWidth) < 40);
    const nextIndex = (currentIndex + 1) % presets.length;
    this.setWidth(presets[nextIndex]);
  }

  /**
   * Configura controles de cabeçalho do painel e arraste flutuante
   */
  private initPaneControls(): void {
    // 1. Alternar Largura (Presets rápidos)
    this.container.querySelector('#btn-cycle-width')?.addEventListener('click', () => {
      this.cycleWidth();
    });

    // 2. Arraste da Janela Flutuante pela Barra de Título
    const header = this.container.querySelector('.office-task-pane-header') as HTMLElement;
    if (header && this.isFloating) {
      let startMouseX = 0;
      let startMouseY = 0;
      let startLeft = 0;
      let startTop = 0;

      const onDragMove = (e: PointerEvent) => {
        const dx = e.clientX - startMouseX;
        const dy = e.clientY - startMouseY;
        this.container.style.left = `${Math.max(10, Math.min(window.innerWidth - 100, startLeft + dx))}px`;
        this.container.style.top = `${Math.max(40, Math.min(window.innerHeight - 100, startTop + dy))}px`;
      };

      const onDragUp = () => {
        window.removeEventListener('pointermove', onDragMove);
        window.removeEventListener('pointerup', onDragUp);
      };

      header.addEventListener('pointerdown', (e: PointerEvent) => {
        // Ignora cliques nos botões de ação
        if ((e.target as HTMLElement).closest('button')) return;
        e.preventDefault();
        startMouseX = e.clientX;
        startMouseY = e.clientY;
        startLeft = this.container.offsetLeft;
        startTop = this.container.offsetTop;
        window.addEventListener('pointermove', onDragMove);
        window.addEventListener('pointerup', onDragUp);
      });
    }

    // 3. Botões de ação do cabeçalho
    this.container.querySelector('#btn-toggle-float')?.addEventListener('click', () => {
      this.toggleFloat();
    });

    this.container.querySelector('#btn-toggle-collapse')?.addEventListener('click', () => {
      this.toggleCollapse();
    });

    this.container.querySelector('#btn-deselect-node')?.addEventListener('click', () => {
      if (this.currentNodeId) {
        this.bus.emit('node:deselected');
      } else {
        this.toggleCollapse(true);
      }
    });
  }

  /**
   * Renderiza propriedades gerais do diagrama quando nenhum nó está selecionado
   */
  private renderEmpty(): void {
    const meta = this.graph.getMetadata();
    const nodeCount = this.graph.getAllNodes().length;
    const edgeCount = this.graph.getAllEdges().length;

    this.container.innerHTML = `
      <div class="office-task-pane-header">
        <div class="office-task-pane-title-group">
          <span class="office-tp-icon">📐</span>
          <span class="office-tp-title">${t('inspector.title_diagram')}</span>
        </div>
        <div class="office-task-pane-actions">
          <button class="office-tp-btn-icon" id="btn-toggle-float" title="${this.isFloating ? 'Ancorar à Direita (Dock)' : t('inspector.controls.toggle_float_tip')}">
            ${this.isFloating ? '📌' : '🗖'}
          </button>
          <button class="office-tp-btn-icon" id="btn-toggle-collapse" title="${t('inspector.controls.toggle_collapse_tip')}">
            ${this.isCollapsed ? '◀' : '▶'}
          </button>
          <button class="office-tp-close-btn" id="btn-deselect-node" title="${t('common.close')}">&times;</button>
        </div>
      </div>

      <div class="office-task-pane-body">
        <div class="office-tp-section">
          <div class="office-tp-section-header">
            <span>${t('inspector.empty_state.graph_info_header')}</span>
          </div>
          <div class="form-group">
            <label class="form-label">${t('inspector.empty_state.graph_name_label')}</label>
            <input type="text" class="form-input form-input-readonly" value="${this.escapeHtml(meta.graphName || 'Arestrix Pipeline')}" readonly />
          </div>
          <div class="form-group">
            <label class="form-label">${t('inspector.empty_state.graph_desc_label')}</label>
            <textarea class="form-textarea form-input-readonly" rows="2" readonly>${this.escapeHtml(meta.description || 'Pipeline Concorrente com Taskflow C++20')}</textarea>
          </div>
        </div>

        <div class="office-tp-section">
          <div class="office-tp-section-header">
            <span>${t('inspector.empty_state.graph_stats_header')}</span>
          </div>
          <div class="office-stats-grid">
            <div class="office-stat-card">
              <span class="stat-number">${nodeCount}</span>
              <span class="stat-label">${t('inspector.empty_state.stat_vertices')}</span>
            </div>
            <div class="office-stat-card">
              <span class="stat-number">${edgeCount}</span>
              <span class="stat-label">${t('inspector.empty_state.stat_connections')}</span>
            </div>
          </div>
        </div>

        <div class="office-tp-section">
          <div class="office-tp-section-header">
            <span>${t('inspector.empty_state.physical_env_header')}</span>
          </div>
          <div class="form-group">
            <label class="form-label">${t('inspector.empty_state.compiler_runtime_label')}</label>
            <input type="text" class="form-input form-input-readonly" value="MSVC C++20 Taskflow (/std:c++20 /O2)" readonly />
          </div>
          <div class="form-group">
            <label class="form-label">${t('inspector.empty_state.sqlite_label')}</label>
            <input type="text" class="form-input form-input-readonly" value="storage/database.sqlite" readonly />
          </div>
        </div>

        <div class="office-hint-callout">
          <div class="hint-icon">💡</div>
          <div class="hint-text">
            ${t('inspector.empty_state.hint_tip')}
          </div>
        </div>
      </div>
    `;

    this.initPaneControls();
  }

  private renderNode(node: INodeData): void {
    const tpl = node.templateId ? this.registry.getTemplate(node.templateId) : undefined;
    const typeLabel = (node.type || 'static').toUpperCase();

    this.container.innerHTML = `
      <!-- Cabeçalho do Task Pane Office -->
      <div class="office-task-pane-header">
        <div class="office-task-pane-title-group">
          <span class="office-tp-icon">⚙️</span>
          <span class="office-tp-title">${t('inspector.title_node')}</span>
        </div>
        <div class="office-task-pane-actions">
          <button class="office-tp-btn-icon" id="btn-toggle-float" title="${this.isFloating ? 'Ancorar à Direita (Dock)' : t('inspector.controls.toggle_float_tip')}">
            ${this.isFloating ? '📌' : '🗖'}
          </button>
          <button class="office-tp-btn-icon" id="btn-toggle-collapse" title="${t('inspector.controls.toggle_collapse_tip')}">
            ${this.isCollapsed ? '◀' : '▶'}
          </button>
          <button class="office-tp-close-btn" id="btn-deselect-node" title="${t('common.close')}">&times;</button>
        </div>
      </div>

      <!-- Abas de Navegação do Task Pane Office -->
      <div class="office-task-pane-tabs">
        <button class="office-tp-tab ${this.activeTab === 'general' ? 'active' : ''}" data-tab="general">${t('inspector.tabs.general')}</button>
        <button class="office-tp-tab ${this.activeTab === 'lambda' ? 'active' : ''}" data-tab="lambda">${t('inspector.tabs.lambda')}</button>
        <button class="office-tp-tab ${this.activeTab === 'agent' ? 'active' : ''}" data-tab="agent">${t('inspector.tabs.agent')}</button>
        <button class="office-tp-tab ${this.activeTab === 'ports' ? 'active' : ''}" data-tab="ports">${t('inspector.tabs.ports')}</button>
      </div>

      <div class="office-task-pane-body">
        <!-- ABA 1: GERAL -->
        <div class="office-tp-tab-content ${this.activeTab === 'general' ? 'active' : ''}" id="tp-content-general">
          <div class="office-node-badge-row">
            <span class="inspector-badge badge-${node.type}">${typeLabel}</span>
            ${tpl ? `<span class="inspector-template-tag">${tpl.icon} ${this.escapeHtml(tpl.name)}</span>` : ''}
          </div>

          <div class="form-group">
            <label class="form-label" for="node-label">${t('inspector.node_view.label_name')}</label>
            <input type="text" id="node-label" class="form-input" value="${this.escapeHtml(node.label)}" />
          </div>

          <div class="form-group">
            <label class="form-label">${t('inspector.node_view.unique_id')}</label>
            <input type="text" class="form-input form-input-readonly" value="${node.id}" readonly />
          </div>

          <div class="form-group">
            <label class="form-label">${t('inspector.node_view.taskflow_type')}</label>
            <select id="node-type-select" class="form-select">
              <option value="static" ${node.type === 'static' ? 'selected' : ''}>Static Task (${t('inspector.node_view.label_name').includes('Rótulo') ? 'Deterministico' : 'Deterministic'})</option>
              <option value="condition" ${node.type === 'condition' ? 'selected' : ''}>Condition Task (${t('inspector.node_view.label_name').includes('Rótulo') ? 'Decisão / Branch' : 'Decision / Branch'})</option>
              <option value="subflow" ${node.type === 'subflow' ? 'selected' : ''}>Subflow Task (${t('inspector.node_view.label_name').includes('Rótulo') ? 'Dinamico' : 'Dynamic'})</option>
              <option value="module" ${node.type === 'module' ? 'selected' : ''}>Module Task (${t('inspector.node_view.label_name').includes('Rótulo') ? 'Hierarquico' : 'Hierarchical'})</option>
              <option value="critical" ${node.type === 'critical' ? 'selected' : ''}>Critical Section (${t('inspector.node_view.label_name').includes('Rótulo') ? 'Mutex Concorrente' : 'Concurrent Mutex'})</option>
              <option value="cuda" ${node.type === 'cuda' ? 'selected' : ''}>CUDA Kernel (${t('inspector.node_view.label_name').includes('Rótulo') ? 'GPU Acelerado' : 'GPU Accelerated'})</option>
              <option value="fork" ${node.type === 'fork' ? 'selected' : ''}>Fork Task (${t('inspector.node_view.label_name').includes('Rótulo') ? 'Despachante Concorrente' : 'Concurrent Dispatcher'})</option>
              <option value="join" ${node.type === 'join' ? 'selected' : ''}>Join Task (${t('inspector.node_view.label_name').includes('Rótulo') ? 'Barreira de Sincronização' : 'Synchronization Barrier'})</option>
              <option value="agent" ${node.type === 'agent' ? 'selected' : ''}>Agent (Code Mode IA)</option>
            </select>
          </div>

          <div class="form-group" style="margin-top: 8px;">
            <div class="office-task-desc-box" id="node-task-desc-box" style="padding: 10px 12px; background: #faf9f8; border: 1px solid #edebe9; border-left: 3px solid #0f6cbd; border-radius: 4px; font-size: 12px; line-height: 1.5; color: #323130;">
              <p style="margin: 0;" id="node-task-explanation-p">
                ${this.getTaskTypeExplanation(node.type)}
              </p>
            </div>
          </div>

          ${(node.type === 'subflow' || node.type === 'module') ? `
            <div class="form-group">
              <button id="btn-dive-subflow" class="btn btn-dive-action">
                ${t('inspector.node_view.open_subflow')}
              </button>
            </div>
          ` : ''}

          <!-- Seção de Parâmetros do Template / Agente -->
          ${this.renderParametersSection(node, tpl)}

          <div class="form-actions" style="margin-top: 16px;">
            <button id="btn-delete-node" class="btn btn-danger">${t('inspector.node_view.delete_node')}</button>
          </div>
        </div>

        <!-- ABA 2: C++20 LAMBDA -->
        <div class="office-tp-tab-content ${this.activeTab === 'lambda' ? 'active' : ''}" id="tp-content-lambda">
          <div class="office-tp-section-header">
            <span>${t('inspector.node_view.lambda_header')}</span>
            ${tpl ? `
              <button id="btn-sync-lambda" class="btn-sync-action" title="${t('inspector.node_view.sync_lambda_tip')}">
                ${t('inspector.node_view.sync_lambda')}
              </button>
            ` : ''}
          </div>

          <div class="code-editor-header">
            <span>auto task = tf.emplace([...](...) {</span>
          </div>
          <textarea id="node-lambda" class="form-textarea code-font" rows="12">${this.escapeHtml(node.customLambdaBody || '')}</textarea>
          <div class="code-editor-footer">
            <span>});</span>
          </div>
          <div class="office-field-hint">${t('inspector.node_view.lambda_hint')}</div>
        </div>

        <!-- ABA 3: GOVERNANÇA & IA -->
        <div class="office-tp-tab-content ${this.activeTab === 'agent' ? 'agent active' : ''}" id="tp-content-agent">
          <div class="office-tp-section-header">
            <span>${t('inspector.node_view.agent_settings_title')}</span>
          </div>

          <div class="agent-command-grid">
            <button class="agent-command-card" id="btn-open-node-sandbox">
              <span class="cmd-icon">🛡️</span>
              <div class="cmd-details">
                <span class="cmd-name">${t('inspector.node_view.card_sandbox_title')} <span class="badge-inherited" style="font-size: 9px; padding: 1px 4px; margin-left: 4px;">🛠️ Workspace</span></span>
                <span class="cmd-desc">${t('inspector.node_view.card_sandbox_desc')}</span>
              </div>
            </button>

            <button class="agent-command-card" id="btn-open-node-model">
              <span class="cmd-icon">🧠</span>
              <div class="cmd-details">
                <span class="cmd-name">${t('inspector.node_view.card_model_title')} <span class="badge-inherited" style="font-size: 9px; padding: 1px 4px; margin-left: 4px;">📦 Projeto</span></span>
                <span class="cmd-desc">${t('inspector.node_view.card_model_desc')}</span>
              </div>
            </button>

            <button class="agent-command-card" id="btn-open-node-tools">
              <span class="cmd-icon">🔧</span>
              <div class="cmd-details">
                <span class="cmd-name">${t('inspector.node_view.card_tools_title')} <span class="badge-inherited" style="font-size: 9px; padding: 1px 4px; margin-left: 4px;">🛠️ / 📦</span></span>
                <span class="cmd-desc">${t('inspector.node_view.card_tools_desc')}</span>
              </div>
            </button>

            <button class="agent-command-card" id="btn-open-node-memory">
              <span class="cmd-icon">💾</span>
              <div class="cmd-details">
                <span class="cmd-name">${t('inspector.node_view.card_memory_title')} <span class="badge-inherited" style="font-size: 9px; padding: 1px 4px; margin-left: 4px;">📦 Projeto</span></span>
                <span class="cmd-desc">${t('inspector.node_view.card_memory_desc')}</span>
              </div>
            </button>

            <button class="agent-command-card" id="btn-open-node-skills">
              <span class="cmd-icon">📜</span>
              <div class="cmd-details">
                <span class="cmd-name">${t('inspector.node_view.card_skills_title')} <span class="badge-inherited" style="font-size: 9px; padding: 1px 4px; margin-left: 4px;">🛠️ / 📦</span></span>
                <span class="cmd-desc">${t('inspector.node_view.card_skills_desc')}</span>
              </div>
            </button>
          </div>
        </div>

        <!-- ABA 4: PORTAS E CONEXÕES -->
        <div class="office-tp-tab-content ${this.activeTab === 'ports' ? 'active' : ''}" id="tp-content-ports">
          <div class="office-tp-section-header">
            <span>${t('inspector.node_view.ports_header')}</span>
          </div>
          <div class="port-list">
            <div class="port-sublist">
              <span class="port-sublist-title">${t('inspector.node_view.in_ports_title', { count: node.inputs.length })}</span>
              ${node.inputs.length === 0 ? `<span class="empty-ports-hint">${t('inspector.node_view.in_ports_empty')}</span>` : ''}
              ${node.inputs.map(p => `<span class="port-tag in-tag">📥 ${p.name}</span>`).join('')}
            </div>
            <div class="port-sublist" style="margin-top: 12px;">
              <span class="port-sublist-title">${t('inspector.node_view.out_ports_title', { count: node.outputs.length })}</span>
              ${node.outputs.length === 0 ? `<span class="empty-ports-hint">${t('inspector.node_view.out_ports_empty')}</span>` : ''}
              ${node.outputs.map(p => `<span class="port-tag out-tag">📤 ${p.name}</span>`).join('')}
            </div>
          </div>
        </div>
      </div>
    `;

    this.initPaneControls();

    // Bind das Abas
    const tabBtns = this.container.querySelectorAll('.office-tp-tab');
    tabBtns.forEach(btn => {
      btn.addEventListener('click', (e) => {
        const targetTab = (e.currentTarget as HTMLElement).getAttribute('data-tab') as any;
        if (targetTab) {
          this.activeTab = targetTab;
          tabBtns.forEach(b => b.classList.remove('active'));
          (e.currentTarget as HTMLElement).classList.add('active');

          const contents = this.container.querySelectorAll('.office-tp-tab-content');
          contents.forEach(c => c.classList.remove('active'));
          const targetContent = this.container.querySelector(`#tp-content-${targetTab}`);
          if (targetContent) targetContent.classList.add('active');
        }
      });
    });

    // Modais Flutuantes de Agente (Fase 7 & 12)
    this.container.querySelector('#btn-open-node-sandbox')?.addEventListener('click', () => {
      this.bus.emit('node:open-sandbox-modal', node.id);
    });
    this.container.querySelector('#btn-open-node-model')?.addEventListener('click', () => {
      this.bus.emit('node:open-model-modal', node.id);
    });
    this.container.querySelector('#btn-open-node-tools')?.addEventListener('click', () => {
      this.bus.emit('node:open-tools-modal', node.id);
    });
    this.container.querySelector('#btn-open-node-memory')?.addEventListener('click', () => {
      this.bus.emit('node:open-memory-modal', node.id);
    });
    this.container.querySelector('#btn-open-node-skills')?.addEventListener('click', () => {
      this.bus.emit('node:open-skills-modal', node.id);
    });

    // Subflow
    this.container.querySelector('#btn-dive-subflow')?.addEventListener('click', () => {
      this.graph.diveIntoSubflow(node.id);
    });

    // Tipo
    const typeSelect = this.container.querySelector('#node-type-select') as HTMLSelectElement;
    let oldType = node.type;
    typeSelect?.addEventListener('change', () => {
      const newType = typeSelect.value as any;
      const explanationP = this.container.querySelector('#node-task-explanation-p');
      if (explanationP) {
        explanationP.textContent = this.getTaskTypeExplanation(newType);
      }
      if (newType !== oldType) {
        const cmd = new UpdateNodeCommand(
          this.graph,
          node.id,
          { type: oldType },
          { type: newType },
          `Alterar tipo de "${node.label}" para ${newType}`
        );
        this.graph.commandManager.execute(cmd);
        oldType = newType;
      }
    });

    // Label
    const labelInput = this.container.querySelector('#node-label') as HTMLInputElement;
    let initialLabel = node.label;
    labelInput?.addEventListener('focus', () => {
      initialLabel = labelInput.value;
    });
    labelInput?.addEventListener('input', () => {
      this.graph.updateNode(node.id, { label: labelInput.value });
    });
    labelInput?.addEventListener('blur', () => {
      if (labelInput.value !== initialLabel) {
        const cmd = new UpdateNodeCommand(
          this.graph,
          node.id,
          { label: initialLabel },
          { label: labelInput.value },
          `Renomear vértice para "${labelInput.value}"`
        );
        this.graph.commandManager.record(cmd);
        initialLabel = labelInput.value;
      }
    });

    // Parâmetros
    this.bindParameterInputs(node, tpl);

    // Sincronizar Lambda
    this.container.querySelector('#btn-sync-lambda')?.addEventListener('click', () => {
      if (tpl) {
        const newBody = tpl.generateLambdaBody(node.parameters, node.label);
        const oldBody = node.customLambdaBody || '';
        const cmd = new UpdateNodeCommand(
          this.graph,
          node.id,
          { customLambdaBody: oldBody },
          { customLambdaBody: newBody },
          `Sincronizar lambda de "${node.label}"`
        );
        this.graph.commandManager.execute(cmd);
        const lambdaTextarea = this.container.querySelector('#node-lambda') as HTMLTextAreaElement;
        if (lambdaTextarea) lambdaTextarea.value = newBody;
      }
    });

    // Lambda textarea
    const lambdaInput = this.container.querySelector('#node-lambda') as HTMLTextAreaElement;
    let initialLambda = node.customLambdaBody || '';
    lambdaInput?.addEventListener('focus', () => {
      initialLambda = lambdaInput.value;
    });
    lambdaInput?.addEventListener('input', () => {
      this.graph.updateNode(node.id, { customLambdaBody: lambdaInput.value });
    });
    lambdaInput?.addEventListener('blur', () => {
      if (lambdaInput.value !== initialLambda) {
        const cmd = new UpdateNodeCommand(
          this.graph,
          node.id,
          { customLambdaBody: initialLambda },
          { customLambdaBody: lambdaInput.value },
          `Editar lambda de "${node.label}"`
        );
        this.graph.commandManager.record(cmd);
        initialLambda = lambdaInput.value;
      }
    });

    // Delete
    const deleteBtn = this.container.querySelector('#btn-delete-node');
    deleteBtn?.addEventListener('click', () => {
      if (confirm(t('inspector.node_view.delete_confirm', { label: node.label }))) {
        const cmd = new RemoveNodeCommand(this.graph, node.id);
        this.graph.commandManager.execute(cmd);
      }
    });
  }

  private renderParametersSection(node: INodeData, tpl?: any): string {
    const params = node.parameters || {};
    const keys = Object.keys(params);

    if (keys.length === 0 && !tpl) {
      return '';
    }

    let rowsHtml = '';

    if (tpl) {
      Object.entries(tpl.parameters).forEach(([key, def]: [string, any]) => {
        const val = params[key] !== undefined ? params[key] : def.defaultValue;
        rowsHtml += `
          <div class="param-row">
            <div class="param-label-group">
              <span class="param-name">${this.escapeHtml(def.label)}</span>
              <span class="param-desc">${this.escapeHtml(def.description)}</span>
            </div>
            <div class="param-input-wrap">
              ${def.type === 'select' ? `
                <select class="form-select param-input" data-param-key="${key}">
                  ${(def.options || []).map((opt: string) => `
                    <option value="${opt}" ${String(val) === String(opt) ? 'selected' : ''}>${opt}</option>
                  `).join('')}
                </select>
              ` : def.type === 'boolean' ? `
                <input type="checkbox" class="param-checkbox param-input" data-param-key="${key}" ${val ? 'checked' : ''} />
              ` : `
                <input type="${def.type === 'number' ? 'number' : 'text'}" 
                       step="${def.type === 'number' ? 'any' : undefined}"
                       class="form-input param-input" 
                       data-param-key="${key}" 
                       value="${this.escapeHtml(String(val))}" />
              `}
            </div>
          </div>
        `;
      });
    } else {
      keys.forEach(key => {
        const val = params[key];
        rowsHtml += `
          <div class="param-row">
            <div class="param-label-group">
              <span class="param-name">${this.escapeHtml(key)}</span>
            </div>
            <div class="param-input-wrap">
              <input type="text" class="form-input param-input" data-param-key="${key}" value="${this.escapeHtml(String(val))}" />
            </div>
          </div>
        `;
      });
    }

    return `
      <div class="office-tp-section" style="margin-top: 14px;">
        <div class="office-tp-section-header">
          <span>${t('inspector.node_view.task_params')}</span>
        </div>
        <div class="params-container">
          ${rowsHtml}
        </div>
      </div>
    `;
  }

  private bindParameterInputs(node: INodeData, tpl?: any): void {
    const inputs = this.container.querySelectorAll('.param-input');
    inputs.forEach(inputEl => {
      const key = inputEl.getAttribute('data-param-key');
      if (!key) return;

      const handleChange = () => {
        let newVal: any;
        if (inputEl instanceof HTMLInputElement && inputEl.type === 'checkbox') {
          newVal = inputEl.checked;
        } else if (inputEl instanceof HTMLInputElement && inputEl.type === 'number') {
          newVal = parseFloat(inputEl.value) || 0;
        } else {
          newVal = (inputEl as HTMLInputElement | HTMLSelectElement).value;
        }

        const oldParams = { ...node.parameters };
        const newParams = { ...node.parameters, [key]: newVal };

        let extraUpdates: Partial<INodeData> = { parameters: newParams };

        if (tpl) {
          const updatedLambda = tpl.generateLambdaBody(newParams, node.label);
          extraUpdates.customLambdaBody = updatedLambda;
          const lambdaEl = this.container.querySelector('#node-lambda') as HTMLTextAreaElement;
          if (lambdaEl) lambdaEl.value = updatedLambda;
        }

        const cmd = new UpdateNodeCommand(
          this.graph,
          node.id,
          { parameters: oldParams },
          extraUpdates,
          `Alterar parâmetro "${key}" em ${node.label}`
        );
        this.graph.commandManager.execute(cmd);
      };

      if (inputEl instanceof HTMLSelectElement || (inputEl instanceof HTMLInputElement && inputEl.type === 'checkbox')) {
        inputEl.addEventListener('change', handleChange);
      } else {
        inputEl.addEventListener('blur', handleChange);
      }
    });
  }

  private getTaskTypeExplanation(type: string): string {
    switch (type) {
      case 'static':
        return t('inspector.explanations.static');
      case 'condition':
        return t('inspector.explanations.condition');
      case 'subflow':
        return t('inspector.explanations.subflow');
      case 'module':
        return t('inspector.explanations.module');
      case 'critical':
        return t('inspector.explanations.critical');
      case 'cuda':
        return t('inspector.explanations.cuda');
      case 'fork':
        return t('inspector.explanations.fork');
      case 'join':
        return t('inspector.explanations.join');
      case 'agent':
        return t('inspector.explanations.agent');
      default:
        return t('inspector.explanations.default');
    }
  }

  private escapeHtml(text: string): string {
    return text
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
}
