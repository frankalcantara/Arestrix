import { Graph } from '../../core/Graph.ts';
import { INodeData, INodeMemoryConfig, DEFAULT_NODE_MEMORY_CONFIG } from '../../core/types.ts';
import { UpdateNodeCommand } from '../../core/history/GraphCommands.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';
import { t } from '../../i18n/index.ts';

export class NodeMemoryModal {
  private overlay: HTMLElement;
  private graph: Graph;
  private windowController?: IModalWindowController;
  private activeTab: 'blackboard' | 'lessons' | 'retention' = 'blackboard';

  constructor(graph: Graph) {
    this.graph = graph;

    this.overlay = document.createElement('div');
    this.overlay.className = 'modal-overlay hidden';
    document.body.appendChild(this.overlay);

    this.bindEvents();
  }

  public open(nodeId: string): void {
    const node = this.graph.getNode(nodeId);
    if (!node) return;

    this.activeTab = 'blackboard';
    this.render(node);
    this.overlay.classList.remove('hidden');
    this.windowController?.bringToFront();
  }

  public hide(): void {
    this.overlay.classList.add('hidden');
  }

  private render(node: INodeData): void {
    const globalDefaults = this.graph.getGlobalDefaults();
    const projectId = this.graph.getProjectId();
    const settings = node.agentSettings || {
      inheritSandbox: true,
      inheritModel: true,
      inheritCodeMode: true,
      inheritMemory: true
    };

    const isInherited = settings.inheritMemory !== false;
    const globalMemory = globalDefaults.memory || DEFAULT_NODE_MEMORY_CONFIG;
    const nodeMemory = settings.memory || {};

    const effectiveMemory: INodeMemoryConfig = isInherited
      ? { ...globalMemory, projectId }
      : { ...DEFAULT_NODE_MEMORY_CONFIG, ...globalMemory, ...nodeMemory, projectId: nodeMemory.projectId || projectId };

    const isEnabled = !!effectiveMemory.enabled;
    const isPersistent = !!effectiveMemory.persistent;

    let statusBadgeText = t('node_modals.status_memory_disabled');
    let statusBadgeClass = 'badge-memory-disabled';
    if (isEnabled) {
      if (isPersistent) {
        statusBadgeText = t('node_modals.status_memory_persistent');
        statusBadgeClass = 'badge-memory-persistent';
      } else {
        statusBadgeText = t('node_modals.status_memory_ephemeral');
        statusBadgeClass = 'badge-memory-ephemeral';
      }
    }

    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: t('node_modals.memory_title'),
      subtitle: t('node_modals.memory_subtitle', { label: this.escapeHtml(node.label), id: node.id, projectId: this.escapeHtml(projectId) }),
      icon: '💾',
      closeBtnId: 'btn-close-memory-modal'
    });

    this.overlay.innerHTML = `
      <div class="modal-dialog node-settings-modal-dialog">
        ${titleBarHtml}

        <!-- Abas Fluent / Office 365 -->
        <div class="modal-tabs" role="tablist">
          <button type="button" class="tab-btn ${this.activeTab === 'blackboard' ? 'active' : ''}" data-tab="blackboard">
            <span>📋</span> ${t('node_modals.tab_blackboard', '1. Blackboard')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'lessons' ? 'active' : ''}" data-tab="lessons">
            <span>🧠</span> ${t('node_modals.tab_lessons', '2. Lições Cognitivas')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'retention' ? 'active' : ''}" data-tab="retention">
            <span>💾</span> ${t('node_modals.tab_retention', '3. Regime & Escopo')}
          </button>
        </div>

        <div class="modal-body-scrollable">
          <!-- Toggle de Herança -->
          <div class="inheritance-toggle-card">
            <label class="inheritance-checkbox-label">
              <input type="checkbox" id="chk-inherit-memory" ${isInherited ? 'checked' : ''} />
              <div class="inheritance-label-text">
                <span class="inheritance-title">${t('node_modals.inherit_memory_title')}</span>
                <span class="inheritance-desc">${t('node_modals.inherit_memory_desc')}</span>
              </div>
            </label>
            <div class="inheritance-status-badge ${isInherited ? 'badge-inherited' : 'badge-overridden'}">
              ${isInherited ? t('node_modals.badge_inherited') : t('node_modals.badge_overridden')}
            </div>
          </div>

          <!-- Status Geral de Memória -->
          <div class="memory-status-banner ${statusBadgeClass}">
            <div class="memory-status-icon">${isEnabled ? (isPersistent ? '🟣' : '⚪') : '🔒'}</div>
            <div class="memory-status-info">
              <span class="memory-status-title">${statusBadgeText}</span>
              <span class="memory-status-desc">
                ${isEnabled
                  ? (isPersistent
                      ? t('node_modals.desc_memory_persistent')
                      : t('node_modals.desc_memory_ephemeral'))
                  : t('node_modals.desc_memory_disabled')}
              </span>
            </div>
          </div>

          <div class="settings-form-wrapper ${isInherited ? 'form-disabled-opacity' : ''}" id="memory-form-wrapper">
            <!-- ABA 1: Memória Blackboard -->
            <div class="tab-pane" id="pane-tab-blackboard" style="${this.activeTab === 'blackboard' ? '' : 'display: none;'}">
              <div class="settings-section-card ${isEnabled ? '' : 'form-disabled-opacity'}">
                <div class="section-card-header">
                  <span class="section-card-icon">📋</span>
                  <div class="section-card-titles">
                    <h3 class="section-card-title">Comunicação via Blackboard</h3>
                    <p class="section-card-desc">Leitura e publicação de deltas no quadro compartilhado do pipeline</p>
                  </div>
                </div>

                <div class="memory-perms-grid">
                  <label class="memory-perm-item">
                    <input type="checkbox" id="mem-perm-read-bb" ${effectiveMemory.readBlackboard ? 'checked' : ''} ${isInherited || !isEnabled ? 'disabled' : ''} />
                    <div>
                      <span class="perm-item-title">${t('node_modals.perm_read_bb_title')}</span>
                      <span class="perm-item-desc">${t('node_modals.perm_read_bb_desc')}</span>
                    </div>
                  </label>

                  <label class="memory-perm-item">
                    <input type="checkbox" id="mem-perm-write-bb" ${effectiveMemory.writeBlackboard ? 'checked' : ''} ${isInherited || !isEnabled ? 'disabled' : ''} />
                    <div>
                      <span class="perm-item-title">${t('node_modals.perm_write_bb_title')}</span>
                      <span class="perm-item-desc">${t('node_modals.perm_write_bb_desc')}</span>
                    </div>
                  </label>
                </div>

                <div class="form-group" style="margin-top: 14px;">
                  <label class="form-label" for="node-memory-keys">Chaves Autorizadas no Blackboard (Opcional)</label>
                  <input type="text" id="node-memory-keys" class="form-input" value="${this.escapeHtml((effectiveMemory.allowedKeys || []).join(', '))}" placeholder="ex: resumo, schema, svg_specs (vazio = todas)" ${isInherited || !isEnabled ? 'disabled' : ''} />
                  <div class="form-hint">Restringe o agente a ler/escrever apenas nestas chaves estruturadas.</div>
                </div>
              </div>
            </div>

            <!-- ABA 2: Lições Cognitivas -->
            <div class="tab-pane" id="pane-tab-lessons" style="${this.activeTab === 'lessons' ? '' : 'display: none;'}">
              <div class="settings-section-card ${isEnabled ? '' : 'form-disabled-opacity'}">
                <div class="section-card-header">
                  <span class="section-card-icon">🧠</span>
                  <div class="section-card-titles">
                    <h3 class="section-card-title">Lições e Experiências Anteriores</h3>
                    <p class="section-card-desc">Injeção preventiva de correções e registro de auditorias de execuções</p>
                  </div>
                </div>

                <div class="memory-perms-grid">
                  <label class="memory-perm-item">
                    <input type="checkbox" id="mem-perm-read-lessons" ${effectiveMemory.semanticLessons ? 'checked' : ''} ${isInherited || !isEnabled ? 'disabled' : ''} />
                    <div>
                      <span class="perm-item-title">${t('node_modals.perm_read_lessons_title')}</span>
                      <span class="perm-item-desc">${t('node_modals.perm_read_lessons_desc')}</span>
                    </div>
                  </label>

                  <label class="memory-perm-item">
                    <input type="checkbox" id="mem-perm-write-lessons" ${effectiveMemory.recordLessons ? 'checked' : ''} ${isInherited || !isEnabled ? 'disabled' : ''} />
                    <div>
                      <span class="perm-item-title">${t('node_modals.perm_write_lessons_title')}</span>
                      <span class="perm-item-desc">${t('node_modals.perm_write_lessons_desc')}</span>
                    </div>
                  </label>
                </div>

                <div class="form-group" style="margin-top: 14px;">
                  <label class="form-label" for="node-memory-domains">Domínios Semânticos Filtrados (Opcional)</label>
                  <input type="text" id="node-memory-domains" class="form-input" value="${this.escapeHtml((effectiveMemory.domains || []).join(', '))}" placeholder="ex: svg_typography, validation (vazio = todos)" ${isInherited || !isEnabled ? 'disabled' : ''} />
                  <div class="form-hint">Filtra as lições semânticas apenas pelas categorias e domínios relevantes.</div>
                </div>
              </div>
            </div>

            <!-- ABA 3: Regime de Persistência & Escopo -->
            <div class="tab-pane" id="pane-tab-retention" style="${this.activeTab === 'retention' ? '' : 'display: none;'}">
              <!-- Ativar/Desativar Memória -->
              <div class="settings-section-card">
                <div class="section-card-header">
                  <span class="section-card-icon">⚡</span>
                  <div class="section-card-titles">
                    <h3 class="section-card-title">${t('node_modals.memory_opt_in_title')}</h3>
                    <p class="section-card-desc">${t('node_modals.memory_opt_in_desc')}</p>
                  </div>
                </div>

                <div class="memory-enable-toggle-row">
                  <label class="memory-toggle-label">
                    <input type="checkbox" id="node-memory-enabled" ${isEnabled ? 'checked' : ''} ${isInherited ? 'disabled' : ''} />
                    <span class="memory-toggle-text">${t('node_modals.toggle_enable_memory')}</span>
                  </label>
                </div>
              </div>

              <!-- Persistência & Escopo de Projeto -->
              <div class="settings-section-card ${isEnabled ? '' : 'form-disabled-opacity'}" id="memory-details-card" style="margin-top: 14px;">
                <div class="section-card-header">
                  <span class="section-card-icon">🗄️</span>
                  <div class="section-card-titles">
                    <h3 class="section-card-title">${t('node_modals.memory_persistence_section_title')}</h3>
                    <p class="section-card-desc">${t('node_modals.memory_persistence_section_desc')}</p>
                  </div>
                </div>

                <div class="form-group">
                  <label class="form-label">${t('node_modals.memory_retention_mode_label')}</label>
                  <div class="memory-persistence-options">
                    <label class="persistence-radio-card ${!isPersistent ? 'selected' : ''}">
                      <input type="radio" name="node-memory-persistence" value="ephemeral" ${!isPersistent ? 'checked' : ''} ${isInherited || !isEnabled ? 'disabled' : ''} />
                      <div class="persistence-card-content">
                        <span class="persistence-card-title">${t('node_modals.opt_ephemeral_title')}</span>
                        <span class="persistence-card-desc">${t('node_modals.opt_ephemeral_desc')}</span>
                      </div>
                    </label>

                    <label class="persistence-radio-card ${isPersistent ? 'selected' : ''}">
                      <input type="radio" name="node-memory-persistence" value="persistent" ${isPersistent ? 'checked' : ''} ${isInherited || !isEnabled ? 'disabled' : ''} />
                      <div class="persistence-card-content">
                        <span class="persistence-card-title">${t('node_modals.opt_persistent_title')}</span>
                        <span class="persistence-card-desc">${t('node_modals.opt_persistent_desc')}</span>
                      </div>
                    </label>
                  </div>
                </div>

                <div class="form-group" style="margin-top: 12px;">
                  <label class="form-label" for="node-memory-project">Escopo do Projeto (Namespace de Isolamento)</label>
                  <input type="text" id="node-memory-project" class="form-input" value="${this.escapeHtml(effectiveMemory.projectId || projectId)}" ${isInherited || !isEnabled ? 'disabled' : ''} />
                  <div class="form-hint">Agentes em projetos distintos operam em partições estritamente isoladas.</div>
                </div>

                <div class="form-group" style="margin-top: 12px;">
                  <label class="form-label" for="node-memory-budget">Orçamento de contexto (tokens)</label>
                  <input type="number" id="node-memory-budget" class="form-input" min="0" max="32000" step="100" value="${effectiveMemory.contextBudgetTokens ?? 800}" ${isInherited || !isEnabled ? 'disabled' : ''} />
                  <div class="form-hint">Limite aproximado do brief de memória injetado no prompt. 0 = sem limite.</div>
                </div>
              </div>
            </div>
          </div>
        </div>

        <div class="modal-footer office-modal-footer">
          <button class="btn btn-secondary-pill btn-secondary-office" id="btn-cancel-memory-modal">${t('global_defaults.btn_cancel')}</button>
          <button class="btn btn-primary btn-primary-office" id="btn-save-memory-modal">${t('node_modals.btn_save_agent_settings')}</button>
        </div>
      </div>
    `;

    const dialog = this.overlay.querySelector('.modal-dialog') as HTMLElement;
    if (dialog) {
      this.windowController = ModalWindowManager.attach({
        dialog,
        onClose: () => this.hide()
      });
    }

    this.attachDialogEvents(node);
  }

  private attachDialogEvents(node: INodeData): void {
    const btnClose = this.overlay.querySelector('#btn-close-memory-modal');
    const btnCancel = this.overlay.querySelector('#btn-cancel-memory-modal');
    const btnSave = this.overlay.querySelector('#btn-save-memory-modal');
    const chkInherit = this.overlay.querySelector<HTMLInputElement>('#chk-inherit-memory');
    const chkEnabled = this.overlay.querySelector<HTMLInputElement>('#node-memory-enabled');
    const radios = this.overlay.querySelectorAll<HTMLInputElement>('input[name="node-memory-persistence"]');

    btnClose?.addEventListener('click', () => this.hide());
    btnCancel?.addEventListener('click', () => this.hide());

    // Manipulador de abas
    const tabButtons = this.overlay.querySelectorAll<HTMLButtonElement>('.modal-tabs .tab-btn');
    tabButtons.forEach(btn => {
      btn.addEventListener('click', () => {
        const tab = (btn.dataset?.tab || btn.getAttribute('data-tab')) as 'blackboard' | 'lessons' | 'retention';
        if (!tab) return;
        this.activeTab = tab;
        tabButtons.forEach(b => {
          const bTab = b.dataset?.tab || b.getAttribute('data-tab');
          if (b.classList && typeof b.classList.toggle === 'function') {
            b.classList.toggle('active', bTab === tab);
          }
        });
        const panes = this.overlay.querySelectorAll<HTMLElement>('.tab-pane');
        panes.forEach(p => {
          p.style.display = p.id === `pane-tab-${tab}` ? '' : 'none';
        });
      });
    });

    btnClose?.addEventListener('click', () => this.hide());
    btnCancel?.addEventListener('click', () => this.hide());

    // Toggle de Herança re-renderiza para atualizar estados bloqueados
    chkInherit?.addEventListener('change', () => {
      this.saveFormState(node, chkInherit.checked);
      this.render(node);
    });

    // Toggle de Habilitar Memória re-renderiza para atualizar visual dos cards
    chkEnabled?.addEventListener('change', () => {
      this.saveFormState(node, false);
      this.render(node);
    });

    // Mudança no radio de persistência
    radios.forEach(r => {
      r.addEventListener('change', () => {
        this.saveFormState(node, false);
        this.render(node);
      });
    });

    // Salvar configurações no Grafo via CommandManager
    btnSave?.addEventListener('click', () => {
      this.saveFormState(node, chkInherit ? chkInherit.checked : false);
      this.hide();
    });
  }

  private saveFormState(node: INodeData, inheritMemory: boolean): void {
    const chkEnabled = this.overlay.querySelector<HTMLInputElement>('#node-memory-enabled');
    const selectedRadio = this.overlay.querySelector<HTMLInputElement>('input[name="node-memory-persistence"]:checked');
    const txtProject = this.overlay.querySelector<HTMLInputElement>('#node-memory-project');
    const chkReadBb = this.overlay.querySelector<HTMLInputElement>('#mem-perm-read-bb');
    const chkWriteBb = this.overlay.querySelector<HTMLInputElement>('#mem-perm-write-bb');
    const chkReadLessons = this.overlay.querySelector<HTMLInputElement>('#mem-perm-read-lessons');
    const chkWriteLessons = this.overlay.querySelector<HTMLInputElement>('#mem-perm-write-lessons');
    const txtKeys = this.overlay.querySelector<HTMLInputElement>('#node-memory-keys');
    const txtDomains = this.overlay.querySelector<HTMLInputElement>('#node-memory-domains');
    const txtBudget = this.overlay.querySelector<HTMLInputElement>('#node-memory-budget');

    const allowedKeys = txtKeys ? txtKeys.value.split(',').map(s => s.trim()).filter(Boolean) : [];
    const domains = txtDomains ? txtDomains.value.split(',').map(s => s.trim()).filter(Boolean) : [];

    const memoryConfig: INodeMemoryConfig = {
      enabled: chkEnabled ? chkEnabled.checked : false,
      persistent: selectedRadio ? selectedRadio.value === 'persistent' : false,
      projectId: txtProject ? txtProject.value.trim() : this.graph.getProjectId(),
      readBlackboard: chkReadBb ? chkReadBb.checked : false,
      writeBlackboard: chkWriteBb ? chkWriteBb.checked : false,
      semanticLessons: chkReadLessons ? chkReadLessons.checked : false,
      recordLessons: chkWriteLessons ? chkWriteLessons.checked : false,
      allowedKeys,
      domains,
      contextBudgetTokens: txtBudget ? Math.max(0, Number(txtBudget.value) || 0) : 800
    };

    const currentSettings = node.agentSettings || {
      inheritSandbox: true,
      inheritModel: true,
      inheritCodeMode: true,
      inheritMemory: true
    };

    const updatedSettings = {
      ...currentSettings,
      inheritMemory,
      memory: memoryConfig
    };

    const cmd = new UpdateNodeCommand(
      this.graph,
      node.id,
      { agentSettings: node.agentSettings },
      { agentSettings: updatedSettings },
      t('node_modals.cmd_change_memory', { label: node.label })
    );

    this.graph.commandManager.execute(cmd);
  }

  private bindEvents(): void {
    this.overlay.addEventListener('click', (e) => {
      if (e.target === this.overlay) {
        this.hide();
      }
    });

    window.addEventListener('keydown', (e) => {
      if (e.key === 'Escape' && !this.overlay.classList.contains('hidden')) {
        this.hide();
      }
    });
  }

  private escapeHtml(text: string): string {
    return text
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }
}
