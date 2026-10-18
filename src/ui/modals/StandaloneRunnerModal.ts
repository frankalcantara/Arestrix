import { BaseModal } from './BaseModal.ts';
import { Graph } from '../../core/Graph.ts';
import { RunnerManager, RunnerStatus } from '../../core/runner/RunnerManager.ts';
import { ProjectManager } from '../../core/ProjectManager.ts';
import { globalEventBus } from '../../core/EventBus.ts';
import { t } from '../../i18n/index.ts';

export type RunnerTab = 'progress' | 'diagnostics' | 'operations' | 'export';

export class StandaloneRunnerModal extends BaseModal {
  private graph: Graph;
  private runnerManager: RunnerManager;
  private projectManager: ProjectManager;

  // Feedback do exportador
  private exportStatusMessage: string = '';
  private exportStatusType: 'info' | 'success' | 'error' = 'info';
  private lastExportResult: any = null;

  constructor(graph: Graph) {
    super({
      id: 'modal-standalone-runner',
      title: t('runner.modal_title', 'Runner Standalone'),
      subtitle: t('runner.modal_subtitle', 'Operação autônoma, diagnóstico em tempo real e entrega de resultados'),
      icon: '🚀',
      minWidth: 840,
      minHeight: 580,
      resizable: true,
      draggable: true,
      tabs: [
        { id: 'progress', label: t('runner.tab_progress', '1. Progresso'), icon: '⏱️' },
        { id: 'diagnostics', label: t('runner.tab_diagnostics', '2. Diagnóstico'), icon: '📊' },
        { id: 'operations', label: t('runner.tab_operations', '3. Operação'), icon: '⚙️' },
        { id: 'export', label: t('runner.tab_export', 'Exportar Executável'), icon: '📦' }
      ],
      defaultTab: 'progress'
    });

    this.graph = graph;
    this.runnerManager = RunnerManager.getInstance(globalEventBus);
    this.projectManager = ProjectManager.getInstance(globalEventBus);

    // Conectar eventos do Runner para re-renderização suave
    globalEventBus.on('runner:state-changed', () => {
      if (this.isOpen) {
        this.updateStateView();
      }
    });

    globalEventBus.on('runner:tick', (data: { elapsedMs: number }) => {
      if (this.isOpen && this.currentTab === 'progress') {
        const timerEl = this.dialog.querySelector('#runner-elapsed-timer');
        if (timerEl) {
          timerEl.textContent = this.formatDuration(data.elapsedMs);
        }
      }
    });

    globalEventBus.on('runner:diagnostics-updated', () => {
      if (this.isOpen && this.currentTab === 'diagnostics') {
        this.updateDiagnosticsView();
      }
    });
  }

  public get currentTab(): RunnerTab {
    return (this.activeTabId || 'progress') as RunnerTab;
  }

  public set currentTab(val: RunnerTab) {
    this.activeTabId = val;
  }

  public override open(initialTab?: RunnerTab): void {
    const cur = this.projectManager.getCurrentProject();
    if (initialTab) {
      this.activeTabId = initialTab;
    } else if (cur.runner?.defaultTab) {
      this.activeTabId = cur.runner.defaultTab as RunnerTab;
    } else {
      this.activeTabId = 'progress';
    }

    this.runnerManager.initGraph(this.graph);
    super.open();
  }

