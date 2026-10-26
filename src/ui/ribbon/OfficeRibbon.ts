import { Graph } from '../../core/Graph.ts';
import { GraphValidator } from '../../core/Validator.ts';
import { Viewport } from '../../view/Viewport.ts';
import { CodeModal } from '../CodeModal.ts';
import { TaskflowNodeType, INodeData, INodeAgentSettings, IOutputSettings, ISwitchSettings, ICallVertexSettings, IModelConfig, ISandboxPolicy, IMcpCallResult, defaultOutputSettings, defaultSwitchSettings, defaultCallVertexSettings } from '../../core/types.ts';
import { PipelineRunner } from '../../core/PipelineRunner.ts';
import { AddNodeCommand, RemoveNodeCommand, UpdateNodeCommand } from '../../core/history/GraphCommands.ts';
import { cloneNodeData } from '../../core/NodeClipboard.ts';
import { IHistoryState } from '../../core/history/CommandManager.ts';
import { globalEventBus } from '../../core/EventBus.ts';
import { ProjectManager } from '../../core/ProjectManager.ts';

import { TemplateModal } from '../TemplateModal.ts';
import { GlobalDefaultsModal } from '../GlobalDefaultsModal.ts';
import { BatchPipelineModal } from '../modals/BatchPipelineModal.ts';
import { ProjectMemoryModal } from '../modals/ProjectMemoryModal.ts';
import { McpServersModal } from '../modals/McpServersModal.ts';
import { ProjectManagerModal } from '../modals/ProjectManagerModal.ts';

import { BackstageView } from './BackstageView.ts';
import { ribbonIcon } from './RibbonIcons.ts';
import type { RibbonIconName } from './RibbonIcons.ts';
import { RibbonDialog } from './RibbonDialog.ts';
import { ImportPiModal } from '../modals/ImportPiModal.ts';
import { ImportClaudeModal } from '../modals/ImportClaudeModal.ts';
import { ImportOpenAiModal } from '../modals/ImportOpenAiModal.ts';
import { ImportHermesModal } from '../modals/ImportHermesModal.ts';
import { ImportExcelModal } from '../modals/ImportExcelModal.ts';
import { GraphvizExportModal } from '../modals/GraphvizExportModal.ts';
import { GitAccountModal } from '../modals/GitAccountModal.ts';
import { StandaloneRunnerModal } from '../modals/StandaloneRunnerModal.ts';
import { GitSyncManager } from '../../core/git/GitSyncManager.ts';
import { t } from '../../i18n/index.ts';
import { ProjectSearchBox, reindexProjectSearch } from '../ProjectSearchBox.ts';
import { sanitizeEnvReferences } from '../../core/env/EnvironmentManager.ts';

export class OfficeRibbon {
  private container: HTMLElement;
  private graph: Graph;
  private viewport: Viewport;
  private codeModal: CodeModal;
  private templateModal: TemplateModal;
  private globalDefaultsModal: GlobalDefaultsModal;
  private batchPipelineModal: BatchPipelineModal;
  private projectMemoryModal: ProjectMemoryModal;
  private mcpServersModal: McpServersModal;
  private projectManagerModal: ProjectManagerModal;
  private graphvizExportModal: GraphvizExportModal;
  private ribbonDialog: RibbonDialog;

  // Backstage View & Modais de Importação
  private backstageView: BackstageView;
  private importPiModal: ImportPiModal;
  private importClaudeModal: ImportClaudeModal;
  private importOpenAiModal: ImportOpenAiModal;
  private importHermesModal: ImportHermesModal;
  private importExcelModal: ImportExcelModal;
  private gitAccountModal: GitAccountModal;
  private standaloneRunnerModal: StandaloneRunnerModal;

  private activeTab: 'home' | 'insert' | 'agents' | 'connectors' | 'data' | 'run' | 'contextual' = 'home';
  private previousActiveTab: 'home' | 'insert' | 'agents' | 'connectors' | 'data' | 'run' = 'home';
  private selectedNode: INodeData | null = null;
  private isCollapsed: boolean = false;
  private isPipelineRunning: boolean = false;

  // AutoSave (Salvamento Automático)
  private projectManager: ProjectManager;
  private isAutoSaveEnabled: boolean = true;
  private autoSaveDebounceTimer: any = null;
  private isSaving: boolean = false;

  constructor(
    parent: HTMLElement,
    graph: Graph,
    viewport: Viewport,
    codeModal: CodeModal,
    templateModal: TemplateModal,
    globalDefaultsModal: GlobalDefaultsModal,
    batchPipelineModal?: BatchPipelineModal,
    projectMemoryModal?: ProjectMemoryModal,
    mcpServersModal?: McpServersModal,
    projectManagerModal?: ProjectManagerModal
  ) {
    this.graph = graph;
    this.viewport = viewport;
    this.codeModal = codeModal;
    this.templateModal = templateModal;
    this.globalDefaultsModal = globalDefaultsModal;
    this.batchPipelineModal = batchPipelineModal || new BatchPipelineModal(graph);
    this.projectMemoryModal = projectMemoryModal || new ProjectMemoryModal(graph);
    this.mcpServersModal = mcpServersModal || new McpServersModal(graph);
    this.projectManagerModal = projectManagerModal || new ProjectManagerModal(graph);
    this.projectManager = ProjectManager.getInstance(globalEventBus);

    if (typeof localStorage !== 'undefined') {
      const stored = localStorage.getItem('arestrix_autosave_enabled');
      this.isAutoSaveEnabled = stored !== 'false';
    } else {
      this.isAutoSaveEnabled = true;
    }

    // Inicializa modais específicos
    this.backstageView = new BackstageView(this.graph, this.codeModal, this.globalDefaultsModal);
    this.importPiModal = new ImportPiModal(this.graph, this.viewport);
    this.importClaudeModal = new ImportClaudeModal(this.graph, this.viewport);
    this.importOpenAiModal = new ImportOpenAiModal(this.graph, this.viewport);
    this.importHermesModal = new ImportHermesModal(this.graph, this.viewport);
    this.importExcelModal = new ImportExcelModal(this.graph, this.viewport);
    this.graphvizExportModal = new GraphvizExportModal(this.graph);
    this.gitAccountModal = new GitAccountModal(globalEventBus);
    this.standaloneRunnerModal = new StandaloneRunnerModal(this.graph);

    if (typeof document !== 'undefined') {
      this.ribbonDialog = new RibbonDialog();
      this.container = document.createElement('header');
      this.container.className = 'office-ribbon-container';
      if (parent && parent.appendChild) {
        parent.appendChild(this.container);
      }
      this.render();
      this.bindKeyboardShortcuts();
      this.bindBusEvents();
    } else {
      this.ribbonDialog = {
        openInput: () => {},
        openConfirm: () => {},
        close: () => {},
        isVisible: () => false
      } as unknown as RibbonDialog;
      this.container = { querySelector: () => null, querySelectorAll: () => [] } as any;
    }
  }

  public getContainer(): HTMLElement {
    return this.container;
  }

  private render(): void {
    const projectName = this.graph.getMetadata().graphName || t('ribbon.titlebar.default_workspace');

    this.container.innerHTML = `
      <!-- 1. BARRA DE TITULO / QUICK ACCESS TOOLBAR (QAT) -->
      <div class="office-titlebar">
        <div class="office-titlebar-left">
          <div class="office-brand-badge">
            <div class="office-brand-icon">
              <svg viewBox="0 0 124 124" width="15" height="15" fill="none" aria-hidden="true" focusable="false">
                <g stroke="#F4F2EE" stroke-width="14" stroke-linecap="round">
                  <line x1="62" y1="21" x2="22" y2="103"/>
                  <line x1="62" y1="21" x2="102" y2="103"/>
                </g>
                <line x1="37" y1="73" x2="87" y2="73" stroke="#0f6cbd" stroke-width="12" stroke-linecap="round"/>
                <circle cx="62" cy="21" r="11" fill="#F4F2EE"/>
                <circle cx="22" cy="103" r="11" fill="#F4F2EE"/>
                <circle cx="102" cy="103" r="11" fill="#F4F2EE"/>
              </svg>
            </div>
            <span class="office-brand-name">Arestrix <span class="office-brand-edition">STUDIO</span></span>
          </div>

          <!-- Acesso Rápido -->
          <div class="office-qat-buttons">
            <!-- Switch de Salvamento Automático (Estilo Microsoft Office) -->
            <div class="office-autosave-group" title="${t('ribbon.titlebar.autosave_tip')}">
              <label class="office-autosave-switch" for="qat-toggle-autosave">
                <input type="checkbox" id="qat-toggle-autosave" ${this.isAutoSaveEnabled ? 'checked' : ''}>
                <span class="office-autosave-slider"></span>
              </label>
              <span class="office-autosave-label" id="qat-autosave-label">${t('ribbon.titlebar.autosave')}</span>
              <span class="office-autosave-badge" id="qat-autosave-badge" title="${t('ribbon.titlebar.autosave_saved_badge')}">✓</span>
            </div>

            <!-- Botão e Indicador de Sincronização Git Privado -->
            <button id="qat-btn-git-sync" class="office-qat-btn office-qat-label" title="${t('git_sync.modal_title', 'Conta Git & Repositório do Projeto')}">
              ${ribbonIcon('git', 17)}
              <span id="qat-git-status-badge">Git</span>
            </button>
            <button id="qat-btn-git-push" class="office-qat-btn office-qat-label" title="${t('git_sync.btn_push_tooltip', 'Sincronizar com GitHub agora (Commit & Push manual)')}">
              ${ribbonIcon('upload', 17)}
              <span>Push</span>
            </button>

            <button id="qat-btn-save" class="office-qat-btn" title="${t('ribbon.titlebar.save_now')}" aria-label="${t('ribbon.titlebar.save_now')}">
              ${ribbonIcon('save', 18)}
            </button>
            <button id="qat-btn-undo" class="office-qat-btn" title="${t('ribbon.titlebar.undo')}" aria-label="${t('ribbon.titlebar.undo')}" disabled>
              ${ribbonIcon('undo', 18)}
            </button>
            <button id="qat-btn-redo" class="office-qat-btn" title="${t('ribbon.titlebar.redo')}" aria-label="${t('ribbon.titlebar.redo')}" disabled>
              ${ribbonIcon('redo', 18)}
            </button>
            <button id="qat-btn-run" class="office-qat-btn ${this.isPipelineRunning ? 'is-running-stop' : ''}" data-icon="${this.isPipelineRunning ? 'stop' : 'play'}" title="${this.isPipelineRunning ? t('ribbon.titlebar.stop') : t('ribbon.titlebar.run')}" aria-label="${this.isPipelineRunning ? t('ribbon.titlebar.stop') : t('ribbon.titlebar.run')}">
              ${ribbonIcon(this.isPipelineRunning ? 'stop' : 'play', 18)}
            </button>
          </div>
        </div>

        <div class="office-titlebar-center">
          <button id="qat-btn-project" class="office-project-indicator" title="${t('ribbon.buttons.project_manager_tip')}">
            ${ribbonIcon('folder', 16)}
            <span id="office-active-project-name">${this.escapeHtml(projectName)}</span>
          </button>
        </div>

        <div class="office-titlebar-right">
          <div class="office-tellme-box" title="${t('search.tooltip', 'Buscar vértices, skills e pipelines do projeto (Ctrl+K)')}">
            ${ribbonIcon('search', 16)}
            <input type="text" id="office-tellme-input">
          </div>
        </div>
      </div>

      <!-- 2. BARRA DE ABAS DA FAIXA DE OPÇÕES -->
      <div class="office-tabs-bar">
        <nav class="office-tabs-nav">
          <button class="office-tab-btn file-tab" id="tab-btn-file" title="${t('ribbon.tabs.file')} (Backstage View)">
            ${t('ribbon.tabs.file')}
          </button>
          <button class="office-tab-btn ${this.activeTab === 'home' ? 'active' : ''}" data-tab="home">
            ${t('ribbon.tabs.home')}
          </button>
          <button class="office-tab-btn ${this.activeTab === 'insert' ? 'active' : ''}" data-tab="insert">
            ${t('ribbon.tabs.insert')}
          </button>
          <button class="office-tab-btn ${this.activeTab === 'agents' ? 'active' : ''}" data-tab="agents">
            ${t('ribbon.tabs.agents')}
          </button>
          <button class="office-tab-btn ${this.activeTab === 'connectors' ? 'active' : ''}" data-tab="connectors">
            ${t('ribbon.tabs.connectors')}
          </button>
          <button class="office-tab-btn ${this.activeTab === 'data' ? 'active' : ''}" data-tab="data">
            ${t('ribbon.tabs.data')}
          </button>
          <button class="office-tab-btn ${this.activeTab === 'run' ? 'active' : ''}" data-tab="run">
            ${t('ribbon.tabs.run')}
          </button>
          ${this.renderContextualTabButton()}
        </nav>

        <div class="office-tabs-actions">
          <button class="office-ribbon-collapse-btn" id="btn-toggle-ribbon" title="${t('ribbon.buttons.collapse_ribbon_tip')}">
            ${this.isCollapsed ? 'ᐯ' : 'ᐱ'}
          </button>
        </div>
      </div>

      <!-- 3. CORPO DA FAIXA DE OPÇÕES (RIBBON BODY) -->
      <div class="office-ribbon-body ${this.isCollapsed ? 'collapsed' : ''}" id="office-ribbon-body">
        ${this.renderRibbonPanes()}
      </div>
    `;

    this.bindInternalEvents();
  }

