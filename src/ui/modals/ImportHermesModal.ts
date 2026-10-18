import { Graph } from '../../core/Graph.ts';
import { Viewport } from '../../view/Viewport.ts';
import { AddNodeCommand } from '../../core/history/GraphCommands.ts';
import { INodeAgentSettings } from '../../core/types.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';
import { t } from '../../i18n/index.ts';

/**
 * Arestrix Studio - Diálogo de Importação do Hermes (Nous Research)
 * Padrão Microsoft Office (Fluent Dialog)
 * Rigoroso respeito: Zero sombras/glows em fontes
 */
export class ImportHermesModal {
  private modal?: HTMLElement;
  private graph: Graph;
  private viewport?: Viewport;
  private windowController?: IModalWindowController;
  private activeTab: 'model' | 'prompt' | 'sandbox' = 'model';

  constructor(graph: Graph, viewport?: Viewport) {
    this.graph = graph;
    this.viewport = viewport;

    if (typeof document !== 'undefined') {
      this.modal = document.createElement('div');
      this.modal.className = 'modal-backdrop';
      this.modal.id = 'modal-import-hermes';
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
      title: t('import_modals.hermes_title'),
      subtitle: t('import_modals.hermes_subtitle'),
      icon: '🏛️',
      closeBtnId: 'btn-close-hermes-modal'
    });