  protected override renderBody(): string {
    const state = this.runnerManager.getState();
    const cur = this.projectManager.getCurrentProject();
    const runnerConfig = cur.runner || {
      title: `${cur.name} Runner`,
      version: cur.version || '1.0.0',
      description: cur.description
    };

    return `
      <div class="standalone-runner-container" style="display: flex; flex-direction: column; height: 100%; box-sizing: border-box; background: #faf9f8; color: #201f1e; font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, sans-serif;">
        
        <!-- BARRA SUPERIOR OPERACIONAL (STATUS & CONTROLES) -->
        <div style="background: #ffffff; border-bottom: 1px solid #edebe9; padding: 12px 18px; display: flex; align-items: center; justify-content: space-between; gap: 14px; flex-shrink: 0;">
          <div style="display: flex; align-items: center; gap: 12px;">
            <div style="width: 36px; height: 36px; border-radius: 4px; background: #0f6cbd; color: #ffffff; display: flex; align-items: center; justify-content: center; font-size: 18px;">
              🚀
            </div>
            <div>
              <div style="display: flex; align-items: center; gap: 8px;">
                <h2 style="font-size: 14px; font-weight: 700; color: #201f1e; margin: 0;">
                  ${this.escapeHtml(runnerConfig.title || cur.name)}
                </h2>
                <span style="background: #edebe9; color: #323130; font-size: 10.5px; font-weight: 600; padding: 1px 6px; border-radius: 3px;">
                  v${this.escapeHtml(runnerConfig.version || '1.0.0')}
                </span>
              </div>
              <p style="font-size: 11px; color: #605e5c; margin: 2px 0 0 0;">
                ${this.escapeHtml(runnerConfig.description || 'Executável do pipeline')}
              </p>
            </div>
          </div>

          <!-- Pílula de Status & Ações Rápidas -->
          <div style="display: flex; align-items: center; gap: 10px;">
            <div id="runner-header-status-pill" style="display: flex; align-items: center; gap: 6px; padding: 4px 10px; border-radius: 12px; font-size: 11px; font-weight: 600; ${this.getStatusPillStyle(state.status)}">
              <span style="font-size: 8px;">●</span>
              <span>${this.getStatusLabel(state.status)}</span>
            </div>

            <!-- Botões Principais de Controle -->
            <button id="btn-runner-start" class="tool-btn btn-action" style="height: 30px; padding: 0 14px; font-size: 11.5px; font-weight: 600; background: #0f6cbd; color: #ffffff; border: none; border-radius: 4px; cursor: pointer; display: flex; align-items: center; gap: 6px;">
              <span>▶</span> ${t('runner.btn_run', 'Executar (F5)')}
            </button>
            <button id="btn-runner-stop" class="tool-btn btn-secondary" style="height: 30px; padding: 0 12px; font-size: 11.5px; font-weight: 600; background: #ffffff; color: #323130; border: 1px solid #d2d0ce; border-radius: 4px; cursor: pointer;">
              <span>⏹</span> ${t('runner.btn_stop', 'Interromper')}
            </button>
            <button id="btn-runner-restart" class="tool-btn btn-secondary" style="height: 30px; padding: 0 10px; font-size: 11.5px; font-weight: 600; background: #ffffff; color: #323130; border: 1px solid #d2d0ce; border-radius: 4px; cursor: pointer;" title="${t('runner.btn_restart_tip', 'Reiniciar execução')}">
              <span>↻</span>
            </button>
          </div>
        </div>

        <!-- CONTEÚDO DA TELA ATIVA -->
        <div id="runner-screen-content" style="flex: 1; overflow-y: auto; padding: 18px; box-sizing: border-box;">
          ${this.renderActiveScreen()}
        </div>
      </div>
    `;
  }

  private renderActiveScreen(): string {
    switch (this.currentTab) {
      case 'progress':
        return this.renderProgressScreen();
      case 'diagnostics':
        return this.renderDiagnosticsScreen();
      case 'operations':
        return this.renderOperationsScreen();
      case 'export':
        return this.renderExportScreen();
      default:
        return this.renderProgressScreen();
    }
  }

