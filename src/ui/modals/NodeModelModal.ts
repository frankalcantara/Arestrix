import { Graph } from '../../core/Graph.ts';
import { INodeData, IModelConfig, LLMProvider } from '../../core/types.ts';
import { UpdateNodeCommand } from '../../core/history/GraphCommands.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';
import { SkillEditorModal } from './SkillEditorModal.ts';
import { t } from '../../i18n/index.ts';

export class NodeModelModal {
  private overlay: HTMLElement;
  private graph: Graph;
  private windowController?: IModalWindowController;
  private skillEditorModal: SkillEditorModal;
  private activeTab: 'provider' | 'prompt' | 'parameters' = 'provider';

  constructor(graph: Graph) {
    this.graph = graph;
    this.skillEditorModal = new SkillEditorModal(this.graph);

    this.overlay = document.createElement('div');
    this.overlay.className = 'modal-overlay hidden';
    document.body.appendChild(this.overlay);

    this.bindEvents();
  }

  public open(nodeId: string): void {
    const node = this.graph.getNode(nodeId);
    if (!node) return;

    this.activeTab = 'provider';
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

    const isInherited = settings.inheritModel !== false;
    const effectiveModel: IModelConfig = isInherited
      ? globalDefaults.model
      : {
          ...globalDefaults.model,
          ...(settings.model || {})
        };

    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: t('node_modals.model_title'),
      subtitle: t('node_modals.model_subtitle', { label: this.escapeHtml(node.label), id: node.id }),
      icon: '🧠',
      closeBtnId: 'btn-close-model-modal'
    });

    this.overlay.innerHTML = `
      <div class="modal-dialog node-settings-modal-dialog">
        ${titleBarHtml}

        <!-- Abas Fluent / Office 365 -->
        <div class="modal-tabs" role="tablist">
          <button type="button" class="tab-btn ${this.activeTab === 'provider' ? 'active' : ''}" data-tab="provider">
            <span>🧠</span> ${t('node_modals.tab_provider', '1. Provedor & Modelo')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'prompt' ? 'active' : ''}" data-tab="prompt">
            <span>📝</span> ${t('node_modals.tab_prompt', '2. Instruções & Skills')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'parameters' ? 'active' : ''}" data-tab="parameters">
            <span>⚙️</span> ${t('node_modals.tab_params', '3. Parâmetros')}
          </button>
        </div>

        <div class="modal-body-scrollable">
          <!-- Toggle de Herança do Projeto -->
          <div class="inheritance-toggle-card">
            <label class="inheritance-checkbox-label">
              <input type="checkbox" id="chk-inherit-model" ${isInherited ? 'checked' : ''} />
              <div class="inheritance-label-text">
                <span class="inheritance-title">${t('node_modals.inherit_model_title')}</span>
                <span class="inheritance-desc">${t('node_modals.inherit_model_desc', { provider: this.getProviderLabel(globalDefaults.model.provider), model: globalDefaults.model.model })}</span>
              </div>
            </label>
            <div class="inheritance-status-badge ${isInherited ? 'badge-inherited' : 'badge-overridden'}">
              ${isInherited ? t('node_modals.badge_inherited') : t('node_modals.badge_overridden')}
            </div>
          </div>

          <div class="settings-form-wrapper ${isInherited ? 'form-disabled-opacity' : ''}" id="model-form-wrapper">
            <!-- ABA 1: Provedor & Modelo -->
            <div class="tab-pane" id="pane-tab-provider" style="${this.activeTab === 'provider' ? '' : 'display: none;'}">
              <div class="form-row-grid">
                <div class="form-group">
                  <label class="form-label" for="node-model-provider">${t('node_modals.provider_label')}</label>
                  <select id="node-model-provider" class="form-select" ${isInherited ? 'disabled' : ''}>
                    <option value="deepseek" ${effectiveModel.provider === 'deepseek' ? 'selected' : ''}>DeepSeek</option>
                    <option value="anthropic" ${effectiveModel.provider === 'anthropic' ? 'selected' : ''}>Anthropic (Claude)</option>
                    <option value="google" ${effectiveModel.provider === 'google' ? 'selected' : ''}>Google (Gemini)</option>
                    <option value="openai" ${effectiveModel.provider === 'openai' ? 'selected' : ''}>OpenAI</option>
                    <option value="jev" ${effectiveModel.provider === 'jev' ? 'selected' : ''}>Jev / Engine Visual</option>
                    <option value="local" ${effectiveModel.provider === 'local' ? 'selected' : ''}>Local (Ollama / vLLM)</option>
                  </select>
                </div>

                <div class="form-group">
                  <label class="form-label" for="node-model-name">${t('node_modals.model_id_label')}</label>
                  <input type="text" id="node-model-name" class="form-input" value="${this.escapeHtml(effectiveModel.model)}" placeholder="ex: claude-opus-5.5, deepseek-v4-pro" ${isInherited ? 'disabled' : ''} />
                </div>
              </div>
            </div>

            <!-- ABA 2: Instruções & Skills -->
            <div class="tab-pane" id="pane-tab-prompt" style="${this.activeTab === 'prompt' ? '' : 'display: none;'}">
              <div class="form-group">
                <label class="form-label" for="node-skill-path">${t('node_modals.skill_path_label')}</label>
                <div style="display: flex; gap: 8px; align-items: center;">
                  <input type="text" id="node-skill-path" class="form-input" style="flex: 1;" value="${this.escapeHtml(effectiveModel.skillPath || '')}" placeholder=".agents/default/SKILL.md" ${isInherited ? 'disabled' : ''} />
                  <button type="button" class="btn btn-secondary-pill" id="btn-edit-skill-modal" title="${t('node_modals.btn_edit_skill_title')}" style="white-space: nowrap; padding: 6px 12px; font-size: 12px;">${t('node_modals.btn_edit_skill')}</button>
                  <button type="button" class="btn btn-secondary-pill" id="btn-new-skill-modal" title="${t('node_modals.btn_new_skill_title')}" style="white-space: nowrap; padding: 6px 12px; font-size: 12px;">${t('node_modals.btn_new_skill')}</button>
                </div>
                <div class="form-hint">${t('node_modals.skill_path_hint')}</div>
              </div>

              <div class="form-group" style="margin-top: 14px;">
                <label class="form-label" for="node-prompt-template">${t('node_modals.prompt_template_label')}</label>
                <textarea id="node-prompt-template" class="form-textarea" rows="4" ${isInherited ? 'disabled' : ''}>${this.escapeHtml(effectiveModel.promptTemplate || '')}</textarea>
                <div class="form-hint">${t('node_modals.prompt_vars_hint')}</div>
              </div>
            </div>

            <!-- ABA 3: Parâmetros Cognitivos -->
            <div class="tab-pane" id="pane-tab-parameters" style="${this.activeTab === 'parameters' ? '' : 'display: none;'}">
              <div class="form-group">
                <label class="form-label" for="node-temperature">${t('node_modals.temperature_label')}</label>
                <input type="number" id="node-temperature" class="form-input" min="0" max="1" step="0.05" value="${effectiveModel.temperature ?? 0.7}" ${isInherited ? 'disabled' : ''} />
                <div class="form-hint">${t('node_modals.temperature_hint', 'Valores menores geram respostas determinísticas; valores maiores geram respostas mais criativas.')}</div>
              </div>
            </div>
          </div>
        </div>

        <div class="modal-footer office-modal-footer">
          <button class="btn btn-secondary-pill btn-secondary-office" id="btn-cancel-model-modal">${t('global_defaults.btn_cancel')}</button>
          <button class="btn btn-primary btn-primary-office" id="btn-save-model-modal">${t('node_modals.btn_save_agent_settings')}</button>
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
    this.overlay.querySelector('#btn-close-model-modal')?.addEventListener('click', () => this.hide());
    this.overlay.querySelector('#btn-cancel-model-modal')?.addEventListener('click', () => this.hide());

    // Manipulador de abas
    const tabButtons = this.overlay.querySelectorAll<HTMLButtonElement>('.modal-tabs .tab-btn');
    tabButtons.forEach(btn => {
      btn.addEventListener('click', () => {
        const tab = (btn.dataset?.tab || btn.getAttribute('data-tab')) as 'provider' | 'prompt' | 'parameters';
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

    const inheritCheck = this.overlay.querySelector('#chk-inherit-model') as HTMLInputElement;
    const formWrapper = this.overlay.querySelector('#model-form-wrapper') as HTMLElement;
    const providerSelect = this.overlay.querySelector('#node-model-provider') as HTMLSelectElement;
    const modelInput = this.overlay.querySelector('#node-model-name') as HTMLInputElement;

    inheritCheck?.addEventListener('change', () => {
      const isInherited = inheritCheck.checked;
      const inputs = formWrapper.querySelectorAll<HTMLInputElement | HTMLSelectElement | HTMLTextAreaElement>('input, select, textarea');
      inputs.forEach(input => {
        input.disabled = isInherited;
      });

      if (isInherited) {
        formWrapper.classList.add('form-disabled-opacity');
      } else {
        formWrapper.classList.remove('form-disabled-opacity');
      }
    });

    providerSelect?.addEventListener('change', () => {
      const val = providerSelect.value;
      if (val === 'deepseek') modelInput.value = 'deepseek-flash';
      else if (val === 'anthropic') modelInput.value = 'claude-opus-5.5';
      else if (val === 'google') modelInput.value = 'gemini-2.5-flash';
      else if (val === 'openai') modelInput.value = 'gpt-4o';
      else if (val === 'jev') modelInput.value = 'jev-svg-validator';
    });

    const skillPathInput = this.overlay.querySelector('#node-skill-path') as HTMLInputElement;

    this.overlay.querySelector('#btn-edit-skill-modal')?.addEventListener('click', () => {
      const isInherited = inheritCheck.checked;
      const path = skillPathInput?.value.trim() || `.agents/${(node.label || 'agent').toLowerCase().replace(/[^a-z0-9_-]/g, '-')}/SKILL.md`;
      this.skillEditorModal.openSkill({
        path,
        nodeId: node.id,
        nodeLabel: node.label,
        readOnly: isInherited,
        onSave: (savedPath) => {
          if (skillPathInput) {
            skillPathInput.value = savedPath;
          }
        }
      });
    });

    this.overlay.querySelector('#btn-new-skill-modal')?.addEventListener('click', () => {
      const isInherited = inheritCheck.checked;
      const suggestedSlug = (node.label || 'agent').toLowerCase().replace(/[^a-z0-9_-]/g, '-');
      const suggestedPath = `.agents/${suggestedSlug}/SKILL.md`;
      this.skillEditorModal.openSkill({
        path: suggestedPath,
        nodeId: node.id,
        nodeLabel: node.label,
        readOnly: isInherited,
        onSave: (savedPath) => {
          if (skillPathInput) {
            skillPathInput.value = savedPath;
          }
        }
      });
    });

    this.overlay.querySelector('#btn-save-model-modal')?.addEventListener('click', () => {
      this.saveNodeModel(node, inheritCheck.checked);
      this.hide();
    });
  }

  private saveNodeModel(node: INodeData, inherit: boolean): void {
    const currentSettings = node.agentSettings || {
      inheritSandbox: true,
      inheritModel: true,
      inheritCodeMode: true
    };

    let newModelData: Partial<IModelConfig> | undefined;

    if (!inherit) {
      const provider = (this.overlay.querySelector('#node-model-provider') as HTMLSelectElement)?.value as LLMProvider || 'deepseek';
      const model = (this.overlay.querySelector('#node-model-name') as HTMLInputElement)?.value.trim() || 'deepseek-flash';
      const skillPath = (this.overlay.querySelector('#node-skill-path') as HTMLInputElement)?.value.trim();
      const promptTemplate = (this.overlay.querySelector('#node-prompt-template') as HTMLTextAreaElement)?.value.trim();
      const temperature = parseFloat((this.overlay.querySelector('#node-temperature') as HTMLInputElement)?.value) || 0.7;

      newModelData = {
        provider,
        model,
        skillPath,
        promptTemplate,
        temperature
      };
    }

    const updatedSettings = {
      ...currentSettings,
      inheritModel: inherit,
      model: inherit ? undefined : newModelData
    };

    const cmd = new UpdateNodeCommand(
      this.graph,
      node.id,
      { agentSettings: node.agentSettings },
      { agentSettings: updatedSettings },
      t('node_modals.cmd_change_model', { label: node.label })
    );
    this.graph.commandManager.execute(cmd);
  }

  private getProviderLabel(provider: LLMProvider): string {
    const map: Record<LLMProvider, string> = {
      anthropic: 'Anthropic',
      deepseek: 'DeepSeek',
      google: 'Google Gemini',
      openai: 'OpenAI',
      jev: 'Jev Visual',
      local: 'Local'
    };
    return map[provider] || provider;
  }

  private escapeHtml(text: string): string {
    return text
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
}