  private renderRibbonPanes(): string {
    return `
      <!-- ABA 1: PAGINA INICIAL (HOME) -->
      <div class="office-ribbon-pane ${this.activeTab === 'home' ? 'active' : ''}" data-pane="home">
        <!-- Grupo Edição & Visão -->
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <div class="ribbon-col-stack">
              <button class="ribbon-btn-small" id="ribbon-btn-undo" disabled>
                <span class="ribbon-sm-icon" data-icon="undo">${ribbonIcon('undo', 16)}</span>
                <span>${t('ribbon.panes.undo')}</span>
              </button>
              <button class="ribbon-btn-small" id="ribbon-btn-redo" disabled>
                <span class="ribbon-sm-icon" data-icon="redo">${ribbonIcon('redo', 16)}</span>
                <span>${t('ribbon.panes.redo')}</span>
              </button>
            </div>
            <div class="ribbon-col-stack">
              <button class="ribbon-btn-small" id="ribbon-btn-zoom-fit">
                <span class="ribbon-sm-icon" data-icon="fit">${ribbonIcon('fit', 16)}</span>
                <span>${t('ribbon.panes.zoom_fit')}</span>
              </button>
              <button class="ribbon-btn-small" id="ribbon-btn-clear">
                <span class="ribbon-sm-icon ribbon-icon-danger" data-icon="trash">${ribbonIcon('trash', 16)}</span>
                <span>${t('ribbon.panes.clear_graph')}</span>
              </button>
            </div>
            <button class="ribbon-btn-large" id="ribbon-btn-auto-layout" title="${t('ribbon.panes.auto_layout_tip')}">
              <span class="ribbon-btn-icon" data-icon="layout">${ribbonIcon('layout', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.auto_layout')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.edit_view')}</div>
        </div>

        <!-- Grupo Vértices do Grafo -->
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-quick-agent" title="${t('ribbon.panes.quick_agent_tip')}">
              <span class="ribbon-btn-icon" data-icon="agent">${ribbonIcon('agent', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.quick_agent')}</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-quick-task" title="${t('ribbon.panes.quick_task_tip')}">
              <span class="ribbon-btn-icon" data-icon="task">${ribbonIcon('task', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.quick_task')}</span>
            </button>
            <div class="ribbon-col-stack ribbon-col-stack-wide">
              <select id="ribbon-select-add-node" class="ribbon-select" title="${t('ribbon.buttons.static_node_tip')}">
                <option value="" disabled selected>${t('ribbon.panes.more_vertices')}</option>
                <option value="static">${t('ribbon.nodes.static')}</option>
                <option value="condition">${t('ribbon.nodes.condition')}</option>
                <option value="switch">${t('ribbon.nodes.switch')}</option>
                <option value="mcp">${t('ribbon.nodes.mcp')}</option>
                <option value="interaction">${t('ribbon.nodes.interaction', 'Interação')}</option>
                <option value="call">${t('ribbon.nodes.call', 'Chamada em fluxo')}</option>
                <option value="subflow">${t('ribbon.nodes.subflow')}</option>
                <option value="module">${t('ribbon.nodes.module')}</option>
                <option value="critical">${t('ribbon.nodes.critical')}</option>
                <option value="agent">${t('ribbon.nodes.agent')}</option>
                <option value="cuda">${t('ribbon.nodes.cuda')}</option>
              </select>
              <button class="ribbon-btn-small" id="ribbon-btn-quick-templates">
                <span class="ribbon-sm-icon" data-icon="catalog">${ribbonIcon('catalog', 16)}</span>
                <span>${t('ribbon.panes.catalog')}</span>
              </button>
            </div>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.graph_vertices')}</div>
        </div>

        <!-- Grupo Execução Primária -->
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large primary-action ${this.isPipelineRunning ? 'is-running-stop' : ''}" id="ribbon-btn-run" title="${this.isPipelineRunning ? t('ribbon.panes.stop_run_tip') : t('ribbon.panes.build_run_tip')}">
              <span class="ribbon-btn-icon" data-icon="${this.isPipelineRunning ? 'stop' : 'play'}">${ribbonIcon(this.isPipelineRunning ? 'stop' : 'play', 30)}</span>
              <span class="ribbon-btn-label">${this.isPipelineRunning ? t('ribbon.panes.stop_run') : t('ribbon.panes.build_run')}</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-validate" title="${t('ribbon.panes.validate_dag_tip')}">
              <span class="ribbon-btn-icon" data-icon="validate">${ribbonIcon('validate', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.validate_dag')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.execution')}</div>
        </div>

        <!-- Grupo Governança & Workspace -->
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-pipeline-defaults" title="${t('ribbon.panes.global_defaults_tip')}">
              <span class="ribbon-btn-icon" data-icon="settings">${ribbonIcon('settings', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.global_defaults')}</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-project-hub" title="${t('ribbon.panes.project_hub_tip')}">
              <span class="ribbon-btn-icon" data-icon="hub">${ribbonIcon('hub', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.project_hub')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.governance')}</div>
        </div>
      </div>

      <!-- ABA 2: INSERIR (INSERT) -->
      <div class="office-ribbon-pane ${this.activeTab === 'insert' ? 'active' : ''}" data-pane="insert">
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" data-add="static">
              <span class="ribbon-btn-icon" data-icon="file">${ribbonIcon('file', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.static_task')}</span>
            </button>
            <button class="ribbon-btn-large" data-add="condition">
              <span class="ribbon-btn-icon" data-icon="branch">${ribbonIcon('branch', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.condition_branch')}</span>
            </button>
            <button class="ribbon-btn-large" data-add="switch">
              <span class="ribbon-btn-icon" data-icon="switch">${ribbonIcon('switch', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.switch_cases')}</span>
            </button>
            <button class="ribbon-btn-large" data-add="mcp">
              <span class="ribbon-btn-icon" data-icon="plug">${ribbonIcon('plug', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.mcp_tool')}</span>
            </button>
            <button class="ribbon-btn-large" data-add="subflow">
              <span class="ribbon-btn-icon" data-icon="subflow">${ribbonIcon('subflow', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.subflow_nested')}</span>
            </button>
            <button class="ribbon-btn-large" data-add="module">
              <span class="ribbon-btn-icon" data-icon="box">${ribbonIcon('box', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.module_encapsulated')}</span>
            </button>
            <button class="ribbon-btn-large" data-add="critical">
              <span class="ribbon-btn-icon" data-icon="lock">${ribbonIcon('lock', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.critical_section')}</span>
            </button>
            <button class="ribbon-btn-large" data-add="output" title="Vértice coletor de saída e entrega de artefatos">
              <span class="ribbon-btn-icon" data-icon="output">${ribbonIcon('output', 30)}</span>
              <span class="ribbon-btn-label">Saída / Artefato</span>
            </button>
            <button class="ribbon-btn-large" data-add="call" title="Chamada em fluxo: executa um alvo chamavel e devolve a resposta ao fluxo">
              <span class="ribbon-btn-icon" data-icon="phone">${ribbonIcon('phone', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.call_vertex', 'Chamada em fluxo')}</span>
            </button>
            <button class="ribbon-btn-large" data-add="interaction" title="Interação humana: formulário, pergunta ou aprovação">
              <span class="ribbon-btn-icon" data-icon="chat">${ribbonIcon('chat', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.interaction', 'Interação')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.flow_concurrency')}</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" data-add="cuda">
              <span class="ribbon-btn-icon" data-icon="bolt">${ribbonIcon('bolt', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.cuda_kernel')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.gpu_accel')}</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" data-add="agent">
              <span class="ribbon-btn-icon" data-icon="agent">${ribbonIcon('agent', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.agent_code_mode')}</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-insert-templates">
              <span class="ribbon-btn-icon" data-icon="catalog">${ribbonIcon('catalog', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.agent_templates')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.cognitive_agents')}</div>
        </div>
      </div>

      <!-- ABA 3: AGENTES & TOOLS -->
      <div class="office-ribbon-pane ${this.activeTab === 'agents' ? 'active' : ''}" data-pane="agents">
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-tools-defaults">
              <span class="ribbon-btn-icon" data-icon="shield">${ribbonIcon('shield', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.sandboxes_iso')}</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-tools-code">
              <span class="ribbon-btn-icon" data-icon="wrench">${ribbonIcon('wrench', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.tools_code_mode')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.sec_workspace')}</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-mcp-mgr">
              <span class="ribbon-btn-icon" data-icon="plug">${ribbonIcon('plug', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.servers_mcp')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.servers_workspace')}</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-open-templates-tab">
              <span class="ribbon-btn-icon" data-icon="book">${ribbonIcon('book', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.template_library')}</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-github-repos" title="${t('ribbon.panes.github_repos_tip')}">
              <span class="ribbon-btn-icon" data-icon="globe">${ribbonIcon('globe', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.github_repos')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.global_cat_studio')}</div>
        </div>
      </div>

      <!-- ABA 4: CONECTORES & IMPORTAÇÃO (PRIORIDADE PI -> CLAUDE -> OPENAI -> HERMES -> EXCEL) -->
      <div class="office-ribbon-pane ${this.activeTab === 'connectors' ? 'active' : ''}" data-pane="connectors">
        <!-- Grupo Assistentes de IA -->
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <!-- PI.AI -->
            <button class="ribbon-btn-large highlight-pi" id="ribbon-btn-import-pi" title="${t('ribbon.panes.import_pi_tip')}">
              <span class="ribbon-pill-tag">${t('ribbon.panes.pill_tests')}</span>
              <span class="ribbon-btn-icon" data-icon="spark">${ribbonIcon('spark', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.import_pi_btn')}</span>
            </button>

            <!-- CLAUDE -->
            <button class="ribbon-btn-large" id="ribbon-btn-import-claude" title="${t('ribbon.panes.import_claude_tip')}">
              <span class="ribbon-btn-icon" data-icon="bolt">${ribbonIcon('bolt', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.import_claude_btn')}</span>
            </button>

            <!-- OPENAI -->
            <button class="ribbon-btn-large" id="ribbon-btn-import-openai" title="${t('ribbon.panes.import_openai_tip')}">
              <span class="ribbon-btn-icon" data-icon="circle">${ribbonIcon('circle', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.import_openai_btn')}</span>
            </button>

            <!-- HERMES -->
            <button class="ribbon-btn-large" id="ribbon-btn-import-hermes" title="${t('ribbon.panes.import_hermes_tip')}">
              <span class="ribbon-btn-icon" data-icon="orbit">${ribbonIcon('orbit', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.import_hermes_btn')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.assistants_llms')}</div>
        </div>

        <!-- Grupo Dados Corporativos & Excel -->
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-import-excel" title="${t('ribbon.panes.import_excel_tip')}">
              <span class="ribbon-pill-tag tag-blue">${t('ribbon.panes.pill_modern')}</span>
              <span class="ribbon-btn-icon" data-icon="sheet">${ribbonIcon('sheet', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.import_excel_btn')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.data_spreadsheets')}</div>
        </div>

        <!-- Grupo Protocolo MCP -->
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-mcp-conn">
              <span class="ribbon-btn-icon" data-icon="plug">${ribbonIcon('plug', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.mcp_connectors')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.open_protocol')}</div>
        </div>
      </div>

      <!-- ABA 5: DADOS & MEMORIA -->
      <div class="office-ribbon-pane ${this.activeTab === 'data' ? 'active' : ''}" data-pane="data">
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-memory-hub">
              <span class="ribbon-btn-icon" data-icon="database">${ribbonIcon('database', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.memory_hub')}</span>
            </button>
            <div class="ribbon-col-stack">
              <button class="ribbon-btn-small" id="ribbon-btn-mem-blackboard">
                <span class="ribbon-sm-icon" data-icon="board">${ribbonIcon('board', 16)}</span>
                <span>${t('ribbon.panes.blackboard')}</span>
              </button>
              <button class="ribbon-btn-small" id="ribbon-btn-mem-lessons">
                <span class="ribbon-sm-icon" data-icon="brain">${ribbonIcon('brain', 16)}</span>
                <span>${t('ribbon.panes.lessons_learned')}</span>
              </button>
            </div>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.proj_memory_grp')}</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-sqlite-db">
              <span class="ribbon-btn-icon" data-icon="database">${ribbonIcon('database', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.sqlite_db')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.workspace_storage')}</div>
        </div>
      </div>

      <!-- ABA 6: EXECUCAO & LOTE -->
      <div class="office-ribbon-pane ${this.activeTab === 'run' ? 'active' : ''}" data-pane="run">
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large primary-action ${this.isPipelineRunning ? 'is-running-stop' : ''}" id="ribbon-btn-run-tab" title="${this.isPipelineRunning ? t('ribbon.panes.stop_run_tip') : t('ribbon.panes.build_run_tip')}">
              <span class="ribbon-btn-icon" data-icon="${this.isPipelineRunning ? 'stop' : 'play'}">${ribbonIcon(this.isPipelineRunning ? 'stop' : 'play', 30)}</span>
              <span class="ribbon-btn-label">${this.isPipelineRunning ? t('ribbon.panes.stop_run') : t('ribbon.panes.compile_run')}</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-batch-modal">
              <span class="ribbon-btn-icon" data-icon="box">${ribbonIcon('box', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.batch_resume')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.taskflow_engine')}</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-standalone-runner" title="${t('ribbon.panes.standalone_runner_tip', 'Abrir ambiente do Runner Standalone com telas de Progresso, Diagnóstico e Operação')}">
              <span class="ribbon-btn-icon" data-icon="rocket">${ribbonIcon('rocket', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.standalone_runner', 'Runner Standalone')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.distribution_grp', 'Distribuição')}</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-export-code-tab">
              <span class="ribbon-btn-icon" data-icon="code">${ribbonIcon('code', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.export_code')}</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-export-svg-tab">
              <span class="ribbon-btn-icon" data-icon="image">${ribbonIcon('image', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.export_svg')}</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-open-artifacts-tab" title="Abrir painel de artefatos gerados pelo pipeline (SVGs, planilhas, PDFs, etc.)">
              <span class="ribbon-btn-icon" data-icon="folderOpen">${ribbonIcon('folderOpen', 30)}</span>
              <span class="ribbon-btn-label">Ver Artefatos</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.export_grp')}</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-load-demo-tab">
              <span class="ribbon-btn-icon" data-icon="bulb">${ribbonIcon('bulb', 30)}</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.load_demo')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.examples_grp')}</div>
        </div>
      </div>

      <!-- ABA CONTEXTUAL DE COMPONENTE / VERTICE (SELECIONADO) -->
      ${this.renderContextualPane()}
    `;
  }