  // =========================================================================
  // TELA 1: PROGRESSO
  // =========================================================================
  private renderProgressScreen(): string {
    const state = this.runnerManager.getState();
    const durationStr = this.formatDuration(state.elapsedMs);

    return `
      <div>
        <!-- Card Geral de Progresso -->
        <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 16px; margin-bottom: 16px;">
          <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 10px;">
            <div>
              <strong style="font-size: 13px; font-weight: 700; color: #0f172a;">
                ${this.escapeHtml(state.statusMessage)}
              </strong>
              <div style="font-size: 11px; color: #64748b; margin-top: 2px;">
                Tempo decorrido: <span id="runner-elapsed-timer" style="font-weight: 700; color: #0f172a;">${durationStr}</span>
              </div>
            </div>
            <div style="font-size: 20px; font-weight: 800; color: #0f6cbd;">
              ${state.progressPercent}%
            </div>
          </div>

          <!-- Barra de Progresso Fluida -->
          <div style="width: 100%; height: 10px; background: #e2e8f0; border-radius: 5px; overflow: hidden;">
            <div style="height: 100%; width: ${state.progressPercent}%; background: ${this.getProgressBarColor(state.status)}; transition: width 0.3s ease; border-radius: 5px;"></div>
          </div>
        </div>

        <!-- Lista / Timeline das Etapas do Grafo -->
        <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 16px;">
          <h3 style="font-size: 13px; font-weight: 700; color: #0f172a; margin: 0 0 12px 0;">
            ${t('runner.steps_title', 'Etapas do Pipeline')} (${state.nodes.length})
          </h3>

          <div style="display: flex; flex-direction: column; gap: 8px;">
            ${state.nodes.length === 0 ? `
              <div style="color: #64748b; font-size: 12px; font-style: italic; text-align: center; padding: 20px;">
                ${t('runner.no_nodes', 'Nenhum vértice configurado no pipeline aberto.')}
              </div>
            ` : state.nodes.map((node, idx) => `
              <div style="display: flex; align-items: center; justify-content: space-between; padding: 10px 14px; background: ${this.getNodeRowBackground(node.status)}; border: 1px solid ${this.getNodeRowBorder(node.status)}; border-radius: 6px;">
                <div style="display: flex; align-items: center; gap: 12px;">
                  <span style="font-size: 11px; font-weight: 800; color: #64748b; width: 22px;">#${idx + 1}</span>
                  <div style="width: 28px; height: 28px; border-radius: 6px; background: #e2e8f0; display: flex; align-items: center; justify-content: center; font-size: 14px;">
                    ${this.getNodeIcon(node.type)}
                  </div>
                  <div>
                    <div style="font-size: 12px; font-weight: 700; color: #0f172a;">
                      ${this.escapeHtml(node.label)}
                    </div>
                    <div style="font-size: 10.5px; color: #64748b;">
                      Tipo: ${this.escapeHtml(node.type)} ${node.durationMs ? `• Duração: ${(node.durationMs / 1000).toFixed(2)}s` : ''}
                    </div>
                  </div>
                </div>

                <!-- Status do Nó -->
                <div style="display: flex; align-items: center; gap: 8px;">
                  <span style="font-size: 11px; font-weight: 700; padding: 3px 8px; border-radius: 4px; ${this.getNodeStatusBadgeStyle(node.status)}">
                    ${this.getNodeStatusText(node.status)}
                  </span>
                </div>
              </div>
            `).join('')}
          </div>
        </div>
      </div>
    `;
  }

