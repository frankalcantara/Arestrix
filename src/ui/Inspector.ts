import { Graph } from '../core/Graph.ts';
import { INodeData } from '../core/types.ts';
import { EventBus } from '../core/EventBus.ts';
import { RemoveNodeCommand, UpdateNodeCommand } from '../core/history/GraphCommands.ts';

export class Inspector {
  private container: HTMLElement;
  private graph: Graph;
  private bus: EventBus;
  private currentNodeId: string | null = null;

  constructor(parent: HTMLElement, graph: Graph, bus: EventBus) {
    this.graph = graph;
    this.bus = bus;

    this.container = document.createElement('aside');
    this.container.className = 'inspector-panel';
    parent.appendChild(this.container);

    this.renderEmpty();
    this.bindEvents();
  }

  private bindEvents(): void {
    this.bus.on('node:selected', (node: INodeData) => {
      this.currentNodeId = node.id;
      this.renderNode(node);
    });

    this.bus.on('node:deselected', () => {
      this.currentNodeId = null;
      this.renderEmpty();
    });

    this.bus.on('node:removed', ({ id }: { id: string }) => {
      if (this.currentNodeId === id) {
        this.currentNodeId = null;
        this.renderEmpty();
      }
    });
  }

  private renderEmpty(): void {
    this.container.innerHTML = `
      <div class="empty-state">
        <div class="empty-icon">⚙️</div>
        <div class="empty-title">Nenhum Nó Selecionado</div>
        <div class="empty-desc">Clique em um nó na tela para inspecionar e editar suas propriedades, regras e lambdas C++20.</div>
      </div>
    `;
  }

  private renderNode(node: INodeData): void {
    this.container.innerHTML = `
      <div class="inspector-header">
        <span class="inspector-badge badge-${node.type}">${node.type.toUpperCase()}</span>
        <button class="btn-close" id="btn-deselect-node">&times;</button>
      </div>

      <div class="inspector-body">
        <div class="form-group">
          <label class="form-label" for="node-label">Nome do Agente / Rótulo</label>
          <input type="text" id="node-label" class="form-input" value="${this.escapeHtml(node.label)}" />
        </div>

        <div class="form-group">
          <label class="form-label">Identificador Único</label>
          <input type="text" class="form-input form-input-readonly" value="${node.id}" readonly />
        </div>

        <div class="form-group">
          <label class="form-label">Tipo de Tarefa Taskflow</label>
          <select id="node-type-select" class="form-select">
            <option value="static" ${node.type === 'static' ? 'selected' : ''}>Static Task (Deterministico)</option>
            <option value="condition" ${node.type === 'condition' ? 'selected' : ''}>Condition Task (Decisão / Branch)</option>
            <option value="subflow" ${node.type === 'subflow' ? 'selected' : ''}>Subflow Task (Dinamico)</option>
            <option value="module" ${node.type === 'module' ? 'selected' : ''}>Module Task (Hierarquico)</option>
            <option value="critical" ${node.type === 'critical' ? 'selected' : ''}>Critical Section (Mutex Concorrente)</option>
          </select>
        </div>

        ${(node.type === 'subflow' || node.type === 'module') ? `
        <div class="form-group">
          <button id="btn-dive-subflow" class="btn btn-dive-action">
            📂 Abrir Subgrafo Filho (Duplo-clique)
          </button>
        </div>
        ` : ''}

        <div class="form-group">
          <label class="form-label" for="node-lambda">Corpo do Lambda C++20</label>
          <div class="code-editor-header">
            <span>auto task = tf.emplace([...](...) {</span>
          </div>
          <textarea id="node-lambda" class="form-textarea code-font" rows="7">${this.escapeHtml(node.customLambdaBody || '')}</textarea>
          <div class="code-editor-footer">
            <span>});</span>
          </div>
        </div>

        <div class="inspector-section">
          <div class="section-title">Portas de Conexão</div>
          <div class="port-list">
            <div class="port-sublist">
              <span class="port-sublist-title">Entradas (${node.inputs.length})</span>
              ${node.inputs.map(p => `<span class="port-tag in-tag">${p.name}</span>`).join('')}
            </div>
            <div class="port-sublist">
              <span class="port-sublist-title">Saídas (${node.outputs.length})</span>
              ${node.outputs.map(p => `<span class="port-tag out-tag">${p.name}</span>`).join('')}
            </div>
          </div>
        </div>

        <div class="form-actions">
          <button id="btn-delete-node" class="btn btn-danger">Excluir Nó</button>
        </div>
      </div>
    `;

    // Eventos do formulário
    this.container.querySelector('#btn-dive-subflow')?.addEventListener('click', () => {
      this.graph.diveIntoSubflow(node.id);
    });

    const typeSelect = this.container.querySelector('#node-type-select') as HTMLSelectElement;
    let oldType = node.type;
    typeSelect?.addEventListener('change', () => {
      const newType = typeSelect.value as any;
      if (newType !== oldType) {
        const cmd = new UpdateNodeCommand(
          this.graph,
          node.id,
          { type: oldType },
          { type: newType },
          `Alterar tipo de "${node.label}" para ${newType}`
        );
        this.graph.commandManager.execute(cmd);
        oldType = newType;
      }
    });

    // Eventos do formulário
    const labelInput = this.container.querySelector('#node-label') as HTMLInputElement;
    let initialLabel = node.label;
    labelInput?.addEventListener('focus', () => {
      initialLabel = labelInput.value;
    });
    labelInput?.addEventListener('input', () => {
      this.graph.updateNode(node.id, { label: labelInput.value });
    });
    labelInput?.addEventListener('blur', () => {
      if (labelInput.value !== initialLabel) {
        const cmd = new UpdateNodeCommand(
          this.graph,
          node.id,
          { label: initialLabel },
          { label: labelInput.value },
          `Renomear nó para "${labelInput.value}"`
        );
        this.graph.commandManager.record(cmd);
        initialLabel = labelInput.value;
      }
    });

    const lambdaInput = this.container.querySelector('#node-lambda') as HTMLTextAreaElement;
    let initialLambda = node.customLambdaBody || '';
    lambdaInput?.addEventListener('focus', () => {
      initialLambda = lambdaInput.value;
    });
    lambdaInput?.addEventListener('input', () => {
      this.graph.updateNode(node.id, { customLambdaBody: lambdaInput.value });
    });
    lambdaInput?.addEventListener('blur', () => {
      if (lambdaInput.value !== initialLambda) {
        const cmd = new UpdateNodeCommand(
          this.graph,
          node.id,
          { customLambdaBody: initialLambda },
          { customLambdaBody: lambdaInput.value },
          `Editar lambda de "${node.label}"`
        );
        this.graph.commandManager.record(cmd);
        initialLambda = lambdaInput.value;
      }
    });

    const deselectBtn = this.container.querySelector('#btn-deselect-node');
    deselectBtn?.addEventListener('click', () => {
      this.bus.emit('node:deselected');
    });

    const deleteBtn = this.container.querySelector('#btn-delete-node');
    deleteBtn?.addEventListener('click', () => {
      if (confirm(`Deseja realmente excluir o nó "${node.label}"?`)) {
        const cmd = new RemoveNodeCommand(this.graph, node.id);
        this.graph.commandManager.execute(cmd);
      }
    });
  }

  private escapeHtml(text: string): string {
    return text
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
}