  private bindInternalEvents(): void {
    // 1. Menu Arquivo (Backstage View)
    this.container.querySelector('#tab-btn-file')?.addEventListener('click', async () => {
      await this.backstageView.open('info');
    });

    // 2. Alternador de Abas
    this.container.querySelectorAll<HTMLButtonElement>('.office-tab-btn[data-tab]').forEach(btn => {
      btn.addEventListener('click', () => {
        const tab = btn.dataset.tab as any;
        if (tab) {
          this.switchTab(tab);
        }
      });
    });

    // 3. Colapsar / Expandir Ribbon
    this.container.querySelector('#btn-toggle-ribbon')?.addEventListener('click', () => {
      this.toggleCollapse();
    });

    // 4. QAT Buttons & AutoSave Switch
    const autoSaveToggle = this.container.querySelector('#qat-toggle-autosave') as HTMLInputElement;
    autoSaveToggle?.addEventListener('change', () => {
      this.isAutoSaveEnabled = autoSaveToggle.checked;
      if (typeof localStorage !== 'undefined') {
        try {
          localStorage.setItem('arestrix_autosave_enabled', this.isAutoSaveEnabled ? 'true' : 'false');
        } catch (_) {}
      }
      this.updateAutoSaveBadge(this.isAutoSaveEnabled ? 'saved' : 'disabled');
      if (this.isAutoSaveEnabled) {
        this.saveNow();
      }
    });

    this.container.querySelector('#qat-btn-git-sync')?.addEventListener('click', () => {
      this.gitAccountModal.open();
    });

    this.container.querySelector('#qat-btn-git-push')?.addEventListener('click', async () => {
      const proj = ProjectManager.getInstance().getCurrentProject();
      if (!proj.git || !proj.git.enabled) {
        this.gitAccountModal.open();
        return;
      }
      const btn = this.container.querySelector('#qat-btn-git-push') as HTMLButtonElement | null;
      if (btn) btn.disabled = true;
      try {
        await GitSyncManager.getInstance().triggerManualPush();
      } finally {
        if (btn) btn.disabled = false;
      }
    });

    this.container.querySelector('#qat-btn-save')?.addEventListener('click', async () => {
      await this.saveNow();
    });

    this.container.querySelector('#qat-btn-undo')?.addEventListener('click', () => this.graph.undo());
    this.container.querySelector('#qat-btn-redo')?.addEventListener('click', () => this.graph.redo());
    this.container.querySelector('#qat-btn-run')?.addEventListener('click', () => this.toggleExecuteOrStop());
    this.container.querySelector('#qat-btn-project')?.addEventListener('click', () => this.projectManagerModal.show());

    // 5. Busca no projeto (ranking no backend C++) + comandos rápidos
    const tellMeInput = this.container.querySelector('#office-tellme-input') as HTMLInputElement | null;
    const tellMeBox = this.container.querySelector('.office-tellme-box') as HTMLElement | null;
    if (tellMeInput && tellMeBox && typeof tellMeInput.setAttribute === 'function') {
      new ProjectSearchBox(tellMeBox, tellMeInput, this.graph, this.viewport, globalEventBus, [
        { id: 'pi', label: t('search.cmd_import_pi', 'Importar projeto Pi'), keywords: ['pi'], run: () => this.importPiModal.show() },
        { id: 'excel', label: t('search.cmd_import_excel', 'Importar Excel'), keywords: ['excel'], run: () => this.importExcelModal.show() },
        { id: 'run', label: t('search.cmd_run', 'Compilar e executar'), keywords: ['run', 'executar', 'compilar'], run: () => this.toggleExecuteOrStop() },
        { id: 'project', label: t('search.cmd_project', 'Gerenciador de projetos'), keywords: ['projeto', 'workspace'], run: () => this.projectManagerModal.show() },
        { id: 'mcp', label: t('search.cmd_mcp', 'Servidores MCP'), keywords: ['mcp'], run: () => this.mcpServersModal.show() },
        { id: 'memory', label: t('search.cmd_memory', 'Memória do projeto'), keywords: ['memoria', 'memória', 'memory'], run: () => this.projectMemoryModal.show() },
        { id: 'batch', label: t('search.cmd_batch', 'Lote e Retomada'), keywords: ['lote', 'batch'], run: () => this.batchPipelineModal.open() },
        { id: 'layout', label: t('search.cmd_auto_layout', 'Auto-Organizar Layout'), keywords: ['layout', 'organizar', 'distribuir', 'alinhar'], run: () => this.graph.autoLayout(true) },
        { id: 'github', label: t('search.cmd_github', 'Repositórios GitHub (catálogo de skills)'), keywords: ['github', 'repo', 'repositório', 'skill'], run: () => globalEventBus.emit('modal:open-github-repos') }
      ]);
    }

    // 6. Botões do Painel Home
    this.container.querySelector('#ribbon-btn-undo')?.addEventListener('click', () => this.graph.undo());
    this.container.querySelector('#ribbon-btn-redo')?.addEventListener('click', () => this.graph.redo());
    this.container.querySelector('#ribbon-btn-zoom-fit')?.addEventListener('click', () => this.viewport.zoomToFit());
    this.container.querySelector('#ribbon-btn-auto-layout')?.addEventListener('click', () => this.graph.autoLayout(true));
    this.container.querySelector('#ribbon-btn-clear')?.addEventListener('click', () => {
      this.ribbonDialog.openConfirm({
        title: t('ribbon.panes.clear_graph'),
        message: t('ribbon.alerts.clear_confirm'),
        confirmLabel: t('ribbon.panes.clear_graph'),
        danger: true,
        onConfirm: () => this.applyClearGraph()
      });
    });

    this.container.querySelector('#ribbon-btn-quick-agent')?.addEventListener('click', () => this.createNewNode('agent'));
    this.container.querySelector('#ribbon-btn-quick-task')?.addEventListener('click', () => this.createNewNode('static'));
    this.container.querySelector('#ribbon-btn-quick-templates')?.addEventListener('click', () => this.templateModal.show());

    const selectAdd = this.container.querySelector('#ribbon-select-add-node') as HTMLSelectElement;
    selectAdd?.addEventListener('change', () => {
      if (selectAdd.value) {
        this.createNewNode(selectAdd.value as TaskflowNodeType);
        selectAdd.value = '';
      }
    });

    this.container.querySelector('#ribbon-btn-run')?.addEventListener('click', () => this.toggleExecuteOrStop());
    this.container.querySelector('#ribbon-btn-validate')?.addEventListener('click', () => this.validateGraph());
    this.container.querySelector('#ribbon-btn-pipeline-defaults')?.addEventListener('click', () => this.globalDefaultsModal.show());
    this.container.querySelector('#ribbon-btn-project-hub')?.addEventListener('click', () => this.projectManagerModal.show());

    // 7. Botões da Aba Inserir
    this.container.querySelectorAll<HTMLButtonElement>('button[data-add]').forEach(btn => {
      btn.addEventListener('click', () => {
        const t = btn.dataset.add as TaskflowNodeType;
        if (t) this.createNewNode(t);
      });
    });
    this.container.querySelector('#ribbon-btn-insert-templates')?.addEventListener('click', () => this.templateModal.show());

    // 8. Botões da Aba Agentes & Tools
    this.container.querySelector('#ribbon-btn-tools-defaults')?.addEventListener('click', () => this.globalDefaultsModal.show());
    this.container.querySelector('#ribbon-btn-tools-code')?.addEventListener('click', () => this.globalDefaultsModal.show());
    this.container.querySelector('#ribbon-btn-mcp-mgr')?.addEventListener('click', () => this.mcpServersModal.show());
    this.container.querySelector('#ribbon-btn-open-templates-tab')?.addEventListener('click', () => this.templateModal.show());
    this.container.querySelector('#ribbon-btn-github-repos')?.addEventListener('click', () => globalEventBus.emit('modal:open-github-repos'));

    // 9. Botões da Aba Conectores & Importação
    this.container.querySelector('#ribbon-btn-import-pi')?.addEventListener('click', () => this.importPiModal.show());
    this.container.querySelector('#ribbon-btn-import-claude')?.addEventListener('click', () => this.importClaudeModal.show());
    this.container.querySelector('#ribbon-btn-import-openai')?.addEventListener('click', () => this.importOpenAiModal.show());
    this.container.querySelector('#ribbon-btn-import-hermes')?.addEventListener('click', () => this.importHermesModal.show());
    this.container.querySelector('#ribbon-btn-import-excel')?.addEventListener('click', () => this.importExcelModal.show());
    this.container.querySelector('#ribbon-btn-mcp-conn')?.addEventListener('click', () => this.mcpServersModal.show());

    // 10. Botões da Aba Dados & Memória
    this.container.querySelector('#ribbon-btn-memory-hub')?.addEventListener('click', () => this.projectMemoryModal.show());
    this.container.querySelector('#ribbon-btn-mem-blackboard')?.addEventListener('click', () => this.projectMemoryModal.show());
    this.container.querySelector('#ribbon-btn-mem-lessons')?.addEventListener('click', () => this.projectMemoryModal.show());
    this.container.querySelector('#ribbon-btn-sqlite-db')?.addEventListener('click', async () => {
      await this.backstageView.open('info');
    });

    // 11. Botões da Aba Execução & Lote
    this.container.querySelector('#ribbon-btn-run-tab')?.addEventListener('click', () => this.toggleExecuteOrStop());
    this.container.querySelector('#ribbon-btn-batch-modal')?.addEventListener('click', () => this.batchPipelineModal.open());
    this.container.querySelector('#ribbon-btn-standalone-runner')?.addEventListener('click', () => this.standaloneRunnerModal.open());
    this.container.querySelector('#ribbon-btn-export-code-tab')?.addEventListener('click', () => this.codeModal.show());
    this.container.querySelector('#ribbon-btn-export-svg-tab')?.addEventListener('click', () => this.exportGraphvizSVG());
    this.container.querySelector('#ribbon-btn-open-artifacts-tab')?.addEventListener('click', () => globalEventBus.emit('console:open-artifacts'));
    this.container.querySelector('#ribbon-btn-load-demo-tab')?.addEventListener('click', () => this.loadDemoComplexSystem());

    // 12. Ações e Botões da Aba Contextual de Vértice
    if (this.selectedNode) {
      this.bindContextualEvents();
    }
  }

  private switchTab(tab: 'home' | 'insert' | 'agents' | 'connectors' | 'data' | 'run' | 'contextual'): void {
    if (tab !== 'contextual') {
      // Lembra a última aba normal escolhida pelo usuário. Ao deselecionar um
      // vértice, a ribbon volta para ela em vez de saltar para a aba anterior
      // à primeira seleção.
      this.previousActiveTab = tab;
    }
    this.activeTab = tab;
    if (this.isCollapsed) {
      this.isCollapsed = false;
    }
    this.render();
  }

  public toggleCollapse(): void {
    this.isCollapsed = !this.isCollapsed;
    const body = this.container.querySelector('#office-ribbon-body');
    const btn = this.container.querySelector('#btn-toggle-ribbon');
    if (body) {
      body.classList.toggle('collapsed', this.isCollapsed);
    }
    if (btn) {
      btn.textContent = this.isCollapsed ? 'ᐯ' : 'ᐱ';
    }
  }