  // =========================================================================
  // TELA 2: DIAGNÓSTICO
  // =========================================================================
  private renderDiagnosticsScreen(): string {
    const diag = this.runnerManager.getDiagnostics();

    return `
      <div>
        <!-- 4 KPI Cards Modernos -->
        <div style="display: grid; grid-template-columns: repeat(4, 1fr); gap: 12px; margin-bottom: 16px;">
          <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 14px;">
            <div style="font-size: 11px; font-weight: 700; color: #64748b; text-transform: uppercase;">Total de Tokens</div>
            <div style="font-size: 22px; font-weight: 800; color: #0f6cbd; margin-top: 4px;">
              ${diag.totalTokens.toLocaleString()}
            </div>
            <div style="font-size: 10.5px; color: #94a3b8; margin-top: 2px;">
              ${diag.promptTokens.toLocaleString()} prm / ${diag.completionTokens.toLocaleString()} cpl
            </div>
          </div>

          <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 14px;">
            <div style="font-size: 11px; font-weight: 700; color: #64748b; text-transform: uppercase;">Ferramentas Acionadas</div>
            <div style="font-size: 22px; font-weight: 800; color: #059669; margin-top: 4px;">
              ${diag.toolCallsCount}
            </div>
            <div style="font-size: 10.5px; color: #94a3b8; margin-top: 2px;">MCP, SQLite e Nativas</div>
          </div>

          <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 14px;">
            <div style="font-size: 11px; font-weight: 700; color: #64748b; text-transform: uppercase;">Retries 429</div>
            <div style="font-size: 22px; font-weight: 800; color: ${diag.rateLimitRetries > 0 ? '#d97706' : '#64748b'}; margin-top: 4px;">
              ${diag.rateLimitRetries}
            </div>
            <div style="font-size: 10.5px; color: #94a3b8; margin-top: 2px;">Rate limits contornados</div>
          </div>

          <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 14px;">
            <div style="font-size: 11px; font-weight: 700; color: #64748b; text-transform: uppercase;">Falhas / Erros</div>
            <div style="font-size: 22px; font-weight: 800; color: ${diag.errors.length > 0 ? '#dc2626' : '#10b981'}; margin-top: 4px;">
              ${diag.errors.length}
            </div>
            <div style="font-size: 10.5px; color: #94a3b8; margin-top: 2px;">Falhas de runtime</div>
          </div>
        </div>

        <!-- Ações do Diagnóstico -->
        <div style="display: flex; justify-content: flex-end; gap: 8px; margin-bottom: 14px;">
          <button id="btn-copy-diag-json" class="tool-btn" style="height: 30px; padding: 0 12px; font-size: 11px; font-weight: 700; background: #ffffff; border: 1px solid #cbd5e1; border-radius: 4px; cursor: pointer;">
            📋 ${t('runner.btn_copy_diag', 'Copiar Diagnóstico (JSON)')}
          </button>
          <button id="btn-clear-diag" class="tool-btn" style="height: 30px; padding: 0 10px; font-size: 11px; font-weight: 600; background: #ffffff; border: 1px solid #cbd5e1; border-radius: 4px; cursor: pointer; color: #64748b;">
            🗑️ ${t('runner.btn_clear_diag', 'Limpar')}
          </button>
        </div>

        <!-- Log de Thinking e Ferramentas em Duas Colunas -->
        <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 14px;">
          
          <!-- Coluna 1: Deliberações Cognitivas (Thinking) -->
          <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 14px; display: flex; flex-direction: column; max-height: 320px;">
            <h4 style="font-size: 12px; font-weight: 700; color: #0f172a; margin: 0 0 10px 0; display: flex; align-items: center; gap: 6px;">
              <span>🧠</span> ${t('runner.thinking_title', 'Raciocínio dos Agentes (Thinking)')}
            </h4>
            <div style="flex: 1; overflow-y: auto; display: flex; flex-direction: column; gap: 8px;">
              ${diag.thinkingLogs.length === 0 ? `
                <div style="font-size: 11.5px; color: #94a3b8; font-style: italic; padding: 12px; text-align: center;">
                  Nenhum registro de pensamento emitido até o momento.
                </div>
              ` : diag.thinkingLogs.map(item => `
                <div style="background: #f8fafc; border-left: 3px solid #8b5cf6; padding: 8px 10px; border-radius: 0 4px 4px 0;">
                  <div style="display: flex; justify-content: space-between; font-size: 10px; color: #64748b; margin-bottom: 4px;">
                    <strong style="color: #7c3aed;">${this.escapeHtml(item.agentName)}</strong>
                    <span>${this.escapeHtml(item.timestamp)}</span>
                  </div>
                  <div style="font-size: 11px; color: #334155; line-height: 1.4; white-space: pre-wrap; font-family: monospace;">
                    ${this.escapeHtml(item.thought)}
                  </div>
                </div>
              `).join('')}
            </div>
          </div>

          <!-- Coluna 2: Chamadas de Ferramentas (Tool Calls) -->
          <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 14px; display: flex; flex-direction: column; max-height: 320px;">
            <h4 style="font-size: 12px; font-weight: 700; color: #0f172a; margin: 0 0 10px 0; display: flex; align-items: center; gap: 6px;">
              <span>⚡</span> ${t('runner.tool_calls_title', 'Chamadas de Ferramentas (I/O)')}
            </h4>
            <div style="flex: 1; overflow-y: auto; display: flex; flex-direction: column; gap: 8px;">
              ${diag.toolCalls.length === 0 ? `
                <div style="font-size: 11.5px; color: #94a3b8; font-style: italic; padding: 12px; text-align: center;">
                  Nenhuma chamada de ferramenta efetuada ainda.
                </div>
              ` : diag.toolCalls.map(tool => `
                <div style="background: #f8fafc; border: 1px solid #e2e8f0; padding: 8px 10px; border-radius: 4px;">
                  <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 2px;">
                    <strong style="font-size: 11px; color: #0f172a; font-family: monospace;">${this.escapeHtml(tool.toolName)}</strong>
                    <span style="font-size: 9.5px; font-weight: 700; color: ${tool.success ? '#059669' : '#dc2626'};">
                      ${tool.success ? '✓ OK' : '✕ FALHA'}
                    </span>
                  </div>
                  <div style="font-size: 10px; color: #64748b;">
                    ${tool.argsSummary ? this.escapeHtml(tool.argsSummary.substring(0, 80)) : 'sem argumentos'}
                  </div>
                </div>
              `).join('')}
            </div>
          </div>
        </div>
      </div>
    `;
  }

