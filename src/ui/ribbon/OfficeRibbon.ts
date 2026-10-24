import { Graph } from '../../core/Graph.ts';
import { GraphValidator } from '../../core/Validator.ts';
import { Viewport } from '../../view/Viewport.ts';
import { CodeModal } from '../CodeModal.ts';
import { TaskflowNodeType } from '../../core/types.ts';
import { PipelineRunner } from '../../core/PipelineRunner.ts';
import { AddNodeCommand } from '../../core/history/GraphCommands.ts';
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

  // Backstage View & Modais de Importação
  private backstageView: BackstageView;
  private importPiModal: ImportPiModal;
  private importClaudeModal: ImportClaudeModal;
  private importOpenAiModal: ImportOpenAiModal;
  private importHermesModal: ImportHermesModal;
  private importExcelModal: ImportExcelModal;
  private gitAccountModal: GitAccountModal;
  private standaloneRunnerModal: StandaloneRunnerModal;

  private activeTab: 'home' | 'insert' | 'agents' | 'connectors' | 'data' | 'run' = 'home';
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
      this.container = document.createElement('header');
      this.container.className = 'office-ribbon-container';
      if (parent && parent.appendChild) {
        parent.appendChild(this.container);
      }
      this.render();
      this.bindKeyboardShortcuts();
      this.bindBusEvents();
    } else {
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
            <div class="office-brand-icon" style="background: #14151A; display: flex; align-items: center; justify-content: center; width: 26px; height: 26px; border-radius: 6px; padding: 2px;">
              <svg viewBox="0 0 124 124" width="18" height="18" fill="none" style="display:block;">
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
            <span style="font-weight: 800; font-family: 'Space Grotesk', sans-serif; letter-spacing: -0.2px;">Arestrix <span style="color: #E8590C; font-size: 11px; font-weight: 700; letter-spacing: 0.8px;">STUDIO</span></span>
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
            <button id="qat-btn-git-sync" class="office-qat-btn" title="${t('git_sync.modal_title', 'Conta Git & Repositório do Projeto')}" style="display: inline-flex; align-items: center; gap: 4px; padding: 2px 8px; border-radius: 4px; margin-left: 2px;">
              <span style="font-size: 13px;">🌿</span>
              <span id="qat-git-status-badge" style="font-size: 11px; font-weight: 700; color: #64748b;">Git</span>
            </button>
            <button id="qat-btn-git-push" class="office-qat-btn" title="${t('git_sync.btn_push_tooltip', 'Sincronizar com GitHub agora (Commit & Push manual)')}" style="display: inline-flex; align-items: center; gap: 3px; padding: 2px 7px; border-radius: 4px; margin-left: 2px; font-size: 11px; font-weight: 600; color: #0f6cbd;">
              <span style="font-size: 12px;">⬆️</span>
              <span>Push</span>
            </button>

            <button id="qat-btn-save" class="office-qat-btn" title="${t('ribbon.titlebar.save_now')}">💾</button>
            <button id="qat-btn-undo" class="office-qat-btn" title="${t('ribbon.titlebar.undo')}" disabled>↶</button>
            <button id="qat-btn-redo" class="office-qat-btn" title="${t('ribbon.titlebar.redo')}" disabled>↷</button>
            <button id="qat-btn-run" class="office-qat-btn ${this.isPipelineRunning ? 'is-running-stop' : ''}" style="${this.isPipelineRunning ? 'color: #dc2626 !important; font-weight: 800;' : 'color: #0f6cbd;'}" title="${this.isPipelineRunning ? t('ribbon.titlebar.stop') : t('ribbon.titlebar.run')}">${this.isPipelineRunning ? '⏹' : '▶'}</button>
          </div>
        </div>

        <div class="office-titlebar-center">
          <button id="qat-btn-project" class="office-project-indicator" title="${t('ribbon.buttons.project_manager_tip')}">
            📁 <span id="office-active-project-name">${this.escapeHtml(projectName)}</span>
          </button>
        </div>

        <div class="office-titlebar-right">
          <div class="office-tellme-box" title="${t('search.tooltip', 'Buscar vértices, skills e pipelines do projeto (Ctrl+K)')}">
            <span style="font-size: 13px; margin-right: 4px; color: #64748b;">💡</span>
            <input type="text" id="office-tellme-input">
          </div>
        </div>
      </div>

      <!-- 2. BARRA DE ABAS DA FAIXA DE OPCOES -->
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
        </nav>

        <div class="office-tabs-actions">
          <button class="office-ribbon-collapse-btn" id="btn-toggle-ribbon" title="${t('ribbon.buttons.collapse_ribbon_tip')}">
            ${this.isCollapsed ? 'ᐯ' : 'ᐱ'}
          </button>
        </div>
      </div>

      <!-- 3. CORPO DA FAIXA DE OPCOES (RIBBON BODY) -->
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
        <!-- Grupo Área de Transferência & Visão -->
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <div class="ribbon-col-stack">
              <button class="ribbon-btn-small" id="ribbon-btn-undo" disabled>
                <span class="ribbon-sm-icon">↶</span> ${t('ribbon.panes.undo')}
              </button>
              <button class="ribbon-btn-small" id="ribbon-btn-redo" disabled>
                <span class="ribbon-sm-icon">↷</span> ${t('ribbon.panes.redo')}
              </button>
            </div>
            <div class="ribbon-col-stack">
              <button class="ribbon-btn-small" id="ribbon-btn-zoom-fit">
                <span class="ribbon-sm-icon">🎯</span> ${t('ribbon.panes.zoom_fit')}
              </button>
              <button class="ribbon-btn-small" id="ribbon-btn-clear" style="color: #ef4444;">
                <span class="ribbon-sm-icon">🗑️</span> ${t('ribbon.panes.clear_graph')}
              </button>
            </div>
            <button class="ribbon-btn-large" id="ribbon-btn-auto-layout" title="${t('ribbon.panes.auto_layout_tip')}">
              <span class="ribbon-btn-icon">✨</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.auto_layout')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.edit_view')}</div>
        </div>

        <!-- Grupo Criação Rápida -->
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-quick-agent" title="${t('ribbon.panes.quick_agent_tip')}">
              <span class="ribbon-btn-icon">🤖</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.quick_agent')}</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-quick-task" title="${t('ribbon.panes.quick_task_tip')}">
              <span class="ribbon-btn-icon">⚡</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.quick_task')}</span>
            </button>
            <div class="ribbon-col-stack">
              <select id="ribbon-select-add-node" class="ribbon-select" title="${t('ribbon.buttons.static_node_tip')}">
                <option value="" disabled selected>${t('ribbon.panes.more_vertices')}</option>
                <option value="static">${t('ribbon.nodes.static')}</option>
                <option value="condition">${t('ribbon.nodes.condition')}</option>
                <option value="switch">${t('ribbon.nodes.switch')}</option>
                <option value="mcp">${t('ribbon.nodes.mcp')}</option>
                <option value="interaction">${t('ribbon.nodes.interaction', 'Interação')}</option>
                <option value="subflow">${t('ribbon.nodes.subflow')}</option>
                <option value="module">${t('ribbon.nodes.module')}</option>
                <option value="critical">${t('ribbon.nodes.critical')}</option>
                <option value="agent">${t('ribbon.nodes.agent')}</option>
                <option value="cuda">${t('ribbon.nodes.cuda')}</option>
              </select>
              <button class="ribbon-btn-small" id="ribbon-btn-quick-templates">
                <span class="ribbon-sm-icon">🧩</span> ${t('ribbon.panes.catalog')}
              </button>
            </div>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.graph_vertices')}</div>
        </div>

        <!-- Grupo Execução Primária -->
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large primary-action ${this.isPipelineRunning ? 'is-running-stop' : ''}" id="ribbon-btn-run" title="${this.isPipelineRunning ? t('ribbon.panes.stop_run_tip') : t('ribbon.panes.build_run_tip')}">
              <span class="ribbon-btn-icon">${this.isPipelineRunning ? '⏹' : '▶'}</span>
              <span class="ribbon-btn-label">${this.isPipelineRunning ? t('ribbon.panes.stop_run') : t('ribbon.panes.build_run')}</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-validate" title="${t('ribbon.panes.validate_dag_tip')}">
              <span class="ribbon-btn-icon">🔍</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.validate_dag')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.execution')}</div>
        </div>

        <!-- Grupo Governança & Workspace -->
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-pipeline-defaults" title="${t('ribbon.panes.global_defaults_tip')}">
              <span class="ribbon-btn-icon">⚙️</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.global_defaults')}</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-project-hub" title="${t('ribbon.panes.project_hub_tip')}">
              <span class="ribbon-btn-icon">📁</span>
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
              <span class="ribbon-btn-icon">⏹️</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.static_task')}</span>
            </button>
            <button class="ribbon-btn-large" data-add="condition">
              <span class="ribbon-btn-icon">🔀</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.condition_branch')}</span>
            </button>
            <button class="ribbon-btn-large" data-add="switch">
              <span class="ribbon-btn-icon">🎛️</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.switch_cases')}</span>
            </button>
            <button class="ribbon-btn-large" data-add="mcp">
              <span class="ribbon-btn-icon">🔌</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.mcp_tool')}</span>
            </button>
            <button class="ribbon-btn-large" data-add="subflow">
              <span class="ribbon-btn-icon">🔄</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.subflow_nested')}</span>
            </button>
            <button class="ribbon-btn-large" data-add="module">
              <span class="ribbon-btn-icon">📦</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.module_encapsulated')}</span>
            </button>
            <button class="ribbon-btn-large" data-add="critical">
              <span class="ribbon-btn-icon">🔒</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.critical_section')}</span>
            </button>
            <button class="ribbon-btn-large" data-add="output" style="border-color: #059669;" title="Vértice coletor de saída e entrega de artefatos">
              <span class="ribbon-btn-icon">📥</span>
              <span class="ribbon-btn-label">Saída / Artefato</span>
            </button>
            <button class="ribbon-btn-large" data-add="interaction" style="border-color: #db2777;" title="Interação humana: formulário, pergunta ou aprovação">
              <span class="ribbon-btn-icon">💬</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.interaction', 'Interação')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.flow_concurrency')}</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" data-add="cuda" style="border-color: #14b8a6;">
              <span class="ribbon-btn-icon">⚡</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.cuda_kernel')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.gpu_accel')}</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" data-add="agent">
              <span class="ribbon-btn-icon">🤖</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.agent_code_mode')}</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-insert-templates">
              <span class="ribbon-btn-icon">🧩</span>
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
              <span class="ribbon-btn-icon">🛡️</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.sandboxes_iso')}</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-tools-code">
              <span class="ribbon-btn-icon">🔧</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.tools_code_mode')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.sec_workspace')}</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-mcp-mgr">
              <span class="ribbon-btn-icon">🔌</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.servers_mcp')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.servers_workspace')}</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-open-templates-tab">
              <span class="ribbon-btn-icon">📚</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.template_library')}</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-github-repos" title="${t('ribbon.panes.github_repos_tip')}">
              <span class="ribbon-btn-icon">🌐</span>
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
            <!-- PI.AI DESTACADO COMO PRIORIDADE DE TESTES DO USUARIO -->
            <button class="ribbon-btn-large highlight-pi" id="ribbon-btn-import-pi" title="${t('ribbon.panes.import_pi_tip')}">
              <span class="ribbon-pill-tag">${t('ribbon.panes.pill_tests')}</span>
              <span class="ribbon-btn-icon" style="color: #0d9488; font-weight: 800;">π</span>
              <span class="ribbon-btn-label" style="font-weight: 800; color: #0f766e;">${t('ribbon.panes.import_pi_btn')}</span>
            </button>

            <!-- CLAUDE -->
            <button class="ribbon-btn-large" id="ribbon-btn-import-claude" title="${t('ribbon.panes.import_claude_tip')}">
              <span class="ribbon-btn-icon" style="color: #8b5cf6;">⚡</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.import_claude_btn')}</span>
            </button>

            <!-- OPENAI -->
            <button class="ribbon-btn-large" id="ribbon-btn-import-openai" title="${t('ribbon.panes.import_openai_tip')}">
              <span class="ribbon-btn-icon" style="color: #10a37f;">🟢</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.import_openai_btn')}</span>
            </button>

            <!-- HERMES -->
            <button class="ribbon-btn-large" id="ribbon-btn-import-hermes" title="${t('ribbon.panes.import_hermes_tip')}">
              <span class="ribbon-btn-icon" style="color: #ec4899;">🔮</span>
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
              <span class="ribbon-btn-icon" style="color: #16a34a;">📊</span>
              <span class="ribbon-btn-label" style="font-weight: 800; color: #15803d;">${t('ribbon.panes.import_excel_btn')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.data_spreadsheets')}</div>
        </div>

        <!-- Grupo Protocolo MCP -->
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-mcp-conn">
              <span class="ribbon-btn-icon">🔌</span>
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
              <span class="ribbon-btn-icon">💾</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.memory_hub')}</span>
            </button>
            <div class="ribbon-col-stack">
              <button class="ribbon-btn-small" id="ribbon-btn-mem-blackboard">
                <span class="ribbon-sm-icon">📋</span> ${t('ribbon.panes.blackboard')}
              </button>
              <button class="ribbon-btn-small" id="ribbon-btn-mem-lessons">
                <span class="ribbon-sm-icon">🧠</span> ${t('ribbon.panes.lessons_learned')}
              </button>
            </div>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.proj_memory_grp')}</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-sqlite-db">
              <span class="ribbon-btn-icon">🗄️</span>
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
              <span class="ribbon-btn-icon">${this.isPipelineRunning ? '⏹' : '▶'}</span>
              <span class="ribbon-btn-label">${this.isPipelineRunning ? t('ribbon.panes.stop_run') : t('ribbon.panes.compile_run')}</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-batch-modal">
              <span class="ribbon-btn-icon">📦</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.batch_resume')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.taskflow_engine')}</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-standalone-runner" title="${t('ribbon.panes.standalone_runner_tip', 'Abrir ambiente do Runner Standalone com telas de Progresso, Diagnóstico e Operação')}">
              <span class="ribbon-btn-icon" style="color: #0284c7;">🚀</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.standalone_runner', 'Runner Standalone')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.distribution_grp', 'Distribuição')}</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-export-code-tab">
              <span class="ribbon-btn-icon">⚙️</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.export_code')}</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-export-svg-tab">
              <span class="ribbon-btn-icon">🖼️</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.export_svg')}</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-open-artifacts-tab" title="Abrir painel de artefatos gerados pelo pipeline (SVGs, planilhas, PDFs, etc.)">
              <span class="ribbon-btn-icon" style="color: #059669;">📂</span>
              <span class="ribbon-btn-label">Ver Artefatos</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.export_grp')}</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-load-demo-tab">
              <span class="ribbon-btn-icon">💡</span>
              <span class="ribbon-btn-label">${t('ribbon.panes.load_demo')}</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">${t('ribbon.panes.examples_grp')}</div>
        </div>
      </div>
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
      if (confirm(t('ribbon.alerts.clear_confirm'))) {
        this.graph.clear();
      }
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
  }

  private switchTab(tab: 'home' | 'insert' | 'agents' | 'connectors' | 'data' | 'run'): void {
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

    if (running) {
      if (rRun) {
        rRun.classList.add('is-running-stop');
        const icon = rRun.querySelector('.ribbon-btn-icon');
        const label = rRun.querySelector('.ribbon-btn-label');
        if (icon) icon.textContent = '⏹';
        if (label) label.innerHTML = t('ribbon.panes.stop_run');
        rRun.title = t('ribbon.panes.stop_run_tip');
      }
      if (rRunTab) {
        rRunTab.classList.add('is-running-stop');
        const icon = rRunTab.querySelector('.ribbon-btn-icon');
        const label = rRunTab.querySelector('.ribbon-btn-label');
        if (icon) icon.textContent = '⏹';
        if (label) label.innerHTML = t('ribbon.panes.stop_run');
        rRunTab.title = t('ribbon.panes.stop_run_tip');
      }
      if (qatRun) {
        qatRun.classList.add('is-running-stop');
        qatRun.textContent = '⏹';
        qatRun.title = t('ribbon.titlebar.stop');
      }
    } else {
      if (rRun) {
        rRun.classList.remove('is-running-stop');
        const icon = rRun.querySelector('.ribbon-btn-icon');
        const label = rRun.querySelector('.ribbon-btn-label');
        if (icon) icon.textContent = '▶';
        if (label) label.innerHTML = t('ribbon.panes.build_run');
        rRun.title = t('ribbon.panes.build_run_tip');
      }
      if (rRunTab) {
        rRunTab.classList.remove('is-running-stop');
        const icon = rRunTab.querySelector('.ribbon-btn-icon');
        const label = rRunTab.querySelector('.ribbon-btn-label');
        if (icon) icon.textContent = '▶';
        if (label) label.innerHTML = t('ribbon.panes.compile_run');
        rRunTab.title = t('ribbon.panes.build_run_tip');
      }
      if (qatRun) {
        qatRun.classList.remove('is-running-stop');
        qatRun.textContent = '▶';
        qatRun.title = t('ribbon.titlebar.run');
      }
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
      if (typeof alert !== 'undefined') alert(t('ribbon.alerts.graph_valid'));
    } else {
      const msg = res.errors.map(err => `[${err.severity.toUpperCase()}] ${err.message}`).join('\n\n');
      if (typeof alert !== 'undefined') alert(t('ribbon.alerts.validation_report', { msg }));
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

  private escapeHtml(str: string): string {
    return str
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }
}
