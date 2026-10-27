import { BaseModal } from './BaseModal.ts';
import { Graph } from '../../core/Graph.ts';
import { INodeData, ICodeModeConfig, CodeModeTool } from '../../core/types.ts';
import { UpdateNodeCommand } from '../../core/history/GraphCommands.ts';
import { t } from '../../i18n/index.ts';
import { McpToolPicker } from '../McpToolPicker.ts';

export class NodeToolsModal extends BaseModal {
  private graph: Graph;
  private mcpPicker?: McpToolPicker;
  private activeNodeId: string | null = null;
  private activeTab: 'native' | 'mcp' | 'governance' = 'native';

  constructor(graph: Graph) {
    super({
      id: 'node-tools-modal',
      title: t('node_modals.tools_title'),
      icon: '🔧',
      minWidth: 720,
      minHeight: 520,
      dialogClass: 'node-settings-modal-dialog',
      closeOnBackdropClick: true
    });
    this.graph = graph;
  }

  public override open(nodeId: string): void {
    const node = this.graph.getNode(nodeId);
    if (!node) return;

    this.activeNodeId = nodeId;
    this.activeTab = 'native';
    this.config.subtitle = t('node_modals.tools_subtitle', {
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

    const isInherited = settings.inheritCodeMode !== false;
    const effectiveTools: ICodeModeConfig = isInherited
      ? globalDefaults.codeMode
      : {
          ...globalDefaults.codeMode,
          ...(settings.codeMode || {})
        };

    const allowed = effectiveTools.allowedTools || [];

    return `
        <div class="modal-tabs" role="tablist">
          <button type="button" class="tab-btn ${this.activeTab === 'native' ? 'active' : ''}" data-tab="native">
            <span>🛠️</span> ${t('node_modals.tab_native_tools', '1. Ferramentas Nativas')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'mcp' ? 'active' : ''}" data-tab="mcp">
            <span>⚡</span> ${t('node_modals.tab_mcp_tools', '2. Servidores MCP')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'governance' ? 'active' : ''}" data-tab="governance">
            <span>🛡️</span> ${t('node_modals.tab_validation', '3. Validação & Lote')}
          </button>
        </div>

          <!-- Toggle de Herança -->
          <div class="inheritance-toggle-card">
            <label class="inheritance-checkbox-label">
              <input type="checkbox" id="chk-inherit-tools" ${isInherited ? 'checked' : ''} />
              <div class="inheritance-label-text">
                <span class="inheritance-title">${t('node_modals.inherit_tools_title')}</span>
                <span class="inheritance-desc">${t('node_modals.inherit_tools_desc', { tools: (globalDefaults.codeMode.allowedTools || []).join(', ') })}</span>
              </div>
            </label>
            <div class="inheritance-status-badge ${isInherited ? 'badge-inherited' : 'badge-overridden'}">
              ${isInherited ? t('node_modals.badge_inherited') : t('node_modals.badge_overridden')}
            </div>
          </div>

          <div class="settings-form-wrapper ${isInherited ? 'form-disabled-opacity' : ''}" id="tools-form-wrapper">
            <!-- ABA 1: Ferramentas Nativas -->
            <div class="tab-pane" id="pane-tab-native" style="${this.activeTab === 'native' ? '' : 'display: none;'}">
              <div class="tools-checkbox-grid">
                <label class="tool-checkbox-pill ${allowed.includes('write') ? 'checked' : ''}">
                  <input type="checkbox" class="node-tool-check" value="write" ${allowed.includes('write') ? 'checked' : ''} ${isInherited ? 'disabled' : ''} />
                  <span class="tool-pill-name">tools.write</span>
                  <span class="tool-pill-desc">${t('node_modals.tool_desc_write')}</span>
                </label>

                <label class="tool-checkbox-pill ${allowed.includes('edit') ? 'checked' : ''}">
                  <input type="checkbox" class="node-tool-check" value="edit" ${allowed.includes('edit') ? 'checked' : ''} ${isInherited ? 'disabled' : ''} />
                  <span class="tool-pill-name">tools.edit</span>
                  <span class="tool-pill-desc">${t('node_modals.tool_desc_edit')}</span>
                </label>

                <label class="tool-checkbox-pill ${allowed.includes('read') ? 'checked' : ''}">
                  <input type="checkbox" class="node-tool-check" value="read" ${allowed.includes('read') ? 'checked' : ''} ${isInherited ? 'disabled' : ''} />
                  <span class="tool-pill-name">tools.read</span>
                  <span class="tool-pill-desc">${t('node_modals.tool_desc_read')}</span>
                </label>

                <label class="tool-checkbox-pill ${allowed.includes('bash') ? 'checked' : ''}">
                  <input type="checkbox" class="node-tool-check" value="bash" ${allowed.includes('bash') ? 'checked' : ''} ${isInherited ? 'disabled' : ''} />
                  <span class="tool-pill-name">tools.bash</span>
                  <span class="tool-pill-desc">${t('node_modals.tool_desc_bash')}</span>
                </label>

                <label class="tool-checkbox-pill ${allowed.includes('validate_svg') ? 'checked' : ''}">
                  <input type="checkbox" class="node-tool-check" value="validate_svg" ${allowed.includes('validate_svg') ? 'checked' : ''} ${isInherited ? 'disabled' : ''} />
                  <span class="tool-pill-name">tools.validate_svg</span>
                  <span class="tool-pill-desc">${t('node_modals.tool_desc_validate_svg')}</span>
                </label>
              </div>

              <div class="form-group" style="margin-top: 16px;">
                <label class="form-label" for="node-custom-scripts">${t('node_modals.custom_scripts_label')}</label>
                <input type="text" id="node-custom-scripts" class="form-input" value="${this.escapeHtml((effectiveTools.customScripts || []).join(', '))}" placeholder="python scripts/audit_exercises.py" ${isInherited ? 'disabled' : ''} />
                <div class="form-hint">${t('node_modals.custom_scripts_hint')}</div>
              </div>
            </div>

            <!-- ABA 2: Servidores MCP -->
            <div class="tab-pane" id="pane-tab-mcp" style="${this.activeTab === 'mcp' ? '' : 'display: none;'}">
              <div style="padding-top: 6px;">
                <div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 10px;">
                  <label class="form-label" style="margin: 0; font-weight: 700;">${t('node_modals.mcp_section_title')}</label>
                  <span style="font-size: 11px; color: var(--text-secondary);">${t('node_modals.mcp_section_sub')}</span>
                </div>
                <div id="node-mcp-picker"></div>
              </div>
            </div>

            <!-- ABA 3: Validação de Artefatos & Lote -->
            <div class="tab-pane" id="pane-tab-governance" style="${this.activeTab === 'governance' ? '' : 'display: none;'}">
              <div class="settings-section-card">
                <div class="section-card-header">
                  <span class="section-card-icon">🛡️</span>
                  <div class="section-card-titles">
                    <h3 class="section-card-title">${t('node_modals.artifact_section_title')}</h3>
                    <p class="section-card-desc">${t('node_modals.artifact_section_desc')}</p>
                  </div>
                </div>

                <div class="form-row-grid">
                  <div class="form-group full-width">
                    <label class="form-label" for="node-artifact-pattern">${t('node_modals.artifact_pattern_label')}</label>
                    <input type="text" id="node-artifact-pattern" class="form-input" value="${this.escapeHtml((settings.validationRules && settings.validationRules[0]?.targetPathPattern) || '')}" placeholder="output/{itemId}.md" />
                    <div class="form-hint">${t('node_modals.artifact_pattern_hint')}</div>
                  </div>

                  <div class="form-group">
                    <label class="form-label" for="node-artifact-min-size">${t('node_modals.artifact_min_size_label')}</label>
                    <input type="number" id="node-artifact-min-size" class="form-input" value="${(settings.validationRules && settings.validationRules[0]?.minSizeBytes) || 0}" min="0" step="100" />
                  </div>

                  <div class="form-group">
                    <label class="form-label" for="node-artifact-regex">${t('node_modals.artifact_regex_label')}</label>
                    <input type="text" id="node-artifact-regex" class="form-input" value="${this.escapeHtml((settings.validationRules && settings.validationRules[0]?.contentPattern) || '')}" placeholder="^---[\\s\\S]+---" />
                  </div>

                  <div class="form-group full-width">
                    <label class="clean-checkbox-label">
                      <input type="checkbox" id="chk-node-serial-batch" ${settings.isSerialAcrossBatch ? 'checked' : ''} />
                      <span><strong>${t('node_modals.serial_batch_label')}</strong> ${t('node_modals.serial_batch_desc')}</span>
                    </label>
                  </div>
                </div>
              </div>
            </div>
          </div>
    `;
  }

  protected override renderFooter(): string {
    return this.renderStandardFooter({
      secondaryId: 'btn-cancel-tools-modal',
      primaryId: 'btn-save-tools-modal',
      secondaryLabel: t('global_defaults.btn_cancel'),
      primaryLabel: t('node_modals.btn_save_agent_settings')
    });
  }

  protected override bindEvents(): void {
    super.bindEvents();
    const node = this.activeNodeId ? this.graph.getNode(this.activeNodeId) : null;
    if (!node) return;

    const globalDefaults = this.graph.getGlobalDefaults();
    const settings = node.agentSettings || {
      inheritSandbox: true,
      inheritModel: true,
      inheritCodeMode: true
    };
    const isInherited = settings.inheritCodeMode !== false;
    const effectiveTools: ICodeModeConfig = isInherited
      ? globalDefaults.codeMode
      : { ...globalDefaults.codeMode, ...(settings.codeMode || {}) };

    const pickerRoot = this.dialog.querySelector('#node-mcp-picker') as HTMLElement | null;
    if (pickerRoot) {
      this.mcpPicker = new McpToolPicker(pickerRoot);
      void this.mcpPicker.load({ servers: effectiveTools.mcpServers || [], tools: effectiveTools.allowedMcpTools || [] }, isInherited);
    }

    this.dialog.querySelector('#btn-cancel-tools-modal')?.addEventListener('click', () => this.hide());

    // Manipulador de abas
    const tabButtons = this.dialog.querySelectorAll<HTMLButtonElement>('.modal-tabs .tab-btn');
    tabButtons.forEach(btn => {
      btn.addEventListener('click', () => {
        const tab = (btn.dataset?.tab || btn.getAttribute('data-tab')) as 'native' | 'mcp' | 'governance';
        if (!tab) return;
        this.activeTab = tab;
        tabButtons.forEach(b => {
          const bTab = b.dataset?.tab || b.getAttribute('data-tab');
          if (b.classList && typeof b.classList.toggle === 'function') {
            b.classList.toggle('active', bTab === tab);
          }
        });
        const panes = this.dialog.querySelectorAll<HTMLElement>('.tab-pane');
        panes.forEach(p => {
          p.style.display = p.id === `pane-tab-${tab}` ? '' : 'none';
        });
      });
    });

    const inheritCheck = this.dialog.querySelector('#chk-inherit-tools') as HTMLInputElement;
    const formWrapper = this.dialog.querySelector('#tools-form-wrapper') as HTMLElement;

    inheritCheck?.addEventListener('change', () => {
      const isInherited = inheritCheck.checked;
      const inputs = formWrapper.querySelectorAll<HTMLInputElement>('input');
      inputs.forEach(input => {
        input.disabled = isInherited;
      });

      this.mcpPicker?.setDisabled(isInherited);

      if (isInherited) {
        formWrapper.classList.add('form-disabled-opacity');
      } else {
        formWrapper.classList.remove('form-disabled-opacity');
      }
    });

    this.dialog.querySelectorAll('.node-tool-check').forEach(chk => {
      chk.addEventListener('change', (e) => {
        const input = e.target as HTMLInputElement;
        const pill = input.closest('.tool-checkbox-pill');
        if (pill) {
          if (input.checked) pill.classList.add('checked');
          else pill.classList.remove('checked');
        }
      });
    });

    this.dialog.querySelector('#btn-save-tools-modal')?.addEventListener('click', () => {
      this.saveNodeTools(node, inheritCheck.checked);
      this.hide();
    });
  }

  private saveNodeTools(node: INodeData, inherit: boolean): void {
    const currentSettings = node.agentSettings || {
      inheritSandbox: true,
      inheritModel: true,
      inheritCodeMode: true
    };

    let newToolsData: Partial<ICodeModeConfig> | undefined;

    if (!inherit) {
      const allowedTools: CodeModeTool[] = [];
      this.dialog.querySelectorAll<HTMLInputElement>('.node-tool-check:checked').forEach(chk => {
        allowedTools.push(chk.value as CodeModeTool);
      });

      const mcp = this.mcpPicker?.getValue() ?? { servers: [], tools: [] };

      const scriptsRaw = (this.dialog.querySelector('#node-custom-scripts') as HTMLInputElement)?.value || '';
      const customScripts = scriptsRaw.split(',').map(s => s.trim()).filter(Boolean);

      newToolsData = {
        allowedTools,
        customScripts,
        mcpServers: mcp.servers,
        allowedMcpTools: mcp.tools
      };
    }

    // Regras de validação de artefato e encadeamento em lote
    const artPattern = (this.dialog.querySelector('#node-artifact-pattern') as HTMLInputElement)?.value.trim() || '';
    const minBytes = parseInt((this.dialog.querySelector('#node-artifact-min-size') as HTMLInputElement)?.value, 10) || 0;
    const artRegex = (this.dialog.querySelector('#node-artifact-regex') as HTMLInputElement)?.value.trim() || '';
    const isSerial = (this.dialog.querySelector('#chk-node-serial-batch') as HTMLInputElement)?.checked || false;

    const validationRules = artPattern ? [{
      targetPathPattern: artPattern,
      minSizeBytes: minBytes,
      contentPattern: artRegex,
      checkLogExitCode: true
    }] : undefined;

    const updatedSettings = {
      ...currentSettings,
      inheritCodeMode: inherit,
      codeMode: inherit ? undefined : newToolsData,
      validationRules,
      isSerialAcrossBatch: isSerial
    };

    const cmd = new UpdateNodeCommand(
      this.graph,
      node.id,
      { agentSettings: node.agentSettings },
      { agentSettings: updatedSettings },
      t('node_modals.cmd_change_tools', { label: node.label })
    );
    this.graph.commandManager.execute(cmd);
  }
}