  // =========================================================================
  // TELA 3: OPERAÇÃO
  // =========================================================================
  private renderOperationsScreen(): string {
    const cur = this.projectManager.getCurrentProject();
    const params: any[] = cur.runner?.parameters || [];

    return `
      <div>
        <!-- Painel de Disparo Operacional -->
        <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 16px; margin-bottom: 16px;">
          <h3 style="font-size: 13px; font-weight: 700; color: #0f172a; margin: 0 0 12px 0; display: flex; align-items: center; gap: 8px;">
            <span>🎮</span> ${t('runner.operation_controls', 'Painel de Controle e Disparo')}
          </h3>
          <p style="font-size: 11.5px; color: #64748b; margin: 0 0 14px 0;">
            ${t('runner.operation_desc', 'Inicie a execução do pipeline corporativo ou forneça parâmetros operacionais de entrada antes do processamento.')}
          </p>

          <div style="display: flex; gap: 10px;">
            <button id="btn-op-run-now" class="tool-btn" style="height: 38px; padding: 0 20px; font-size: 12.5px; font-weight: 700; background: #0f6cbd; color: #ffffff; border: none; border-radius: 4px; cursor: pointer; display: flex; align-items: center; gap: 8px;">
              <span>▶</span> Iniciar Execução Completa
            </button>
            <button id="btn-op-stop-now" class="tool-btn" style="height: 38px; padding: 0 16px; font-size: 12px; font-weight: 700; background: #e2e8f0; color: #475569; border: 1px solid #cbd5e1; border-radius: 4px; cursor: pointer;">
              <span>⏹</span> Interromper
            </button>
          </div>
        </div>

        <!-- Parâmetros Operacionais Configuráveis -->
        <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 16px; margin-bottom: 16px;">
          <h3 style="font-size: 13px; font-weight: 700; color: #0f172a; margin: 0 0 12px 0; display: flex; align-items: center; gap: 8px;">
            <span>📋</span> ${t('runner.parameters_title', 'Parâmetros Operacionais de Entrada')}
          </h3>

          ${params.length === 0 ? `
            <div style="color: #64748b; font-size: 11.5px; font-style: italic; background: #f8fafc; border: 1px dashed #cbd5e1; padding: 14px; border-radius: 6px; text-align: center;">
              Este projeto não requer parâmetros manuais adicionais. A execução ocorrerá com os padrões corporativos.
            </div>
          ` : `
            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px;">
              ${params.map(p => `
                <div>
                  <label style="display: block; font-size: 11.5px; font-weight: 600; color: #334155; margin-bottom: 4px;">
                    ${this.escapeHtml(p.label || p.id)}:
                  </label>
                  <input type="text" class="office-input runner-param-input" data-param-id="${this.escapeHtml(p.id)}" value="${this.escapeHtml(this.runnerManager.getParameter(p.id) ?? p.defaultValue ?? '')}" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #cbd5e1; border-radius: 4px; box-sizing: border-box;">
                  ${p.description ? `<span style="display: block; font-size: 10px; color: #64748b; margin-top: 2px;">${this.escapeHtml(p.description)}</span>` : ''}
                </div>
              `).join('')}
            </div>
          `}
        </div>

        <!-- Acesso aos Entregáveis e Artefatos Gerados -->
        <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 16px;">
          <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px;">
            <h3 style="font-size: 13px; font-weight: 700; color: #0f172a; margin: 0; display: flex; align-items: center; gap: 8px;">
              <span>📁</span> ${t('runner.artifacts_title', 'Entregáveis & Artefatos')}
            </h3>
            <button id="btn-op-open-explorer" class="tool-btn" style="height: 28px; padding: 0 10px; font-size: 11px; font-weight: 700; background: #ffffff; border: 1px solid #cbd5e1; border-radius: 4px; cursor: pointer;">
              📂 Abrir Pasta de Saída no Explorer
            </button>
          </div>
          <p style="font-size: 11.5px; color: #64748b; margin: 0 0 12px 0;">
            Os arquivos produzidos pelo pipeline (relatórios, tabelas de dados, vetores SVG e PDFs) ficam armazenados na pasta <code>Output/</code> do projeto.
          </p>
        </div>
      </div>
    `;
  }

