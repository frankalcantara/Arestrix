import { Graph } from '../../core/Graph.ts';
import { Viewport } from '../../view/Viewport.ts';
import { AddNodeCommand } from '../../core/history/GraphCommands.ts';
import { INodeAgentSettings } from '../../core/types.ts';

/**
 * Arestrix Studio - Diálogo de Importação do Hermes (Nous Research)
 * Padrão Microsoft Office (Fluent Dialog)
 * Rigoroso respeito: Zero sombras/glows em fontes
 */
export class ImportHermesModal {
  private modal?: HTMLElement;
  private graph: Graph;
  private viewport?: Viewport;

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
    }
  }

  public hide(): void {
    if (this.modal) {
      this.modal.style.display = 'none';
    }
  }

  private render(): void {
    if (!this.modal) return;
    this.modal.innerHTML = `
      <div class="modal-window" style="max-width: 680px; width: 92%; max-height: 88vh; display: flex; flex-direction: column;">
        <!-- Cabeçalho Estilo Office -->
        <div class="modal-header">
          <div>
            <h3 class="modal-title">Importar Agente Hermes</h3>
            <p style="margin: 2px 0 0 0; font-size: 12px; color: #616161; font-family: 'Segoe UI', sans-serif;">
              Configure as diretrizes de raciocínio local (Scratchpad / Function Calling) e o modelo Hermes.
            </p>
          </div>
          <button id="btn-close-hermes-modal" class="modal-close-btn" title="Fechar">&times;</button>
        </div>

        <!-- Corpo do Diálogo -->
        <div class="modal-body" style="flex: 1; overflow-y: auto; padding: 16px; display: flex; flex-direction: column; gap: 12px;">
          <div class="office-group" style="margin-bottom: 0;">
            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-bottom: 12px;">
              <div>
                <label class="office-label" for="hermes-agent-name">Nome do agente:</label>
                <input type="text" id="hermes-agent-name" class="office-input" value="Agente_Hermes_Local">
              </div>
              <div>
                <label class="office-label" for="hermes-model-select">Modelo Hermes:</label>
                <select id="hermes-model-select" class="office-select">
                  <option value="hermes-3-llama-3.1-70b">Hermes 3 (Llama 3.1 70B)</option>
                  <option value="hermes-3-llama-3.1-8b">Hermes 3 (Llama 3.1 8B)</option>
                  <option value="openhermes-2.5-mistral-7b">OpenHermes 2.5 (Mistral 7B)</option>
                </select>
              </div>
            </div>

            <div>
              <label class="office-label" for="hermes-prompt-input">Instruções do sistema (Scratchpad & Funções):</label>
              <textarea id="hermes-prompt-input" class="office-textarea" rows="8" placeholder="Cole o system prompt com tags <scratchpad> ou dataset de instrução aqui..."></textarea>
            </div>
          </div>
        </div>

        <!-- Rodapé Estilo Office -->
        <div class="modal-footer">
          <span class="footer-info">Pronto para criar o nó.</span>
          <div class="footer-actions">
            <button id="btn-cancel-hermes" class="tool-btn btn-secondary">Cancelar</button>
            <button id="btn-inject-hermes-agent" class="tool-btn btn-action">Importar</button>
          </div>
        </div>
      </div>
    `;

    this.bindEvents();
  }

  private bindEvents(): void {
    if (!this.modal) return;
    this.modal.querySelector('#btn-close-hermes-modal')?.addEventListener('click', () => this.hide());
    this.modal.querySelector('#btn-cancel-hermes')?.addEventListener('click', () => this.hide());
    this.modal.querySelector('#btn-inject-hermes-agent')?.addEventListener('click', () => this.injectHermesAgent());
  }

  public injectHermesAgent(options?: { name?: string; model?: string; prompt?: string }): void {
    const nameInput = this.modal?.querySelector('#hermes-agent-name') as HTMLInputElement;
    const modelSelect = this.modal?.querySelector('#hermes-model-select') as HTMLSelectElement;
    const promptInput = this.modal?.querySelector('#hermes-prompt-input') as HTMLTextAreaElement;

    const agentName = options?.name || nameInput?.value?.trim() || 'Agente_Hermes';
    const modelName = options?.model || modelSelect?.value || 'hermes-3-llama-3.1-70b';
    const prompt = options?.prompt || promptInput?.value?.trim() || 'Instruções padrão Hermes';

    const nodeId = `hermes_agent_${Date.now()}`;
    const center = this.viewport ? this.viewport.getCameraCenter() : { x: 0, y: 0 };
    const node = this.graph.buildNode(nodeId, agentName, 'agent', center.x, center.y);

    const agentSettings: INodeAgentSettings = {
      inheritSandbox: false,
      sandbox: {
        isolationLevel: 'microvm',
        network: 'offline',
        maxMemoryMB: 4096,
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
