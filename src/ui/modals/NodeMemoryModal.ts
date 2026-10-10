import { Graph } from '../../core/Graph.ts';
import { INodeData, INodeMemoryConfig, DEFAULT_NODE_MEMORY_CONFIG } from '../../core/types.ts';
import { UpdateNodeCommand } from '../../core/history/GraphCommands.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';

export class NodeMemoryModal {
  private overlay: HTMLElement;
  private graph: Graph;
  private windowController?: IModalWindowController;

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

    let statusBadgeText = 'MEMÓRIA DESATIVADA (ISOLAMENTO TOTAL)';
    let statusBadgeClass = 'badge-memory-disabled';
    if (isEnabled) {
      if (isPersistent) {
        statusBadgeText = 'MEMÓRIA ATIVA (PERSISTENTE ENTRE SESSÕES)';
        statusBadgeClass = 'badge-memory-persistent';
      } else {
        statusBadgeText = 'MEMÓRIA ATIVA (EFÊMERA / SESSÃO)';
        statusBadgeClass = 'badge-memory-ephemeral';
      }
    }

    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: 'Memória do Agente & Contexto',
      subtitle: `Nó: <strong>${this.escapeHtml(node.label)}</strong> (ID: ${node.id}) | Projeto: <strong>${this.escapeHtml(projectId)}</strong>`,
      icon: '💾',
      closeBtnId: 'btn-close-memory-modal'
    });

    this.overlay.innerHTML = `
      <div class="modal-dialog node-settings-modal-dialog">
        ${titleBarHtml}
        <div class="modal-body-scrollable">
          <!-- Toggle de Herança -->
          <div class="inheritance-toggle-card">
            <label class="inheritance-checkbox-label">
              <input type="checkbox" id="chk-inherit-memory" ${isInherited ? 'checked' : ''} />
              <div class="inheritance-label-text">
                <span class="inheritance-title">Herdar Padrões Globais de Memória</span>
                <span class="inheritance-desc">Usa a política de memória configurada para o pipeline (Padrão: Desativada)</span>
              </div>
            </label>
            <div class="inheritance-status-badge ${isInherited ? 'badge-inherited' : 'badge-overridden'}">
              ${isInherited ? 'HERDADO DO PIPELINE' : 'CUSTOMIZADO PARA ESTE NÓ'}
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
                      ? 'Estado gravado em disco com retenção contínua entre sessões para este projeto.'
                      : 'Memória operando em RAM exclusivamente nesta sessão. Descartada ao reiniciar a sessão.')
                  : 'Este agente operará 100% isolado, sem acesso ao Blackboard e sem consulta de lições prévias.'}
              </span>
            </div>
          </div>

          <div class="settings-form-wrapper ${isInherited ? 'form-disabled-opacity' : ''}" id="memory-form-wrapper">
            <!-- 1. Toggle Principal: Ativar/Desativar Memória -->
            <div class="settings-section-card">
              <div class="section-card-header">
                <span class="section-card-icon">⚡</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">Habilitação de Memória (Opt-in)</h3>
                  <p class="section-card-desc">A memória não é padrão e nem obrigatória. Selecione se este agente terá acesso.</p>
                </div>
              </div>

              <div class="memory-enable-toggle-row">
                <label class="memory-toggle-label">
                  <input type="checkbox" id="node-memory-enabled" ${isEnabled ? 'checked' : ''} ${isInherited ? 'disabled' : ''} />
                  <span class="memory-toggle-text">Habilitar Acesso à Memória para este Agente</span>
                </label>
              </div>
            </div>

            <!-- 2. Persistência & Escopo de Projeto (Apenas se ativado) -->
            <div class="settings-section-card ${isEnabled ? '' : 'form-disabled-opacity'}" id="memory-details-card">
              <div class="section-card-header">
                <span class="section-card-icon">🗄️</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">Regime de Persistência & Projeto</h3>
                  <p class="section-card-desc">Defina se a memória persiste entre sessões e o escopo de isolamento do projeto</p>
                </div>
              </div>

              <div class="form-group">
                <label class="form-label">Modo de Retenção</label>
                <div class="memory-persistence-options">
                  <label class="persistence-radio-card ${!isPersistent ? 'selected' : ''}">
                    <input type="radio" name="node-memory-persistence" value="ephemeral" ${!isPersistent ? 'checked' : ''} ${isInherited || !isEnabled ? 'disabled' : ''} />
                    <div class="persistence-card-content">
                      <span class="persistence-card-title">⚪ Efêmera (Apenas nesta Sessão / RAM)</span>
                      <span class="persistence-card-desc">A memória de trabalho e reflexões ficam na memória temporária e são descartadas ao reiniciar a sessão.</span>
                    </div>
                  </label>

                  <label class="persistence-radio-card ${isPersistent ? 'selected' : ''}">
                    <input type="radio" name="node-memory-persistence" value="persistent" ${isPersistent ? 'checked' : ''} ${isInherited || !isEnabled ? 'disabled' : ''} />
                    <div class="persistence-card-content">
                      <span class="persistence-card-title">🟣 Persistente Entre Sessões (Disco)</span>
                      <span class="persistence-card-desc">Gravação atômica em disco vinculada ao projeto. O histórico de lições e dados é mantido entre sessões.</span>
                    </div>
                  </label>
                </div>
              </div>

              <div class="form-group" style="margin-top: 12px;">
                <label class="form-label" for="node-memory-project">Escopo do Projeto (Namespace de Isolamento)</label>
                <input type="text" id="node-memory-project" class="form-input" value="${this.escapeHtml(effectiveMemory.projectId || projectId)}" ${isInherited || !isEnabled ? 'disabled' : ''} />
                <div class="form-hint">Agentes em projetos distintos operam em partições estritamente isoladas.</div>
              </div>
            </div>

            <!-- 3. Permissões Granulares da Memória -->
            <div class="settings-section-card ${isEnabled ? '' : 'form-disabled-opacity'}" id="memory-permissions-card">
              <div class="section-card-header">
                <span class="section-card-icon">🛡️</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">Permissões de Acesso à Memória</h3>
                  <p class="section-card-desc">Controle granular sobre o Blackboard intra-pipeline e lições semânticas</p>
                </div>
              </div>

              <div class="memory-perms-grid">
                <label class="memory-perm-item">
                  <input type="checkbox" id="mem-perm-read-bb" ${effectiveMemory.readBlackboard ? 'checked' : ''} ${isInherited || !isEnabled ? 'disabled' : ''} />
                  <div>
                    <span class="perm-item-title">Leitura do Blackboard (Memória de Trabalho)</span>
                    <span class="perm-item-desc">Acessa decisões e dados compartilhados gerados por agentes anteriores</span>
                  </div>
                </label>

                <label class="memory-perm-item">
                  <input type="checkbox" id="mem-perm-write-bb" ${effectiveMemory.writeBlackboard ? 'checked' : ''} ${isInherited || !isEnabled ? 'disabled' : ''} />
                  <div>
                    <span class="perm-item-title">Escrita no Blackboard (Publicar Deltas)</span>
                    <span class="perm-item-desc">Disponibiliza saídas estruturadas para agentes subsequentes</span>
                  </div>
                </label>

                <label class="memory-perm-item">
                  <input type="checkbox" id="mem-perm-read-lessons" ${effectiveMemory.semanticLessons ? 'checked' : ''} ${isInherited || !isEnabled ? 'disabled' : ''} />
                  <div>
                    <span class="perm-item-title">Consultar Lições Aprendidas (Semântica)</span>
                    <span class="perm-item-desc">Injeta regras profiláticas preventivas no prompt de sistema</span>
                  </div>
                </label>

                <label class="memory-perm-item">
                  <input type="checkbox" id="mem-perm-write-lessons" ${effectiveMemory.recordLessons ? 'checked' : ''} ${isInherited || !isEnabled ? 'disabled' : ''} />
                  <div>
                    <span class="perm-item-title">Registrar Novas Lições (Auditoria & Falhas)</span>
                    <span class="perm-item-desc">Indexa regras preventivas derivadas de erros e auditorias</span>
                  </div>
                </label>
              </div>

              <div class="form-row-grid" style="margin-top: 14px;">
                <div class="form-group">
                  <label class="form-label" for="node-memory-keys">Chaves Autorizadas no Blackboard (Opcional)</label>
                  <input type="text" id="node-memory-keys" class="form-input" value="${this.escapeHtml((effectiveMemory.allowedKeys || []).join(', '))}" placeholder="ex: resumo, schema, svg_specs (vazio = todas)" ${isInherited || !isEnabled ? 'disabled' : ''} />
                </div>

                <div class="form-group">
                  <label class="form-label" for="node-memory-domains">Domínios Semânticos Filtrados (Opcional)</label>
                  <input type="text" id="node-memory-domains" class="form-input" value="${this.escapeHtml((effectiveMemory.domains || []).join(', '))}" placeholder="ex: svg_typography, validation (vazio = todos)" ${isInherited || !isEnabled ? 'disabled' : ''} />
                </div>
              </div>
            </div>
          </div>
        </div>

        <div class="modal-footer">
          <button class="btn btn-secondary" id="btn-cancel-memory-modal">Fechar</button>
          <button class="btn btn-primary" id="btn-save-memory-modal">Salvar Configurações</button>
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
      domains
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
      `Atualizar Configuração de Memória de "${node.label}"`
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