  // =========================================================================
  // TELA 4: EXPORTAR EXECUTÁVEL
  // =========================================================================
  private renderExportScreen(): string {
    const cur = this.projectManager.getCurrentProject();
    const runnerConfig = cur.runner || {
      title: `${cur.name} Runner`,
      version: cur.version || '1.0.0',
      description: cur.description || '',
      outputDir: 'dist/runner',
      outputName: `${cur.name.toLowerCase().replace(/[^a-z0-9]/g, '_')}_runner.exe`,
      autoStart: false,
      defaultTab: 'progress',
      allowRestart: true
    };

    return `
      <div>
        <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 16px; margin-bottom: 16px;">
          <h3 style="font-size: 13px; font-weight: 700; color: #0f172a; margin: 0 0 12px 0; display: flex; align-items: center; gap: 8px;">
            <span>📦</span> ${t('runner.export_settings_title', 'Configuração do Executável Corporativo')}
          </h3>
          <p style="font-size: 11.5px; color: #64748b; margin: 0 0 14px 0;">
            Gera um pacote standalone autocontido para distribuição corporativa, com o executável nativo do pipeline, o manifesto de execução e scripts de inicialização direta.
          </p>

          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 14px; margin-bottom: 12px;">
            <div>
              <label style="display: block; font-size: 11.5px; font-weight: 600; color: #334155; margin-bottom: 4px;">
                Título da Aplicação:
              </label>
              <input type="text" id="runner-input-title" class="office-input" value="${this.escapeHtml(runnerConfig.title || '')}" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #cbd5e1; border-radius: 4px; box-sizing: border-box;">
            </div>

            <div>
              <label style="display: block; font-size: 11.5px; font-weight: 600; color: #334155; margin-bottom: 4px;">
                Versão:
              </label>
              <input type="text" id="runner-input-version" class="office-input" value="${this.escapeHtml(runnerConfig.version || '1.0.0')}" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #cbd5e1; border-radius: 4px; box-sizing: border-box;">
            </div>

            <div>
              <label style="display: block; font-size: 11.5px; font-weight: 600; color: #334155; margin-bottom: 4px;">
                Pasta de Saída (Output Dir):
              </label>
              <input type="text" id="runner-input-dir" class="office-input" value="${this.escapeHtml(runnerConfig.outputDir || 'dist/runner')}" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #cbd5e1; border-radius: 4px; box-sizing: border-box;">
            </div>

            <div>
              <label style="display: block; font-size: 11.5px; font-weight: 600; color: #334155; margin-bottom: 4px;">
                Nome do Executável (.exe):
              </label>
              <input type="text" id="runner-input-exe-name" class="office-input" value="${this.escapeHtml(runnerConfig.outputName || 'runner_app.exe')}" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #cbd5e1; border-radius: 4px; box-sizing: border-box;">
            </div>
          </div>

          <div style="display: flex; flex-direction: column; gap: 8px; margin-top: 10px;">
            <label style="display: flex; align-items: center; gap: 8px; font-size: 12px; cursor: pointer;">
              <input type="checkbox" id="runner-chk-autostart" ${runnerConfig.autoStart ? 'checked' : ''}>
              <span>Iniciar execução do pipeline imediatamente ao abrir o Runner</span>
            </label>
            <label style="display: flex; align-items: center; gap: 8px; font-size: 12px; cursor: pointer;">
              <input type="checkbox" id="runner-chk-restart" ${runnerConfig.allowRestart !== false ? 'checked' : ''}>
              <span>Permitir reinicialização manual pelo operador</span>
            </label>
          </div>
        </div>

        <!-- Ação de Exportação & Feedback -->
        <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 8px; padding: 16px;">
          ${this.exportStatusMessage ? `
            <div style="padding: 10px 14px; border-radius: 4px; font-size: 12px; font-weight: 600; margin-bottom: 12px; ${this.getExportMessageStyle()}">
              ${this.escapeHtml(this.exportStatusMessage)}
            </div>
          ` : ''}

          <div style="display: flex; gap: 10px;">
            <button id="btn-export-runner-now" class="tool-btn" style="height: 38px; padding: 0 20px; font-size: 12.5px; font-weight: 700; background: #0f6cbd; color: #ffffff; border: none; border-radius: 4px; cursor: pointer; display: flex; align-items: center; gap: 8px;">
              <span>🚀</span> Gerar Pacote Standalone Agora
            </button>
            <button id="btn-launch-exported-runner" class="tool-btn" style="height: 38px; padding: 0 16px; font-size: 12px; font-weight: 700; background: #e2e8f0; color: #475569; border: 1px solid #cbd5e1; border-radius: 4px; cursor: pointer;" ${!this.lastExportResult ? 'disabled' : ''}>
              <span>📂</span> Abrir Pasta do Executável
            </button>
          </div>
        </div>
      </div>
    `;
  }

  protected override renderFooter(): string {
    return `
      <div style="display: flex; justify-content: flex-end; gap: 10px; width: 100%; padding: 10px 20px;">
        <button id="btn-close-runner-modal" class="tool-btn btn-secondary" style="height: 32px; padding: 0 16px; font-size: 12px; font-weight: 600;">
          ${t('common.close', 'Fechar')}
        </button>
      </div>
    `;
  }