  private bindBusEvents(): void {
    // Atualização de Histórico Undo/Redo
    globalEventBus.on('history:changed', (state: IHistoryState) => {
      const qatUndo = this.container.querySelector('#qat-btn-undo') as HTMLButtonElement;
      const qatRedo = this.container.querySelector('#qat-btn-redo') as HTMLButtonElement;
      const rUndo = this.container.querySelector('#ribbon-btn-undo') as HTMLButtonElement;
      const rRedo = this.container.querySelector('#ribbon-btn-redo') as HTMLButtonElement;

      if (qatUndo) qatUndo.disabled = !state.canUndo;
      if (qatRedo) qatRedo.disabled = !state.canRedo;
      if (rUndo) rUndo.disabled = !state.canUndo;
      if (rRedo) rRedo.disabled = !state.canRedo;
    });

    // Mudança de Projeto
    globalEventBus.on('project:changed', (proj: any) => {
      const el = this.container.querySelector('#office-active-project-name');
      if (el && proj) {
        el.textContent = proj.name;
      }
    });

    // Export SVG acionado externamente
    globalEventBus.on('ribbon:export-svg', () => {
      this.exportGraphvizSVG();
    });

    // Abertura do Modal de Gerenciamento de Projetos acionado externamente
    globalEventBus.on('modal:open-project-manager', (tab?: any) => {
      this.projectManagerModal.show(tab || 'settings');
    });

    // Gatilhos de Salvamento Automático a cada modificação (estilo Office)
    const onModification = () => {
      if (this.isAutoSaveEnabled && !this.isSaving) {
        this.triggerAutoSave();
      }
    };

    globalEventBus.on('graph:changed', onModification);
    globalEventBus.on('node:moved', onModification);
    globalEventBus.on('graph:defaults-updated', onModification);
    globalEventBus.on('project:updated', onModification);
    globalEventBus.on('history:changed', onModification);

    // Re-renderização no evento de alteração de idioma
    globalEventBus.on('locale:changed', () => {
      this.render();
    });

    // Abertura do Modal de Conta Git
    globalEventBus.on('modal:open-git-account', () => {
      this.gitAccountModal.open();
    });

    // Atualização de status do Git no QAT
    globalEventBus.on('git:syncing', () => {
      const badge = this.container.querySelector('#qat-git-status-badge') as HTMLElement;
      if (badge) {
        badge.textContent = 'Enviando...';
        badge.style.color = '#0284c7';
      }
    });

    globalEventBus.on('git:synced', () => {
      const badge = this.container.querySelector('#qat-git-status-badge') as HTMLElement;
      if (badge) {
        const lastSt = GitSyncManager.getInstance().getLastStatus();
        const isLoggedIn = lastSt && (
          (Array.isArray(lastSt.gcmAccounts) && lastSt.gcmAccounts.length > 0) ||
          (lastSt.isIdentityConfigured && !!lastSt.remoteUrl)
        );
        if (isLoggedIn) {
          badge.textContent = 'Git ✓';
          badge.style.color = '#0f766e';
        } else {
          badge.textContent = 'Git';
          badge.style.color = '#64748b';
        }
      }
    });

    globalEventBus.on('git:error', () => {
      const badge = this.container.querySelector('#qat-git-status-badge') as HTMLElement;
      if (badge) {
        badge.textContent = 'Git ⚠️';
        badge.style.color = '#dc2626';
      }
    });

    globalEventBus.on('git:status-changed', (st: any) => {
      const badge = this.container.querySelector('#qat-git-status-badge') as HTMLElement;
      const pushBtn = this.container.querySelector('#qat-btn-git-push') as HTMLButtonElement | null;
      const proj = ProjectManager.getInstance().getCurrentProject();
      const isGitEnabled = proj.git && proj.git.enabled !== false;

      if (!isGitEnabled) {
        if (badge) {
          badge.textContent = 'Local';
          badge.style.color = '#64748b';
        }
        if (pushBtn) {
          pushBtn.style.opacity = '0.5';
          pushBtn.title = t('git_sync.local_mode_tip', 'Modo Local: Sincronizao Git desativada (Opcional)');
        }
        return;
      }

      if (pushBtn) {
        pushBtn.style.opacity = '1';
        pushBtn.title = t('git_sync.btn_push_tooltip', 'Sincronizar com GitHub agora (Commit & Push manual)');
      }

      if (badge) {
        const isLoggedIn = st && (
          (Array.isArray(st.gcmAccounts) && st.gcmAccounts.length > 0) ||
          (st.isIdentityConfigured && !!st.remoteUrl)
        );

        if (!st.gitInstalled) {
          badge.textContent = 'Sem Git';
          badge.style.color = '#94a3b8';
        } else if (!isLoggedIn) {
          // Usuário não autenticado: neutro, sem check e sem verde
          badge.textContent = 'Git';
          badge.style.color = '#64748b';
        } else if (st.hasUncommittedChanges || st.hasUnpushedCommits || (st.unpushedCommitsCount ?? 0) > 0) {
          badge.textContent = 'Git *';
          badge.style.color = '#d97706';
        } else {
          badge.textContent = 'Git ✓';
          badge.style.color = '#0f766e';
        }
      }
    });

    globalEventBus.on('runner:open-modal', (tab?: any) => {
      this.standaloneRunnerModal.open(tab);
    });

    // Consulta inicial silenciosa de status do Git
    void GitSyncManager.getInstance().refreshStatus();

    // Estado de Execução (Build & Run <-> Stop)
    globalEventBus.on('pipeline:compiling', () => this.setExecutionRunning(true));
    globalEventBus.on('pipeline:running', () => this.setExecutionRunning(true));
    globalEventBus.on('pipeline:success', () => this.setExecutionRunning(false));
    globalEventBus.on('pipeline:error', () => this.setExecutionRunning(false));
    globalEventBus.on('pipeline:stopped', () => this.setExecutionRunning(false));

    // Seleção / Foco de Nó - Abas Contextuais Estilo Microsoft Office
    globalEventBus.on('node:selected', (payload: any) => {
      const nodeId = typeof payload === 'string' ? payload : payload?.id;
      if (!nodeId) return;
      const node = this.graph.getNode(nodeId);
      if (!node) return;

      if (this.activeTab !== 'contextual') {
        this.previousActiveTab = this.activeTab;
      }
      this.selectedNode = node;
      this.activeTab = 'contextual';
      if (this.isCollapsed) {
        this.isCollapsed = false;
      }
      this.render();
    });

    globalEventBus.on('node:deselected', () => {
      if (!this.selectedNode) return;
      this.selectedNode = null;
      if (this.activeTab === 'contextual') {
        this.activeTab = this.previousActiveTab || 'home';
      }
      this.render();
    });

    globalEventBus.on('node:removed', (payload: any) => {
      const removedId = typeof payload === 'string' ? payload : payload?.id;
      if (this.selectedNode && this.selectedNode.id === removedId) {
        this.selectedNode = null;
        if (this.activeTab === 'contextual') {
          this.activeTab = this.previousActiveTab || 'home';
        }
        this.render();
      }
    });

    globalEventBus.on('node:updated', (payload: any) => {
      const updatedId = typeof payload === 'string' ? payload : payload?.id;
      if (this.selectedNode && this.selectedNode.id === updatedId) {
        const updated = this.graph.getNode(updatedId);
        if (updated) {
          this.selectedNode = updated;
          this.render();
        }
      }
    });

    globalEventBus.on('graph:cleared', () => {
      this.selectedNode = null;
      if (this.activeTab === 'contextual') {
        this.activeTab = 'home';
      }
      this.render();
    });

    // Abrir outro projeto ou entrar num subflow troca o grafo ativo. O vértice
    // selecionado pode não existir mais; a aba contextual não deve sobreviver a ele.
    globalEventBus.on('graph:loaded', () => {
      if (this.selectedNode && !this.graph.getNode(this.selectedNode.id)) {
        this.selectedNode = null;
        if (this.activeTab === 'contextual') {
          this.activeTab = this.previousActiveTab || 'home';
        }
        this.render();
      }
    });
  }

  public getAutoSaveEnabled(): boolean {
    return this.isAutoSaveEnabled;
  }

  public setAutoSaveEnabled(enabled: boolean): void {
    this.isAutoSaveEnabled = enabled;
    if (typeof localStorage !== 'undefined') {
      try {
        localStorage.setItem('arestrix_autosave_enabled', enabled ? 'true' : 'false');
      } catch (_) {}
    }
    const chk = this.container.querySelector('#qat-toggle-autosave') as HTMLInputElement;
    if (chk) chk.checked = enabled;
    this.updateAutoSaveBadge(enabled ? 'saved' : 'disabled');
    if (enabled) {
      this.saveNow();
    }
  }

  public triggerAutoSave(immediate: boolean = false): void {
    if (!this.isAutoSaveEnabled) return;
    if (this.autoSaveDebounceTimer) {
      clearTimeout(this.autoSaveDebounceTimer);
      this.autoSaveDebounceTimer = null;
    }
    if (immediate) {
      this.saveAll(true);
    } else {
      this.autoSaveDebounceTimer = setTimeout(() => {
        this.saveAll(true);
      }, 500);
    }
  }

  public async saveNow(): Promise<boolean> {
    if (this.autoSaveDebounceTimer) {
      clearTimeout(this.autoSaveDebounceTimer);
      this.autoSaveDebounceTimer = null;
    }
    return await this.saveAll(false);
  }

  public async saveAll(isAuto: boolean = false): Promise<boolean> {
    if (this.isSaving) return false;
    this.isSaving = true;
    this.updateAutoSaveBadge('saving');

    try {
      // 1. Sincroniza metadados e configurações globais com o projeto ativo
      const globalDefaults = this.graph.getGlobalDefaults();
      this.projectManager.updateProjectMetadata({
        globalDefaults
      }, this.graph);

      // 2. Salva o projeto e pipeline via ProjectManager
      await this.projectManager.saveProject(this.graph);
      void reindexProjectSearch();

      // 3. Salva também o grafo avulso e estado no localStorage
      const graphJson = this.graph.getRootSchema();
      const graphStr = JSON.stringify(graphJson, null, 2);
      if (typeof localStorage !== 'undefined') {
        try {
          localStorage.setItem('arestrix_standalone_graph', graphStr);
          localStorage.setItem('arestrix_last_saved_graph', graphStr);
          localStorage.setItem('arestrix_global_defaults', JSON.stringify(globalDefaults, null, 2));
          // So referencias ${NOME} sao persistidas: valores literais de segredos jamais chegam ao localStorage.
          const env = sanitizeEnvReferences(this.projectManager.getCurrentProject().environment);
          localStorage.setItem('arestrix_environment_vars', JSON.stringify(env, null, 2));
        } catch (_) {}
      }

      // 4. Emite evento global de salvamento
      globalEventBus.emit('autosave:saved', { isAuto, timestamp: new Date().toISOString() });

      this.updateAutoSaveBadge('saved');
      return true;
    } catch (e) {
      console.warn('[AutoSave] Falha ao executar salvamento:', e);
      this.updateAutoSaveBadge('error');
      return false;
    } finally {
      this.isSaving = false;
    }
  }

  private updateAutoSaveBadge(state: 'saved' | 'saving' | 'error' | 'disabled'): void {
    const badge = this.container.querySelector('#qat-autosave-badge') as HTMLElement;
    const label = this.container.querySelector('#qat-autosave-label') as HTMLElement;
    if (!badge || !label) return;

    if (!this.isAutoSaveEnabled) {
      badge.textContent = '○';
      badge.className = 'office-autosave-badge disabled';
      badge.title = t('ribbon.titlebar.autosave_disabled_badge');
      label.textContent = t('ribbon.titlebar.autosave');
      return;
    }

    if (state === 'saving') {
      badge.textContent = '⏳';
      badge.className = 'office-autosave-badge saving';
      badge.title = t('ribbon.titlebar.autosave_saving_badge');
      label.textContent = t('ribbon.titlebar.autosave_saving_label');
    } else if (state === 'saved') {
      badge.textContent = '✓';
      badge.className = 'office-autosave-badge saved';
      badge.title = t('ribbon.titlebar.autosave_saved_badge');
      label.textContent = t('ribbon.titlebar.autosave');
    } else if (state === 'error') {
      badge.textContent = '⚠️';
      badge.className = 'office-autosave-badge error';
      badge.title = t('ribbon.titlebar.autosave_error_badge');
      label.textContent = t('ribbon.titlebar.autosave_error_label');
    }
  }

  public isRunning(): boolean {
    return this.isPipelineRunning;
  }

  public setExecutionRunning(running: boolean): void {
    this.isPipelineRunning = running;

    const rRun = this.container.querySelector('#ribbon-btn-run') as HTMLButtonElement | null;
    const rRunTab = this.container.querySelector('#ribbon-btn-run-tab') as HTMLButtonElement | null;
    const qatRun = this.container.querySelector('#qat-btn-run') as HTMLButtonElement | null;
    const iconName = running ? 'stop' : 'play';

    const updateLargeRunButton = (button: HTMLButtonElement | null, labelKey: string): void => {
      if (!button) return;
      button.classList.toggle('is-running-stop', running);
      const icon = button.querySelector('.ribbon-btn-icon') as HTMLElement | null;
      const label = button.querySelector('.ribbon-btn-label');
      if (icon) {
        icon.innerHTML = ribbonIcon(iconName, 30);
        icon.setAttribute('data-icon', iconName);
      }
      if (label) label.innerHTML = t(labelKey);
      button.title = t(running ? 'ribbon.panes.stop_run_tip' : 'ribbon.panes.build_run_tip');
    };

    updateLargeRunButton(rRun, running ? 'ribbon.panes.stop_run' : 'ribbon.panes.build_run');
    updateLargeRunButton(rRunTab, running ? 'ribbon.panes.stop_run' : 'ribbon.panes.compile_run');

    if (qatRun) {
      qatRun.classList.toggle('is-running-stop', running);
      qatRun.innerHTML = ribbonIcon(iconName, 18);
      qatRun.setAttribute('data-icon', iconName);
      const title = t(running ? 'ribbon.titlebar.stop' : 'ribbon.titlebar.run');
      qatRun.title = title;
      qatRun.setAttribute('aria-label', title);
    }
  }

  public toggleExecuteOrStop(): void {
    if (this.isPipelineRunning) {
      this.stopPipeline();
    } else {
      this.executePipeline();
    }
  }

  public executePipeline(): void {
    if (this.isPipelineRunning) {
      this.stopPipeline();
      return;
    }
    PipelineRunner.run(this.graph);
  }

  public stopPipeline(): void {
    PipelineRunner.stop();
  }

  public validateGraph(): void {
    const res = GraphValidator.validate(this.graph);
    if (res.isValid) {
      this.notify(t('ribbon.panes.validate_dag'), t('ribbon.alerts.graph_valid'), 'success');
    } else {
      const msg = res.errors.map(err => `[${err.severity.toUpperCase()}] ${err.message}`).join('\n');
      this.notify(t('ribbon.panes.validate_dag'), t('ribbon.alerts.validation_report', { msg }), 'error');
    }
  }

  private bindKeyboardShortcuts(): void {
    window.addEventListener('keydown', (e: KeyboardEvent) => {
      // Ctrl+Shift+S abre a tela "Salvar Como" no Backstage
      if (e.ctrlKey && e.shiftKey && e.key.toLowerCase() === 's') {
        e.preventDefault();
        this.backstageView.open('saveas');
        return;
      }

      // Ctrl+S salva imediatamente todo o projeto e grafo
      if (e.ctrlKey && e.key.toLowerCase() === 's') {
        e.preventDefault();
        this.saveNow();
      }

      // Shift+F5 interrompe; F5 ou Ctrl+Enter alterna entre rodar e parar
      if (e.key === 'F5' && e.shiftKey) {
        e.preventDefault();
        this.stopPipeline();
      } else if (e.key === 'F5' || (e.ctrlKey && e.key === 'Enter')) {
        e.preventDefault();
        this.toggleExecuteOrStop();
      }

      // Ctrl+F1 minimiza/expande a Ribbon (padrão oficial do Microsoft Office!)
      if (e.ctrlKey && e.key === 'F1') {
        e.preventDefault();
        this.toggleCollapse();
      }
    });
  }

