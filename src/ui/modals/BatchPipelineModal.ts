import { Graph } from '../../core/Graph.ts';
import { IBatchPipelineConfig, IPipelineCheckpointState, INodeData } from '../../core/types.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';
import { t } from '../../i18n/index.ts';

declare global {
  interface Window {
    get_pipeline_checkpoint?: () => Promise<string>;
    reset_pipeline_checkpoint?: (payload: string) => Promise<boolean>;
  }
}

export class BatchPipelineModal {
  private overlay: HTMLElement;
  private graph: Graph;
  private windowController?: IModalWindowController;
  private activeTab: 'items' | 'serial' | 'matrix' = 'items';
  private config: IBatchPipelineConfig = {
    items: ['ART-001', 'ART-002', 'ART-003'],
    serializedStages: [],
    maxConcurrency: 1,
    cooldownMs: 2000,
    stateFilePath: 'pipeline-state.json',
    autoResume: true
  };
  private checkpointState: IPipelineCheckpointState = {
    pipelineId: 'arestrix-batch',
    lastUpdated: new Date().toISOString(),
    items: {}
  };

  constructor(graph: Graph) {
    this.graph = graph;

    this.overlay = document.createElement('div');
    this.overlay.className = 'modal-overlay hidden';
    document.body.appendChild(this.overlay);

    this.bindEvents();
  }

  public async open(): Promise<void> {
    await this.fetchCheckpointState();
    this.render();
    this.overlay.classList.remove('hidden');
    this.windowController?.bringToFront();
  }

  public hide(): void {
    this.overlay.classList.add('hidden');
  }

  private async fetchCheckpointState(): Promise<void> {
    if (typeof window !== 'undefined' && window.get_pipeline_checkpoint) {
      try {
        const raw = await window.get_pipeline_checkpoint();
        if (raw) {
          this.checkpointState = JSON.parse(raw);
        }
      } catch (err) {
        console.warn('[BatchPipelineModal] Falha ao ler checkpoint do backend C++:', err);
      }
    }
  }

  private async resetCheckpoint(itemId: string = '', stageId: string = ''): Promise<void> {
    if (typeof window !== 'undefined' && window.reset_pipeline_checkpoint) {
      try {
        await window.reset_pipeline_checkpoint(JSON.stringify({ itemId, stageId }));
        await this.fetchCheckpointState();
        this.render();
      } catch (err) {
        console.error('[BatchPipelineModal] Falha ao resetar checkpoint:', err);
      }
    } else {
      if (!itemId) {
        this.checkpointState.items = {};
      } else if (!stageId) {
        delete this.checkpointState.items[itemId];
      } else if (this.checkpointState.items[itemId]?.stages) {
        delete this.checkpointState.items[itemId].stages[stageId];
      }
      this.render();
    }
  }

  private getAgentNodes(): INodeData[] {
    return this.graph.getAllNodes().filter(n => n.type === 'agent');
  }