  protected override bindEvents(): void {
    // 1. Alternador de telas (Abas)
    this.dialog.querySelectorAll<HTMLButtonElement>('.runner-nav-tab').forEach(btn => {
      btn.addEventListener('click', () => {
        const tab = btn.dataset.tab as RunnerTab;
        if (tab && tab !== this.currentTab) {
          this.currentTab = tab;
          this.render();
        }
      });
    });

    // 2. Controles de execução na barra superior
    this.dialog.querySelector('#btn-runner-start')?.addEventListener('click', async () => {
      await this.runnerManager.runPipeline(this.graph);
    });
    this.dialog.querySelector('#btn-runner-stop')?.addEventListener('click', async () => {
      await this.runnerManager.stopPipeline();
    });
    this.dialog.querySelector('#btn-runner-restart')?.addEventListener('click', async () => {
      await this.runnerManager.restartPipeline(this.graph);
    });

    // 3. Controles da tela de operação
    this.dialog.querySelector('#btn-op-run-now')?.addEventListener('click', async () => {
      await this.runnerManager.runPipeline(this.graph);
    });
    this.dialog.querySelector('#btn-op-stop-now')?.addEventListener('click', async () => {
      await this.runnerManager.stopPipeline();
    });
    this.dialog.querySelector('#btn-op-open-explorer')?.addEventListener('click', () => {
      // @ts-ignore
      if (typeof webui !== 'undefined') webui.call('open_output_folder', '');
    });

    // Captura inputs de parâmetros operacionais
    this.dialog.querySelectorAll<HTMLInputElement>('.runner-param-input').forEach(inp => {
      inp.addEventListener('input', () => {
        const id = inp.dataset.paramId;
        if (id) {
          this.runnerManager.setParameter(id, inp.value);
        }
      });
    });

    // 4. Controles da tela de diagnóstico
    this.dialog.querySelector('#btn-copy-diag-json')?.addEventListener('click', () => {
      const diag = this.runnerManager.getDiagnostics();
      if (navigator.clipboard) {
        navigator.clipboard.writeText(JSON.stringify(diag, null, 2)).then(() => {
          const btn = this.dialog.querySelector('#btn-copy-diag-json') as HTMLButtonElement;
          if (btn) {
            const old = btn.textContent;
            btn.textContent = '✓ Copiado!';
            setTimeout(() => { btn.textContent = old; }, 1400);
          }
        });
      }
    });

    this.dialog.querySelector('#btn-clear-diag')?.addEventListener('click', () => {
      this.runnerManager.resetDiagnostics();
      this.render();
    });

    // 5. Controles da tela de exportação
    this.dialog.querySelector('#btn-export-runner-now')?.addEventListener('click', async () => {
      await this.handleExportRunner();
    });

    this.dialog.querySelector('#btn-launch-exported-runner')?.addEventListener('click', async () => {
      if (this.lastExportResult?.executablePath) {
        await this.runnerManager.launchStandaloneRunner(this.lastExportResult.executablePath);
      }
    });

    // 6. Fechar modal
    this.dialog.querySelector('#btn-close-runner-modal')?.addEventListener('click', () => {
      this.hide();
    });
  }

  private async handleExportRunner(): Promise<void> {
    const title = (this.dialog.querySelector('#runner-input-title') as HTMLInputElement)?.value.trim();
    const version = (this.dialog.querySelector('#runner-input-version') as HTMLInputElement)?.value.trim();
    const outputDir = (this.dialog.querySelector('#runner-input-dir') as HTMLInputElement)?.value.trim();
    const outputName = (this.dialog.querySelector('#runner-input-exe-name') as HTMLInputElement)?.value.trim();
    const autoStart = (this.dialog.querySelector('#runner-chk-autostart') as HTMLInputElement)?.checked ?? false;
    const allowRestart = (this.dialog.querySelector('#runner-chk-restart') as HTMLInputElement)?.checked ?? true;

    this.exportStatusMessage = 'Compilando e gerando pacote standalone...';
    this.exportStatusType = 'info';
    this.render();

    const res = await this.runnerManager.exportStandaloneRunner(this.graph, {
      title,
      version,
      outputDir,
      outputName,
      autoStart,
      allowRestart,
      defaultTab: 'progress'
    });

    this.lastExportResult = res;
    if (res.ok) {
      this.exportStatusMessage = `Pacote standalone gerado com sucesso em: ${res.outputDir}`;
      this.exportStatusType = 'success';
    } else {
      this.exportStatusMessage = res.error || 'Falha ao exportar executável standalone.';
      this.exportStatusType = 'error';
    }
    this.render();
  }

