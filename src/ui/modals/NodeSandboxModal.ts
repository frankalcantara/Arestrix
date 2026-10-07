import { Graph } from '../../core/Graph.ts';
import { INodeData, ISandboxPolicy, SandboxIsolationLevel, SandboxNetworkPolicy } from '../../core/types.ts';
import { UpdateNodeCommand } from '../../core/history/GraphCommands.ts';

export class NodeSandboxModal {
  private overlay: HTMLElement;
  private graph: Graph;

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
  }

  public hide(): void {
    this.overlay.classList.add('hidden');
  }

  private render(node: INodeData): void {
    const globalDefaults = this.graph.getGlobalDefaults();
    const settings = node.agentSettings || {
      inheritSandbox: true,
      inheritModel: true,
      inheritCodeMode: true
    };

    const isInherited = settings.inheritSandbox !== false;
    const effectiveSandbox: ISandboxPolicy = isInherited
      ? globalDefaults.sandbox
      : {
          ...globalDefaults.sandbox,
          ...(settings.sandbox || {})
        };

    this.overlay.innerHTML = `
      <div class="modal-dialog node-settings-modal-dialog">
        <div class="modal-header">
          <div class="modal-title-group">
            <div class="modal-header-icon">🛡️</div>
            <div>
              <h2 class="modal-title">Sandbox & Máquina Virtual</h2>
              <div class="modal-subtitle">Nó: <strong>${this.escapeHtml(node.label)}</strong> (ID: ${node.id})</div>
            </div>
          </div>
          <button class="modal-close-btn" id="btn-close-sandbox-modal">&times;</button>
        </div>

        <div class="modal-body-scrollable">
          <!-- Toggle de Herança -->
          <div class="inheritance-toggle-card">
            <label class="inheritance-checkbox-label">
              <input type="checkbox" id="chk-inherit-sandbox" ${isInherited ? 'checked' : ''} />
              <div class="inheritance-label-text">
                <span class="inheritance-title">Herdar Padrões Globais do Pipeline</span>
                <span class="inheritance-desc">Usa o ambiente configurado na Toolbar (${this.getIsolationLabel(globalDefaults.sandbox.isolationLevel)}, ${globalDefaults.sandbox.maxMemoryMB} MB RAM)</span>
              </div>
            </label>
            <div class="inheritance-status-badge ${isInherited ? 'badge-inherited' : 'badge-overridden'}">
              ${isInherited ? 'HERDADO DO PIPELINE' : 'CUSTOMIZADO PARA ESTE NÓ'}
            </div>
          </div>

          <div class="settings-form-wrapper ${isInherited ? 'form-disabled-opacity' : ''}" id="sandbox-form-wrapper">
            <div class="form-row-grid">
              <div class="form-group">
                <label class="form-label" for="node-sandbox-isolation">Nível de Isolamento</label>
                <select id="node-sandbox-isolation" class="form-select" ${isInherited ? 'disabled' : ''}>
                  <option value="container" ${effectiveSandbox.isolationLevel === 'container' ? 'selected' : ''}>Container Docker/Podman</option>
                  <option value="microvm" ${effectiveSandbox.isolationLevel === 'microvm' ? 'selected' : ''}>MicroVM Isolada (Firecracker / WSL2)</option>
                  <option value="host_direct" ${effectiveSandbox.isolationLevel === 'host_direct' ? 'selected' : ''}>Host Direto (Fora da VM / Sem Isolamento)</option>
                </select>
              </div>

              <div class="form-group">
                <label class="form-label" for="node-sandbox-network">Política de Rede</label>
                <select id="node-sandbox-network" class="form-select" ${isInherited ? 'disabled' : ''}>
                  <option value="egress_llm_only" ${effectiveSandbox.network === 'egress_llm_only' ? 'selected' : ''}>Egress Apenas para API LLM</option>
                  <option value="offline" ${effectiveSandbox.network === 'offline' ? 'selected' : ''}>Totalmente Offline (Bloqueada)</option>
                  <option value="full" ${effectiveSandbox.network === 'full' ? 'selected' : ''}>Internet Livre (Acesso Irrestrito)</option>
                </select>
              </div>
            </div>

            <div class="form-row-grid">
              <div class="form-group">
                <label class="form-label" for="node-sandbox-memory">Limite de RAM (MB)</label>
                <input type="number" id="node-sandbox-memory" class="form-input" value="${effectiveSandbox.maxMemoryMB}" min="64" step="64" ${isInherited ? 'disabled' : ''} />
              </div>

              <div class="form-group">
                <label class="form-label" for="node-sandbox-timeout">Timeout (segundos)</label>
                <input type="number" id="node-sandbox-timeout" class="form-input" value="${effectiveSandbox.timeoutSeconds}" min="10" step="10" ${isInherited ? 'disabled' : ''} />
              </div>
            </div>

            <div class="form-group">
              <label class="form-label" for="node-sandbox-ro">Montagens Somente-Leitura (RO)</label>
              <input type="text" id="node-sandbox-ro" class="form-input" value="${this.escapeHtml((effectiveSandbox.readOnlyMounts || []).join(', '))}" placeholder="plano.md, .agents/" ${isInherited ? 'disabled' : ''} />
              <div class="form-hint">Caminhos montados com flag de proteção contra escrita.</div>
            </div>

            <div class="form-group">
              <label class="form-label" for="node-sandbox-rw">Pastas de Escrita Permitida (RW)</label>
              <input type="text" id="node-sandbox-rw" class="form-input" value="${this.escapeHtml((effectiveSandbox.readWriteMounts || []).join(', '))}" placeholder="aulas/${node.id}.md, assets/" ${isInherited ? 'disabled' : ''} />
              <div class="form-hint">Apenas estas pastas podem ser modificadas pelas ferramentas do agente.</div>
            </div>
          </div>
        </div>

        <div class="modal-footer">
          <button class="btn btn-secondary-pill" id="btn-cancel-sandbox-modal">Cancelar</button>
          <button class="btn btn-primary" id="btn-save-sandbox-modal">Salvar Configuração</button>
        </div>
      </div>
    `;

    this.bindDialogEvents(node);
  }

  private bindEvents(): void {
    this.overlay.addEventListener('click', (e) => {
      if (e.target === this.overlay) this.hide();
    });
  }

  private bindDialogEvents(node: INodeData): void {
    this.overlay.querySelector('#btn-close-sandbox-modal')?.addEventListener('click', () => this.hide());
    this.overlay.querySelector('#btn-cancel-sandbox-modal')?.addEventListener('click', () => this.hide());

    const inheritCheck = this.overlay.querySelector('#chk-inherit-sandbox') as HTMLInputElement;
    const formWrapper = this.overlay.querySelector('#sandbox-form-wrapper') as HTMLElement;

    inheritCheck?.addEventListener('change', () => {
      const isInherited = inheritCheck.checked;
      const inputs = formWrapper.querySelectorAll<HTMLInputElement | HTMLSelectElement>('input, select');
      inputs.forEach(input => {
        input.disabled = isInherited;
      });

      if (isInherited) {
        formWrapper.classList.add('form-disabled-opacity');
      } else {
        formWrapper.classList.remove('form-disabled-opacity');
      }
    });

    this.overlay.querySelector('#btn-save-sandbox-modal')?.addEventListener('click', () => {
      this.saveNodeSandbox(node, inheritCheck.checked);
      this.hide();
    });
  }

  private saveNodeSandbox(node: INodeData, inherit: boolean): void {
    const currentSettings = node.agentSettings || {
      inheritSandbox: true,
      inheritModel: true,
      inheritCodeMode: true
    };

    let newSandboxData: Partial<ISandboxPolicy> | undefined;

    if (!inherit) {
      const isolationLevel = (this.overlay.querySelector('#node-sandbox-isolation') as HTMLSelectElement)?.value as SandboxIsolationLevel || 'container';
      const network = (this.overlay.querySelector('#node-sandbox-network') as HTMLSelectElement)?.value as SandboxNetworkPolicy || 'egress_llm_only';
      const maxMemoryMB = parseInt((this.overlay.querySelector('#node-sandbox-memory') as HTMLInputElement)?.value, 10) || 512;
      const timeoutSeconds = parseInt((this.overlay.querySelector('#node-sandbox-timeout') as HTMLInputElement)?.value, 10) || 300;

      const roRaw = (this.overlay.querySelector('#node-sandbox-ro') as HTMLInputElement)?.value || '';
      const readOnlyMounts = roRaw.split(',').map(s => s.trim()).filter(Boolean);

      const rwRaw = (this.overlay.querySelector('#node-sandbox-rw') as HTMLInputElement)?.value || '';
      const readWriteMounts = rwRaw.split(',').map(s => s.trim()).filter(Boolean);

      newSandboxData = {
        isolationLevel,
        network,
        maxMemoryMB,
        timeoutSeconds,
        readOnlyMounts,
        readWriteMounts
      };
    }

    const updatedSettings = {
      ...currentSettings,
      inheritSandbox: inherit,
      sandbox: inherit ? undefined : newSandboxData
    };

    const cmd = new UpdateNodeCommand(
      this.graph,
      node.id,
      { agentSettings: node.agentSettings },
      { agentSettings: updatedSettings },
      `Alterar política de sandbox de "${node.label}"`
    );
    this.graph.commandManager.execute(cmd);
  }

  private getIsolationLabel(level: SandboxIsolationLevel): string {
    if (level === 'container') return 'Docker Container';
    if (level === 'microvm') return 'MicroVM';
    return 'Host Direto';
  }

  private escapeHtml(text: string): string {
    return text
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
}
