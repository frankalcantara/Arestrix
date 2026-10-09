import { Graph } from '../../core/Graph.ts';
import { Viewport } from '../../view/Viewport.ts';
import { AddNodeCommand } from '../../core/history/GraphCommands.ts';
import { INodeAgentSettings } from '../../core/types.ts';

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
      <div class="modal-window" style="max-width: 800px; width: 92%; max-height: 90vh; display: flex; flex-direction: column;">
        <div class="modal-header" style="border-bottom: 2px solid #ec4899; padding-bottom: 12px;">
          <div style="display: flex; align-items: center; gap: 10px;">
            <div style="width: 38px; height: 38px; background: #ec4899; color: white; border-radius: 10px; display: flex; align-items: center; justify-content: center; font-size: 20px; font-weight: 800;">
              🔮
            </div>
            <div>
              <h3 style="margin: 0; font-size: 17px; font-weight: 800; color: #0f172a; text-shadow: none;">
                Importação Hermes (Nous Research / OpenHermes / Hermes 3)
              </h3>
              <p style="margin: 2px 0 0 0; font-size: 12px; color: #64748b; text-shadow: none;">
                Importe datasets de instrução, raciocínio agêntico aberto e fluxos do Hermes
              </p>
            </div>
          </div>
          <button id="btn-close-hermes-modal" class="modal-close-btn">&times;</button>
        </div>

        <div class="modal-body" style="flex: 1; overflow-y: auto; padding: 18px 0; display: flex; flex-direction: column; gap: 16px;">
          <div style="background: #fdf2f8; border: 1px solid #fbcfe8; border-radius: 10px; padding: 14px;">
            <p style="margin: 0; font-size: 12px; color: #9d174d;">
              O Hermes é pioneiro em execução autônoma de chamadas de funções abertas, raciocínio em cadeia com tags <code>&lt;scratchpad&gt;</code> e execução local sem telemetria externa.
            </p>
          </div>

          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 14px;">
            <div>
              <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">Nome do Agente:</label>
              <input type="text" id="hermes-agent-name" value="Agente_Hermes_Local" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 8px; font-size: 13px;">
            </div>
            <div>
              <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">Modelo Hermes:</label>
              <select id="hermes-model-select" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 8px; font-size: 13px;">
                <option value="hermes-3-llama-3.1-70b">Hermes 3 (Llama 3.1 70B - Raciocínio & Ferramentas)</option>
                <option value="hermes-3-llama-3.1-8b">Hermes 3 (Llama 3.1 8B - Alta Velocidade Local)</option>
                <option value="openhermes-2.5-mistral-7b">OpenHermes 2.5 (Mistral 7B)</option>
              </select>
            </div>
          </div>

          <div>
            <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">
              System Prompt com Scratchpad & Funções:
            </label>
            <textarea id="hermes-prompt-input" rows="7" placeholder="Cole o system prompt ou dataset de instrução aqui..." style="width: 100%; padding: 10px 12px; border: 1px solid #cbd5e1; border-radius: 8px; font-family: monospace; font-size: 12px;"></textarea>
          </div>
        </div>

        <div class="modal-footer" style="border-top: 1px solid #e2e8f0; padding-top: 14px; display: flex; justify-content: flex-end; gap: 10px;">
          <button id="btn-cancel-hermes" class="tool-btn btn-secondary">Cancelar</button>
          <button id="btn-inject-hermes-agent" class="tool-btn btn-action" style="background: #ec4899; color: white; font-weight: 700;">
            🚀 Injetar Agente Hermes
          </button>
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
    if (typeof alert !== 'undefined') {
      alert(`✅ ${agentName} inserido no grafo!`);
    }
  }
}
