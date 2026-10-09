import { Graph } from '../../core/Graph.ts';
import { Viewport } from '../../view/Viewport.ts';
import { AddNodeCommand } from '../../core/history/GraphCommands.ts';
import { INodeAgentSettings } from '../../core/types.ts';

export class ImportOpenAiModal {
  private modal?: HTMLElement;
  private graph: Graph;
  private viewport?: Viewport;

  constructor(graph: Graph, viewport?: Viewport) {
    this.graph = graph;
    this.viewport = viewport;

    if (typeof document !== 'undefined') {
      this.modal = document.createElement('div');
      this.modal.className = 'modal-backdrop';
      this.modal.id = 'modal-import-openai';
      this.modal.style.display = 'none';
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
        <div class="modal-header" style="border-bottom: 2px solid #10a37f; padding-bottom: 12px;">
          <div style="display: flex; align-items: center; gap: 10px;">
            <div style="width: 38px; height: 38px; background: #10a37f; color: white; border-radius: 10px; display: flex; align-items: center; justify-content: center; font-size: 20px; font-weight: 800;">
              🟢
            </div>
            <div>
              <h3 style="margin: 0; font-size: 17px; font-weight: 800; color: #0f172a; text-shadow: none;">
                Importação OpenAI (ChatGPT / Assistants API)
              </h3>
              <p style="margin: 2px 0 0 0; font-size: 12px; color: #64748b; text-shadow: none;">
                Importe threads de conversas, Custom GPTs ou configurações de Assistants
              </p>
            </div>
          </div>
          <button id="btn-close-openai-modal" class="modal-close-btn">&times;</button>
        </div>

        <div class="modal-body" style="flex: 1; overflow-y: auto; padding: 18px 0; display: flex; flex-direction: column; gap: 16px;">
          <div style="background: #f0fdf4; border: 1px solid #bbf7d0; border-radius: 10px; padding: 14px;">
            <p style="margin: 0; font-size: 12px; color: #166534;">
              Suporte a exportação de dados do ChatGPT (<code>conversations.json</code>) e definições de ferramentas (Code Interpreter, Function Calling) mapeadas para Code Mode no Arestrix.
            </p>
          </div>

          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 14px;">
            <div>
              <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">Nome do Agente:</label>
              <input type="text" id="openai-agent-name" value="Agente_GPT4o" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 8px; font-size: 13px;">
            </div>
            <div>
              <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">Modelo OpenAI:</label>
              <select id="openai-model-select" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 8px; font-size: 13px;">
                <option value="gpt-4o">GPT-4o (Omni Multimodal)</option>
                <option value="o1-preview">o1 (Raciocínio Lógico Complexo)</option>
                <option value="gpt-4o-mini">GPT-4o Mini (Econômico)</option>
              </select>
            </div>
          </div>

          <div>
            <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">
              Instruções do Sistema / Histórico Exportado:
            </label>
            <textarea id="openai-prompt-input" rows="7" placeholder="Cole as instruções ou o arquivo de conversa aqui..." style="width: 100%; padding: 10px 12px; border: 1px solid #cbd5e1; border-radius: 8px; font-family: monospace; font-size: 12px;"></textarea>
          </div>
        </div>

        <div class="modal-footer" style="border-top: 1px solid #e2e8f0; padding-top: 14px; display: flex; justify-content: flex-end; gap: 10px;">
          <button id="btn-cancel-openai" class="tool-btn btn-secondary">Cancelar</button>
          <button id="btn-inject-openai-agent" class="tool-btn btn-action" style="background: #10a37f; color: white; font-weight: 700;">
            🚀 Injetar Agente OpenAI
          </button>
        </div>
      </div>
    `;

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
    if (typeof alert !== 'undefined') {
      alert(`✅ ${agentName} inserido no grafo!`);
    }
  }
}
