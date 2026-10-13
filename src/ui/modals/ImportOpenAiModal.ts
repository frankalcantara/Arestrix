import { Graph } from '../../core/Graph.ts';
import { Viewport } from '../../view/Viewport.ts';
import { AddNodeCommand } from '../../core/history/GraphCommands.ts';
import { INodeAgentSettings } from '../../core/types.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';
import { t } from '../../i18n/index.ts';

/**
 * Arestrix Studio - Diálogo de Importação da OpenAI (ChatGPT / Assistants)
 * Padrão Microsoft Office (Fluent Dialog)
 * Rigoroso respeito: Zero sombras/glows em fontes
 */
export class ImportOpenAiModal {
  private modal?: HTMLElement;
  private graph: Graph;
  private viewport?: Viewport;
  private windowController?: IModalWindowController;

  constructor(graph: Graph, viewport?: Viewport) {
    this.graph = graph;
    this.viewport = viewport;

    if (typeof document !== 'undefined') {
      this.modal = document.createElement('div');
      this.modal.className = 'modal-backdrop';
      this.modal.id = 'modal-import-openai';
      this.modal.style.display = 'none';
      this.modal.addEventListener('click', (e: MouseEvent) => {
        if (e.target === this.modal) this.hide();
      });
      document.body.appendChild(this.modal);
      this.render();
    }
  }

  public show(): void {
    if (this.modal) {
      this.render();
      this.modal.style.display = 'flex';
      this.windowController?.bringToFront();
    }
  }

  public hide(): void {
    if (this.modal) {
      this.modal.style.display = 'none';
    }
  }

  private render(): void {
    if (!this.modal) return;

    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: t('import_modals.openai_title'),
      subtitle: t('import_modals.openai_subtitle'),
      icon: '🤖',
      closeBtnId: 'btn-close-openai-modal'
    });

    this.modal.innerHTML = `
      <div class="modal-window" style="max-width: 680px; width: 92%; max-height: 88vh; display: flex; flex-direction: column;">
        ${titleBarHtml}

        <!-- Corpo do Diálogo -->
        <div class="modal-body" style="flex: 1; overflow-y: auto; padding: 16px; display: flex; flex-direction: column; gap: 12px;">
          <div class="office-group" style="margin-bottom: 0;">
            <div style="grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-bottom: 12px; display: grid;">
              <div>
                <label class="office-label" for="openai-agent-name">${t('import_modals.label_agent_name')}</label>
                <input type="text" id="openai-agent-name" class="office-input" value="Agente_GPT4o">
              </div>
              <div>
                <label class="office-label" for="openai-model-select">${t('import_modals.openai_model_label')}</label>
                <select id="openai-model-select" class="office-select">
                  <option value="gpt-4o">${t('import_modals.opt_gpt_4o')}</option>
                  <option value="o1-preview">${t('import_modals.opt_o1_preview')}</option>
                  <option value="gpt-4o-mini">${t('import_modals.opt_gpt_4o_mini')}</option>
                </select>
              </div>
            </div>

            <div>
              <label class="office-label" for="openai-prompt-input">${t('import_modals.openai_prompt_label')}</label>
              <textarea id="openai-prompt-input" class="office-textarea" rows="8" placeholder="${t('import_modals.openai_prompt_placeholder')}"></textarea>
            </div>
          </div>
        </div>

        <!-- Rodapé Estilo Office -->
        <div class="modal-footer">
          <span class="footer-info">${t('import_modals.ready_create_vertex')}</span>
          <div class="footer-actions">
            <button id="btn-cancel-openai" class="tool-btn btn-secondary">${t('import_modals.btn_cancel')}</button>
            <button id="btn-inject-openai-agent" class="tool-btn btn-action">${t('import_modals.btn_import')}</button>
          </div>
        </div>
      </div>
    `;

    const dialog = this.modal.querySelector?.('.modal-window') as HTMLElement;
    if (dialog) {
      this.windowController = ModalWindowManager.attach({
        dialog,
        minWidth: 540,
        minHeight: 400,
        onClose: () => this.hide()
      });
    }

    this.bindEvents();
  }

  private bindEvents(): void {
    if (!this.modal) return;
    this.modal.querySelector('#btn-close-openai-modal')?.addEventListener('click', () => this.hide());
    this.modal.querySelector('#btn-cancel-openai')?.addEventListener('click', () => this.hide());
    this.modal.querySelector('#btn-inject-openai-agent')?.addEventListener('click', () => this.injectOpenAiAgent());
  }

  public injectOpenAiAgent(options?: { name?: string; model?: string; prompt?: string }): void {
    const nameInput = this.modal?.querySelector('#openai-agent-name') as HTMLInputElement;
    const modelSelect = this.modal?.querySelector('#openai-model-select') as HTMLSelectElement;
    const promptInput = this.modal?.querySelector('#openai-prompt-input') as HTMLTextAreaElement;

    const agentName = options?.name || nameInput?.value?.trim() || 'Agente_OpenAI';
    const modelName = options?.model || modelSelect?.value || 'gpt-4o';
    const prompt = options?.prompt || promptInput?.value?.trim() || 'Instruções padrão OpenAI';

    const nodeId = `openai_agent_${Date.now()}`;
    const center = this.viewport ? this.viewport.getCameraCenter() : { x: 0, y: 0 };
    const node = this.graph.buildNode(nodeId, agentName, 'agent', center.x, center.y);

    const agentSettings: INodeAgentSettings = {
      inheritSandbox: false,
      sandbox: {
        isolationLevel: 'container',
        network: 'egress_llm_only',
        maxMemoryMB: 1536,
        timeoutSeconds: 300,
        readOnlyMounts: ['/workspace'],
        readWriteMounts: ['/workspace/out']
      },
      inheritModel: false,
      model: {
        provider: 'openai',
        model: modelName,
        temperature: 0.3,
        promptTemplate: prompt
      },
      inheritCodeMode: false,
      codeMode: {
        allowedTools: ['read', 'write', 'bash'],
        customScripts: ['scripts/openai/run.py']
      }
    };

    node.agentSettings = agentSettings;

    const cmd = new AddNodeCommand(this.graph, node);
    this.graph.commandManager.execute(cmd);

    if (this.viewport) {
      this.viewport.selectNode(nodeId);
    }

    this.hide();
  }
}