  private updateStateView(): void {
    if (this.currentTab === 'progress') {
      const container = this.dialog.querySelector('#runner-screen-content');
      if (container) {
        container.innerHTML = this.renderProgressScreen();
      }
    }
    const pill = this.dialog.querySelector('#runner-header-status-pill');
    if (pill) {
      const state = this.runnerManager.getState();
      pill.setAttribute('style', `display: flex; align-items: center; gap: 6px; padding: 4px 10px; border-radius: 20px; font-size: 11px; font-weight: 700; ${this.getStatusPillStyle(state.status)}`);
      pill.innerHTML = `<span style="font-size: 8px;">●</span><span>${this.getStatusLabel(state.status)}</span>`;
    }
  }

  private updateDiagnosticsView(): void {
    const container = this.dialog.querySelector('#runner-screen-content');
    if (container) {
      container.innerHTML = this.renderDiagnosticsScreen();
      this.bindEvents();
    }
  }

  private getStatusPillStyle(status: RunnerStatus): string {
    switch (status) {
      case 'running': return 'background: #eff6ff; color: #1d4ed8; border: 1px solid #bfdbfe;';
      case 'compiling': return 'background: #fffbeb; color: #b45309; border: 1px solid #fde68a;';
      case 'success': return 'background: #f0fdf4; color: #15803d; border: 1px solid #bbf7d0;';
      case 'error': return 'background: #fef2f2; color: #b91c1c; border: 1px solid #fecaca;';
      case 'stopped': return 'background: #f8fafc; color: #64748b; border: 1px solid #e2e8f0;';
      default: return 'background: #f1f5f9; color: #475569; border: 1px solid #cbd5e1;';
    }
  }

  private getStatusLabel(status: RunnerStatus): string {
    switch (status) {
      case 'running': return 'Executando...';
      case 'compiling': return 'Compilando...';
      case 'success': return 'Concluído';
      case 'error': return 'Falha';
      case 'stopped': return 'Interrompido';
      default: return 'Pronto';
    }
  }

  private getProgressBarColor(status: RunnerStatus): string {
    switch (status) {
      case 'success': return '#10b981';
      case 'error': return '#ef4444';
      case 'compiling': return '#f59e0b';
      default: return '#0f6cbd';
    }
  }

  private getNodeRowBackground(status: string): string {
    switch (status) {
      case 'running': return '#f0f9ff';
      case 'success': return '#f0fdf4';
      case 'error': return '#fef2f2';
      default: return '#ffffff';
    }
  }

  private getNodeRowBorder(status: string): string {
    switch (status) {
      case 'running': return '#bae6fd';
      case 'success': return '#bbf7d0';
      case 'error': return '#fecaca';
      default: return '#e2e8f0';
    }
  }

  private getNodeStatusBadgeStyle(status: string): string {
    switch (status) {
      case 'running': return 'background: #0284c7; color: #ffffff;';
      case 'success': return 'background: #16a34a; color: #ffffff;';
      case 'error': return 'background: #dc2626; color: #ffffff;';
      default: return 'background: #f1f5f9; color: #64748b;';
    }
  }

  private getNodeStatusText(status: string): string {
    switch (status) {
      case 'running': return 'Em Execução';
      case 'success': return '✓ Concluído';
      case 'error': return '✕ Erro';
      default: return 'Aguardando';
    }
  }

  private getNodeIcon(type: string): string {
    switch (type?.toLowerCase()) {
      case 'agent': return '🤖';
      case 'condition': return '🔀';
      case 'mcp': return '⚡';
      case 'subflow': return '📦';
      case 'fork': return '🔱';
      case 'join': return '🔗';
      case 'output': return '📤';
      default: return '⚙️';
    }
  }

  private getExportMessageStyle(): string {
    switch (this.exportStatusType) {
      case 'success': return 'background: #f0fdf4; color: #166534; border: 1px solid #bbf7d0;';
      case 'error': return 'background: #fef2f2; color: #991b1b; border: 1px solid #fecaca;';
      default: return 'background: #eff6ff; color: #1e40af; border: 1px solid #bfdbfe;';
    }
  }

  private formatDuration(ms: number): string {
    const sec = Math.floor(ms / 1000);
    const m = Math.floor(sec / 60);
    const s = sec % 60;
    const tenths = Math.floor((ms % 1000) / 100);
    return `${m.toString().padStart(2, '0')}:${s.toString().padStart(2, '0')}.${tenths}`;
  }
}
