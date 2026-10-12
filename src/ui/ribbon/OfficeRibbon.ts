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
import { GraphvizGenerator } from '../../core/GraphvizGenerator.ts';

import { BackstageView } from './BackstageView.ts';
import { ImportPiModal } from '../modals/ImportPiModal.ts';
import { ImportClaudeModal } from '../modals/ImportClaudeModal.ts';
import { ImportOpenAiModal } from '../modals/ImportOpenAiModal.ts';
import { ImportHermesModal } from '../modals/ImportHermesModal.ts';
import { ImportExcelModal } from '../modals/ImportExcelModal.ts';

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

  // Backstage View & Modais de Importação
  private backstageView: BackstageView;
  private importPiModal: ImportPiModal;
  private importClaudeModal: ImportClaudeModal;
  private importOpenAiModal: ImportOpenAiModal;
  private importHermesModal: ImportHermesModal;
  private importExcelModal: ImportExcelModal;

  private activeTab: 'home' | 'insert' | 'agents' | 'connectors' | 'data' | 'run' = 'home';
  private isCollapsed: boolean = false;

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
    const projectName = this.graph.getMetadata().graphName || 'Workspace Padrão';

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
            <div class="office-autosave-group" title="Salvamento Automático: Salva automaticamente todo o projeto, grafo e configurações a cada modificação">
              <label class="office-autosave-switch" for="qat-toggle-autosave">
                <input type="checkbox" id="qat-toggle-autosave" ${this.isAutoSaveEnabled ? 'checked' : ''}>
                <span class="office-autosave-slider"></span>
              </label>
              <span class="office-autosave-label" id="qat-autosave-label">Salvamento Automático</span>
              <span class="office-autosave-badge" id="qat-autosave-badge" title="Todas as alterações foram salvas">✓</span>
            </div>

            <button id="qat-btn-save" class="office-qat-btn" title="Salvar Agora (Ctrl+S)">💾</button>
            <button id="qat-btn-undo" class="office-qat-btn" title="Desfazer (Ctrl+Z)" disabled>↶</button>
            <button id="qat-btn-redo" class="office-qat-btn" title="Refazer (Ctrl+Y)" disabled>↷</button>
            <button id="qat-btn-run" class="office-qat-btn" style="color: #0f6cbd;" title="Compilar & Executar (F5)">▶</button>
          </div>
        </div>

        <div class="office-titlebar-center">
          <button id="qat-btn-project" class="office-project-indicator" title="Gerenciador de Projetos e Workspaces (Fase 14)">
            📁 <span id="office-active-project-name">${this.escapeHtml(projectName)}</span>
          </button>
        </div>

        <div class="office-titlebar-right">
          <div class="office-tellme-box" title="Pesquisar comandos e ações no Arestrix">
            <span style="font-size: 13px; margin-right: 4px; color: #64748b;">💡</span>
            <input type="text" id="office-tellme-input" placeholder="O que você deseja fazer?">
          </div>
        </div>
      </div>

      <!-- 2. BARRA DE ABAS DA FAIXA DE OPCOES -->
      <div class="office-tabs-bar">
        <nav class="office-tabs-nav">
          <button class="office-tab-btn file-tab" id="tab-btn-file" title="Menu Arquivo (Backstage View)">
            Arquivo
          </button>
          <button class="office-tab-btn ${this.activeTab === 'home' ? 'active' : ''}" data-tab="home">
            Página Inicial
          </button>
          <button class="office-tab-btn ${this.activeTab === 'insert' ? 'active' : ''}" data-tab="insert">
            Inserir
          </button>
          <button class="office-tab-btn ${this.activeTab === 'agents' ? 'active' : ''}" data-tab="agents">
            Agentes & Tools
          </button>
          <button class="office-tab-btn ${this.activeTab === 'connectors' ? 'active' : ''}" data-tab="connectors">
            Conectores & Importação
          </button>
          <button class="office-tab-btn ${this.activeTab === 'data' ? 'active' : ''}" data-tab="data">
            Dados & Memória
          </button>
          <button class="office-tab-btn ${this.activeTab === 'run' ? 'active' : ''}" data-tab="run">
            Execução & Lote
          </button>
        </nav>

        <div class="office-tabs-actions">
          <button class="office-ribbon-collapse-btn" id="btn-toggle-ribbon" title="Minimizar a Faixa de Opções (Ctrl+F1)">
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
                <span class="ribbon-sm-icon">↶</span> Desfazer
              </button>
              <button class="ribbon-btn-small" id="ribbon-btn-redo" disabled>
                <span class="ribbon-sm-icon">↷</span> Refazer
              </button>
            </div>
            <div class="ribbon-col-stack">
              <button class="ribbon-btn-small" id="ribbon-btn-zoom-fit">
                <span class="ribbon-sm-icon">🎯</span> Ajustar Vista
              </button>
              <button class="ribbon-btn-small" id="ribbon-btn-clear" style="color: #ef4444;">
                <span class="ribbon-sm-icon">🗑️</span> Limpar Grafo
              </button>
            </div>
          </div>
          <div class="office-ribbon-group-title">Edição & Visão</div>
        </div>

        <!-- Grupo Criação Rápida -->
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-quick-agent" title="Adicionar Agente Code Mode">
              <span class="ribbon-btn-icon">🤖</span>
              <span class="ribbon-btn-label">+ Agente IA</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-quick-task" title="Adicionar Tarefa C++ Estática">
              <span class="ribbon-btn-icon">⚡</span>
              <span class="ribbon-btn-label">+ Tarefa</span>
            </button>
            <div class="ribbon-col-stack">
              <select id="ribbon-select-add-node" class="ribbon-select" title="Adicionar vértice específico">
                <option value="" disabled selected>+ Mais Vértices...</option>
                <option value="static">Estático (Task)</option>
                <option value="condition">Condição (Branch)</option>
                <option value="subflow">Subflow (Aninhado)</option>
                <option value="module">Módulo</option>
                <option value="critical">Crítico (Mutex)</option>
                <option value="agent">Agente (Code Mode)</option>
                <option value="cuda">CUDA (Kernel GPU)</option>
              </select>
              <button class="ribbon-btn-small" id="ribbon-btn-quick-templates">
                <span class="ribbon-sm-icon">🧩</span> Catálogo
              </button>
            </div>
          </div>
          <div class="office-ribbon-group-title">Vértices do Grafo</div>
        </div>

        <!-- Grupo Execução Primária -->
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large primary-action" id="ribbon-btn-run" title="Compila e executa com MSVC C++20 (F5)">
              <span class="ribbon-btn-icon">▶</span>
              <span class="ribbon-btn-label">Build & Run<br>(F5)</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-validate" title="Valida estrutura e ciclos do grafo">
              <span class="ribbon-btn-icon">🔍</span>
              <span class="ribbon-btn-label">Validar<br>DAG</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">Execução</div>
        </div>

        <!-- Grupo Governança & Workspace -->
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-pipeline-defaults" title="Padrões globais de modelo, sandbox e tools">
              <span class="ribbon-btn-icon">⚙️</span>
              <span class="ribbon-btn-label">Padrões<br>Globais</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-project-hub" title="Gerenciador de Workspaces (Fase 14)">
              <span class="ribbon-btn-icon">📁</span>
              <span class="ribbon-btn-label">Hub de<br>Projetos</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">Governança</div>
        </div>
      </div>

      <!-- ABA 2: INSERIR (INSERT) -->
      <div class="office-ribbon-pane ${this.activeTab === 'insert' ? 'active' : ''}" data-pane="insert">
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" data-add="static">
              <span class="ribbon-btn-icon">⏹️</span>
              <span class="ribbon-btn-label">Tarefa<br>Estática</span>
            </button>
            <button class="ribbon-btn-large" data-add="condition">
              <span class="ribbon-btn-icon">🔀</span>
              <span class="ribbon-btn-label">Condição<br>(Branch)</span>
            </button>
            <button class="ribbon-btn-large" data-add="subflow">
              <span class="ribbon-btn-icon">🔄</span>
              <span class="ribbon-btn-label">Subflow<br>Aninhado</span>
            </button>
            <button class="ribbon-btn-large" data-add="module">
              <span class="ribbon-btn-icon">📦</span>
              <span class="ribbon-btn-label">Módulo<br>Encapsulado</span>
            </button>
            <button class="ribbon-btn-large" data-add="critical">
              <span class="ribbon-btn-icon">🔒</span>
              <span class="ribbon-btn-label">Seção<br>Crítica</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">Fluxo & Concorrência</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" data-add="cuda" style="border-color: #14b8a6;">
              <span class="ribbon-btn-icon">⚡</span>
              <span class="ribbon-btn-label">Kernel CUDA<br>(GPU)</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">Aceleração GPU</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" data-add="agent">
              <span class="ribbon-btn-icon">🤖</span>
              <span class="ribbon-btn-label">Agente Code<br>Mode</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-insert-templates">
              <span class="ribbon-btn-icon">🧩</span>
              <span class="ribbon-btn-label">Modelos de<br>Agentes</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">Agentes Cognitivos</div>
        </div>
      </div>

      <!-- ABA 3: AGENTES & TOOLS -->
      <div class="office-ribbon-pane ${this.activeTab === 'agents' ? 'active' : ''}" data-pane="agents">
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-tools-defaults">
              <span class="ribbon-btn-icon">🛡️</span>
              <span class="ribbon-btn-label">Sandboxes &<br>Isolamento</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-tools-code">
              <span class="ribbon-btn-icon">🔧</span>
              <span class="ribbon-btn-label">Ferramentas<br>Code Mode</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">Segurança & Sandbox</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-mcp-mgr">
              <span class="ribbon-btn-icon">🔌</span>
              <span class="ribbon-btn-label">Servidores<br>MCP</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">Protocolo MCP</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-open-templates-tab">
              <span class="ribbon-btn-icon">📚</span>
              <span class="ribbon-btn-label">Biblioteca de<br>Templates</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-github-repos" title="Gerenciar repositórios open source globais do GitHub para o catálogo de Skills">
              <span class="ribbon-btn-icon">🌐</span>
              <span class="ribbon-btn-label">Repositórios<br>GitHub</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">Catálogo & Skills</div>
        </div>
      </div>

      <!-- ABA 4: CONECTORES & IMPORTAÇÃO (PRIORIDADE PI -> CLAUDE -> OPENAI -> HERMES -> EXCEL) -->
      <div class="office-ribbon-pane ${this.activeTab === 'connectors' ? 'active' : ''}" data-pane="connectors">
        <!-- Grupo Assistentes de IA -->
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <!-- PI.AI DESTACADO COMO PRIORIDADE DE TESTES DO USUARIO -->
            <button class="ribbon-btn-large highlight-pi" id="ribbon-btn-import-pi" title="Importar conversas, raciocínio e persona do Pi (Inflection AI) - Prioridade 1 para testes!">
              <span class="ribbon-pill-tag">TESTES</span>
              <span class="ribbon-btn-icon" style="color: #0d9488; font-weight: 800;">π</span>
              <span class="ribbon-btn-label" style="font-weight: 800; color: #0f766e;">Importar do<br>Pi (Pi.ai)</span>
            </button>

            <!-- CLAUDE -->
            <button class="ribbon-btn-large" id="ribbon-btn-import-claude" title="Importar do Anthropic Claude (Sonnet / Opus)">
              <span class="ribbon-btn-icon" style="color: #8b5cf6;">⚡</span>
              <span class="ribbon-btn-label">Importar do<br>Claude</span>
            </button>

            <!-- OPENAI -->
            <button class="ribbon-btn-large" id="ribbon-btn-import-openai" title="Importar do OpenAI (ChatGPT / Assistants API)">
              <span class="ribbon-btn-icon" style="color: #10a37f;">🟢</span>
              <span class="ribbon-btn-label">Importar da<br>OpenAI</span>
            </button>

            <!-- HERMES -->
            <button class="ribbon-btn-large" id="ribbon-btn-import-hermes" title="Importar do Nous Research Hermes">
              <span class="ribbon-btn-icon" style="color: #ec4899;">🔮</span>
              <span class="ribbon-btn-label">Importar do<br>Hermes</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">Assistentes & LLMs</div>
        </div>

        <!-- Grupo Dados Corporativos & Excel -->
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-import-excel" title="Converter planilhas complexas em Bancos de Dados e Redes de Agentes (Fase 20)">
              <span class="ribbon-pill-tag tag-blue">MODERNO</span>
              <span class="ribbon-btn-icon" style="color: #16a34a;">📊</span>
              <span class="ribbon-btn-label" style="font-weight: 800; color: #15803d;">Importar do<br>Excel (.xlsx)</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">Dados & Planilhas</div>
        </div>

        <!-- Grupo Protocolo MCP -->
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-mcp-conn">
              <span class="ribbon-btn-icon">🔌</span>
              <span class="ribbon-btn-label">Conectores<br>MCP</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">Protocolo Aberto</div>
        </div>
      </div>

      <!-- ABA 5: DADOS & MEMORIA -->
      <div class="office-ribbon-pane ${this.activeTab === 'data' ? 'active' : ''}" data-pane="data">
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-memory-hub">
              <span class="ribbon-btn-icon">💾</span>
              <span class="ribbon-btn-label">Painel de<br>Memória</span>
            </button>
            <div class="ribbon-col-stack">
              <button class="ribbon-btn-small" id="ribbon-btn-mem-blackboard">
                <span class="ribbon-sm-icon">📋</span> Blackboard
              </button>
              <button class="ribbon-btn-small" id="ribbon-btn-mem-lessons">
                <span class="ribbon-sm-icon">🧠</span> Lições Aprendidas
              </button>
            </div>
          </div>
          <div class="office-ribbon-group-title">Memória do Workspace</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-sqlite-db">
              <span class="ribbon-btn-icon">🗄️</span>
              <span class="ribbon-btn-label">Banco SQLite<br>do Projeto</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">Persistência Relacional</div>
        </div>
      </div>

      <!-- ABA 6: EXECUCAO & LOTE -->
      <div class="office-ribbon-pane ${this.activeTab === 'run' ? 'active' : ''}" data-pane="run">
        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large primary-action" id="ribbon-btn-run-tab">
              <span class="ribbon-btn-icon">▶</span>
              <span class="ribbon-btn-label">Compilar &<br>Rodar (F5)</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-batch-modal">
              <span class="ribbon-btn-icon">📦</span>
              <span class="ribbon-btn-label">Lote & Resume<br>Engine</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">Motor Taskflow C++20</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-export-code-tab">
              <span class="ribbon-btn-icon">⚙️</span>
              <span class="ribbon-btn-label">Exportar<br>Código C++</span>
            </button>
            <button class="ribbon-btn-large" id="ribbon-btn-export-svg-tab">
              <span class="ribbon-btn-icon">🖼️</span>
              <span class="ribbon-btn-label">Exportar<br>SVG (Graphviz)</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">Exportação</div>
        </div>

        <div class="office-ribbon-group">
          <div class="office-ribbon-group-tools">
            <button class="ribbon-btn-large" id="ribbon-btn-load-demo-tab">
              <span class="ribbon-btn-icon">💡</span>
              <span class="ribbon-btn-label">Carregar Demo<br>Concorrente</span>
            </button>
          </div>
          <div class="office-ribbon-group-title">Exemplos</div>
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

    this.container.querySelector('#qat-btn-save')?.addEventListener('click', async () => {
      await this.saveNow();
    });

    this.container.querySelector('#qat-btn-undo')?.addEventListener('click', () => this.graph.undo());
    this.container.querySelector('#qat-btn-redo')?.addEventListener('click', () => this.graph.redo());
    this.container.querySelector('#qat-btn-run')?.addEventListener('click', () => this.executePipeline());
    this.container.querySelector('#qat-btn-project')?.addEventListener('click', () => this.projectManagerModal.show());

    // 5. Pesquisa Rápida (Tell Me)
    const tellMeInput = this.container.querySelector('#office-tellme-input') as HTMLInputElement;
    tellMeInput?.addEventListener('keydown', (e: KeyboardEvent) => {
      if (e.key === 'Enter') {
        const val = tellMeInput.value.toLowerCase().trim();
        if (val.includes('pi')) {
          this.importPiModal.show();
        } else if (val.includes('excel')) {
          this.importExcelModal.show();
        } else if (val.includes('run') || val.includes('executar') || val.includes('compilar')) {
          this.executePipeline();
        } else if (val.includes('projeto') || val.includes('workspace')) {
          this.projectManagerModal.show();
        } else if (val.includes('mcp')) {
          this.mcpServersModal.show();
        } else if (val.includes('memoria') || val.includes('memória')) {
          this.projectMemoryModal.show();
        } else if (val.includes('lote') || val.includes('batch')) {
          this.batchPipelineModal.open();
        } else if (val.includes('github') || val.includes('repo') || val.includes('repositório') || val.includes('skill')) {
          globalEventBus.emit('modal:open-github-repos');
        } else {
          if (typeof alert !== 'undefined') alert(`Comando "${tellMeInput.value}" localizado! Abrindo opções correspondentes.`);
          this.switchTab('connectors');
        }
        tellMeInput.value = '';
      }
    });

    // 6. Botões do Painel Home
    this.container.querySelector('#ribbon-btn-undo')?.addEventListener('click', () => this.graph.undo());
    this.container.querySelector('#ribbon-btn-redo')?.addEventListener('click', () => this.graph.redo());
    this.container.querySelector('#ribbon-btn-zoom-fit')?.addEventListener('click', () => this.viewport.zoomToFit());
    this.container.querySelector('#ribbon-btn-clear')?.addEventListener('click', () => {
      if (confirm('Deseja realmente limpar todo o fluxo de vértices?')) {
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

    this.container.querySelector('#ribbon-btn-run')?.addEventListener('click', () => this.executePipeline());
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
    this.container.querySelector('#ribbon-btn-run-tab')?.addEventListener('click', () => this.executePipeline());
    this.container.querySelector('#ribbon-btn-batch-modal')?.addEventListener('click', () => this.batchPipelineModal.open());
    this.container.querySelector('#ribbon-btn-export-code-tab')?.addEventListener('click', () => this.codeModal.show());
    this.container.querySelector('#ribbon-btn-export-svg-tab')?.addEventListener('click', () => this.exportGraphvizSVG());
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

      // 3. Salva também o grafo avulso e estado no localStorage
      const graphJson = this.graph.toJSON();
      const graphStr = JSON.stringify(graphJson, null, 2);
      if (typeof localStorage !== 'undefined') {
        try {
          localStorage.setItem('arestrix_standalone_graph', graphStr);
          localStorage.setItem('arestrix_last_saved_graph', graphStr);
          localStorage.setItem('arestrix_global_defaults', JSON.stringify(globalDefaults, null, 2));
          const env = this.projectManager.getCurrentProject().environment || {};
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
      badge.title = 'Salvamento automático desativado';
      label.textContent = 'Salvamento Automático';
      return;
    }

    if (state === 'saving') {
      badge.textContent = '⏳';
      badge.className = 'office-autosave-badge saving';
      badge.title = 'Salvando alterações...';
      label.textContent = 'Salvando...';
    } else if (state === 'saved') {
      badge.textContent = '✓';
      badge.className = 'office-autosave-badge saved';
      badge.title = 'Todas as alterações foram salvas';
      label.textContent = 'Salvamento Automático';
    } else if (state === 'error') {
      badge.textContent = '⚠️';
      badge.className = 'office-autosave-badge error';
      badge.title = 'Erro ao salvar alterações';
      label.textContent = 'Falha ao salvar';
    }
  }

  public executePipeline(): void {
    PipelineRunner.run(this.graph);
  }

  public validateGraph(): void {
    const res = GraphValidator.validate(this.graph);
    if (res.isValid) {
      if (typeof alert !== 'undefined') alert('✅ Grafo 100% Válido!\nTodas as dependências e sentinelas de condição do Taskflow estão corretas.');
    } else {
      const msg = res.errors.map(err => `[${err.severity.toUpperCase()}] ${err.message}`).join('\n\n');
      if (typeof alert !== 'undefined') alert(`⚠️ Relatório de Validação:\n\n${msg}`);
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

      // F5 ou Ctrl+Enter dispara o build e run automático
      if (e.key === 'F5' || (e.ctrlKey && e.key === 'Enter')) {
        e.preventDefault();
        this.executePipeline();
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
      join: `Join_Barrier_${existingCount + 1}`
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

    setTimeout(() => {
      this.viewport.zoomToFit();
    }, 100);
  }

  public exportGraphvizSVG(): void {
    const dotString = GraphvizGenerator.generateDot(this.graph);

    const html = '<!DOCTYPE html>\n' +
'<html>\n' +
'<head>\n' +
'  <meta charset="utf-8">\n' +
'  <title>Arestrix Graphviz Export</title>\n' +
'  <style>\n' +
'    body { margin: 0; padding: 0; background: #f6f8fc; font-family: Inter, sans-serif; display: flex; flex-direction: column; height: 100vh; }\n' +
'    header { background: #ffffff; padding: 16px 24px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); display: flex; justify-content: space-between; align-items: center; }\n' +
'    h1 { margin: 0; font-size: 18px; color: #0f172a; font-weight: 800; }\n' +
'    .btn { background: #0f6cbd; color: white; border: none; padding: 8px 16px; border-radius: 8px; font-weight: 700; cursor: pointer; }\n' +
'    .btn:hover { background: #115ea3; }\n' +
'    #graph-container { flex: 1; overflow: auto; padding: 24px; display: flex; justify-content: center; align-items: center; }\n' +
'    svg { max-width: 100%; max-height: 100%; }\n' +
'  </style>\n' +
'</head>\n' +
'<body>\n' +
'  <header>\n' +
'    <h1>Arestrix Studio - Arquitetura de Grafo Concorrente (Taskflow C++20)</h1>\n' +
'    <button class="btn" id="btn-download">Baixar SVG</button>\n' +
'  </header>\n' +
'  <div id="graph-container">Carregando renderizador Graphviz (WASM)...</div>\n' +
'  \n' +
'  <script src="https://d3js.org/d3.v7.min.js"></script>\n' +
'  <script src="https://unpkg.com/@hpcc-js/wasm@2.13.0/dist/index.min.js"></script>\n' +
'  <script src="https://unpkg.com/d3-graphviz@5.4.0/build/d3-graphviz.js"></script>\n' +
'  \n' +
'  <script>\n' +
'    const dotSrc = `' + dotString.replace(/`/g, '\\`') + '`;\n' +
'    \n' +
'    d3.select("#graph-container")\n' +
'      .graphviz()\n' +
'      .renderDot(dotSrc)\n' +
'      .on("end", function() {\n' +
'        document.getElementById("btn-download").addEventListener("click", () => {\n' +
'          const svgData = document.querySelector("#graph-container svg").outerHTML;\n' +
'          const blob = new Blob([svgData], { type: "image/svg+xml;charset=utf-8" });\n' +
'          const url = URL.createObjectURL(blob);\n' +
'          const a = document.createElement("a");\n' +
'          a.href = url;\n' +
'          a.download = "arestrix_architecture.svg";\n' +
'          a.click();\n' +
'          URL.revokeObjectURL(url);\n' +
'        });\n' +
'      });\n' +
'  </script>\n' +
'</body>\n' +
'</html>';

    const blob = new Blob([html], { type: 'text/html;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    window.open(url, '_blank');
  }

  public getBackstageView(): BackstageView {
    return this.backstageView;
  }

  public openBackstage(section: 'info' | 'new' | 'open' | 'saveas' | 'export' = 'info'): Promise<void> {
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
