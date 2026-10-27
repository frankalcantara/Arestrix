import { BaseModal } from './BaseModal.ts';
import { Graph } from '../../core/Graph.ts';
import { INodeData, ISandboxPolicy, SandboxIsolationLevel, SandboxNetworkPolicy } from '../../core/types.ts';
import { UpdateNodeCommand } from '../../core/history/GraphCommands.ts';
import { t } from '../../i18n/index.ts';

/**
 * Sandbox individual do vértice.
 *
 * Usa o mesmo shell Office dos modais de Propriedades e mantém a herança
 * explícita em relação ao sandbox do Workspace/Projeto.
 */
export class NodeSandboxModal extends BaseModal {
  private graph: Graph;
  private activeNodeId: string | null = null;
  private activeTab: 'isolation' | 'network' | 'mounts' = 'isolation';

  constructor(graph: Graph) {
    super({
      id: 'node-sandbox-modal',
      title: t('node_modals.sandbox_title'),
      icon: '🛡️',
      minWidth: 700,
      minHeight: 500,
      dialogClass: 'node-settings-modal-dialog',
      closeOnBackdropClick: true
    });
    this.graph = graph;
  }

  public override open(nodeId: string): void {
    const node = this.graph.getNode(nodeId);
    if (!node) return;

    this.activeNodeId = nodeId;
    this.activeTab = 'isolation';
    this.config.subtitle = t('node_modals.sandbox_subtitle', {
      label: this.escapeHtml(node.label),
      id: node.id
    });
    super.open();
  }

  public override hide(): void {
    super.hide();
    this.activeNodeId = null;
  }

  protected override renderBody(): string {
    const node = this.activeNodeId ? this.graph.getNode(this.activeNodeId) : null;
    if (!node) {
      return `<div class="properties-empty">${t('node_modals.no_active_vertex', 'Nenhum vértice ativo selecionado.')}</div>`;
    }

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

    return `
      <div class="modal-tabs" role="tablist">
        <button type="button" class="tab-btn ${this.activeTab === 'isolation' ? 'active' : ''}" data-tab="isolation">
          <span>🛡️</span> ${t('node_modals.tab_isolation', '1. Isolamento & Recursos')}
        </button>
        <button type="button" class="tab-btn ${this.activeTab === 'network' ? 'active' : ''}" data-tab="network">
          <span>🌐</span> ${t('node_modals.tab_network', '2. Rede & Confinamento')}
        </button>
        <button type="button" class="tab-btn ${this.activeTab === 'mounts' ? 'active' : ''}" data-tab="mounts">
          <span>📁</span> ${t('node_modals.tab_mounts', '3. Pastas & Volumes')}
        </button>
      </div>

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
        <div class="tab-pane" id="pane-tab-isolation" style="${this.activeTab === 'isolation' ? '' : 'display: none;'}">
          <div class="form-group">
            <label class="form-label" for="node-sandbox-isolation">${t('node_modals.isolation_label')}</label>
            <select id="node-sandbox-isolation" class="form-select" ${isInherited ? 'disabled' : ''}>
              <option value="container" ${effectiveSandbox.isolationLevel === 'container' ? 'selected' : ''}>${t('node_modals.isolation_container')}</option>
              <option value="microvm" ${effectiveSandbox.isolationLevel === 'microvm' ? 'selected' : ''}>${t('node_modals.isolation_microvm')}</option>
              <option value="host_direct" ${effectiveSandbox.isolationLevel === 'host_direct' ? 'selected' : ''}>${t('node_modals.isolation_host_direct')}</option>
            </select>
          </div>

          <div class="form-row-grid" style="margin-top: 14px;">
            <div class="form-group">
              <label class="form-label" for="node-sandbox-memory">${t('node_modals.memory_limit_label')}</label>
              <input type="number" id="node-sandbox-memory" class="form-input" value="${effectiveSandbox.maxMemoryMB}" min="64" step="64" ${isInherited ? 'disabled' : ''} />
            </div>

            <div class="form-group">
              <label class="form-label" for="node-sandbox-timeout">${t('node_modals.timeout_label')}</label>
              <input type="number" id="node-sandbox-timeout" class="form-input" value="${effectiveSandbox.timeoutSeconds}" min="10" step="10" ${isInherited ? 'disabled' : ''} />
            </div>
          </div>
        </div>

        <div class="tab-pane" id="pane-tab-network" style="${this.activeTab === 'network' ? '' : 'display: none;'}">
          <div class="form-group">
            <label class="form-label" for="node-sandbox-network">${t('node_modals.network_label')}</label>
            <select id="node-sandbox-network" class="form-select" ${isInherited ? 'disabled' : ''}>
              <option value="egress_llm_only" ${effectiveSandbox.network === 'egress_llm_only' ? 'selected' : ''}>${t('node_modals.network_egress_llm')}</option>
              <option value="offline" ${effectiveSandbox.network === 'offline' ? 'selected' : ''}>${t('node_modals.network_offline')}</option>
              <option value="full" ${effectiveSandbox.network === 'full' ? 'selected' : ''}>${t('node_modals.network_full')}</option>
            </select>
            <div class="form-hint" style="margin-top: 6px;">${t('node_modals.network_policy_hint', 'A restrição de rede impede vazamento de dados confidenciais e conexões indevidas.')}</div>
          </div>
        </div>

        <div class="tab-pane" id="pane-tab-mounts" style="${this.activeTab === 'mounts' ? '' : 'display: none;'}">
          <div class="form-group">
            <label class="form-label" for="node-sandbox-ro">${t('node_modals.ro_mounts_label')}</label>
            <input type="text" id="node-sandbox-ro" class="form-input" value="${this.escapeHtml((effectiveSandbox.readOnlyMounts || []).join(', '))}" placeholder=".agents/, shared/" ${isInherited ? 'disabled' : ''} />
            <div class="form-hint">${t('node_modals.ro_mounts_desc')}</div>
          </div>

          <div class="form-group" style="margin-top: 14px;">
            <label class="form-label" for="node-sandbox-rw">${t('node_modals.rw_mounts_label')}</label>
            <input type="text" id="node-sandbox-rw" class="form-input" value="${this.escapeHtml((effectiveSandbox.readWriteMounts || []).join(', '))}" placeholder="output/, assets/" ${isInherited ? 'disabled' : ''} />
            <div class="form-hint">${t('node_modals.rw_mounts_desc')}</div>
          </div>
        </div>
      </div>
    `;
  }

