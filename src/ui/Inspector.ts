import { Graph } from '../core/Graph.ts';
import { INodeData, IPort, ISwitchSettings, SwitchMode, defaultSwitchSettings, IMcpVertexSettings, defaultMcpSettings, IOutputSettings, defaultOutputSettings, OutputArtifactType, defaultCallSettings, IInteractionSettings, IFormField, FormFieldType, defaultInteractionSettings } from '../core/types.ts';
import { EventBus } from '../core/EventBus.ts';
import { RemoveNodeCommand, RemoveEdgeCommand, UpdateNodeCommand, CompoundCommand } from '../core/history/GraphCommands.ts';
import { AgentTemplateRegistry } from '../core/templates/AgentTemplateRegistry.ts';
import { t, translateMcpError } from '../i18n/index.ts';

/**
 * Arestrix Studio - Microsoft Office Task Pane (Painel de Tarefas / Formatar Agente)
 * Painel dinâmico, redimensionável por splitter, recolhível e destacável em janela flutuante.
 */
export class Inspector {
  private container: HTMLElement;          // visão Propriedades (redesenhada a cada seleção)
  private paneEl!: HTMLElement;           // o painel em si: largura, flutuar, recolher
  private paneTabsEl!: HTMLElement;
  private assistantViewEl!: HTMLElement;
  private paneTab: 'properties' | 'assistant' = 'properties';
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

    // 2. Contêiner principal do Task Pane, com abas de topo: Propriedades (este Inspector) e Assistente.
    //    O Inspector redesenha só a própria visão; a do Assistente sobrevive às trocas de seleção.
    this.paneEl = document.createElement('aside');
    this.paneEl.className = 'inspector-panel office-task-pane';
    this.paneEl.style.width = `${this.panelWidth}px`;
    this.paneTabsEl = document.createElement('div');
    this.paneTabsEl.className = 'task-pane-tabs';
    (this.paneTabsEl as any).role = 'tablist';
    this.container = document.createElement('div');
    this.container.className = 'task-pane-view task-pane-view-properties';
    this.assistantViewEl = document.createElement('div');
    this.assistantViewEl.className = 'task-pane-view task-pane-view-assistant';
    this.assistantViewEl.style.display = 'none';
    this.paneEl.appendChild(this.paneTabsEl);
    this.paneEl.appendChild(this.container);
    this.paneEl.appendChild(this.assistantViewEl);
    parent.appendChild(this.paneEl);
    this.renderPaneTabs();

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

  // Visão onde o Assistente desenha (preservada entre as trocas de seleção).
  public getAssistantContainer(): HTMLElement {
    return this.assistantViewEl;
  }

  public showPaneTab(tab: 'properties' | 'assistant'): void {
    this.paneTab = tab;
    this.container.style.display = tab === 'properties' ? '' : 'none';
    this.assistantViewEl.style.display = tab === 'assistant' ? '' : 'none';
    this.renderPaneTabs();
    if (this.isCollapsed) this.toggleCollapse(false);
    this.bus.emit('taskpane:tab-changed', tab);
  }

