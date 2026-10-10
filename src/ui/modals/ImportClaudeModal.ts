import { Graph } from '../../core/Graph.ts';
import { Viewport } from '../../view/Viewport.ts';
import { AddNodeCommand } from '../../core/history/GraphCommands.ts';
import { INodeAgentSettings } from '../../core/types.ts';

/**
 * Arestrix Studio - Diálogo de Importação do Claude (Anthropic)
 * Padrão Microsoft Office (Fluent Dialog)
 * Rigoroso respeito: Zero sombras/glows em fontes
 */
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
      <div class="modal-window" style="max-width: 680px; width: 92%; max-height: 88vh; display: flex; flex-direction: column;">
        <!-- Cabeçalho Estilo Office -->
        <div class="modal-header">
          <div>
            <h3 class="modal-title">Importar Agente Claude</h3>
            <p style="margin: 2px 0 0 0; font-size: 12px; color: #616161; font-family: 'Segoe UI', sans-serif;">
              Configure as instruções e o modelo Anthropic Claude para ingestão no grafo.
            </p>
          </div>
          <button id="btn-close-claude-modal" class="modal-close-btn" title="Fechar">&times;</button>
        </div>

        <!-- Corpo do Diálogo -->
        <div class="modal-body" style="flex: 1; overflow-y: auto; padding: 16px; display: flex; flex-direction: column; gap: 12px;">
          <div class="office-group" style="margin-bottom: 0;">
            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-bottom: 12px;">
              <div>
                <label class="office-label" for="claude-agent-name">Nome do agente:</label>
                <input type="text" id="claude-agent-name" class="office-input" value="Agente_Claude_Sonnet">
              </div>
              <div>
                <label class="office-label" for="claude-model-select">Modelo Anthropic:</label>
                <select id="claude-model-select" class="office-select">
                  <option value="claude-3-5-sonnet">Claude 3.5 Sonnet</option>
                  <option value="claude-3-opus">Claude 3 Opus</option>
                  <option value="claude-3-5-haiku">Claude 3.5 Haiku</option>
                </select>
              </div>
            </div>

            <div>
              <label class="office-label" for="claude-prompt-input">Instruções do sistema (System Prompt / Tags XML):</label>
              <textarea id="claude-prompt-input" class="office-textarea" rows="8" placeholder="Cole as instruções estruturadas ou contexto do Claude aqui..."></textarea>
            </div>
          </div>
        </div>

        <!-- Rodapé Estilo Office -->
        <div class="modal-footer">
          <span class="footer-info">Pronto para criar o nó.</span>
          <div class="footer-actions">
            <button id="btn-cancel-claude" class="tool-btn btn-secondary">Cancelar</button>
            <button id="btn-inject-claude-agent" class="tool-btn btn-action">Importar</button>
          </div>
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
  }
}
