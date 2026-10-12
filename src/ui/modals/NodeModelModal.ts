import { Graph } from '../../core/Graph.ts';
import { INodeData, IModelConfig, LLMProvider } from '../../core/types.ts';
import { UpdateNodeCommand } from '../../core/history/GraphCommands.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';
import { SkillEditorModal } from './SkillEditorModal.ts';

export class NodeModelModal {
  private overlay: HTMLElement;
  private graph: Graph;
  private windowController?: IModalWindowController;
  private skillEditorModal: SkillEditorModal;

  constructor(graph: Graph) {
    this.graph = graph;
    this.skillEditorModal = new SkillEditorModal();

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

    const isInherited = settings.inheritModel !== false;
    const effectiveModel: IModelConfig = isInherited
      ? globalDefaults.model
      : {
          ...globalDefaults.model,
          ...(settings.model || {})
        };

    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: 'Modelo de IA, Skill & Prompt',
      subtitle: `Vértice: <strong>${this.escapeHtml(node.label)}</strong> (ID: ${node.id})`,
      icon: '🧠',
      closeBtnId: 'btn-close-model-modal'
    });

    this.overlay.innerHTML = `
      <div class="modal-dialog node-settings-modal-dialog">
        ${titleBarHtml}
        <div class="modal-body-scrollable">
          <!-- Toggle de Herança -->
          <div class="inheritance-toggle-card">
            <label class="inheritance-checkbox-label">
              <input type="checkbox" id="chk-inherit-model" ${isInherited ? 'checked' : ''} />
              <div class="inheritance-label-text">
                <span class="inheritance-title">Herdar Padrões Globais de IA</span>
                <span class="inheritance-desc">Usa o modelo configurado na Toolbar (${this.getProviderLabel(globalDefaults.model.provider)} / ${globalDefaults.model.model})</span>
              </div>
            </label>
            <div class="inheritance-status-badge ${isInherited ? 'badge-inherited' : 'badge-overridden'}">
              ${isInherited ? 'HERDADO DO PIPELINE' : 'CUSTOMIZADO PARA ESTE VÉRTICE'}
            </div>
          </div>

          <div class="settings-form-wrapper ${isInherited ? 'form-disabled-opacity' : ''}" id="model-form-wrapper">
            <div class="form-row-grid">
              <div class="form-group">
                <label class="form-label" for="node-model-provider">Provedor de IA</label>
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
                <label class="form-label" for="node-model-name">Identificador do Modelo</label>
                <input type="text" id="node-model-name" class="form-input" value="${this.escapeHtml(effectiveModel.model)}" placeholder="ex: claude-opus-5.5, deepseek-v4-pro" ${isInherited ? 'disabled' : ''} />
              </div>
            </div>

            <div class="form-group">
              <label class="form-label" for="node-skill-path">Arquivo de Skill (.md)</label>
              <div style="display: flex; gap: 8px; align-items: center;">
                <input type="text" id="node-skill-path" class="form-input" style="flex: 1;" value="${this.escapeHtml(effectiveModel.skillPath || '')}" placeholder=".agents/default/SKILL.md" ${isInherited ? 'disabled' : ''} />
                <button type="button" class="btn btn-secondary-pill" id="btn-edit-skill-modal" title="Abrir editor de Markdown para este skill" style="white-space: nowrap; padding: 6px 12px; font-size: 12px;">✎ Editar Skill</button>
                <button type="button" class="btn btn-secondary-pill" id="btn-new-skill-modal" title="Criar um novo arquivo de skill a partir do template padrão" style="white-space: nowrap; padding: 6px 12px; font-size: 12px;">＋ Novo Skill</button>
              </div>
              <div class="form-hint">Caminho relativo para as diretrizes e instruções de especialização do agente.</div>
            </div>

            <div class="form-group">
              <label class="form-label" for="node-prompt-template">Template de Prompt da Tarefa</label>
              <textarea id="node-prompt-template" class="form-textarea" rows="4" ${isInherited ? 'disabled' : ''}>${this.escapeHtml(effectiveModel.promptTemplate || '')}</textarea>
              <div class="form-hint">Variáveis interpoladas: <code>\${id}</code>, <code>\${arquivoArtigo}</code>, <code>\${arquivoPlanSvg}</code>.</div>
            </div>

            <div class="form-group">
              <label class="form-label" for="node-temperature">Temperatura de Amostragem (0.0 a 1.0)</label>
              <input type="number" id="node-temperature" class="form-input" min="0" max="1" step="0.05" value="${effectiveModel.temperature ?? 0.7}" ${isInherited ? 'disabled' : ''} />
            </div>
          </div>
        </div>

        <div class="modal-footer">
          <button class="btn btn-secondary-pill" id="btn-cancel-model-modal">Cancelar</button>
          <button class="btn btn-primary" id="btn-save-model-modal">Salvar Configuração</button>
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
      `Alterar modelo de IA de "${node.label}"`
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
