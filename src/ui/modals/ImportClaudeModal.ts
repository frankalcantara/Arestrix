import { Graph } from '../../core/Graph.ts';
import { Viewport } from '../../view/Viewport.ts';
import { AddNodeCommand } from '../../core/history/GraphCommands.ts';
import { INodeAgentSettings } from '../../core/types.ts';

export class ImportClaudeModal {
  private modal?: HTMLElement;
  private graph: Graph;
  private viewport?: Viewport;

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
        <div class="modal-header" style="border-bottom: 2px solid #8b5cf6; padding-bottom: 12px;">
          <div style="display: flex; align-items: center; gap: 10px;">
            <div style="width: 38px; height: 38px; background: #8b5cf6; color: white; border-radius: 10px; display: flex; align-items: center; justify-content: center; font-size: 20px; font-weight: 800;">
              ⚡
            </div>
            <div>
              <h3 style="margin: 0; font-size: 17px; font-weight: 800; color: #0f172a; text-shadow: none;">
                Importação do Claude (Anthropic Claude 3.5 Sonnet / Opus)
              </h3>
              <p style="margin: 2px 0 0 0; font-size: 12px; color: #64748b; text-shadow: none;">
                Importe artefatos, conversas e prompts estruturados do Claude para tarefas C++20
              </p>
            </div>
          </div>
          <button id="btn-close-claude-modal" class="modal-close-btn">&times;</button>
        </div>

        <div class="modal-body" style="flex: 1; overflow-y: auto; padding: 18px 0; display: flex; flex-direction: column; gap: 16px;">
          <div style="background: #faf5ff; border: 1px solid #e9d5ff; border-radius: 10px; padding: 14px;">
            <p style="margin: 0; font-size: 12px; color: #581c87;">
              Ingestão de históricos de conversas do Claude, prompts de sistema com tags XML (<code>&lt;context&gt;</code>, <code>&lt;rules&gt;</code>) e conversão em nós de agentes com isolamento em container.
            </p>
          </div>

          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 14px;">
            <div>
              <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">Nome do Agente:</label>
              <input type="text" id="claude-agent-name" value="Agente_Claude_Sonnet" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 8px; font-size: 13px;">
            </div>
            <div>
              <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">Modelo Anthropic:</label>
              <select id="claude-model-select" style="width: 100%; padding: 8px 12px; border: 1px solid #cbd5e1; border-radius: 8px; font-size: 13px;">
                <option value="claude-3-5-sonnet">Claude 3.5 Sonnet (Recomendado)</option>
                <option value="claude-3-opus">Claude 3 Opus (Raciocínio Profundo)</option>
                <option value="claude-3-5-haiku">Claude 3.5 Haiku (Ultra Rápido)</option>
              </select>
            </div>
          </div>

          <div>
            <label style="display: block; font-size: 12px; font-weight: 700; color: #334155; margin-bottom: 6px;">
              System Prompt / Instruções com Tags XML:
            </label>
            <textarea id="claude-prompt-input" rows="7" placeholder="Cole as instruções do Claude aqui..." style="width: 100%; padding: 10px 12px; border: 1px solid #cbd5e1; border-radius: 8px; font-family: monospace; font-size: 12px;"></textarea>
          </div>
        </div>

        <div class="modal-footer" style="border-top: 1px solid #e2e8f0; padding-top: 14px; display: flex; justify-content: flex-end; gap: 10px;">
          <button id="btn-cancel-claude" class="tool-btn btn-secondary">Cancelar</button>
          <button id="btn-inject-claude-agent" class="tool-btn btn-action" style="background: #8b5cf6; color: white; font-weight: 700;">
            🚀 Injetar Agente Claude
          </button>
        </div>
      </div>
    `;

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
    if (typeof alert !== 'undefined') {
      alert(`✅ ${agentName} inserido no grafo!`);
    }
  }
}