  public createNewNode(type: TaskflowNodeType): void {
    const existingCount = this.graph.getAllNodes().length;
    const id = `node_${Date.now()}`;

    const center = this.viewport.getCameraCenter();
    const offset = (existingCount % 6) * 35;
    const x = Math.round((center.x + offset) / 10) * 10;
    const y = Math.round((center.y - offset) / 10) * 10;

    const labelMap: Record<TaskflowNodeType, string> = {
      static: `Agente_Worker_${existingCount + 1}`,
      condition: `Decisor_Stochastic_${existingCount + 1}`,
      subflow: `Dynamic_Cluster_${existingCount + 1}`,
      module: `Module_Pipeline_${existingCount + 1}`,
      critical: `Shared_State_Lock_${existingCount + 1}`,
      cuda: `GPU_Kernel_${existingCount + 1}`,
      agent: `Agente_CodeMode_${existingCount + 1}`,
      fork: `Fork_Dispatcher_${existingCount + 1}`,
      join: `Join_Barrier_${existingCount + 1}`,
      switch: `Switch_${existingCount + 1}`,
      mcp: `MCP_${existingCount + 1}`,
      interaction: `Interacao_${existingCount + 1}`,
      call: `Chamada_${existingCount + 1}`,
      output: `Entrega_Artefato_${existingCount + 1}`
    };

    const node = this.graph.buildNode(id, labelMap[type], type, x, y);
    const cmd = new AddNodeCommand(this.graph, node);
    this.graph.commandManager.execute(cmd);

    this.viewport.selectNode(id);
  }

  public loadDemoComplexSystem(): void {
    this.graph.clear();

    const n1 = this.graph.createNode('node_perceive', '1. Percepcao_Ambiente', 'static', -480, 80);
    const n2 = this.graph.createNode('node_state_lock', '2. Atualizar_Estado_Global', 'critical', -160, 80);
    const n3 = this.graph.createNode('node_evaluator', '3. Avaliacao_Convergencia', 'condition', 160, 80);
    const n4 = this.graph.createNode('node_cluster', '4. Decomposicao_Subflow', 'subflow', 480, 160);
    const n5 = this.graph.createNode('node_cleanup', '5. Modulo_Consolidacao', 'module', 800, 160);

    // Conexoes
    this.graph.createEdge(n1.id, n1.outputs[0].id, n2.id, n2.inputs[0].id);
    this.graph.createEdge(n2.id, n2.outputs[0].id, n3.id, n3.inputs[0].id);

    // Condicao: Branch 0 -> Sucesso (vai para Subflow n4)
    this.graph.createEdge(n3.id, n3.outputs[0].id, n4.id, n4.inputs[0].id);

    // Condicao: Branch 1 -> Feedback Loop! Volta para n1 para nova iteracao
    this.graph.createEdge(n3.id, n3.outputs[1].id, n1.id, n1.inputs[0].id);

    // Subflow -> Modulo final
    this.graph.createEdge(n4.id, n4.outputs[0].id, n5.id, n5.inputs[0].id);

    this.graph.autoLayout(false);

    setTimeout(() => {
      this.viewport.zoomToFit();
    }, 100);
  }

  public async exportGraphvizSVG(openNewTab: boolean = false): Promise<void> {
    if (openNewTab) {
      await this.graphvizExportModal.refreshRender();
      this.graphvizExportModal.openStandaloneTab();
    } else {
      await this.graphvizExportModal.show();
    }
  }

  public getBackstageView(): BackstageView {
    return this.backstageView;
  }

  public openBackstage(section: 'info' | 'new' | 'open' | 'save' | 'saveas' | 'export' | 'options' = 'info'): Promise<void> {
    return this.backstageView.open(section);
  }

  public getSelectedNode(): INodeData | null {
    return this.selectedNode;
  }

  public setSelectedNode(node: INodeData | null): void {
    if (node) {
      if (this.activeTab !== 'contextual') {
        this.previousActiveTab = this.activeTab;
      }
      this.selectedNode = node;
      this.activeTab = 'contextual';
      if (this.isCollapsed) this.isCollapsed = false;
    } else {
      this.selectedNode = null;
      if (this.activeTab === 'contextual') {
        this.activeTab = this.previousActiveTab || 'home';
      }
    }
    this.render();
  }

  private renderContextualTabButton(): string {
    if (!this.selectedNode) return '';
    const category = this.getContextualCategoryLabel();
    const label = this.getContextualTabLabel();
    const isActive = this.activeTab === 'contextual';
    return `
      <button class="office-tab-btn contextual-tab ${isActive ? 'active' : ''}" data-tab="contextual" id="tab-btn-contextual" title="${this.escapeHtml(this.selectedNode.label)}">
        <span class="contextual-tab-pill">${category}</span>
        <span class="contextual-tab-icon">${ribbonIcon(this.getContextualIconName(this.selectedNode.type), 16)}</span>
        <span class="contextual-tab-title">${this.escapeHtml(label)}</span>
      </button>
    `;
  }

  private renderContextualPane(): string {
    if (!this.selectedNode) return '';
    const isActive = this.activeTab === 'contextual';
    const type = this.selectedNode.type;

    return `
      <div class="office-ribbon-pane ${isActive ? 'active' : ''}" data-pane="contextual">
        ${this.renderContextualGroupsForType(type)}
      </div>
    `;
  }

  private getContextualCategoryLabel(): string {
    if (!this.selectedNode) return t('ribbon.contextual.node', 'VÉRTICE');
    const typeMap: Record<TaskflowNodeType, string> = {
      agent: t('ribbon.contextual.cat_agent', 'AGENTE'),
      static: t('ribbon.contextual.cat_static', 'TAREFA'),
      condition: t('ribbon.contextual.cat_condition', 'DECISOR'),
      switch: t('ribbon.contextual.cat_switch', 'SWITCH'),
      mcp: t('ribbon.contextual.cat_mcp', 'MCP'),
      cuda: t('ribbon.contextual.cat_cuda', 'CUDA GPU'),
      subflow: t('ribbon.contextual.cat_subflow', 'SUBFLUXO'),
      module: t('ribbon.contextual.cat_module', 'MÓDULO'),
      critical: t('ribbon.contextual.cat_critical', 'CONCORRÊNCIA'),
      interaction: t('ribbon.contextual.cat_interaction', 'INTERAÇÃO'),
      call: t('ribbon.contextual.cat_call', 'CHAMADA'),
      output: t('ribbon.contextual.cat_output', 'ARTEFATO'),
      fork: t('ribbon.contextual.cat_fork', 'DISPARO'),
      join: t('ribbon.contextual.cat_join', 'BARREIRA')
    };
    return typeMap[this.selectedNode.type] || t('ribbon.contextual.node', 'VÉRTICE');
  }

  private getContextualIconName(type: TaskflowNodeType): RibbonIconName {
    const iconMap: Record<TaskflowNodeType, RibbonIconName> = {
      agent: 'agent',
      static: 'task',
      condition: 'branch',
      switch: 'switch',
      mcp: 'plug',
      cuda: 'bolt',
      subflow: 'subflow',
      module: 'box',
      critical: 'lock',
      interaction: 'chat',
      call: 'phone',
      output: 'output',
      fork: 'branch',
      join: 'users'
    };
    return iconMap[type] || 'task';
  }

  private getContextualTabLabel(): string {
    if (!this.selectedNode) return '';
    return this.selectedNode.label;
  }

