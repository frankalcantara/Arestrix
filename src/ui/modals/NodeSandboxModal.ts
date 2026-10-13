import { Graph } from '../../core/Graph.ts';
import { INodeData, ISandboxPolicy, SandboxIsolationLevel, SandboxNetworkPolicy } from '../../core/types.ts';
import { UpdateNodeCommand } from '../../core/history/GraphCommands.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';
import { t } from '../../i18n/index.ts';

export class NodeSandboxModal {
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

    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: t('node_modals.sandbox_title'),
      subtitle: t('node_modals.sandbox_subtitle', { label: this.escapeHtml(node.label), id: node.id }),
      icon: '🛡️',
      closeBtnId: 'btn-close-sandbox-modal'
    });

    this.overlay.innerHTML = `
      <div class="modal-dialog node-settings-modal-dialog">
        ${titleBarHtml}
        <div class="modal-body-scrollable">
          <!-- Toggle de Herança -->
          <div class="inheritance-toggle-card">
            <label class="inheritance-checkbox-label">
              <input type="checkbox" id="chk-inherit-sandbox" ${isInherited ? 'checked' : ''} />
              <div class="inheritance-label-text">
                <span class="inheritance-title">${t('node_modals.inherit_sandbox_title')}</span>
                <span class="inheritance-desc">${t('node_modals.inherit_sandbox_desc', { isolation: this.getIsolationLabel(globalDefaults.sandbox.isolationLevel), memory: globalDefaults.sandbox.maxMemoryMB })}</span>
              </div>
            </label>
            <div class="inheritance-status-badge ${isInherited ? 'badge-inherited' : 'badge-overridden'}">
              ${isInherited ? t('node_modals.badge_inherited') : t('node_modals.badge_overridden')}
            </div>
          </div>

          <div class="settings-form-wrapper ${isInherited ? 'form-disabled-opacity' : ''}" id="sandbox-form-wrapper">
            <div class="form-row-grid">
              <div class="form-group">
                <label class="form-label" for="node-sandbox-isolation">${t('node_modals.isolation_label')}</label>
                <select id="node-sandbox-isolation" class="form-select" ${isInherited ? 'disabled' : ''}>
                  <option value="container" ${effectiveSandbox.isolationLevel === 'container' ? 'selected' : ''}>${t('node_modals.isolation_container')}</option>
                  <option value="microvm" ${effectiveSandbox.isolationLevel === 'microvm' ? 'selected' : ''}>${t('node_modals.isolation_microvm')}</option>
                  <option value="host_direct" ${effectiveSandbox.isolationLevel === 'host_direct' ? 'selected' : ''}>${t('node_modals.isolation_host_direct')}</option>
                </select>
              </div>

              <div class="form-group">
                <label class="form-label" for="node-sandbox-network">${t('node_modals.network_label')}</label>
                <select id="node-sandbox-network" class="form-select" ${isInherited ? 'disabled' : ''}>
                  <option value="egress_llm_only" ${effectiveSandbox.network === 'egress_llm_only' ? 'selected' : ''}>${t('node_modals.network_egress_llm')}</option>
                  <option value="offline" ${effectiveSandbox.network === 'offline' ? 'selected' : ''}>${t('node_modals.network_offline')}</option>
                  <option value="full" ${effectiveSandbox.network === 'full' ? 'selected' : ''}>${t('node_modals.network_full')}</option>
                </select>
              </div>
            </div>

            <div class="form-row-grid">
              <div class="form-group">
                <label class="form-label" for="node-sandbox-memory">${t('node_modals.memory_limit_label')}</label>
                <input type="number" id="node-sandbox-memory" class="form-input" value="${effectiveSandbox.maxMemoryMB}" min="64" step="64" ${isInherited ? 'disabled' : ''} />
              </div>

              <div class="form-group">
                <label class="form-label" for="node-sandbox-timeout">${t('node_modals.timeout_label')}</label>
                <input type="number" id="node-sandbox-timeout" class="form-input" value="${effectiveSandbox.timeoutSeconds}" min="10" step="10" ${isInherited ? 'disabled' : ''} />
              </div>
            </div>

            <div class="form-group">
              <label class="form-label" for="node-sandbox-ro">${t('node_modals.ro_mounts_label')}</label>
              <input type="text" id="node-sandbox-ro" class="form-input" value="${this.escapeHtml((effectiveSandbox.readOnlyMounts || []).join(', '))}" placeholder=".agents/, shared/" ${isInherited ? 'disabled' : ''} />
              <div class="form-hint">${t('node_modals.ro_mounts_desc')}</div>
            </div>

            <div class="form-group">
              <label class="form-label" for="node-sandbox-rw">${t('node_modals.rw_mounts_label')}</label>
              <input type="text" id="node-sandbox-rw" class="form-input" value="${this.escapeHtml((effectiveSandbox.readWriteMounts || []).join(', '))}" placeholder="output/, assets/" ${isInherited ? 'disabled' : ''} />
              <div class="form-hint">${t('node_modals.rw_mounts_desc')}</div>
            </div>
          </div>
        </div>

        <div class="modal-footer">
          <button class="btn btn-secondary-pill" id="btn-cancel-sandbox-modal">${t('global_defaults.btn_cancel')}</button>
          <button class="btn btn-primary" id="btn-save-sandbox-modal">${t('node_modals.btn_save_agent_settings')}</button>
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
      t('node_modals.cmd_change_sandbox', { label: node.label })
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