  private renderPaneTabs(): void {
    const tabs: Array<['properties' | 'assistant', string, string]> = [
      ['properties', '📐', t('inspector.pane_tabs.properties')],
      ['assistant', '✨', t('inspector.pane_tabs.assistant')]
    ];
    this.paneTabsEl.innerHTML = tabs.map(([id, icon, label]) => `
      <button type="button" role="tab" class="task-pane-tab ${this.paneTab === id ? 'active' : ''}" data-pane-tab="${id}" aria-selected="${this.paneTab === id}">
        <span aria-hidden="true">${icon}</span><span>${label}</span>
      </button>
    `).join('');
    this.paneTabsEl.querySelectorAll?.<HTMLElement>('[data-pane-tab]').forEach(btn => {
      btn.addEventListener('click', () => this.showPaneTab(btn.dataset.paneTab === 'assistant' ? 'assistant' : 'properties'));
    });
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
      this.renderPaneTabs();
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
      this.paneEl.classList.remove('collapsed');
      this.paneEl.classList.add('is-floating');
      this.toggleStripEl.style.display = 'none';
      this.splitterEl.style.display = 'none';

      // Posiciona a janela flutuante no canto superior direito do workspace
      const defaultLeft = Math.max(20, window.innerWidth - this.panelWidth - 30);
      const defaultTop = 110;
      this.paneEl.style.left = `${defaultLeft}px`;
      this.paneEl.style.top = `${defaultTop}px`;
      this.paneEl.style.width = `${this.panelWidth}px`;
      this.paneEl.style.height = '580px';
    } else {
      this.paneEl.classList.remove('is-floating');
      this.paneEl.style.left = '';
      this.paneEl.style.top = '';
      this.paneEl.style.height = '100%';
      this.paneEl.style.width = `${this.panelWidth}px`;
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
      this.paneEl.classList.add('collapsed');
      this.paneEl.style.width = '0px';
      this.splitterEl.style.display = 'none';
      this.toggleStripEl.style.display = 'flex';
    } else {
      this.paneEl.classList.remove('collapsed');
      this.paneEl.style.width = `${this.panelWidth}px`;
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
      startWidth = this.paneEl.offsetWidth || this.panelWidth;
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
    this.paneEl.style.width = `${this.panelWidth}px`;
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
        this.paneEl.style.left = `${Math.max(10, Math.min(window.innerWidth - 100, startLeft + dx))}px`;
        this.paneEl.style.top = `${Math.max(40, Math.min(window.innerHeight - 100, startTop + dy))}px`;
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
        startLeft = this.paneEl.offsetLeft;
        startTop = this.paneEl.offsetTop;
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
              <option value="mcp" ${node.type === 'mcp' ? 'selected' : ''}>MCP (${t('inspector.node_view.label_name').includes('Rótulo') ? 'Ferramenta externa' : 'External tool'})</option>
              <option value="switch" ${node.type === 'switch' ? 'selected' : ''}>Switch (${t('inspector.node_view.label_name').includes('Rótulo') ? 'Seleção multivia' : 'Multiway selection'})</option>
              <option value="condition" ${node.type === 'condition' ? 'selected' : ''}>Condition Task (${t('inspector.node_view.label_name').includes('Rótulo') ? 'Decisão / Branch' : 'Decision / Branch'})</option>
              <option value="subflow" ${node.type === 'subflow' ? 'selected' : ''}>Subflow Task (${t('inspector.node_view.label_name').includes('Rótulo') ? 'Dinamico' : 'Dynamic'})</option>
              <option value="module" ${node.type === 'module' ? 'selected' : ''}>Module Task (${t('inspector.node_view.label_name').includes('Rótulo') ? 'Hierarquico' : 'Hierarchical'})</option>
              <option value="critical" ${node.type === 'critical' ? 'selected' : ''}>Critical Section (${t('inspector.node_view.label_name').includes('Rótulo') ? 'Mutex Concorrente' : 'Concurrent Mutex'})</option>
              <option value="cuda" ${node.type === 'cuda' ? 'selected' : ''}>CUDA Kernel (${t('inspector.node_view.label_name').includes('Rótulo') ? 'GPU Acelerado' : 'GPU Accelerated'})</option>
              <option value="fork" ${node.type === 'fork' ? 'selected' : ''}>Fork Task (${t('inspector.node_view.label_name').includes('Rótulo') ? 'Despachante Concorrente' : 'Concurrent Dispatcher'})</option>
              <option value="join" ${node.type === 'join' ? 'selected' : ''}>Join Task (${t('inspector.node_view.label_name').includes('Rótulo') ? 'Barreira de Sincronização' : 'Synchronization Barrier'})</option>
              <option value="agent" ${node.type === 'agent' ? 'selected' : ''}>Agent (Code Mode IA)</option>
              <option value="output" ${node.type === 'output' ? 'selected' : ''}>Output / Artifact (${t('inspector.node_view.label_name').includes('Rótulo') ? 'Entrega e Saída' : 'Delivery & Output'})</option>
              <option value="interaction" ${node.type === 'interaction' ? 'selected' : ''}>Interaction (${t('inspector.node_view.label_name').includes('Rótulo') ? 'Pergunta ao usuário' : 'Ask the user'})</option>
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

          ${node.type === 'switch' ? this.renderSwitchSection(node) : ''}
          ${node.type === 'mcp' ? this.renderMcpSection(node) : ''}
          ${node.type === 'output' ? this.renderOutputSection(node) : ''}
          ${node.type === 'agent' ? this.renderCallSection(node) : ''}
          ${node.type === 'interaction' ? this.renderInteractionSection(node) : ''}

          <!-- Seção de Parâmetros do Template / Agente -->
          ${(node.type === 'switch' || node.type === 'mcp' || node.type === 'output') ? '' : this.renderParametersSection(node, tpl)}

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
          ${node.type === 'switch' && (node.switchSettings?.mode ?? 'code') !== 'code' ? `<div class="office-field-hint" style="color: #a80000;">${t('inspector.switch.lambda_unused')}</div>` : ''}
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
        // De/para switch as portas de saída mudam: só é seguro sem conexões de saída.
        const crossesSwitch = (newType === 'switch') !== (oldType === 'switch');
        const hasOutEdges = this.graph.getEdgesForNode(node.id).some(e => e.sourceNodeId === node.id);
        if (crossesSwitch && hasOutEdges) {
          typeSelect.value = oldType;
          if (explanationP) explanationP.textContent = t('inspector.switch.convert_blocked');
          return;
        }
        const current = this.graph.getNode(node.id) ?? node;
        const oldProps: Partial<INodeData> = { type: oldType };
        const newProps: Partial<INodeData> = { type: newType };
        if ((newType === 'mcp') !== (oldType === 'mcp')) {
          oldProps.mcpSettings = current.mcpSettings ? JSON.parse(JSON.stringify(current.mcpSettings)) : undefined;
          newProps.mcpSettings = newType === 'mcp' ? defaultMcpSettings() : undefined;
        }
        if ((newType === 'output') !== (oldType === 'output')) {
          oldProps.outputSettings = current.outputSettings ? JSON.parse(JSON.stringify(current.outputSettings)) : undefined;
          newProps.outputSettings = newType === 'output' ? defaultOutputSettings() : undefined;
          if (newType === 'output') {
            newProps.customLambdaBody = `// Coletor de Entrega / Artefato: ${node.label}\nstd::cout << "[OUTPUT: ${node.label}] Entregável registrado com sucesso!\\n";`;
          }
        }
        if ((newType === 'interaction') !== (oldType === 'interaction')) {
          oldProps.interactionSettings = current.interactionSettings ? JSON.parse(JSON.stringify(current.interactionSettings)) : undefined;
          newProps.interactionSettings = newType === 'interaction' ? defaultInteractionSettings(node.label) : undefined;
          if (newType === 'interaction') {
            newProps.customLambdaBody = '';
            newProps.inputs = [{ id: `${node.id}_in_0`, name: 'in', direction: 'input', type: 'flow' }];
            newProps.outputs = [{ id: `${node.id}_out_0`, name: 'out', direction: 'output', type: 'flow' }];
          }
        }
        if (crossesSwitch) {
          oldProps.outputs = JSON.parse(JSON.stringify(current.outputs));
          newProps.outputs = newType === 'switch'
            ? Graph.switchOutputs(node.id, ['case 1', 'case 2'])
            : [{ id: `${node.id}_out_0`, name: 'out', direction: 'output', type: 'flow' }];
          oldProps.switchSettings = current.switchSettings ? JSON.parse(JSON.stringify(current.switchSettings)) : undefined;
          newProps.switchSettings = newType === 'switch' ? defaultSwitchSettings('model') : undefined;
          if (newType === 'switch' && !current.agentSettings) {
            oldProps.agentSettings = undefined;
            newProps.agentSettings = { inheritSandbox: true, inheritModel: true, inheritCodeMode: true, inheritMemory: true };
          }
        }
        const cmd = new UpdateNodeCommand(
          this.graph,
          node.id,
          oldProps,
          newProps,
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

    // Casos do switch
    if (node.type === 'switch') {
      this.bindSwitchCases(node);
      this.bindSwitchSettings(node);
    }
    if (node.type === 'mcp') this.bindMcpSettings(node);
    if (node.type === 'output') this.bindOutputSection(node);
    if (node.type === 'agent') this.bindCallSection();
    if (node.type === 'interaction') this.bindInteractionSection(node);

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

  // --- Vértice MCP: servidor, ferramenta e argumentos ---------------------------------------------

  private backend(): any | null {
    const w = typeof window !== 'undefined' ? (window as any) : null;
    return w && w.webui && typeof w.webui.call === 'function' ? w.webui : null;
  }

  private mcpSettingsOf(node: INodeData): IMcpVertexSettings {
    return node.mcpSettings ?? defaultMcpSettings();
  }

  private renderMcpSection(node: INodeData): string {
    const s = this.mcpSettingsOf(node);
    const opt = (value: string, label: string, sel: boolean) => `<option value="${this.escapeHtml(value)}" ${sel ? 'selected' : ''}>${this.escapeHtml(label)}</option>`;
    return `
      <div class="settings-section-card" style="margin-top: 12px; padding: 12px; border: 1px solid #edebe9; border-radius: 6px;">
        <div style="font-weight: 700; font-size: 13px; margin-bottom: 8px;">${t('inspector.mcp.title')}</div>
        <div class="office-field-hint" style="margin-bottom: 8px;">${t('inspector.mcp.hint')}</div>

        <div class="form-group">
          <label class="form-label" for="mcp-server">${t('inspector.mcp.server_label')}</label>
          <div style="display: flex; gap: 6px;">
            <select id="mcp-server" class="form-select" style="flex: 1;">${opt(s.server, s.server || t('inspector.mcp.choose'), true)}</select>
            <button type="button" class="btn btn-secondary btn-sm" id="btn-mcp-refresh" title="${t('inspector.mcp.refresh_tip')}">${t('common.refresh', 'Atualizar')}</button>
          </div>
          <div class="office-field-hint" id="mcp-status"></div>
        </div>

        <div class="form-group">
          <label class="form-label" for="mcp-tool">${t('inspector.mcp.tool_label')}</label>
          <select id="mcp-tool" class="form-select">${opt(s.tool, s.tool || t('inspector.mcp.choose'), true)}</select>
          <div class="office-field-hint" id="mcp-tool-hint"></div>
        </div>

        <div class="form-group">
          <label class="form-label" for="mcp-args">${t('inspector.mcp.args_label')}</label>
          <textarea id="mcp-args" class="form-textarea code-font" rows="4" spellcheck="false">${this.escapeHtml(JSON.stringify(s.arguments, null, 2))}</textarea>
          <div class="office-field-hint" id="mcp-args-error" style="color: #a80000;"></div>
          <div class="office-field-hint">${t('inspector.mcp.args_hint')}</div>
        </div>

        <div class="form-group">
          <label class="form-label" for="mcp-source-kind">${t('inspector.mcp.source_label')}</label>
          <select id="mcp-source-kind" class="form-select">
            ${opt('previous', t('inspector.mcp.source_previous'), s.source.kind === 'previous')}
            ${opt('file', t('inspector.mcp.source_file'), s.source.kind === 'file')}
            ${opt('none', t('inspector.mcp.source_none'), s.source.kind === 'none')}
          </select>
          ${s.source.kind === 'file' ? `<input type="text" id="mcp-source-path" class="form-input" style="margin-top: 6px; font-family: monospace;" value="${this.escapeHtml(s.source.path ?? '')}" placeholder="${t('inspector.mcp.path_placeholder')}" />` : ''}
        </div>

        <div class="form-group">
          <label class="form-label" for="mcp-output-file">${t('inspector.mcp.output_label')}</label>
          <input type="text" id="mcp-output-file" class="form-input" style="font-family: monospace;" value="${this.escapeHtml(s.output.file ?? '')}" placeholder="${t('inspector.mcp.output_placeholder')}" />
          <div class="office-field-hint">${t('inspector.mcp.output_hint', { id: this.escapeHtml(node.id) })}</div>
        </div>
      </div>`;
  }

  private updateMcpSettings(node: INodeData, mutate: (s: IMcpVertexSettings) => void, description: string, rerender = false): void {
    const current = this.graph.getNode(node.id);
    if (!current) return;
    const old = current.mcpSettings ? JSON.parse(JSON.stringify(current.mcpSettings)) as IMcpVertexSettings : undefined;
    const next: IMcpVertexSettings = old ? JSON.parse(JSON.stringify(old)) : defaultMcpSettings();
    mutate(next);
    this.graph.commandManager.execute(new UpdateNodeCommand(this.graph, node.id, { mcpSettings: old }, { mcpSettings: next }, description));
    if (rerender) {
      const fresh = this.graph.getNode(node.id);
      if (fresh) this.renderNode(fresh);
    }
  }

  private bindMcpSettings(node: INodeData): void {
    const q = <T extends Element>(sel: string) => this.container.querySelector(sel) as T | null;
    const serverSel = q<HTMLSelectElement>('#mcp-server');
    const toolSel = q<HTMLSelectElement>('#mcp-tool');
    if (!serverSel || typeof serverSel.addEventListener !== 'function') return;   // ambientes sem DOM completo (testes)

    const setStatus = (msg: string) => { const el = q<HTMLElement>('#mcp-status'); if (el) el.textContent = msg; };
    const fillOptions = (select: HTMLSelectElement, items: Array<{ value: string; label: string }>, current: string) => {
      select.innerHTML = '';
      const all = items.some(i => i.value === current) || !current ? items : [{ value: current, label: current }, ...items];
      for (const it of [{ value: '', label: t('inspector.mcp.choose') }, ...all]) {
        const o = document.createElement('option');
        o.value = it.value;
        o.textContent = it.label;
        if (it.value === current) o.selected = true;
        select.appendChild(o);
      }
    };

    const loadTools = async (server: string, keepTool: string) => {
      if (!server || !toolSel) return;
      const b = this.backend();
      if (!b) { setStatus(t('inspector.mcp.needs_native')); return; }
      setStatus(t('inspector.mcp.loading_tools'));
      try {
        const res = JSON.parse((await b.call('list_project_mcp_tools', JSON.stringify({ server }))) || '{}');
        const tools: Array<{ name: string; description: string; inputSchema?: any }> = res.tools || [];
        fillOptions(toolSel, tools.map(x => ({ value: x.name, label: x.name })), keepTool);
        setStatus(res.error ? translateMcpError(res.errorCode, res.error, res.errorDetail) : t('inspector.mcp.tools_found', { count: tools.length }));
        const hint = (name: string) => {
          const tool = tools.find(x => x.name === name);
          const el = q<HTMLElement>('#mcp-tool-hint');
          if (!el) return;
          const props = tool?.inputSchema?.properties ? Object.keys(tool.inputSchema.properties) : [];
          const required: string[] = tool?.inputSchema?.required ?? [];
          el.textContent = tool ? `${tool.description || ''} ${props.length ? '— ' + t('inspector.mcp.arguments') + ': ' + props.map(p => p + (required.includes(p) ? '*' : '')).join(', ') : ''}`.trim() : '';
        };
        hint(toolSel.value);
        (toolSel as any).__hint = hint;
        (toolSel as any).__tools = tools;
      } catch {
        setStatus(t('inspector.mcp.failed'));
      }
    };

    const loadServers = async () => {
      const b = this.backend();
      if (!b) { setStatus(t('inspector.mcp.needs_native')); return; }
      try {
        const res = JSON.parse((await b.call('list_project_mcp_servers')) || '{}');
        const servers: Array<{ id: string; name: string; isEnabled: boolean; isNative?: boolean }> = res.servers || [];
        if (res.error) {
          setStatus(translateMcpError(res.errorCode, res.error, res.errorDetail));
          return;
        }
        const current = this.mcpSettingsOf(this.graph.getNode(node.id) ?? node).server;
        fillOptions(serverSel, servers.map(sv => ({
          value: sv.id,
          label: sv.isNative
            ? `⚡ ${sv.name} — ${t('inspector.mcp.native_tag')}`
            : `${sv.name || sv.id} (${sv.id})${sv.isEnabled ? '' : ' — ' + t('inspector.mcp.disabled')}`
        })), current);
        if (current) await loadTools(current, this.mcpSettingsOf(this.graph.getNode(node.id) ?? node).tool);
      } catch {
        setStatus(t('inspector.mcp.failed'));
      }
    };

    serverSel.addEventListener('change', () => {
      const server = serverSel.value;
      this.updateMcpSettings(node, s => { s.server = server; s.tool = ''; }, 'Escolher o servidor MCP');
      if (toolSel) fillOptions(toolSel, [], '');
      void loadTools(server, '');
    });
    toolSel?.addEventListener('change', () => {
      const tool = toolSel.value;
      this.updateMcpSettings(node, s => {
        s.tool = tool;
        // esqueleto dos argumentos a partir do esquema da ferramenta, se ainda estiverem vazios
        const tools = (toolSel as any).__tools as Array<{ name: string; inputSchema?: any }> | undefined;
        const props = tools?.find(x => x.name === tool)?.inputSchema?.properties;
        if (props && Object.keys(s.arguments).length === 0) {
          for (const k of Object.keys(props)) s.arguments[k] = '';
        }
      }, 'Escolher a ferramenta MCP', true);
    });
    q<HTMLElement>('#btn-mcp-refresh')?.addEventListener('click', () => { void loadServers(); });

    q<HTMLTextAreaElement>('#mcp-args')?.addEventListener('change', (e) => {
      const el = e.currentTarget as HTMLTextAreaElement;
      const err = q<HTMLElement>('#mcp-args-error');
      try {
        const parsed = JSON.parse(el.value.trim() || '{}');
        if (parsed === null || typeof parsed !== 'object' || Array.isArray(parsed)) throw new Error(t('inspector.mcp.args_not_object'));
        if (err) err.textContent = '';
        this.updateMcpSettings(node, s => { s.arguments = parsed; }, 'Alterar os argumentos MCP');
      } catch (ex: any) {
        if (err) err.textContent = t('inspector.mcp.args_invalid', { message: String(ex?.message ?? ex) });
      }
    });
    q<HTMLSelectElement>('#mcp-source-kind')?.addEventListener('change', (e) => {
      const kind = (e.currentTarget as HTMLSelectElement).value as 'previous' | 'file' | 'none';
      this.updateMcpSettings(node, s => { s.source = { kind, path: s.source.path }; }, 'Alterar a origem da entrada MCP', true);
    });
    q<HTMLInputElement>('#mcp-source-path')?.addEventListener('change', (e) => {
      const path = (e.currentTarget as HTMLInputElement).value.trim();
      this.updateMcpSettings(node, s => { s.source = { kind: 'file', path }; }, 'Alterar o arquivo de entrada MCP');
    });
    q<HTMLInputElement>('#mcp-output-file')?.addEventListener('change', (e) => {
      const file = (e.currentTarget as HTMLInputElement).value.trim();
      this.updateMcpSettings(node, s => { s.output = file ? { file } : {}; }, 'Alterar o arquivo de saída MCP');
    });

    void loadServers();
  }

  // --- Switch: como escolher o caminho (sem código) -------------------------------------------

  private switchSettingsOf(node: INodeData): ISwitchSettings {
    // Vértices antigos (sem configuração) continuam usando o corpo C++.
    return node.switchSettings ?? defaultSwitchSettings('code');
  }

  private renderSwitchSection(node: INodeData): string {
    const s = this.switchSettingsOf(node);
    const cases = node.outputs.slice(0, -1);
    const modeRadio = (mode: SwitchMode, label: string, desc: string) => `
      <label style="display: flex; gap: 8px; align-items: flex-start; padding: 8px 10px; margin-bottom: 6px; border: 1px solid ${s.mode === mode ? '#0f6cbd' : '#edebe9'}; border-radius: 6px; background: ${s.mode === mode ? '#f0f6fc' : '#ffffff'}; cursor: pointer;">
        <input type="radio" name="switch-mode" class="switch-mode-radio" value="${mode}" ${s.mode === mode ? 'checked' : ''} style="margin-top: 3px;" />
        <span><strong style="font-size: 12.5px;">${label}</strong><br /><span style="font-size: 11.5px; color: #616161;">${desc}</span></span>
      </label>`;

    const sourceBlock = s.mode === 'code' ? '' : `
      <div class="form-group" style="margin-top: 10px;">
        <label class="form-label" for="switch-source-kind">${t('inspector.switch.source_label')}</label>
        <select id="switch-source-kind" class="form-select">
          <option value="previous" ${s.source.kind === 'previous' ? 'selected' : ''}>${t('inspector.switch.source_previous')}</option>
          <option value="file" ${s.source.kind === 'file' ? 'selected' : ''}>${t('inspector.switch.source_file')}</option>
        </select>
        ${s.source.kind === 'file' ? `<input type="text" id="switch-source-path" class="form-input" style="margin-top: 6px; font-family: monospace;" value="${this.escapeHtml(s.source.path ?? '')}" placeholder="${t('inspector.switch.source_path_placeholder')}" />` : `<div class="office-field-hint">${t('inspector.switch.source_previous_hint')}</div>`}
      </div>`;

    let modeBlock = '';
    if (s.mode === 'model') {
      modeBlock = `
        <div class="form-group" style="margin-top: 10px;">
          <label class="form-label" for="switch-instruction">${t('inspector.switch.instruction_label')}</label>
          <textarea id="switch-instruction" class="form-textarea" rows="3" placeholder="${t('inspector.switch.instruction_placeholder')}">${this.escapeHtml(s.instruction ?? '')}</textarea>
          <div class="office-field-hint">${t('inspector.switch.model_hint')}</div>
          <button type="button" class="btn btn-secondary btn-sm" id="btn-switch-model" style="margin-top: 6px;">${t('inspector.switch.btn_model')}</button>
        </div>`;
    } else if (s.mode === 'rules') {
      modeBlock = `
        <div class="form-group" style="margin-top: 10px;">
          <label class="form-label">${t('inspector.switch.rules_label')}</label>
          ${cases.map(p => `
            <div style="display: flex; gap: 6px; align-items: center; margin-bottom: 6px;">
              <span style="width: 110px; font-size: 12px; font-weight: 600; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;" title="${this.escapeHtml(p.name)}">${this.escapeHtml(p.name)}</span>
              <input type="text" class="form-input switch-rule" data-port-id="${this.escapeHtml(p.id)}" value="${this.escapeHtml(s.rules[p.id] ?? '')}" placeholder="${t('inspector.switch.rules_placeholder')}" style="flex: 1; height: 30px; font-size: 12px;" />
            </div>`).join('')}
          <div class="office-field-hint">${t('inspector.switch.rules_hint')}</div>
        </div>`;
    } else {
      modeBlock = `<div class="office-field-hint" style="margin-top: 10px;">${t('inspector.switch.code_hint')}</div>`;
    }

    return `
      <div class="settings-section-card" style="margin-top: 12px; padding: 12px; border: 1px solid #edebe9; border-radius: 6px;">
        ${this.renderSwitchCases(node)}
        <div style="font-weight: 700; font-size: 13px; margin: 4px 0 8px;">${t('inspector.switch.mode_title')}</div>
        ${modeRadio('model', t('inspector.switch.mode_model'), t('inspector.switch.mode_model_desc'))}
        ${modeRadio('rules', t('inspector.switch.mode_rules'), t('inspector.switch.mode_rules_desc'))}
        ${modeRadio('code', t('inspector.switch.mode_code'), t('inspector.switch.mode_code_desc'))}
        ${sourceBlock}
        ${modeBlock}
      </div>`;
  }

  /** Aplica uma alteração nas configurações do switch como um comando desfazível e redesenha o painel. */
  private updateSwitchSettings(node: INodeData, mutate: (s: ISwitchSettings) => void, description: string, rerender = true): void {
    const current = this.graph.getNode(node.id);
    if (!current) return;
    const old = current.switchSettings ? JSON.parse(JSON.stringify(current.switchSettings)) as ISwitchSettings : undefined;
    const next: ISwitchSettings = old ? JSON.parse(JSON.stringify(old)) : defaultSwitchSettings('code');
    mutate(next);
    this.graph.commandManager.execute(new UpdateNodeCommand(this.graph, node.id, { switchSettings: old }, { switchSettings: next }, description));
    if (rerender) {
      const fresh = this.graph.getNode(node.id);
      if (fresh) this.renderNode(fresh);
    }
  }

  private bindSwitchSettings(node: INodeData): void {
    this.container.querySelectorAll<HTMLInputElement>('.switch-mode-radio').forEach(radio => {
      radio.addEventListener('change', () => {
        const mode = radio.value as SwitchMode;
        this.updateSwitchSettings(node, s => { s.mode = mode; }, 'Alterar o modo de escolha do switch');
      });
    });
    this.container.querySelector('#switch-source-kind')?.addEventListener('change', (e) => {
      const kind = (e.currentTarget as HTMLSelectElement).value as 'previous' | 'file';
      this.updateSwitchSettings(node, s => { s.source = { kind, path: s.source.path }; }, 'Alterar a origem do texto do switch');
    });
    this.container.querySelector('#switch-source-path')?.addEventListener('change', (e) => {
      const path = (e.currentTarget as HTMLInputElement).value.trim();
      this.updateSwitchSettings(node, s => { s.source = { kind: 'file', path }; }, 'Alterar o arquivo do switch', false);
    });
    this.container.querySelector('#switch-instruction')?.addEventListener('change', (e) => {
      const text = (e.currentTarget as HTMLTextAreaElement).value;
      this.updateSwitchSettings(node, s => { s.instruction = text; }, 'Alterar a orientação do switch', false);
    });
    this.container.querySelectorAll<HTMLInputElement>('.switch-rule').forEach(input => {
      input.addEventListener('change', () => {
        const portId = input.dataset.portId!;
        const words = input.value;
        this.updateSwitchSettings(node, s => { s.rules[portId] = words; }, 'Alterar as palavras de um caso', false);
      });
    });
    this.container.querySelector('#btn-switch-model')?.addEventListener('click', () => {
      this.bus.emit('node:open-model-modal', node.id);
    });
  }

  // --- Switch: casos (portas de saída) -------------------------------------------------------

  private renderSwitchCases(node: INodeData): string {
    const cases = node.outputs.slice(0, -1);
    const def = node.outputs[node.outputs.length - 1];
    return `
      <div class="port-sublist" id="switch-cases" style="margin-bottom: 14px;">
        <span class="port-sublist-title">${t('inspector.switch.title')}</span>
        <div class="office-field-hint" style="margin: 4px 0 8px;">${t('inspector.switch.hint')}</div>
        ${cases.map((p, i) => `
          <div style="display: flex; gap: 6px; align-items: center; margin-bottom: 6px;">
            <span style="width: 22px; font-family: monospace; color: #616161;">${i}</span>
            <input type="text" class="form-input switch-case-name" data-port-id="${this.escapeHtml(p.id)}" value="${this.escapeHtml(p.name)}" style="flex: 1; height: 30px; font-size: 12px;" />
            <button type="button" class="btn btn-secondary btn-sm switch-case-remove" data-port-id="${this.escapeHtml(p.id)}" ${cases.length <= 1 ? 'disabled' : ''} title="${t('inspector.switch.remove')}">✕</button>
          </div>`).join('')}
        <div style="display: flex; gap: 6px; align-items: center; margin-bottom: 8px;">
          <span style="width: 22px; font-family: monospace; color: #616161;">${cases.length}</span>
          <input type="text" class="form-input form-input-readonly" value="${this.escapeHtml(def?.name ?? 'default')}" readonly style="flex: 1; height: 30px; font-size: 12px;" />
          <span style="font-size: 11px; color: #616161;">${t('inspector.switch.default_tag')}</span>
        </div>
        <button type="button" class="btn btn-secondary btn-sm" id="btn-switch-add-case">${t('inspector.switch.add')}</button>
      </div>`;
  }

  /** Aplica uma nova lista de portas de saída (desfazível); remove as arestas das portas excluídas. */
  private applySwitchOutputs(node: INodeData, newCases: IPort[], description: string): void {
    const current = this.graph.getNode(node.id);
    if (!current) return;
    const oldOutputs: IPort[] = JSON.parse(JSON.stringify(current.outputs));
    const def = oldOutputs[oldOutputs.length - 1];
    const keptIds = new Set([...newCases.map(p => p.id), def.id]);
    const newOutputs: IPort[] = [...newCases, def].map((p, i) => ({ ...p, conditionBranchIndex: i }));

    const removeEdges = this.graph.getAllEdges()
      .filter(e => e.sourceNodeId === node.id && !keptIds.has(e.sourcePortId))
      .map(e => new RemoveEdgeCommand(this.graph, e.id));
    const oldSettings = current.switchSettings ? JSON.parse(JSON.stringify(current.switchSettings)) as ISwitchSettings : undefined;
    let newSettings: ISwitchSettings | undefined = undefined;
    if (oldSettings) {
      newSettings = JSON.parse(JSON.stringify(oldSettings)) as ISwitchSettings;
      Object.keys(newSettings.rules).forEach(k => { if (!keptIds.has(k)) delete newSettings!.rules[k]; });
    }
    const update = new UpdateNodeCommand(this.graph, node.id,
      { outputs: oldOutputs, switchSettings: oldSettings }, { outputs: newOutputs, switchSettings: newSettings }, description);
    this.graph.commandManager.execute(new CompoundCommand(description, [...removeEdges, update]));

    const fresh = this.graph.getNode(node.id);
    if (fresh) this.renderNode(fresh);
  }

  private bindSwitchCases(node: INodeData): void {
    this.container.querySelectorAll<HTMLInputElement>('.switch-case-name').forEach(input => {
      input.addEventListener('change', () => {
        const current = this.graph.getNode(node.id);
        if (!current) return;
        const cases: IPort[] = JSON.parse(JSON.stringify(current.outputs.slice(0, -1)));
        const target = cases.find(p => p.id === input.dataset.portId);
        const name = input.value.trim();
        if (!target || !name || name === target.name) { input.value = target?.name ?? ''; return; }
        target.name = name;
        this.applySwitchOutputs(node, cases, `Renomear caso para "${name}"`);
      });
    });

    this.container.querySelectorAll<HTMLButtonElement>('.switch-case-remove').forEach(btn => {
      btn.addEventListener('click', () => {
        const current = this.graph.getNode(node.id);
        if (!current) return;
        const cases: IPort[] = JSON.parse(JSON.stringify(current.outputs.slice(0, -1)));
        if (cases.length <= 1) return;
        this.applySwitchOutputs(node, cases.filter(p => p.id !== btn.dataset.portId), 'Remover caso do switch');
      });
    });

    this.container.querySelector('#btn-switch-add-case')?.addEventListener('click', () => {
      const current = this.graph.getNode(node.id);
      if (!current) return;
      const cases: IPort[] = JSON.parse(JSON.stringify(current.outputs.slice(0, -1)));
      cases.push({
        id: `${node.id}_case_${Date.now().toString(36)}`,
        name: `case ${cases.length + 1}`,
        direction: 'output',
        type: 'flow'
      });
      this.applySwitchOutputs(node, cases, 'Adicionar caso ao switch');
    });
  }

  // --- Output / Entregáveis do Pipeline ---------------------------------------

  private outputSettingsOf(node: INodeData): IOutputSettings {
    return node.outputSettings ?? defaultOutputSettings();
  }

  private renderOutputSection(node: INodeData): string {
    const s = this.outputSettingsOf(node);
    const opt = (val: string, label: string) => `<option value="${val}" ${s.artifactType === val ? 'selected' : ''}>${label}</option>`;

    return `
      <div class="settings-section-card" style="margin-top: 12px; padding: 12px; border: 1px solid #10b981; border-radius: 6px; background: #f0fdf4;">
        <div style="font-weight: 700; font-size: 13px; margin-bottom: 6px; color: #065f46; display: flex; align-items: center; gap: 6px;">
          <span>📥</span> <span>${t('inspector.output.title', 'Entrega e Coletor de Artefato')}</span>
        </div>
        <div class="office-field-hint" style="margin-bottom: 10px; color: #047857;">
          ${t('inspector.output.hint', 'Define onde o entregável produzido pelo pipeline se encontra e como será apresentado na aba Artefatos.')}
        </div>

        <div class="form-group">
          <label class="form-label" for="output-source-path">${t('inspector.output.source_path_label', 'Caminho de Origem (Workspace)')}</label>
          <input type="text" id="output-source-path" class="form-input code-font" value="${this.escapeHtml(s.sourcePath)}" placeholder="ex: workspace/scene.svg ou workspace/dados.csv" />
          <div class="office-field-hint">${t('inspector.output.source_path_hint', 'Arquivo relativo gerado pelos agentes ou tarefas anteriores')}</div>
        </div>

        <div class="form-group">
          <label class="form-label" for="output-dest-path">${t('inspector.output.dest_path_label', 'Caminho de Destino / Exportação (Opcional)')}</label>
          <input type="text" id="output-dest-path" class="form-input code-font" value="${this.escapeHtml(s.destinationPath ?? '')}" placeholder="ex: exports/scene.svg" />
          <div class="office-field-hint">${t('inspector.output.dest_path_hint', 'Se informado, o artefato final será copiado para este caminho no término')}</div>
        </div>

        <div class="form-group">
          <label class="form-label" for="output-artifact-type">${t('inspector.output.type_label', 'Tipo / Formato de Visualização')}</label>
          <select id="output-artifact-type" class="form-select">
            ${opt('auto', t('inspector.output.type_auto', 'Automático (Detectar por extensão)'))}
            ${opt('svg', t('inspector.output.type_svg', 'Vetor SVG (Visualizador com Pan/Zoom)'))}
            ${opt('sheet', t('inspector.output.type_sheet', 'Planilha / Grid Tabular (CSV, TSV, XLSX)'))}
            ${opt('pdf', t('inspector.output.type_pdf', 'Documento PDF'))}
            ${opt('markdown', t('inspector.output.type_markdown', 'Texto Formatado / Markdown (.md)'))}
            ${opt('text', t('inspector.output.type_text', 'Texto Puro (.txt, .log)'))}
            ${opt('json', t('inspector.output.type_json', 'Estrutura JSON (.json)'))}
            ${opt('image', t('inspector.output.type_image', 'Imagem Raster (.png, .jpg, .webp)'))}
          </select>
        </div>

        <div class="form-group">
          <label class="form-label" for="output-display-name">${t('inspector.output.name_label', 'Rótulo do Entregável')}</label>
          <input type="text" id="output-display-name" class="form-input" value="${this.escapeHtml(s.displayName ?? '')}" placeholder="ex: SVG Final da Cena" />
        </div>

        <div class="form-group">
          <label class="form-label" for="output-description">${t('inspector.output.desc_label', 'Descrição')}</label>
          <textarea id="output-description" class="form-textarea" rows="2" placeholder="Breve resumo da finalidade deste entregável">${this.escapeHtml(s.description ?? '')}</textarea>
        </div>

        <div class="form-group" style="margin-top: 6px;">
          <label style="display: flex; gap: 8px; align-items: center; font-size: 12px; cursor: pointer; color: #1e293b;">
            <input type="checkbox" id="output-auto-open" ${s.autoOpenPreview !== false ? 'checked' : ''} />
            <span>${t('inspector.output.auto_open_label', 'Abrir automaticamente na aba Artefatos ao concluir')}</span>
          </label>
        </div>

        <div style="margin-top: 10px; display: flex; gap: 8px;">
          <button type="button" class="btn btn-secondary btn-sm" id="btn-view-artifact-now" style="flex: 1; border-color: #10b981; color: #047857;">
            ${t('inspector.output.view_now', 'Ver no Painel de Artefatos')}
          </button>
        </div>
      </div>
    `;
  }

  private updateOutputSettings(node: INodeData, mutator: (s: IOutputSettings) => void, description: string): void {
    const current = this.graph.getNode(node.id) ?? node;
    const old = current.outputSettings ? JSON.parse(JSON.stringify(current.outputSettings)) as IOutputSettings : defaultOutputSettings();
    const next: IOutputSettings = JSON.parse(JSON.stringify(old));
    mutator(next);
    this.graph.commandManager.execute(new UpdateNodeCommand(this.graph, node.id, { outputSettings: old }, { outputSettings: next }, description));
  }

  private bindOutputSection(node: INodeData): void {
    const srcInput = this.container.querySelector('#output-source-path') as HTMLInputElement;
    srcInput?.addEventListener('change', () => {
      this.updateOutputSettings(node, s => { s.sourcePath = srcInput.value.trim(); }, 'Alterar caminho de origem do artefato');
    });

    const dstInput = this.container.querySelector('#output-dest-path') as HTMLInputElement;
    dstInput?.addEventListener('change', () => {
      this.updateOutputSettings(node, s => { s.destinationPath = dstInput.value.trim(); }, 'Alterar caminho de destino do artefato');
    });

    const typeSelect = this.container.querySelector('#output-artifact-type') as HTMLSelectElement;
    typeSelect?.addEventListener('change', () => {
      this.updateOutputSettings(node, s => { s.artifactType = typeSelect.value as OutputArtifactType; }, 'Alterar formato do artefato');
    });

    const nameInput = this.container.querySelector('#output-display-name') as HTMLInputElement;
    nameInput?.addEventListener('change', () => {
      this.updateOutputSettings(node, s => { s.displayName = nameInput.value.trim(); }, 'Alterar rótulo do entregável');
    });

    const descInput = this.container.querySelector('#output-description') as HTMLTextAreaElement;
    descInput?.addEventListener('change', () => {
      this.updateOutputSettings(node, s => { s.description = descInput.value.trim(); }, 'Alterar descrição do entregável');
    });

    const autoOpenChk = this.container.querySelector('#output-auto-open') as HTMLInputElement;
    autoOpenChk?.addEventListener('change', () => {
      this.updateOutputSettings(node, s => { s.autoOpenPreview = autoOpenChk.checked; }, 'Alterar abertura automática do artefato');
    });

    this.container.querySelector('#btn-view-artifact-now')?.addEventListener('click', () => {
      this.bus.emit('console:open-artifacts', { nodeId: node.id });
    });
  }

  // --- Delegação (arestas de chamada) ---------------------------------------------------
  private renderCallSection(node: INodeData): string {
    const calls = this.graph.getAllEdges().filter(e => e.sourceNodeId === node.id && e.kind === 'call');
    if (calls.length === 0) {
      return `
        <div class="form-group">
          <label class="form-label">${t('inspector.calls.title', 'Delegação (chamadas)')}</label>
          <div class="office-field-hint">${t('inspector.calls.empty', 'Arraste da porta CALL para um agente para declarar uma chamada.')}</div>
        </div>
      `;
    }

    const rows = calls.map(edge => {
      const target = this.graph.getNode(edge.targetNodeId);
      const settings = edge.callSettings ?? defaultCallSettings();
      const targetLabel = target?.label ?? edge.targetNodeId;
      return `
        <div class="form-group" style="border-left: 3px solid #7c3aed; padding-left: 8px;">
          <label class="form-label">→ ${this.escapeHtml(targetLabel)}</label>
          <div style="display: flex; gap: 8px; align-items: center;">
            <input type="number" class="form-input" data-call-max="${edge.id}" min="1"
                   value="${settings.maxCalls}" style="width: 90px;" />
            <input type="text" class="form-input" data-call-desc="${edge.id}"
                   value="${this.escapeHtml(settings.description || '')}"
                   placeholder="${t('inspector.calls.desc_placeholder', 'Quando o agente deve chamar este alvo')}" />
          </div>
          <div class="office-field-hint">${t('inspector.calls.max_hint', 'Máximo de chamadas por execução deste agente')}</div>
        </div>
      `;
    }).join('');

    return `
      <div class="form-group">
        <label class="form-label">${t('inspector.calls.title', 'Delegação (chamadas)')}</label>
        ${rows}
      </div>
    `;
  }

  private bindCallSection(): void {
    this.container.querySelectorAll<HTMLInputElement>('[data-call-max]').forEach(input => {
      input.addEventListener('change', () => {
        const edgeId = input.getAttribute('data-call-max');
        const edge = edgeId ? this.graph.getEdge(edgeId) : undefined;
        if (!edge) return;
        const settings = { ...(edge.callSettings ?? defaultCallSettings()) };
        settings.maxCalls = Math.max(1, parseInt(input.value, 10) || 1);
        this.graph.updateEdge(edge.id, { callSettings: settings });
      });
    });

    this.container.querySelectorAll<HTMLInputElement>('[data-call-desc]').forEach(input => {
      input.addEventListener('change', () => {
        const edgeId = input.getAttribute('data-call-desc');
        const edge = edgeId ? this.graph.getEdge(edgeId) : undefined;
        if (!edge) return;
        const settings = { ...(edge.callSettings ?? defaultCallSettings()) };
        settings.description = input.value.trim();
        this.graph.updateEdge(edge.id, { callSettings: settings });
      });
    });
  }

  // --- Interação humana (autoria e formulário gerado) ------------------------------------
  private renderInteractionSection(node: INodeData): string {
    const settings = node.interactionSettings ?? defaultInteractionSettings(node.label);
    const fields = settings.form?.fields ?? [];
    const fieldRows = fields.map(field => {
      const id = this.escapeHtml(field.id);
      return `
        <div class="form-group" style="border-left: 3px solid #db2777; padding-left: 8px;">
          <div style="display: flex; gap: 6px; align-items: center;">
            <input class="form-input" data-field-label="${id}" value="${this.escapeHtml(field.label)}" placeholder="Pergunta/campo" />
            <select class="form-select" data-field-type="${id}" style="width: 120px;">
              ${this.formFieldTypeOptions(field.type)}
            </select>
            <button class="btn btn-small" data-field-remove="${id}" title="Remover campo">✕</button>
          </div>
          <div style="display: flex; gap: 6px; align-items: center; margin-top: 4px;">
            <label style="font-size: 11px; white-space: nowrap;"><input type="checkbox" data-field-required="${id}" ${field.required ? 'checked' : ''}/> ${t('inspector.interaction.required', 'Obrigatório')}</label>
            <input class="form-input" data-field-options="${id}" value="${this.escapeHtml(this.formFieldOptionsText(field))}" placeholder="${t('inspector.interaction.options_placeholder', 'Opções separadas por vírgula (choice)')}" />
          </div>
        </div>
      `;
    }).join('');

    const channelOptions = ['any', 'studio', 'runner', 'cli']
      .map(value => `<option value="${value}" ${settings.channel === value ? 'selected' : ''}>${value}</option>`).join('');
    const timeoutOptions = ['fail', 'default', 'abort']
      .map(value => `<option value="${value}" ${settings.onTimeout === value ? 'selected' : ''}>${value}</option>`).join('');
    const whoOptions = ['any', 'operator', 'end_user']
      .map(value => `<option value="${value}" ${settings.who === value ? 'selected' : ''}>${value}</option>`).join('');
    const schemaSource = settings.schemaSource ?? 'static';
    const globalModel = this.graph.getGlobalDefaults().model;
    const generator = settings.generator ?? {
      provider: globalModel.provider,
      model: globalModel.model,
      temperature: globalModel.temperature,
      fallback: 'fail' as const
    };
    const artifactsText = (settings.form?.context?.artifacts ?? [])
      .map(a => [a.path, a.type, a.label].filter(Boolean).join(' | '))
      .join('\n');
    const fieldsBlock = schemaSource === 'static' ? `
        <div style="margin-top: 8px;"><strong style="font-size: 12px;">${t('inspector.interaction.fields', 'Campos')}</strong></div>
        ${fieldRows || `<div class="office-field-hint">${t('inspector.interaction.no_fields', 'Nenhum campo; adicione um.')}</div>`}
        <button class="btn btn-small" id="btn-add-interaction-field" style="margin-top: 6px;">+ ${t('inspector.interaction.add_field', 'Campo')}</button>
      ` : `
        <div class="office-field-hint" style="background:#fdf2f8; border:1px solid #fbcfe8; padding:8px; margin-top:8px; color:#9d174d;">
          ${t('inspector.interaction.generated_notice', 'Os campos serão gerados pelo modelo em tempo de execução a partir do contexto e dos artefatos; o runtime valida o schema antes de exibir.')}
        </div>
      `;

    return `
      <div class="form-group">
        <label class="form-label">${t('inspector.interaction.title', 'Pergunta / Interação')}</label>
        <div class="office-field-hint" style="color: #5c5c5c;">${t('inspector.interaction.pending', 'Execução: modal seguro no Studio ou respostas headless com --answers.')}</div>

        <input class="form-input" id="interaction-title" value="${this.escapeHtml(settings.form?.title ?? node.label)}" placeholder="${t('inspector.interaction.form_title', 'Título do formulário')}" />
        <textarea class="form-textarea" id="interaction-description" rows="2" placeholder="${t('inspector.interaction.form_description', 'Descrição/ajuda')}">${this.escapeHtml(settings.form?.description ?? '')}</textarea>
        <textarea class="form-textarea" id="interaction-context" rows="3" placeholder="${t('inspector.interaction.context', 'Contexto em Markdown exibido junto às perguntas')}">${this.escapeHtml(settings.form?.context?.markdown ?? '')}</textarea>
        <textarea class="form-textarea" id="interaction-artifacts" rows="2" placeholder="${t('inspector.interaction.artifacts_placeholder', 'Artefatos: caminho | tipo | rótulo (um por linha)')}">${this.escapeHtml(artifactsText)}</textarea>

        <div style="display: flex; gap: 6px; margin-top: 8px; align-items: center;">
          <label style="font-size: 11px; white-space: nowrap;">${t('inspector.interaction.schema_source', 'Origem do formulário')}</label>
          <select class="form-select" id="interaction-schema-source">
            <option value="static" ${schemaSource === 'static' ? 'selected' : ''}>${t('inspector.interaction.schema_static', 'Estático (campos abaixo)')}</option>
            <option value="vertex" ${schemaSource === 'vertex' ? 'selected' : ''}>${t('inspector.interaction.schema_vertex', 'Gerado pelo vértice (modelo)')}</option>
          </select>
        </div>

        ${schemaSource === 'vertex' ? `
          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 6px; margin-top: 8px;">
            <div class="form-group" style="margin:0;">
              <label class="form-label">${t('inspector.interaction.generator_provider', 'Provedor do gerador')}</label>
              <input class="form-input" id="interaction-gen-provider" value="${this.escapeHtml(generator.provider ?? '')}" placeholder="deepseek" />
            </div>
            <div class="form-group" style="margin:0;">
              <label class="form-label">${t('inspector.interaction.generator_model', 'Modelo do gerador')}</label>
              <input class="form-input" id="interaction-gen-model" value="${this.escapeHtml(generator.model ?? '')}" placeholder="${this.escapeHtml(globalModel.model)}" />
            </div>
            <div class="form-group" style="margin:0;">
              <label class="form-label">${t('inspector.interaction.generator_temperature', 'Temperatura')}</label>
              <input type="number" min="0" max="2" step="0.1" class="form-input" id="interaction-gen-temperature" value="${generator.temperature ?? ''}" />
            </div>
            <div class="form-group" style="margin:0;">
              <label class="form-label">${t('inspector.interaction.generator_fallback', 'Se a geração falhar')}</label>
              <select class="form-select" id="interaction-gen-fallback">
                <option value="fail" ${(generator.fallback ?? 'fail') === 'fail' ? 'selected' : ''}>fail</option>
                <option value="free_text" ${generator.fallback === 'free_text' ? 'selected' : ''}>free_text</option>
              </select>
            </div>
          </div>
        ` : ''}

        ${fieldsBlock}

        <div style="display: flex; gap: 6px; margin-top: 8px; align-items: center;">
          <label style="font-size: 11px; white-space: nowrap;">${t('inspector.interaction.channel', 'Canal')}</label>
          <select class="form-select" id="interaction-channel">${channelOptions}</select>
          <label style="font-size: 11px; white-space: nowrap;">${t('inspector.interaction.timeout', 'Timeout (s)')}</label>
          <input type="number" class="form-input" id="interaction-timeout" min="0" value="${settings.timeoutSeconds}" style="width: 90px;" />
        </div>
        <div style="display: flex; gap: 6px; margin-top: 6px; align-items: center;">
          <label style="font-size: 11px; white-space: nowrap;">${t('inspector.interaction.on_timeout', 'No timeout')}</label>
          <select class="form-select" id="interaction-on-timeout">${timeoutOptions}</select>
          <label style="font-size: 11px; white-space: nowrap;">${t('inspector.interaction.who', 'Quem responde')}</label>
          <select class="form-select" id="interaction-who">${whoOptions}</select>
        </div>
        <label style="font-size: 11px; margin-top: 6px; display: block;"><input type="checkbox" id="interaction-sensitive" ${settings.sensitive ? 'checked' : ''}/> ${t('inspector.interaction.sensitive', 'Resposta sensível (fora da telemetria)')}</label>
      </div>
    `;
  }

  private formFieldTypeOptions(selected: FormFieldType): string {
    const types: FormFieldType[] = ['text', 'textarea', 'number', 'boolean', 'choice', 'multi_choice', 'date'];
    return types.map(type => `<option value="${type}" ${type === selected ? 'selected' : ''}>${type}</option>`).join('');
  }

  private formFieldOptionsText(field: IFormField): string {
    return (field.options ?? []).map(option => option.value).join(', ');
  }

  private updateInteractionSettings(node: INodeData, mutate: (settings: IInteractionSettings) => void, description: string): void {
    const current = node.interactionSettings ?? defaultInteractionSettings(node.label);
    const oldSettings = JSON.parse(JSON.stringify(current)) as IInteractionSettings;
    const nextSettings = JSON.parse(JSON.stringify(current)) as IInteractionSettings;
    mutate(nextSettings);
    const cmd = new UpdateNodeCommand(
      this.graph,
      node.id,
      { interactionSettings: oldSettings },
      { interactionSettings: nextSettings },
      description
    );
    this.graph.commandManager.execute(cmd);
  }

  private bindInteractionSection(node: INodeData): void {
    const title = this.container.querySelector<HTMLInputElement>('#interaction-title');
    title?.addEventListener('change', () => {
      this.updateInteractionSettings(node, s => { s.form.title = title.value.trim(); }, 'Alterar título da interação');
    });
    const description = this.container.querySelector<HTMLTextAreaElement>('#interaction-description');
    description?.addEventListener('change', () => {
      this.updateInteractionSettings(node, s => { s.form.description = description.value.trim(); }, 'Alterar descrição da interação');
    });
    const context = this.container.querySelector<HTMLTextAreaElement>('#interaction-context');
    context?.addEventListener('change', () => {
      this.updateInteractionSettings(node, s => {
        s.form.context = { ...(s.form.context ?? {}), markdown: context.value };
      }, 'Alterar contexto da interação');
    });
    const artifacts = this.container.querySelector<HTMLTextAreaElement>('#interaction-artifacts');
    artifacts?.addEventListener('change', () => {
      const parsed = artifacts.value.split(/\r?\n/).map(line => {
        const parts = line.split('|').map(part => part.trim());
        if (!parts[0]) return null;
        return { path: parts[0], type: parts[1] || 'text', label: parts[2] || parts[0] };
      }).filter((a): a is { path: string; type: string; label: string } => !!a);
      this.updateInteractionSettings(node, s => {
        s.form.context = { ...(s.form.context ?? {}), artifacts: parsed };
      }, 'Alterar artefatos do contexto');
    });
    const schemaSource = this.container.querySelector<HTMLSelectElement>('#interaction-schema-source');
    schemaSource?.addEventListener('change', () => {
      this.updateInteractionSettings(node, s => {
        s.schemaSource = schemaSource.value as IInteractionSettings['schemaSource'];
        if (s.schemaSource === 'vertex') {
          const globalModel = this.graph.getGlobalDefaults().model;
          s.generator = {
            provider: s.generator?.provider || globalModel.provider,
            model: s.generator?.model || globalModel.model,
            temperature: s.generator?.temperature ?? globalModel.temperature,
            fallback: s.generator?.fallback ?? 'fail'
          };
        }
      }, 'Alterar origem do formulário');
    });
    const genProvider = this.container.querySelector<HTMLInputElement>('#interaction-gen-provider');
    genProvider?.addEventListener('change', () => {
      this.updateInteractionSettings(node, s => {
        s.generator = { ...(s.generator ?? { provider: 'deepseek', model: '' }), provider: genProvider.value.trim() || 'deepseek' };
      }, 'Alterar provedor do gerador');
    });
    const genModel = this.container.querySelector<HTMLInputElement>('#interaction-gen-model');
    genModel?.addEventListener('change', () => {
      this.updateInteractionSettings(node, s => {
        s.generator = { ...(s.generator ?? { provider: 'deepseek', model: '' }), model: genModel.value.trim() };
      }, 'Alterar modelo do gerador');
    });
    const genTemperature = this.container.querySelector<HTMLInputElement>('#interaction-gen-temperature');
    genTemperature?.addEventListener('change', () => {
      this.updateInteractionSettings(node, s => {
        const value = parseFloat(genTemperature.value);
        s.generator = { ...(s.generator ?? { provider: 'deepseek', model: '' }) };
        if (Number.isFinite(value)) s.generator.temperature = value;
        else delete s.generator.temperature;
      }, 'Alterar temperatura do gerador');
    });
    const genFallback = this.container.querySelector<HTMLSelectElement>('#interaction-gen-fallback');
    genFallback?.addEventListener('change', () => {
      this.updateInteractionSettings(node, s => {
        s.generator = {
          ...(s.generator ?? { provider: 'deepseek', model: '' }),
          fallback: genFallback.value as 'fail' | 'free_text'
        };
      }, 'Alterar fallback do gerador');
    });

    const channel = this.container.querySelector<HTMLSelectElement>('#interaction-channel');
    channel?.addEventListener('change', () => {
      this.updateInteractionSettings(node, s => { s.channel = channel.value as IInteractionSettings['channel']; }, 'Alterar canal da interação');
    });
    const timeout = this.container.querySelector<HTMLInputElement>('#interaction-timeout');
    timeout?.addEventListener('change', () => {
      this.updateInteractionSettings(node, s => { s.timeoutSeconds = Math.max(0, parseInt(timeout.value, 10) || 0); }, 'Alterar timeout da interação');
    });
    const onTimeout = this.container.querySelector<HTMLSelectElement>('#interaction-on-timeout');
    onTimeout?.addEventListener('change', () => {
      this.updateInteractionSettings(node, s => { s.onTimeout = onTimeout.value as IInteractionSettings['onTimeout']; }, 'Alterar política de timeout');
    });
    const who = this.container.querySelector<HTMLSelectElement>('#interaction-who');
    who?.addEventListener('change', () => {
      this.updateInteractionSettings(node, s => { s.who = who.value as IInteractionSettings['who']; }, 'Alterar quem responde');
    });
    const sensitive = this.container.querySelector<HTMLInputElement>('#interaction-sensitive');
    sensitive?.addEventListener('change', () => {
      this.updateInteractionSettings(node, s => { s.sensitive = sensitive.checked; }, 'Alterar sensibilidade da resposta');
    });

    this.container.querySelectorAll<HTMLInputElement>('[data-field-label]').forEach(input => {
      input.addEventListener('change', () => {
        const id = input.getAttribute('data-field-label');
        if (!id) return;
        this.updateInteractionSettings(node, s => {
          const field = s.form.fields.find(f => f.id === id);
          if (field) field.label = input.value.trim();
        }, 'Alterar rótulo do campo');
      });
    });

    this.container.querySelectorAll<HTMLSelectElement>('[data-field-type]').forEach(select => {
      select.addEventListener('change', () => {
        const id = select.getAttribute('data-field-type');
        if (!id) return;
        this.updateInteractionSettings(node, s => {
          const field = s.form.fields.find(f => f.id === id);
          if (!field) return;
          field.type = select.value as FormFieldType;
          if ((field.type === 'choice' || field.type === 'multi_choice') && (!field.options || field.options.length < 2)) {
            field.options = [{ value: 'opcao_1', label: 'Opção 1' }, { value: 'opcao_2', label: 'Opção 2' }];
          }
        }, 'Alterar tipo do campo');
      });
    });

    this.container.querySelectorAll<HTMLInputElement>('[data-field-required]').forEach(input => {
      input.addEventListener('change', () => {
        const id = input.getAttribute('data-field-required');
        if (!id) return;
        this.updateInteractionSettings(node, s => {
          const field = s.form.fields.find(f => f.id === id);
          if (field) field.required = input.checked;
        }, 'Alterar obrigatoriedade do campo');
      });
    });

    this.container.querySelectorAll<HTMLInputElement>('[data-field-options]').forEach(input => {
      input.addEventListener('change', () => {
        const id = input.getAttribute('data-field-options');
        if (!id) return;
        this.updateInteractionSettings(node, s => {
          const field = s.form.fields.find(f => f.id === id);
          if (!field) return;
          field.options = input.value.split(',').map(part => part.trim()).filter(Boolean)
            .map(value => ({ value, label: value }));
        }, 'Alterar opções do campo');
      });
    });

    this.container.querySelectorAll<HTMLButtonElement>('[data-field-remove]').forEach(button => {
      button.addEventListener('click', () => {
        const id = button.getAttribute('data-field-remove');
        if (!id) return;
        this.updateInteractionSettings(node, s => {
          s.form.fields = s.form.fields.filter(field => field.id !== id);
        }, 'Remover campo da interação');
        const updated = this.graph.getNode(node.id);
        if (updated) this.renderNode(updated);
      });
    });

    this.container.querySelector('#btn-add-interaction-field')?.addEventListener('click', () => {
      this.updateInteractionSettings(node, s => {
        let index = s.form.fields.length + 1;
        let id = `campo_${index}`;
        while (s.form.fields.some(field => field.id === id)) id = `campo_${++index}`;
        s.form.fields.push({ id, type: 'text', label: `Campo ${index}`, required: false });
      }, 'Adicionar campo à interação');
      const updated = this.graph.getNode(node.id);
      if (updated) this.renderNode(updated);
    });
  }

  private getTaskTypeExplanation(type: string): string {
    switch (type) {
      case 'static':
        return t('inspector.explanations.static');
      case 'condition':
        return t('inspector.explanations.condition');
      case 'switch':
        return t('inspector.explanations.switch');
      case 'mcp':
        return t('inspector.explanations.mcp');
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
      case 'interaction':
        return t('inspector.explanations.interaction', 'Vértice de interação humana: formulário seguro com pergunta, opções e resposta. O Studio abre o formulário como modal; execuções headless usam --answers.');
      case 'output':
        return t('inspector.explanations.output', 'Vértice coletor de saída e entrega de artefatos. Permite definir os arquivos gerados (SVGs, planilhas, relatórios Markdown, PDFs, JSONs) para inspeção visual interativa na aba Artefatos e cópia para diretórios finais.');
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