  private renderContextualGroupsForType(type: TaskflowNodeType): string {
    if (!this.selectedNode) return '';

    let typeSpecificGroups = '';

    switch (type) {
      case 'agent': {
        const effective = this.effectiveAgentSettings(this.selectedNode);
        const modelName = effective.model.model || '—';
        const isolation = effective.sandbox.isolationLevel;
        const temp = effective.model.temperature ?? 0.7;
        const timeoutSec = Math.round(effective.sandbox.timeoutSeconds ?? 300);

        typeSpecificGroups = `
          <!-- Grupo 1: Cérebro & LLM -->
          <div class="office-ribbon-group">
            <div class="office-ribbon-group-tools">
              <button class="ribbon-btn-large" id="btn-ctx-agent-model" title="Configurar provedor e modelo LLM">
                <span class="ribbon-btn-icon" data-icon="brain">${ribbonIcon('brain', 30)}</span>
                <span class="ribbon-btn-label">Modelo LLM</span>
              </button>
              <div class="ribbon-col-stack">
                <button class="ribbon-btn-small" id="btn-ctx-agent-prompt" title="Abrir instruções e system prompt">
                  <span class="ribbon-sm-icon" data-icon="chat">${ribbonIcon('chat', 16)}</span>
                  <span>Prompt</span>
                </button>
                <button class="ribbon-btn-small" id="btn-ctx-agent-temp" title="Ajustar temperatura efetiva do modelo (${temp})">
                  <span class="ribbon-sm-icon" data-icon="thermometer">${ribbonIcon('thermometer', 16)}</span>
                  <span>Temp: ${temp}</span>
                </button>
              </div>
            </div>
            <div class="office-ribbon-group-title">Cérebro (${this.escapeHtml(modelName)})</div>
          </div>

          <!-- Grupo 2: Isolamento & Sandbox -->
          <div class="office-ribbon-group">
            <div class="office-ribbon-group-tools">
              <button class="ribbon-btn-large" id="btn-ctx-agent-sandbox" title="Configurar nível de isolamento e sandbox">
                <span class="ribbon-btn-icon" data-icon="shield">${ribbonIcon('shield', 30)}</span>
                <span class="ribbon-btn-label">Sandbox VM</span>
              </button>
              <div class="ribbon-col-stack">
                <span class="ribbon-badge-info" title="Isolamento efetivo (${this.isAgentSandboxInherited(this.selectedNode) ? 'herdado dos padrões globais' : 'definido no vértice'})">
                  ${this.escapeHtml(this.getIsolationLabel(isolation))}
                </span>
                <button class="ribbon-btn-small" id="btn-ctx-agent-timeout" title="Tempo limite efetivo em segundos">
                  <span class="ribbon-sm-icon" data-icon="clock">${ribbonIcon('clock', 16)}</span>
                  <span>${timeoutSec}s</span>
                </button>
              </div>
            </div>
            <div class="office-ribbon-group-title">Segurança</div>
          </div>

          <!-- Grupo 3: Ferramentas & Skills -->
          <div class="office-ribbon-group">
            <div class="office-ribbon-group-tools">
              <button class="ribbon-btn-large" id="btn-ctx-agent-tools" title="Atribuir ferramentas MCP, shell e filesystem">
                <span class="ribbon-btn-icon" data-icon="wrench">${ribbonIcon('wrench', 30)}</span>
                <span class="ribbon-btn-label">Ferramentas</span>
              </button>
              <button class="ribbon-btn-large" id="btn-ctx-agent-skills" title="Gerenciar skills especializadas">
                <span class="ribbon-btn-icon" data-icon="book">${ribbonIcon('book', 30)}</span>
                <span class="ribbon-btn-label">Skills</span>
              </button>
              <button class="ribbon-btn-large" id="btn-ctx-agent-skill-editor" title="Editor de skills personalizadas">
                <span class="ribbon-btn-icon" data-icon="pencil">${ribbonIcon('pencil', 30)}</span>
                <span class="ribbon-btn-label">Editor Skill</span>
              </button>
            </div>
            <div class="office-ribbon-group-title">Capacidades</div>
          </div>

          <!-- Grupo 4: Memória & Estado -->
          <div class="office-ribbon-group">
            <div class="office-ribbon-group-tools">
              <button class="ribbon-btn-large" id="btn-ctx-agent-memory" title="Configurar memória semântica e blackboard">
                <span class="ribbon-btn-icon" data-icon="database">${ribbonIcon('database', 30)}</span>
                <span class="ribbon-btn-label">Memória</span>
              </button>
            </div>
            <div class="office-ribbon-group-title">Contexto</div>
          </div>
        `;
        break;
      }

      case 'static': {
        typeSpecificGroups = `
          <!-- Grupo 1: Execução & Script -->
          <div class="office-ribbon-group">
            <div class="office-ribbon-group-tools">
              <button class="ribbon-btn-large" id="btn-ctx-static-code" title="Abrir código C++20 / Script no Monaco Editor">
                <span class="ribbon-btn-icon" data-icon="code">${ribbonIcon('code', 30)}</span>
                <span class="ribbon-btn-label">Ver Código</span>
              </button>
            </div>
            <div class="office-ribbon-group-title">Execução</div>
          </div>
        `;
        break;
      }

      case 'condition': {
        const prob = this.conditionProbability(this.selectedNode);
        const probLabel = prob === null ? 'Código' : `P0: ${prob}%`;
        typeSpecificGroups = `
          <!-- Grupo 1: Regras & Decisão -->
          <div class="office-ribbon-group">
            <div class="office-ribbon-group-tools">
              <button class="ribbon-btn-large" id="btn-ctx-cond-expr" title="Abrir o corpo C++ que decide o ramo">
                <span class="ribbon-btn-icon" data-icon="branch">${ribbonIcon('branch', 30)}</span>
                <span class="ribbon-btn-label">Decisão</span>
              </button>
              <button class="ribbon-btn-large" id="btn-ctx-cond-prob" title="Ajustar a probabilidade do ramo 0 no corpo padrão">
                <span class="ribbon-btn-icon" data-icon="dice">${ribbonIcon('dice', 30)}</span>
                <span class="ribbon-btn-label">${probLabel}</span>
              </button>
            </div>
            <div class="office-ribbon-group-title">Lógica Decisória</div>
          </div>
        `;
        break;
      }

      case 'switch': {
        const switchSettings = this.selectedNode.switchSettings ?? defaultSwitchSettings();
        const mode = this.switchModeLabel(switchSettings.mode || 'code');
        const sourceKind = this.switchSourceLabel(switchSettings.source?.kind || 'previous');
        const caseCount = Math.max(0, this.selectedNode.outputs.length - 1);
        typeSpecificGroups = `
          <!-- Grupo 1: Roteamento Multivia -->
          <div class="office-ribbon-group">
            <div class="office-ribbon-group-tools">
              <button class="ribbon-btn-large" id="btn-ctx-switch-add-case" title="Adicionar um novo caso antes do default">
                <span class="ribbon-btn-icon" data-icon="plus">${ribbonIcon('plus', 30)}</span>
                <span class="ribbon-btn-label">Caso (${caseCount})</span>
              </button>
              <button class="ribbon-btn-large" id="btn-ctx-switch-mode" title="Alternar entre código, palavras e modelo">
                <span class="ribbon-btn-icon" data-icon="switch">${ribbonIcon('switch', 30)}</span>
                <span class="ribbon-btn-label">Modo: ${this.escapeHtml(mode)}</span>
              </button>
              <button class="ribbon-btn-large" id="btn-ctx-switch-source" title="Escolher a origem do texto avaliado">
                <span class="ribbon-btn-icon" data-icon="input">${ribbonIcon('input', 30)}</span>
                <span class="ribbon-btn-label">Fonte: ${this.escapeHtml(sourceKind)}</span>
              </button>
            </div>
            <div class="office-ribbon-group-title">Casos &amp; Rotas</div>
          </div>
          <div class="office-ribbon-group">
            <div class="office-ribbon-group-tools">
              <button class="ribbon-btn-large" id="btn-ctx-switch-rules" title="Editar as palavras de cada caso no Inspetor">
                <span class="ribbon-btn-icon" data-icon="list">${ribbonIcon('list', 30)}</span>
                <span class="ribbon-btn-label">Regras</span>
              </button>
            </div>
            <div class="office-ribbon-group-title">Palavras-chave</div>
          </div>
        `;
        break;
      }

      case 'mcp': {
        typeSpecificGroups = `
          <!-- Grupo 1: Servidor & Tool MCP -->
          <div class="office-ribbon-group">
            <div class="office-ribbon-group-tools">
              <button class="ribbon-btn-large" id="btn-ctx-mcp-servers" title="Gerenciar servidores MCP configurados no projeto">
                <span class="ribbon-btn-icon" data-icon="plug">${ribbonIcon('plug', 30)}</span>
                <span class="ribbon-btn-label">Servidores</span>
              </button>
              <button class="ribbon-btn-large" id="btn-ctx-mcp-tool" title="Configurar ferramenta e argumentos no Inspetor">
                <span class="ribbon-btn-icon" data-icon="wrench">${ribbonIcon('wrench', 30)}</span>
                <span class="ribbon-btn-label">Ferramenta</span>
              </button>
              <button class="ribbon-btn-large" id="btn-ctx-mcp-test" title="Executar a chamada MCP configurada agora">
                <span class="ribbon-btn-icon" data-icon="test">${ribbonIcon('test', 30)}</span>
                <span class="ribbon-btn-label">Testar</span>
              </button>
            </div>
            <div class="office-ribbon-group-title">Protocolo MCP</div>
          </div>
        `;
        break;
      }

      case 'cuda': {
        typeSpecificGroups = `
          <!-- Grupo 1: Kernel & GPU -->
          <div class="office-ribbon-group">
            <div class="office-ribbon-group-tools">
              <button class="ribbon-btn-large" id="btn-ctx-cuda-code" title="Visualizar kernel C++20 com Taskflow CUDA Flow">
                <span class="ribbon-btn-icon" data-icon="bolt">${ribbonIcon('bolt', 30)}</span>
                <span class="ribbon-btn-label">Kernel CUDA</span>
              </button>
            </div>
            <div class="office-ribbon-group-title">Aceleração GPU</div>
          </div>
        `;
        break;
      }

      case 'subflow':
      case 'module': {
        const isMod = type === 'module';
        typeSpecificGroups = `
          <!-- Grupo 1: Hierarquia & Subfluxo -->
          <div class="office-ribbon-group">
            <div class="office-ribbon-group-tools">
              <button class="ribbon-btn-large" id="btn-ctx-subflow-enter" title="Entrar no grafo interno hierárquico">
                <span class="ribbon-btn-icon" data-icon="enter">${ribbonIcon('enter', 30)}</span>
                <span class="ribbon-btn-label">${isMod ? 'Abrir Módulo' : 'Entrar Subfluxo'}</span>
              </button>
            </div>
            <div class="office-ribbon-group-title">Estrutura Hierárquica</div>
          </div>
        `;
        break;
      }

      case 'critical': {
        const key = this.selectedNode.criticalSectionId || 'default';
        const conc = this.criticalConcurrency(this.selectedNode);
        typeSpecificGroups = `
          <!-- Grupo 1: Mutex & Concorrência -->
          <div class="office-ribbon-group">
            <div class="office-ribbon-group-tools">
              <button class="ribbon-btn-large" id="btn-ctx-crit-key" title="Identificador da seção crítica no C++ gerado">
                <span class="ribbon-btn-icon" data-icon="key">${ribbonIcon('key', 30)}</span>
                <span class="ribbon-btn-label">Chave Mutex</span>
              </button>
              <button class="ribbon-btn-large" id="btn-ctx-crit-concurrency" title="Semáforos simultâneos permitidos nesta seção">
                <span class="ribbon-btn-icon" data-icon="users">${ribbonIcon('users', 30)}</span>
                <span class="ribbon-btn-label">Max: ${conc}</span>
              </button>
            </div>
            <div class="office-ribbon-group-title">Sincronização (${this.escapeHtml(key)})</div>
          </div>
        `;
        break;
      }

      case 'interaction': {
        typeSpecificGroups = `
          <!-- Grupo 1: Formulário & Canal -->
          <div class="office-ribbon-group">
            <div class="office-ribbon-group-tools">
              <button class="ribbon-btn-large" id="btn-ctx-inter-preview" title="Abrir pré-visualização do formulário interativo">
                <span class="ribbon-btn-icon" data-icon="eye">${ribbonIcon('eye', 30)}</span>
                <span class="ribbon-btn-label">Pré-visualizar</span>
              </button>
              <button class="ribbon-btn-large" id="btn-ctx-inter-fields" title="Configurar campos e perguntas do formulário">
                <span class="ribbon-btn-icon" data-icon="clipboard">${ribbonIcon('clipboard', 30)}</span>
                <span class="ribbon-btn-label">Campos Form</span>
              </button>
            </div>
            <div class="office-ribbon-group-title">Interação Humana</div>
          </div>
        `;
        break;
      }

      case 'call': {
        const callSettings = this.selectedNode.callSettings ?? defaultCallVertexSettings();
        const srcKind = this.switchSourceLabel(callSettings.source?.kind || 'previous');
        const hasFixedInput = !!callSettings.input?.trim();
        typeSpecificGroups = `
          <!-- Grupo 1: Chamada de Fluxo -->
          <div class="office-ribbon-group">
            <div class="office-ribbon-group-tools">
              <button class="ribbon-btn-large" id="btn-ctx-call-source" title="Escolher origem do input (anterior, arquivo ou nenhum)">
                <span class="ribbon-btn-icon" data-icon="input">${ribbonIcon('input', 30)}</span>
                <span class="ribbon-btn-label">Fonte: ${srcKind}</span>
              </button>
              <button class="ribbon-btn-large" id="btn-ctx-call-input" title="Definir um input fixo para a chamada">
                <span class="ribbon-btn-icon" data-icon="keyboard">${ribbonIcon('keyboard', 30)}</span>
                <span class="ribbon-btn-label">${hasFixedInput ? 'Input fixo ✓' : 'Input fixo'}</span>
              </button>
              <button class="ribbon-btn-large" id="btn-ctx-call-target" title="Inspecionar vértice chamável de destino">
                <span class="ribbon-btn-icon" data-icon="target">${ribbonIcon('target', 30)}</span>
                <span class="ribbon-btn-label">Alvo Chamável</span>
              </button>
            </div>
            <div class="office-ribbon-group-title">Chamada em Fluxo</div>
          </div>
        `;
        break;
      }

      case 'output': {
        const output = this.selectedNode.outputSettings ?? defaultOutputSettings();
        typeSpecificGroups = `
          <!-- Grupo 1: Entrega & Artefatos -->
          <div class="office-ribbon-group">
            <div class="office-ribbon-group-tools">
              <button class="ribbon-btn-large" id="btn-ctx-out-artifacts" title="Abrir painel de artefatos gerados pelo pipeline">
                <span class="ribbon-btn-icon" data-icon="folderOpen">${ribbonIcon('folderOpen', 30)}</span>
                <span class="ribbon-btn-label">Ver Artefatos</span>
              </button>
              <button class="ribbon-btn-large" id="btn-ctx-out-source" title="Arquivo de origem copiado para a entrega">
                <span class="ribbon-btn-icon" data-icon="file">${ribbonIcon('file', 30)}</span>
                <span class="ribbon-btn-label">Origem</span>
              </button>
              <button class="ribbon-btn-large" id="btn-ctx-out-destination" title="Arquivo de destino da entrega">
                <span class="ribbon-btn-icon" data-icon="output">${ribbonIcon('output', 30)}</span>
                <span class="ribbon-btn-label">Destino</span>
              </button>
              <button class="ribbon-btn-large" id="btn-ctx-out-type" title="Formato usado na visualização do artefato">
                <span class="ribbon-btn-icon" data-icon="tag">${ribbonIcon('tag', 30)}</span>
                <span class="ribbon-btn-label">${this.escapeHtml(output.artifactType)}</span>
              </button>
            </div>
            <div class="office-ribbon-group-title">Entrega de Artefatos</div>
          </div>
        `;
        break;
      }

      default: {
        typeSpecificGroups = `
          <!-- Grupo Padrão -->
          <div class="office-ribbon-group">
            <div class="office-ribbon-group-tools">
              <button class="ribbon-btn-large" id="btn-ctx-node-inspect" title="Inspecionar propriedades">
                <span class="ribbon-btn-icon" data-icon="settings">${ribbonIcon('settings', 30)}</span>
                <span class="ribbon-btn-label">Propriedades</span>
              </button>
            </div>
            <div class="office-ribbon-group-title">Configurações</div>
          </div>
        `;
        break;
      }
    }

    // Grupo Universal: Operações do Vértice (Renomear, Duplicar, Inspecionar, Excluir)
    const operationsGroup = `
      <div class="office-ribbon-group">
        <div class="office-ribbon-group-tools">
          <div class="ribbon-col-stack">
            <button class="ribbon-btn-small" id="btn-ctx-node-rename" title="Renomear este vértice">
              <span class="ribbon-sm-icon" data-icon="tag">${ribbonIcon('tag', 16)}</span>
              <span>Renomear</span>
            </button>
            <button class="ribbon-btn-small" id="btn-ctx-node-duplicate" title="Duplicar este vértice com suas configurações">
              <span class="ribbon-sm-icon" data-icon="copy">${ribbonIcon('copy', 16)}</span>
              <span>Duplicar</span>
            </button>
          </div>
          <div class="ribbon-col-stack">
            <button class="ribbon-btn-small" id="btn-ctx-node-inspect" title="Inspecionar todos os detalhes técnicos no painel lateral">
              <span class="ribbon-sm-icon" data-icon="search">${ribbonIcon('search', 16)}</span>
              <span>Inspecionar</span>
            </button>
            <button class="ribbon-btn-small" id="btn-ctx-node-delete" style="color: #dc2626;" title="Excluir este nó e suas conexões">
              <span class="ribbon-sm-icon ribbon-icon-danger" data-icon="trash">${ribbonIcon('trash', 16)}</span>
              <span>Excluir</span>
            </button>
          </div>
        </div>
        <div class="office-ribbon-group-title">Operações</div>
      </div>
    `;

    return typeSpecificGroups + operationsGroup;
  }