  protected override renderFooter(): string {
    return this.renderStandardFooter({
      secondaryId: 'btn-cancel-sandbox-modal',
      primaryId: 'btn-save-sandbox-modal',
      secondaryLabel: t('global_defaults.btn_cancel'),
      primaryLabel: t('node_modals.btn_save_agent_settings')
    });
  }

  protected override bindEvents(): void {
    super.bindEvents();

    const node = this.activeNodeId ? this.graph.getNode(this.activeNodeId) : null;
    if (!node) return;

    this.dialog.querySelector('#btn-cancel-sandbox-modal')?.addEventListener('click', () => this.hide());

    const tabButtons = this.dialog.querySelectorAll<HTMLButtonElement>('.modal-tabs .tab-btn');
    tabButtons.forEach(btn => {
      btn.addEventListener('click', () => {
        const tab = (btn.dataset?.tab || btn.getAttribute('data-tab')) as 'isolation' | 'network' | 'mounts';
        if (!tab) return;
        this.activeTab = tab;
        tabButtons.forEach(other => {
          const otherTab = other.dataset?.tab || other.getAttribute('data-tab');
          other.classList.toggle('active', otherTab === tab);
        });
        const panes = this.dialog.querySelectorAll<HTMLElement>('.tab-pane');
        panes.forEach(pane => {
          pane.style.display = pane.id === `pane-tab-${tab}` ? '' : 'none';
        });
      });
    });

    const inheritCheck = this.dialog.querySelector('#chk-inherit-sandbox') as HTMLInputElement | null;
    const formWrapper = this.dialog.querySelector('#sandbox-form-wrapper') as HTMLElement | null;

    inheritCheck?.addEventListener('change', () => {
      const isInherited = inheritCheck.checked;
      formWrapper?.querySelectorAll<HTMLInputElement | HTMLSelectElement>('input, select').forEach(input => {
        input.disabled = isInherited;
      });
      formWrapper?.classList.toggle('form-disabled-opacity', isInherited);
    });

    this.dialog.querySelector('#btn-save-sandbox-modal')?.addEventListener('click', () => {
      this.saveNodeSandbox(node, inheritCheck?.checked ?? true);
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
      const isolationLevel = (this.dialog.querySelector('#node-sandbox-isolation') as HTMLSelectElement)?.value as SandboxIsolationLevel || 'container';
      const network = (this.dialog.querySelector('#node-sandbox-network') as HTMLSelectElement)?.value as SandboxNetworkPolicy || 'egress_llm_only';
      const maxMemoryMB = parseInt((this.dialog.querySelector('#node-sandbox-memory') as HTMLInputElement)?.value, 10) || 512;
      const timeoutSeconds = parseInt((this.dialog.querySelector('#node-sandbox-timeout') as HTMLInputElement)?.value, 10) || 300;

      const roRaw = (this.dialog.querySelector('#node-sandbox-ro') as HTMLInputElement)?.value || '';
      const readOnlyMounts = roRaw.split(',').map(value => value.trim()).filter(Boolean);

      const rwRaw = (this.dialog.querySelector('#node-sandbox-rw') as HTMLInputElement)?.value || '';
      const readWriteMounts = rwRaw.split(',').map(value => value.trim()).filter(Boolean);

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
}