    this.modal.innerHTML = `
      <div class="modal-window" style="max-width: 680px; width: 92%; max-height: 88vh; display: flex; flex-direction: column;">
        ${titleBarHtml}

        <!-- Abas Estilo Office 365 -->
        <div class="modal-tabs" role="tablist">
          <button type="button" class="tab-btn ${this.activeTab === 'model' ? 'active' : ''}" data-tab="model" role="tab" aria-selected="${this.activeTab === 'model'}">
            ${t('import_modals.tab_model')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'prompt' ? 'active' : ''}" data-tab="prompt" role="tab" aria-selected="${this.activeTab === 'prompt'}">
            ${t('import_modals.tab_prompt')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'sandbox' ? 'active' : ''}" data-tab="sandbox" role="tab" aria-selected="${this.activeTab === 'sandbox'}">
            ${t('import_modals.tab_sandbox')}
          </button>
        </div>

        <!-- Corpo do Diálogo -->
        <div class="modal-body" style="flex: 1; overflow-y: auto; padding: 16px; display: flex; flex-direction: column; gap: 12px;">
          <!-- Aba 1: Modelo & Agente -->
          <div id="pane-tab-model" class="tab-pane" style="${this.activeTab === 'model' ? '' : 'display: none;'}">
            <div class="office-group" style="margin-bottom: 0;">
              <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px;">
                <div>
                  <label class="office-label" for="hermes-agent-name">${t('import_modals.label_agent_name')}</label>
                  <input type="text" id="hermes-agent-name" class="office-input" value="Agente_Hermes_Local">
                </div>
                <div>
                  <label class="office-label" for="hermes-model-select">${t('import_modals.hermes_model_label')}</label>
                  <select id="hermes-model-select" class="office-select">
                    <option value="hermes-3-llama-3.1-70b">${t('import_modals.opt_hermes_3_70b')}</option>
                    <option value="hermes-3-llama-3.1-8b">${t('import_modals.opt_hermes_3_8b')}</option>
                    <option value="openhermes-2.5-mistral-7b">${t('import_modals.opt_openhermes_25_7b')}</option>
                  </select>
                </div>
              </div>
            </div>
          </div>

          <!-- Aba 2: Prompt -->
          <div id="pane-tab-prompt" class="tab-pane" style="${this.activeTab === 'prompt' ? '' : 'display: none;'}">
            <div class="office-group" style="margin-bottom: 0;">
              <label class="office-label" for="hermes-prompt-input">${t('import_modals.hermes_prompt_label')}</label>
              <textarea id="hermes-prompt-input" class="office-textarea" rows="8" placeholder="${t('import_modals.hermes_prompt_placeholder')}"></textarea>
            </div>
          </div>

          <!-- Aba 3: Sandbox & Isolamento -->
          <div id="pane-tab-sandbox" class="tab-pane" style="${this.activeTab === 'sandbox' ? '' : 'display: none;'}">
            <div class="office-group" style="margin-bottom: 0;">
              <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px;">
                <div>
                  <label class="office-label" for="hermes-sandbox-level">${t('import_modals.label_sandbox_level')}</label>
                  <select id="hermes-sandbox-level" class="office-select">
                    <option value="microvm" selected>${t('import_modals.opt_sandbox_microvm')}</option>
                    <option value="container">${t('import_modals.opt_sandbox_container')}</option>
                    <option value="process">${t('import_modals.opt_sandbox_process')}</option>
                  </select>
                </div>
                <div>
                  <label class="office-label" for="hermes-max-memory">${t('import_modals.label_max_memory')}</label>
                  <input type="number" id="hermes-max-memory" class="office-input" value="4096" min="512" step="512" />
                </div>
              </div>
            </div>
          </div>
        </div>

        <!-- Rodapé Estilo Office -->
        <div class="modal-footer office-modal-footer">
          <span class="footer-info">${t('import_modals.ready_create_vertex')}</span>
          <div class="footer-actions">
            <button id="btn-cancel-hermes" class="tool-btn btn-secondary btn-secondary-office">${t('import_modals.btn_cancel')}</button>
            <button id="btn-inject-hermes-agent" class="tool-btn btn-action btn-primary-office">${t('import_modals.btn_import')}</button>
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

    // Alternância de abas
    this.modal.querySelectorAll('.modal-tabs .tab-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        const tab = (btn as HTMLElement).getAttribute('data-tab') as 'model' | 'prompt' | 'sandbox';
        if (!tab) return;
        this.activeTab = tab;
        this.modal?.querySelectorAll('.modal-tabs .tab-btn').forEach(b => {
          const isCurrent = b.getAttribute('data-tab') === tab;
          b.classList.toggle('active', isCurrent);
          b.setAttribute('aria-selected', isCurrent ? 'true' : 'false');
        });
        this.modal?.querySelectorAll('.tab-pane').forEach(p => {
          const el = p as HTMLElement;
          el.style.display = el.id === `pane-tab-${tab}` ? '' : 'none';
        });
      });
    });

    this.modal.querySelector('#btn-close-hermes-modal')?.addEventListener('click', () => this.hide());
    this.modal.querySelector('#btn-cancel-hermes')?.addEventListener('click', () => this.hide());
    this.modal.querySelector('#btn-inject-hermes-agent')?.addEventListener('click', () => this.injectHermesAgent());
  }

  public injectHermesAgent(options?: { name?: string; model?: string; prompt?: string }): void {
    const nameInput = this.modal?.querySelector('#hermes-agent-name') as HTMLInputElement;
    const modelSelect = this.modal?.querySelector('#hermes-model-select') as HTMLSelectElement;
    const promptInput = this.modal?.querySelector('#hermes-prompt-input') as HTMLTextAreaElement;

    const sandboxSelect = this.modal?.querySelector('#hermes-sandbox-level') as HTMLSelectElement;
    const memoryInput = this.modal?.querySelector('#hermes-max-memory') as HTMLInputElement;

    const agentName = options?.name || nameInput?.value?.trim() || 'Agente_Hermes';
    const modelName = options?.model || modelSelect?.value || 'hermes-3-llama-3.1-70b';
    const prompt = options?.prompt || promptInput?.value?.trim() || 'Instruções padrão Hermes';
    const isolationLevel = (sandboxSelect?.value as any) || 'microvm';
    const maxMemoryMB = parseInt(memoryInput?.value || '4096', 10) || 4096;

    const nodeId = `hermes_agent_${Date.now()}`;
    const center = this.viewport ? this.viewport.getCameraCenter() : { x: 0, y: 0 };
    const node = this.graph.buildNode(nodeId, agentName, 'agent', center.x, center.y);

    const agentSettings: INodeAgentSettings = {
      inheritSandbox: false,
      sandbox: {
        isolationLevel,
        network: 'offline',
        maxMemoryMB,
        timeoutSeconds: 300,
        readOnlyMounts: ['/workspace'],
        readWriteMounts: ['/workspace/out']
      },
      inheritModel: false,
      model: {
        provider: 'local',
        model: modelName,
        temperature: 0.2,
        promptTemplate: prompt
      },
      inheritCodeMode: false,
      codeMode: {
        allowedTools: ['read', 'write', 'bash'],
        customScripts: ['scripts/hermes/run.py']
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