  private bindContextualEvents(): void {
    if (!this.selectedNode) return;

    // Ações universais
    this.container.querySelector('#btn-ctx-node-rename')?.addEventListener('click', () => this.renameSelectedNode());
    this.container.querySelector('#btn-ctx-node-duplicate')?.addEventListener('click', () => this.duplicateSelectedNode());
    this.container.querySelector('#btn-ctx-node-delete')?.addEventListener('click', () => this.deleteSelectedNode());
    this.container.querySelector('#btn-ctx-node-inspect')?.addEventListener('click', () => this.inspectSelectedNode());

    // Ações de Agente
    this.container.querySelector('#btn-ctx-agent-model')?.addEventListener('click', () => {
      if (this.selectedNode) globalEventBus.emit('node:open-model-modal', this.selectedNode.id);
    });
    this.container.querySelector('#btn-ctx-agent-prompt')?.addEventListener('click', () => {
      if (this.selectedNode) globalEventBus.emit('node:open-model-modal', { nodeId: this.selectedNode.id, tab: 'prompt' });
    });
    this.container.querySelector('#btn-ctx-agent-sandbox')?.addEventListener('click', () => {
      if (this.selectedNode) globalEventBus.emit('node:open-sandbox-modal', this.selectedNode.id);
    });
    this.container.querySelector('#btn-ctx-agent-tools')?.addEventListener('click', () => {
      if (this.selectedNode) globalEventBus.emit('node:open-tools-modal', this.selectedNode.id);
    });
    this.container.querySelector('#btn-ctx-agent-skills')?.addEventListener('click', () => {
      if (this.selectedNode) globalEventBus.emit('node:open-skills-modal', this.selectedNode.id);
    });
    this.container.querySelector('#btn-ctx-agent-skill-editor')?.addEventListener('click', () => {
      if (this.selectedNode) globalEventBus.emit('node:open-skill-editor', { nodeId: this.selectedNode.id });
    });
    this.container.querySelector('#btn-ctx-agent-memory')?.addEventListener('click', () => {
      if (this.selectedNode) globalEventBus.emit('node:open-memory-modal', this.selectedNode.id);
    });
    this.container.querySelector('#btn-ctx-agent-temp')?.addEventListener('click', () => this.promptChangeTemperature());
    this.container.querySelector('#btn-ctx-agent-timeout')?.addEventListener('click', () => this.promptChangeTimeout());

    // Ações de Static
    this.container.querySelector('#btn-ctx-static-code')?.addEventListener('click', () => this.codeModal.show());

    // Ações de Condition
    this.container.querySelector('#btn-ctx-cond-expr')?.addEventListener('click', () => this.codeModal.show());
    this.container.querySelector('#btn-ctx-cond-prob')?.addEventListener('click', () => this.promptChangeConditionProb());

    // Ações de Switch
    this.container.querySelector('#btn-ctx-switch-add-case')?.addEventListener('click', () => this.addSwitchCase());
    this.container.querySelector('#btn-ctx-switch-mode')?.addEventListener('click', () => this.promptChangeSwitchMode());
    this.container.querySelector('#btn-ctx-switch-source')?.addEventListener('click', () => this.promptChangeSwitchSource());
    this.container.querySelector('#btn-ctx-switch-rules')?.addEventListener('click', () => this.inspectSelectedNode());

    // Ações de MCP
    this.container.querySelector('#btn-ctx-mcp-servers')?.addEventListener('click', () => this.mcpServersModal.show());
    this.container.querySelector('#btn-ctx-mcp-tool')?.addEventListener('click', () => this.inspectSelectedNode());
    this.container.querySelector('#btn-ctx-mcp-test')?.addEventListener('click', () => void this.testMcpTool());

    // Ações de CUDA
    this.container.querySelector('#btn-ctx-cuda-code')?.addEventListener('click', () => this.codeModal.show());

    // Ações de Subflow / Module
    this.container.querySelector('#btn-ctx-subflow-enter')?.addEventListener('click', () => this.enterSubflow());

    // Ações de Critical
    this.container.querySelector('#btn-ctx-crit-key')?.addEventListener('click', () => this.promptChangeCriticalKey());
    this.container.querySelector('#btn-ctx-crit-concurrency')?.addEventListener('click', () => this.promptChangeCriticalConcurrency());

    // Ações de Interaction
    this.container.querySelector('#btn-ctx-inter-preview')?.addEventListener('click', () => this.previewInteraction());
    this.container.querySelector('#btn-ctx-inter-fields')?.addEventListener('click', () => this.inspectSelectedNode());

    // Ações de Call
    this.container.querySelector('#btn-ctx-call-source')?.addEventListener('click', () => this.promptChangeCallSource());
    this.container.querySelector('#btn-ctx-call-input')?.addEventListener('click', () => this.promptChangeCallInput());
    this.container.querySelector('#btn-ctx-call-target')?.addEventListener('click', () => this.inspectSelectedNode());

    // Ações de Output
    this.container.querySelector('#btn-ctx-out-artifacts')?.addEventListener('click', () => globalEventBus.emit('console:open-artifacts'));
    this.container.querySelector('#btn-ctx-out-source')?.addEventListener('click', () => this.promptChangeOutputSource());
    this.container.querySelector('#btn-ctx-out-destination')?.addEventListener('click', () => this.promptChangeOutputDestination());
    this.container.querySelector('#btn-ctx-out-type')?.addEventListener('click', () => this.promptChangeOutputType());
  }

  public renameSelectedNode(): void {
    const node = this.currentNode();
    if (!node) return;
    this.ribbonDialog.openInput({
      title: 'Renomear vértice',
      fields: [{ id: 'name', label: 'Nome do vértice', type: 'text', value: node.label }],
      validate: values => values.name.trim() ? null : 'Informe um nome.',
      onConfirm: values => this.applyRename(node.id, values.name)
    });
  }

  public applyRename(nodeId: string, name: string): boolean {
    const node = this.graph.getNode(nodeId);
    const clean = name.trim();
    if (!node || !clean) return false;
    this.commitNodePatch(node, { label: clean }, `Renomear para "${clean}"`);
    return true;
  }

  public duplicateSelectedNode(): void {
    const node = this.currentNode();
    if (!node) return;
    const copy = cloneNodeData(this.graph, node, t('context_menu.copy_suffix', '(cópia)'));
    this.graph.commandManager.execute(new AddNodeCommand(this.graph, copy));
    this.viewport.selectNode(copy.id);
  }

  public deleteSelectedNode(): void {
    const node = this.currentNode();
    if (!node) return;
    this.ribbonDialog.openConfirm({
      title: 'Excluir vértice',
      message: t('ribbon.contextual.delete_confirm', `Deseja realmente excluir o vértice "${node.label}"?`),
      confirmLabel: 'Excluir',
      danger: true,
      onConfirm: () => this.applyDeleteSelectedNode(node.id)
    });
  }

  public applyDeleteSelectedNode(nodeId?: string): void {
    const id = nodeId || this.selectedNode?.id;
    if (!id || !this.graph.getNode(id)) return;
    this.graph.commandManager.execute(new RemoveNodeCommand(this.graph, id));
    this.selectedNode = null;
    this.activeTab = this.previousActiveTab || 'home';
    this.render();
  }

  public applyClearGraph(): void {
    this.graph.clear();
    this.selectedNode = null;
    this.activeTab = 'home';
    this.render();
  }

  public inspectSelectedNode(): void {
    const node = this.currentNode();
    if (!node) return;
    globalEventBus.emit('node:selected', node);
  }

  public promptChangeTemperature(): void {
    const node = this.currentNode();
    if (!node) return;
    const effective = this.effectiveAgentSettings(node);
    this.ribbonDialog.openInput({
      title: 'Temperatura do modelo',
      message: 'Ao alterar no vértice, o modelo deixa de herdar os padrões globais para esta propriedade.',
      fields: [{
        id: 'temperature',
        label: 'Temperatura (0 a 2)',
        type: 'number',
        value: effective.model.temperature ?? 0.7,
        min: 0,
        max: 2,
        step: 0.05
      }],
      validate: values => {
        const value = Number(values.temperature);
        return Number.isFinite(value) && value >= 0 && value <= 2 ? null : 'Use um valor entre 0 e 2.';
      },
      onConfirm: values => this.applyAgentTemperature(Number(values.temperature))
    });
  }

  public applyAgentTemperature(value: number): boolean {
    const node = this.currentNode();
    if (!node) return false;
    if (!Number.isFinite(value) || value < 0 || value > 2) return false;
    const base = this.agentSettingsBase(node);
    const effective = this.effectiveAgentSettings(node);
    const next: INodeAgentSettings = {
      ...base,
      inheritModel: false,
      model: { ...effective.model, temperature: value }
    };
    this.commitNodePatch(node, { agentSettings: next }, 'Alterar temperatura');
    return true;
  }

  public promptChangeTimeout(): void {
    const node = this.currentNode();
    if (!node) return;
    const effective = this.effectiveAgentSettings(node);
    this.ribbonDialog.openInput({
      title: 'Timeout do sandbox',
      message: 'Ao alterar no vértice, o sandbox deixa de herdar os padrões globais para esta propriedade.',
      fields: [{
        id: 'timeout',
        label: 'Tempo limite (segundos)',
        type: 'number',
        value: Math.round(effective.sandbox.timeoutSeconds ?? 300),
        min: 1,
        max: 86400,
        step: 1
      }],
      validate: values => {
        const value = Number(values.timeout);
        return Number.isInteger(value) && value > 0 ? null : 'Use um inteiro maior que zero.';
      },
      onConfirm: values => this.applyAgentTimeout(Number(values.timeout))
    });
  }

  public applyAgentTimeout(value: number): boolean {
    const node = this.currentNode();
    if (!node) return false;
    if (!Number.isInteger(value) || value < 1 || value > 86400) return false;
    const base = this.agentSettingsBase(node);
    const effective = this.effectiveAgentSettings(node);
    const next: INodeAgentSettings = {
      ...base,
      inheritSandbox: false,
      sandbox: { ...effective.sandbox, timeoutSeconds: value }
    };
    this.commitNodePatch(node, { agentSettings: next }, 'Alterar timeout');
    return true;
  }

  public conditionProbability(node?: INodeData): number | null {
    const current = node ?? this.currentNode();
    if (!current) return null;
    const body = (current.customLambdaBody || '').trim();
    if (!body) return 50;
    const explicit = body.match(/\(\s*std::rand\(\)\s*%\s*100\s*\)\s*<\s*(\d{1,3})\s*\?/);
    if (explicit) {
      const value = Number(explicit[1]);
      return value >= 0 && value <= 100 ? value : null;
    }
    if (/rand\(\)\s*%\s*2/.test(body)) return 50;
    return null;
  }

  public promptChangeConditionProb(): void {
    const node = this.currentNode();
    if (!node) return;
    const current = this.conditionProbability(node);
    if (current === null) {
      this.notify(
        'Probabilidade do ramo 0',
        'O corpo deste decisor já tem código personalizado. Ajuste a probabilidade no editor de código.',
        'warning'
      );
      this.codeModal.show();
      return;
    }
    this.ribbonDialog.openInput({
      title: 'Probabilidade do ramo 0',
      message: 'O corpo C++ será gerado como um sorteio determinístico de 0 a 99.',
      fields: [{
        id: 'probability',
        label: 'Chance do ramo 0 (%)',
        type: 'number',
        value: current,
        min: 0,
        max: 100,
        step: 1
      }],
      validate: values => {
        const value = Number(values.probability);
        return Number.isInteger(value) && value >= 0 && value <= 100 ? null : 'Use um inteiro entre 0 e 100.';
      },
      onConfirm: values => this.applyConditionProbability(Number(values.probability))
    });
  }

  public applyConditionProbability(value: number): boolean {
    const node = this.currentNode();
    if (!node) return false;
    if (!Number.isInteger(value) || value < 0 || value > 100) return false;
    if (this.conditionProbability(node) === null) {
      this.notify(
        'Probabilidade do ramo 0',
        'O corpo deste decisor já tem código personalizado e não foi sobrescrito.',
        'warning'
      );
      return false;
    }
    const body = `// Decisão estocástica: ${value}% de chance para o ramo 0.\nreturn (std::rand() % 100) < ${value} ? 0 : 1;`;
    this.commitNodePatch(node, { customLambdaBody: body }, 'Alterar probabilidade do decisor');
    return true;
  }

  public addSwitchCase(): boolean {
    const node = this.currentNode();
    if (!node || node.type !== 'switch') return false;

    const outputs = deepClone(node.outputs);
    if (outputs.length === 0) return false;
    let defaultIndex = outputs.findIndex(port => port.name === 'default' || port.id.endsWith('_out_default'));
    if (defaultIndex < 0) defaultIndex = outputs.length - 1;
    const defaultPort = outputs[defaultIndex];
    const newBranchIndex = Number.isFinite(defaultPort.conditionBranchIndex)
      ? defaultPort.conditionBranchIndex
      : defaultIndex;

    let caseNumber = Math.max(1, outputs.length - 1);
    let newPortId = `${node.id}_case_${caseNumber}`;
    while (outputs.some(port => port.id === newPortId)) {
      caseNumber += 1;
      newPortId = `${node.id}_case_${caseNumber}`;
    }

    const newPort = {
      id: newPortId,
      name: `Caso ${caseNumber}`,
      direction: 'output' as const,
      type: 'flow' as const,
      conditionBranchIndex: newBranchIndex
    };
    const nextOutputs = [
      ...outputs.slice(0, defaultIndex),
      newPort,
      ...outputs.slice(defaultIndex)
    ].map((port, index) => ({ ...port, conditionBranchIndex: index }));

    const baseSwitch = node.switchSettings ?? defaultSwitchSettings();
    const nextSettings: ISwitchSettings = {
      ...baseSwitch,
      rules: { ...(baseSwitch.rules || {}), [newPortId]: '' }
    };

    this.commitNodePatch(node, { outputs: nextOutputs, switchSettings: nextSettings }, 'Adicionar caso ao switch');
    return true;
  }

  public promptChangeSwitchMode(): void {
    const node = this.currentNode();
    if (!node) return;
    const baseSwitch = node.switchSettings ?? defaultSwitchSettings();
    this.ribbonDialog.openInput({
      title: 'Modo de decisão do Switch',
      message: 'Código usa o corpo C++; palavras usa as regras por caso; modelo consulta a IA local/remota.',
      fields: [{
        id: 'mode',
        label: 'Modo',
        type: 'select',
        value: baseSwitch.mode || 'code',
        options: [
          { value: 'code', label: 'Código C++' },
          { value: 'rules', label: 'Palavras-chave' },
          { value: 'model', label: 'Modelo de IA' }
        ]
      }],
      onConfirm: values => this.applySwitchMode(values.mode as ISwitchSettings['mode'])
    });
  }

  public applySwitchMode(mode: ISwitchSettings['mode']): boolean {
    const node = this.currentNode();
    if (!node) return false;
    if (!['code', 'rules', 'model'].includes(mode)) return false;
    const base = node.switchSettings ?? defaultSwitchSettings();
    this.commitNodePatch(node, { switchSettings: { ...base, mode } }, 'Alterar modo do switch');
    return true;
  }

  public promptChangeSwitchSource(): void {
    const node = this.currentNode();
    if (!node) return;
    const baseSwitch = node.switchSettings ?? defaultSwitchSettings();
    const source = baseSwitch.source ?? { kind: 'previous' as const, path: '' };
    this.ribbonDialog.openInput({
      title: 'Origem do texto do Switch',
      message: 'A origem alimenta as palavras-chave ou o modelo que escolhe o caso.',
      fields: [
        {
          id: 'kind',
          label: 'Origem',
          type: 'select',
          value: source.kind || 'previous',
          options: [
            { value: 'previous', label: 'Saída do vértice anterior' },
            { value: 'file', label: 'Arquivo do projeto' }
          ]
        },
        {
          id: 'path',
          label: 'Caminho do arquivo',
          type: 'text',
          value: source.path || '',
          placeholder: 'workspace/resultado.md',
          help: 'Obrigatório quando a origem é um arquivo.'
        }
      ],
      validate: values => values.kind === 'file' && !values.path.trim() ? 'Informe o arquivo de origem.' : null,
      onConfirm: values => this.applySwitchSource(values.kind as 'previous' | 'file', values.path)
    });
  }

