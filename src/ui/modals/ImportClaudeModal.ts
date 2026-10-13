import { Graph } from '../../core/Graph.ts';
import { Viewport } from '../../view/Viewport.ts';
import { AddNodeCommand } from '../../core/history/GraphCommands.ts';
import { INodeAgentSettings } from '../../core/types.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';
import { t } from '../../i18n/index.ts';

/**
 * Arestrix Studio - Diálogo de Importação do Claude (Anthropic)
 * Padrão Microsoft Office (Fluent Dialog)
 * Rigoroso respeito: Zero sombras/glows em fontes
 */
export class ImportClaudeModal {
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
      this.modal.id = 'modal-import-claude';
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
      title: t('import_modals.claude_title'),
      subtitle: t('import_modals.claude_subtitle'),
      icon: '🧠',
      closeBtnId: 'btn-close-claude-modal'
    });

    this.modal.innerHTML = `
      <div class="modal-window" style="max-width: 680px; width: 92%; max-height: 88vh; display: flex; flex-direction: column;">
        ${titleBarHtml}

        <!-- Corpo do Diálogo -->
        <div class="modal-body" style="flex: 1; overflow-y: auto; padding: 16px; display: flex; flex-direction: column; gap: 12px;">
          <div class="office-group" style="margin-bottom: 0;">
            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-bottom: 12px;">
              <div>
                <label class="office-label" for="claude-agent-name">${t('import_modals.label_agent_name')}</label>
                <input type="text" id="claude-agent-name" class="office-input" value="Agente_Claude_Sonnet">
              </div>
              <div>
                <label class="office-label" for="claude-model-select">${t('import_modals.claude_model_label')}</label>
                <select id="claude-model-select" class="office-select">
                  <option value="claude-3-5-sonnet">${t('import_modals.opt_claude_35_sonnet')}</option>
                  <option value="claude-3-opus">${t('import_modals.opt_claude_3_opus')}</option>
                  <option value="claude-3-5-haiku">${t('import_modals.opt_claude_35_haiku')}</option>
                </select>
              </div>
            </div>

            <div>
              <label class="office-label" for="claude-prompt-input">${t('import_modals.claude_prompt_label')}</label>
              <textarea id="claude-prompt-input" class="office-textarea" rows="8" placeholder="${t('import_modals.claude_prompt_placeholder')}"></textarea>
            </div>
          </div>
        </div>

        <!-- Rodapé Estilo Office -->
        <div class="modal-footer">
          <span class="footer-info">${t('import_modals.ready_create_vertex')}</span>
          <div class="footer-actions">
            <button id="btn-cancel-claude" class="tool-btn btn-secondary">${t('import_modals.btn_cancel')}</button>
            <button id="btn-inject-claude-agent" class="tool-btn btn-action">${t('import_modals.btn_import')}</button>
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
    this.modal.querySelector('#btn-close-claude-modal')?.addEventListener('click', () => this.hide());
    this.modal.querySelector('#btn-cancel-claude')?.addEventListener('click', () => this.hide());
    this.modal.querySelector('#btn-inject-claude-agent')?.addEventListener('click', () => this.injectClaudeAgent());
  }

  public injectClaudeAgent(options?: { name?: string; model?: string; prompt?: string }): void {
    const nameInput = this.modal?.querySelector('#claude-agent-name') as HTMLInputElement;
    const modelSelect = this.modal?.querySelector('#claude-model-select') as HTMLSelectElement;
    const promptInput = this.modal?.querySelector('#claude-prompt-input') as HTMLTextAreaElement;

    const agentName = options?.name || nameInput?.value?.trim() || 'Agente_Claude';
    const modelName = options?.model || modelSelect?.value || 'claude-3-5-sonnet';
    const prompt = options?.prompt || promptInput?.value?.trim() || 'Instruções padrão do Claude';

    const nodeId = `claude_agent_${Date.now()}`;
    const center = this.viewport ? this.viewport.getCameraCenter() : { x: 0, y: 0 };
    const node = this.graph.buildNode(nodeId, agentName, 'agent', center.x, center.y);

    const agentSettings: INodeAgentSettings = {
      inheritSandbox: false,
      sandbox: {
        isolationLevel: 'container',
        network: 'egress_llm_only',
        maxMemoryMB: 2048,
        timeoutSeconds: 300,
        readOnlyMounts: ['/workspace'],
        readWriteMounts: ['/workspace/out']
      },
      inheritModel: false,
      model: {
        provider: 'anthropic',
        model: modelName,
        temperature: 0.2,
        promptTemplate: prompt
      },
      inheritCodeMode: false,
      codeMode: {
        allowedTools: ['read', 'write', 'edit', 'bash'],
        customScripts: ['scripts/claude/run.py']
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