  private render(): void {
    const agentNodes = this.getAgentNodes();
    const itemsText = this.config.items.join('\n');

    // Identifica nós que são serializados por padrão
    const serializedSet = new Set(this.config.serializedStages);
    agentNodes.forEach(n => {
      if (n.agentSettings?.isSerialAcrossBatch && !serializedSet.has(n.id)) {
        serializedSet.add(n.id);
      }
    });

    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: t('batch_pipeline.title'),
      subtitle: t('batch_pipeline.subtitle', { projectId: this.escapeHtml(this.graph.getProjectId()) }),
      icon: '📦',
      closeBtnId: 'btn-close-batch-modal'
    });

    this.overlay.innerHTML = `
      <div class="modal-dialog batch-pipeline-modal-dialog">
        ${titleBarHtml}
        <div class="modal-tabs" role="tablist">
          <button type="button" class="tab-btn ${this.activeTab === 'items' ? 'active' : ''}" data-tab="items" role="tab" aria-selected="${this.activeTab === 'items'}">
            ${t('batch_pipeline.tab_items')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'serial' ? 'active' : ''}" data-tab="serial" role="tab" aria-selected="${this.activeTab === 'serial'}">
            ${t('batch_pipeline.tab_serial')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'matrix' ? 'active' : ''}" data-tab="matrix" role="tab" aria-selected="${this.activeTab === 'matrix'}">
            ${t('batch_pipeline.tab_matrix')}
          </button>
        </div>
        <div class="modal-body-scrollable">
          <!-- 1. Configuração de Itens e Concorrência -->
          <div id="pane-tab-items" class="tab-pane" style="${this.activeTab === 'items' ? '' : 'display: none;'}">
            <div class="settings-section-card">
              <div class="section-card-header">
                <span class="section-card-icon">📋</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('batch_pipeline.params_title')}</h3>
                  <p class="section-card-desc">${t('batch_pipeline.params_desc')}</p>
                </div>
              </div>

              <div class="form-row-grid">
                <div class="form-group full-width">
                  <label class="form-label" for="batch-items-input">${t('batch_pipeline.items_label')}</label>
                  <textarea id="batch-items-input" class="form-textarea" rows="4" placeholder="ART-001&#10;ART-002&#10;ART-003">${itemsText}</textarea>
                </div>

                <div class="form-group">
                  <label class="form-label" for="batch-concurrency">${t('batch_pipeline.concurrency_label')}</label>
                  <input id="batch-concurrency" type="number" min="1" max="16" value="${this.config.maxConcurrency}" class="form-input" />
                </div>

                <div class="form-group">
                  <label class="form-label" for="batch-cooldown">${t('batch_pipeline.cooldown_label')}</label>
                  <input id="batch-cooldown" type="number" min="0" step="500" value="${this.config.cooldownMs}" class="form-input" />
                </div>

                <div class="form-group full-width">
                  <label class="form-label" for="batch-state-file">${t('batch_pipeline.state_file_label')}</label>
                  <input id="batch-state-file" type="text" value="${this.config.stateFilePath}" class="form-input" />
                </div>

                <div class="form-group full-width">
                  <label class="clean-checkbox-label">
                    <input type="checkbox" id="batch-auto-resume" ${this.config.autoResume ? 'checked' : ''} />
                    <span>${t('batch_pipeline.auto_resume_label')}</span>
                  </label>
                </div>
              </div>
            </div>
          </div>

          <!-- 2. Etapas Sequenciais (Serializadas entre Itens) -->
          <div id="pane-tab-serial" class="tab-pane" style="${this.activeTab === 'serial' ? '' : 'display: none;'}">
            <div class="settings-section-card">
              <div class="section-card-header">
                <span class="section-card-icon">⛓️</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('batch_pipeline.serial_title')}</h3>
                  <p class="section-card-desc">${t('batch_pipeline.serial_desc')}</p>
                </div>
              </div>

              <div class="tools-checkbox-grid">
                ${agentNodes.length === 0 ? `<div class="empty-hint">${t('batch_pipeline.no_agent_nodes')}</div>` : ''}
                ${agentNodes.map(node => {
                  const checked = serializedSet.has(node.id) ? 'checked' : '';
                  return `
                    <label class="tool-checkbox-item">
                      <input type="checkbox" class="serial-stage-cb" data-node-id="${node.id}" ${checked} />
                      <div class="tool-info">
                        <span class="tool-name">${this.escapeHtml(node.label)}</span>
                        <span class="tool-desc">ID: ${node.id}</span>
                      </div>
                    </label>
                  `;
                }).join('')}
              </div>
            </div>
          </div>

          <!-- 3. Matriz de Estado do Checkpoint (Resume Table) -->
          <div id="pane-tab-matrix" class="tab-pane" style="${this.activeTab === 'matrix' ? '' : 'display: none;'}">
            <div class="settings-section-card">
              <div class="section-card-header">
                <span class="section-card-icon">📊</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('batch_pipeline.matrix_title')}</h3>
                  <p class="section-card-desc">${t('batch_pipeline.matrix_desc', { time: new Date(this.checkpointState.lastUpdated || Date.now()).toLocaleTimeString() })}</p>
                </div>
              </div>

              <div class="batch-matrix-wrapper">
                <table class="batch-status-table">
                  <thead>
                    <tr>
                      <th>${t('batch_pipeline.th_item_id')}</th>
                      ${agentNodes.map(n => `<th>${this.escapeHtml(n.label)}</th>`).join('')}
                      <th>${t('batch_pipeline.th_actions')}</th>
                    </tr>
                  </thead>
                  <tbody>
                    ${this.config.items.length === 0 ? `<tr><td colspan="10" class="empty-hint">${t('batch_pipeline.empty_items')}</td></tr>` : ''}
                    ${this.config.items.map(itemId => {
                      const itemData = this.checkpointState.items?.[itemId];
                      return `
                        <tr>
                          <td class="batch-item-cell"><strong>${this.escapeHtml(itemId)}</strong></td>
                          ${agentNodes.map(n => {
                            const stageInfo = itemData?.stages?.[n.id];
                            const status = stageInfo?.status || 'pendente';
                            let badgeClass = 'pill-pending';
                            let badgeText = t('batch_pipeline.status_pending');

                            if (status === 'concluido') {
                              if (stageInfo?.recoveredFromArtifact) {
                                badgeClass = 'pill-cached';
                                badgeText = t('batch_pipeline.status_cached');
                              } else {
                                badgeClass = 'pill-completed';
                                badgeText = t('batch_pipeline.status_ok');
                              }
                            } else if (status === 'executando') {
                              badgeClass = 'pill-running';
                              badgeText = t('batch_pipeline.status_running');
                            } else if (status === 'erro') {
                              badgeClass = 'pill-error';
                              badgeText = t('batch_pipeline.status_error');
                            }

                            return `
                              <td class="batch-stage-cell">
                                <span class="pill-badge ${badgeClass}">${badgeText}</span>
                              </td>
                            `;
                          }).join('')}
                          <td class="batch-actions-cell">
                            <button class="batch-item-reset-btn" data-item-id="${itemId}" title="${t('batch_pipeline.btn_reset_item_tip')}">Reset</button>
                          </td>
                        </tr>
                      `;
                    }).join('')}
                  </tbody>
                </table>
              </div>
            </div>
          </div>
        </div>

        <div class="modal-footer office-modal-footer">
          <div class="footer-left-group">
            <button id="btn-batch-refresh" class="btn-clean-secondary btn-secondary-office">${t('batch_pipeline.btn_refresh')}</button>
            <button id="btn-batch-clear-all" class="btn-clean-danger">${t('batch_pipeline.btn_clear_all')}</button>
          </div>
          <div class="footer-right-group">
            <button id="btn-batch-close" class="btn-clean-secondary btn-secondary-office">${t('batch_pipeline.btn_close')}</button>
            <button id="btn-batch-save-run" class="btn-clean-primary btn-primary-office">${t('batch_pipeline.btn_save_run')}</button>
          </div>
        </div>
      </div>
    `;

    const dialog = this.overlay.querySelector('.modal-dialog') as HTMLElement;
    if (dialog) {
      this.windowController = ModalWindowManager.attach({
        dialog,
        minWidth: 600,
        minHeight: 450,
        onClose: () => this.hide()
      });
    }

    this.bindDialogEvents();
  }

  private bindEvents(): void {
    this.overlay.addEventListener('click', (e) => {
      if (e.target === this.overlay) {
        this.hide();
      }
    });
  }

  private bindDialogEvents(): void {
    // Tab switching
    this.overlay.querySelectorAll('.modal-tabs .tab-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        const tab = (btn as HTMLElement).getAttribute('data-tab') as 'items' | 'serial' | 'matrix';
        if (!tab) return;
        this.activeTab = tab;
        this.overlay.querySelectorAll('.modal-tabs .tab-btn').forEach(b => {
          const isCurrent = b.getAttribute('data-tab') === tab;
          b.classList.toggle('active', isCurrent);
          b.setAttribute('aria-selected', isCurrent ? 'true' : 'false');
        });
        const panes = this.overlay.querySelectorAll('.tab-pane');
        panes.forEach(p => {
          const el = p as HTMLElement;
          el.style.display = el.id === `pane-tab-${tab}` ? '' : 'none';
        });
      });
    });

    const btnClose = this.overlay.querySelector('#btn-close-batch-modal');
    const btnCloseBottom = this.overlay.querySelector('#btn-batch-close');
    btnClose?.addEventListener('click', () => this.hide());
    btnCloseBottom?.addEventListener('click', () => this.hide());

    const btnRefresh = this.overlay.querySelector('#btn-batch-refresh');
    btnRefresh?.addEventListener('click', async () => {
      await this.fetchCheckpointState();
      this.render();
    });

    const btnClearAll = this.overlay.querySelector('#btn-batch-clear-all');
    btnClearAll?.addEventListener('click', async () => {
      if (confirm(t('batch_pipeline.confirm_clear_all'))) {
        await this.resetCheckpoint('', '');
      }
    });

    // Reset por item
    this.overlay.querySelectorAll('.batch-item-reset-btn').forEach(btn => {
      btn.addEventListener('click', async (e) => {
        const itemId = (e.currentTarget as HTMLElement).getAttribute('data-item-id');
        if (itemId) {
          await this.resetCheckpoint(itemId, '');
        }
      });
    });

    // Inputs e serial checkboxes
    const textarea = this.overlay.querySelector('#batch-items-input') as HTMLTextAreaElement;
    textarea?.addEventListener('input', () => {
      const raw = textarea.value;
      this.config.items = raw
        .split(/[\n,]/)
        .map(s => s.trim())
        .filter(s => s.length > 0);
    });

    const concurrencyInput = this.overlay.querySelector('#batch-concurrency') as HTMLInputElement;
    concurrencyInput?.addEventListener('change', () => {
      this.config.maxConcurrency = parseInt(concurrencyInput.value, 10) || 1;
    });

    const cooldownInput = this.overlay.querySelector('#batch-cooldown') as HTMLInputElement;
    cooldownInput?.addEventListener('change', () => {
      this.config.cooldownMs = parseInt(cooldownInput.value, 10) || 0;
    });

    const stateFileInput = this.overlay.querySelector('#batch-state-file') as HTMLInputElement;
    stateFileInput?.addEventListener('change', () => {
      this.config.stateFilePath = stateFileInput.value.trim() || 'pipeline-state.json';
    });

    const autoResumeCb = this.overlay.querySelector('#batch-auto-resume') as HTMLInputElement;
    autoResumeCb?.addEventListener('change', () => {
      this.config.autoResume = autoResumeCb.checked;
    });

    const btnSaveRun = this.overlay.querySelector('#btn-batch-save-run');
    btnSaveRun?.addEventListener('click', () => {
      // Coleta estágios serializados selecionados
      const serialStages: string[] = [];
      this.overlay.querySelectorAll('.serial-stage-cb:checked').forEach(cb => {
        const nodeId = cb.getAttribute('data-node-id');
        if (nodeId) serialStages.push(nodeId);
      });
      this.config.serializedStages = serialStages;

      console.log('[BatchPipelineModal] Configuração salva para execução em lote:', this.config);
      this.hide();

      // Dispara evento de execução em lote para ouvintes da aplicação
      window.dispatchEvent(new CustomEvent('arestrix:batch-run', { detail: this.config }));
      window.dispatchEvent(new CustomEvent('taskforge:batch-run', { detail: this.config }));
    });
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