  public applySwitchSource(kind: 'previous' | 'file', path: string = ''): boolean {
    const node = this.currentNode();
    if (!node) return false;
    if (kind !== 'previous' && kind !== 'file') return false;
    const cleanPath = path.trim();
    if (kind === 'file' && !cleanPath) return false;
    const base = node.switchSettings ?? defaultSwitchSettings();
    this.commitNodePatch(
      node,
      { switchSettings: { ...base, source: { kind, path: kind === 'file' ? cleanPath : '' } } },
      'Alterar origem do switch'
    );
    return true;
  }

  public async testMcpTool(): Promise<boolean> {
    const node = this.currentNode();
    if (!node) return false;
    const settings = node.mcpSettings;
    if (!settings?.server || !settings?.tool) {
      this.notify('Teste MCP', 'Configure o servidor e a ferramenta no Inspetor antes de testar.', 'warning');
      this.inspectSelectedNode();
      return false;
    }

    const webui = typeof window !== 'undefined' ? (window as any).webui : undefined;
    if (!webui || typeof webui.call !== 'function') {
      this.notify('Teste MCP', 'A execução real está disponível apenas no aplicativo desktop.', 'warning');
      return false;
    }

    this.notify('Teste MCP', `Executando ${settings.server}/${settings.tool}...`, 'info');
    try {
      const payload = JSON.stringify({
        serverId: settings.server,
        toolName: settings.tool,
        arguments: settings.arguments || {}
      });
      const raw = await webui.call('call_mcp_tool', payload);
      if (!raw) return false;
      const result = JSON.parse(raw) as IMcpCallResult;
      if (result.isError) {
        this.notify(
          'Teste MCP',
          result.errorMessage || result.errorCode || 'A ferramenta retornou erro.',
          'error'
        );
        return false;
      }
      const preview = (result.content || '').slice(0, 280);
      this.notify('Teste MCP', preview || 'Chamada concluída.', 'success');
      return true;
    } catch (error) {
      this.notify('Teste MCP', `Falha na chamada: ${String(error)}`, 'error');
      return false;
    }
  }

  public enterSubflow(): void {
    const node = this.currentNode();
    if (!node) return;
    if (!this.graph.diveIntoSubflow(node.id)) {
      this.notify('Subfluxo', 'Este vértice não é um subfluxo ou módulo navegável.', 'warning');
      return;
    }
    this.selectedNode = null;
    this.activeTab = this.previousActiveTab || 'home';
    this.render();
    try {
      this.viewport.zoomToFit();
    } catch (_) {
      // O viewport pode não existir em testes de unidade.
    }
  }

  public criticalConcurrency(node?: INodeData): number {
    const current = node ?? this.currentNode();
    const raw = current?.parameters?.maxConcurrency;
    const value = typeof raw === 'number' ? raw : Number(raw);
    return Number.isInteger(value) && value >= 1 && value <= 64 ? value : 1;
  }

  public promptChangeCriticalKey(): void {
    const node = this.currentNode();
    if (!node) return;
    this.ribbonDialog.openInput({
      title: 'Chave da seção crítica',
      message: 'Vértices com a mesma chave compartilham o mesmo semáforo no C++ gerado.',
      fields: [{
        id: 'key',
        label: 'Chave (vazia = default)',
        type: 'text',
        value: node.criticalSectionId || 'default',
        placeholder: 'default'
      }],
      onConfirm: values => this.applyCriticalSectionKey(values.key)
    });
  }

  public applyCriticalSectionKey(key: string): boolean {
    const node = this.currentNode();
    if (!node) return false;
    const clean = key.trim() || 'default';
    this.commitNodePatch(node, { criticalSectionId: clean }, 'Alterar chave da seção crítica');
    return true;
  }

  public promptChangeCriticalConcurrency(): void {
    const node = this.currentNode();
    if (!node) return;
    this.ribbonDialog.openInput({
      title: 'Concorrência da seção crítica',
      message: 'O gerador cria um tf::Semaphore por chave. 1 equivale a exclusão mútua.',
      fields: [{
        id: 'concurrency',
        label: 'Aquisições simultâneas (1 a 64)',
        type: 'number',
        value: this.criticalConcurrency(node),
        min: 1,
        max: 64,
        step: 1
      }],
      validate: values => {
        const value = Number(values.concurrency);
        return Number.isInteger(value) && value >= 1 && value <= 64 ? null : 'Use um inteiro entre 1 e 64.';
      },
      onConfirm: values => this.applyCriticalConcurrency(Number(values.concurrency))
    });
  }

  public applyCriticalConcurrency(value: number): boolean {
    const node = this.currentNode();
    if (!node) return false;
    if (!Number.isInteger(value) || value < 1 || value > 64) return false;
    const parameters = { ...(node.parameters || {}), maxConcurrency: value };
    this.commitNodePatch(node, { parameters }, 'Alterar concorrência da seção crítica');
    return true;
  }

  public previewInteraction(): void {
    const node = this.currentNode();
    if (!node) return;
    globalEventBus.emit('pipeline:interaction', {
      requestId: `preview_${Date.now()}`,
      nodeId: node.id,
      nodeLabel: node.label,
      form: node.interactionSettings?.form ?? {
        title: node.label,
        description: 'Pré-visualização do formulário de interação humana.',
        fields: [
          { id: 'f1', type: 'text', label: 'Resposta do Operador', required: true }
        ]
      }
    });
  }

  public promptChangeCallSource(): void {
    const node = this.currentNode();
    if (!node) return;
    const current: ICallVertexSettings = node.callSettings ?? defaultCallVertexSettings();
    const source = current.source ?? { kind: 'previous' as const, path: '' };
    this.ribbonDialog.openInput({
      title: 'Origem do input da chamada',
      message: 'O input fixo, quando preenchido, tem prioridade sobre a origem.',
      fields: [
        {
          id: 'kind',
          label: 'Origem',
          type: 'select',
          value: source.kind || 'previous',
          options: [
            { value: 'previous', label: 'Saída do vértice anterior' },
            { value: 'file', label: 'Arquivo do projeto' },
            { value: 'none', label: 'Nenhuma' }
          ]
        },
        {
          id: 'path',
          label: 'Caminho do arquivo',
          type: 'text',
          value: source.path || '',
          placeholder: 'workspace/entrada.md',
          help: 'Obrigatório quando a origem é um arquivo.'
        }
      ],
      validate: values => values.kind === 'file' && !values.path.trim() ? 'Informe o arquivo de origem.' : null,
      onConfirm: values => this.applyCallSource(values.kind as 'previous' | 'file' | 'none', values.path)
    });
  }

  public applyCallSource(kind: 'previous' | 'file' | 'none', path: string = ''): boolean {
    const node = this.currentNode();
    if (!node) return false;
    if (!['previous', 'file', 'none'].includes(kind)) return false;
    const cleanPath = path.trim();
    if (kind === 'file' && !cleanPath) return false;
    const current: ICallVertexSettings = node.callSettings ?? defaultCallVertexSettings();
    this.commitNodePatch(
      node,
      { callSettings: { ...current, source: { kind, path: kind === 'file' ? cleanPath : '' } } },
      'Alterar origem do input da chamada'
    );
    return true;
  }

  public promptChangeCallInput(): void {
    const node = this.currentNode();
    if (!node) return;
    const current: ICallVertexSettings = node.callSettings ?? defaultCallVertexSettings();
    this.ribbonDialog.openInput({
      title: 'Input fixo da chamada',
      message: 'Quando preenchido, este texto é enviado ao alvo em vez de coletar a origem.',
      fields: [{ id: 'input', label: 'Input fixo', type: 'textarea', value: current.input || '' }],
      onConfirm: values => this.applyCallInput(values.input)
    });
  }

  public applyCallInput(input: string): boolean {
    const node = this.currentNode();
    if (!node) return false;
    const current: ICallVertexSettings = node.callSettings ?? defaultCallVertexSettings();
    this.commitNodePatch(node, { callSettings: { ...current, input } }, 'Alterar input fixo da chamada');
    return true;
  }

  public promptChangeOutputSource(): void {
    const node = this.currentNode();
    if (!node) return;
    const current: IOutputSettings = node.outputSettings ?? defaultOutputSettings();
    this.ribbonDialog.openInput({
      title: 'Arquivo de origem da entrega',
      message: 'O coletor copia este arquivo para o destino quando ele existir.',
      fields: [{
        id: 'path',
        label: 'Origem',
        type: 'text',
        value: current.sourcePath || '',
        placeholder: 'workspace/scene.svg'
      }],
      onConfirm: values => this.applyOutputSource(values.path)
    });
  }

  public applyOutputSource(path: string): boolean {
    const node = this.currentNode();
    if (!node) return false;
    const current: IOutputSettings = node.outputSettings ?? defaultOutputSettings();
    this.commitNodePatch(node, { outputSettings: { ...current, sourcePath: path.trim() } }, 'Alterar origem da entrega');
    return true;
  }

  public promptChangeOutputDestination(): void {
    const node = this.currentNode();
    if (!node) return;
    const current: IOutputSettings = node.outputSettings ?? defaultOutputSettings();
    this.ribbonDialog.openInput({
      title: 'Arquivo de destino da entrega',
      message: 'O destino é criado e recebe uma cópia do artefato de origem.',
      fields: [{
        id: 'path',
        label: 'Destino',
        type: 'text',
        value: current.destinationPath || '',
        placeholder: 'exports/scene.svg'
      }],
      onConfirm: values => this.applyOutputDestination(values.path)
    });
  }

  public applyOutputDestination(path: string): boolean {
    const node = this.currentNode();
    if (!node) return false;
    const current: IOutputSettings = node.outputSettings ?? defaultOutputSettings();
    this.commitNodePatch(node, { outputSettings: { ...current, destinationPath: path.trim() } }, 'Alterar destino da entrega');
    return true;
  }

  public promptChangeOutputType(): void {
    const node = this.currentNode();
    if (!node) return;
    const current: IOutputSettings = node.outputSettings ?? defaultOutputSettings();
    this.ribbonDialog.openInput({
      title: 'Tipo do artefato',
      message: 'O tipo orienta a visualização no painel de artefatos.',
      fields: [{
        id: 'type',
        label: 'Formato',
        type: 'select',
        value: current.artifactType || 'auto',
        options: ['auto', 'svg', 'sheet', 'pdf', 'markdown', 'text', 'json', 'image', 'binary']
          .map(value => ({ value, label: value }))
      }],
      onConfirm: values => this.applyOutputType(values.type as IOutputSettings['artifactType'])
    });
  }

  public applyOutputType(type: IOutputSettings['artifactType']): boolean {
    const node = this.currentNode();
    if (!node) return false;
    const allowed: IOutputSettings['artifactType'][] = ['auto', 'svg', 'sheet', 'pdf', 'markdown', 'text', 'json', 'image', 'binary'];
    if (!allowed.includes(type)) return false;
    const current: IOutputSettings = node.outputSettings ?? defaultOutputSettings();
    this.commitNodePatch(node, { outputSettings: { ...current, artifactType: type } }, 'Alterar tipo da entrega');
    return true;
  }

  private currentNode(): INodeData | null {
    if (!this.selectedNode) return null;
    return this.graph.getNode(this.selectedNode.id) ?? null;
  }

  private agentSettingsBase(node: INodeData): INodeAgentSettings {
    return node.agentSettings ?? {
      inheritSandbox: true,
      inheritModel: true,
      inheritCodeMode: true,
      inheritMemory: true
    };
  }

  private isAgentSandboxInherited(node: INodeData): boolean {
    return this.agentSettingsBase(node).inheritSandbox !== false;
  }

  private effectiveAgentSettings(node: INodeData): { model: IModelConfig; sandbox: ISandboxPolicy } {
    const defaults = this.graph.getGlobalDefaults();
    const settings = this.agentSettingsBase(node);
    const model: IModelConfig = settings.inheritModel === false
      ? { ...defaults.model, ...(settings.model ?? {}) }
      : { ...defaults.model };
    const sandbox: ISandboxPolicy = settings.inheritSandbox === false
      ? { ...defaults.sandbox, ...(settings.sandbox ?? {}) }
      : { ...defaults.sandbox };
    return { model, sandbox };
  }

  private getIsolationLabel(level: string): string {
    switch (level) {
      case 'container': return 'Container';
      case 'microvm': return 'MicroVM';
      case 'host_direct': return 'Host';
      default: return level || '—';
    }
  }

  private switchModeLabel(mode: string): string {
    switch (mode) {
      case 'code': return 'Código';
      case 'rules': return 'Palavras';
      case 'model': return 'Modelo';
      default: return mode || '—';
    }
  }

  private switchSourceLabel(kind: string): string {
    switch (kind) {
      case 'previous': return 'Anterior';
      case 'file': return 'Arquivo';
      case 'none': return 'Nenhuma';
      default: return kind || '—';
    }
  }

  private commitNodePatch(node: INodeData, patch: Partial<INodeData>, description: string): void {
    const current = this.graph.getNode(node.id);
    if (!current) return;
    const oldProps: Partial<INodeData> = {};
    for (const key of Object.keys(patch) as (keyof INodeData)[]) {
      const value = current[key];
      oldProps[key] = (value === undefined ? undefined : deepClone(value)) as never;
    }
    this.graph.commandManager.execute(new UpdateNodeCommand(this.graph, node.id, oldProps, patch, description));
    this.selectedNode = this.graph.getNode(node.id) ?? null;
    this.render();
  }

  private notify(title: string, message: string, type: 'info' | 'success' | 'warning' | 'error' = 'info'): void {
    globalEventBus.emit('notification:show', { title, message, severity: type });
  }

  private escapeHtml(str: string): string {
    return str
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }
}

function deepClone<T>(value: T): T {
  if (value === undefined) return value;
  return JSON.parse(JSON.stringify(value)) as T;
}
