import { Graph } from '../core/Graph.ts';
import { INodeData } from '../core/types.ts';
import { EventBus } from '../core/EventBus.ts';
import { RemoveNodeCommand, UpdateNodeCommand } from '../core/history/GraphCommands.ts';
import { AgentTemplateRegistry } from '../core/templates/AgentTemplateRegistry.ts';

export class Inspector {
  private container: HTMLElement;
  private graph: Graph;
  private bus: EventBus;
  private currentNodeId: string | null = null;
  private registry: AgentTemplateRegistry;

  constructor(parent: HTMLElement, graph: Graph, bus: EventBus) {
    this.graph = graph;
    this.bus = bus;
    this.registry = AgentTemplateRegistry.getInstance();

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

    this.bus.on('node:updated', (node: INodeData) => {
      if (this.currentNodeId === node.id) {
        // Atualiza campos que podem ter mudado se não estiverem com foco
        const activeEl = document.activeElement;
        if (!activeEl || !this.container.contains(activeEl)) {
          this.renderNode(node);
        }
      }
    });
  }

  private renderEmpty(): void {
    this.container.innerHTML = `
      <div class="empty-state">
        <div class="empty-icon">⚙️</div>
        <div class="empty-title">Nenhum Nó Selecionado</div>
        <div class="empty-desc">Clique em um nó na tela para inspecionar e editar suas propriedades, parâmetros de template e lambdas C++20.</div>
      </div>
    `;
  }

  private renderNode(node: INodeData): void {
    const tpl = node.templateId ? this.registry.getTemplate(node.templateId) : undefined;

    this.container.innerHTML = `
      <div class="inspector-header">
        <div class="inspector-header-left">
          <span class="inspector-badge badge-${node.type}">${node.type.toUpperCase()}</span>
          ${tpl ? `<span class="inspector-template-tag">${tpl.icon} ${this.escapeHtml(tpl.name)}</span>` : ''}
        </div>
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
              <option value="cuda" ${node.type === 'cuda' ? 'selected' : ''}>CUDA Kernel (GPU Acelerado)</option>
          </select>
        </div>

        <!-- Painel Rapido de Modais Flutuantes do Agente (Fase 7) -->
        <div class="agent-quick-modals-box">
          <div class="agent-modals-title">Configurações Avançadas do Agente</div>
          <div class="agent-modals-row">
            <button class="btn-agent-modal-pill" id="btn-open-node-sandbox" title="Configurar Nível de Isolamento da VM, RAM e Pastas">
              🛡️ Sandbox & Máquina Virtual
            </button>
            <button class="btn-agent-modal-pill" id="btn-open-node-model" title="Configurar Provedor (Opus, DeepSeek, Jev), Modelo e Prompt">
              🧠 Modelo de IA, Skill & Prompt
            </button>
            <button class="btn-agent-modal-pill" id="btn-open-node-tools" title="Configurar Ferramentas Code Mode (write, edit, bash) e Conectores MCP">
              🔧 Ferramentas & MCP
            </button>
            <button class="btn-agent-modal-pill" id="btn-open-node-memory" title="Configurar Acesso à Memória (Blackboard & Semântica)">
              💾 Memória & Contexto
            </button>
          </div>
        </div>

        ${(node.type === 'subflow' || node.type === 'module') ? `
        <div class="form-group">
          <button id="btn-dive-subflow" class="btn btn-dive-action">
            📂 Abrir Subgrafo Filho (Duplo-clique)
          </button>
        </div>
        ` : ''}

        <!-- Seção de Parâmetros do Agente -->
        ${this.renderParametersSection(node, tpl)}

        <div class="form-group">
          <div class="lambda-header-row">
            <label class="form-label" for="node-lambda">Corpo do Lambda C++20</label>
            ${tpl ? `
              <button id="btn-sync-lambda" class="btn-sync-action" title="Recompõe o código lambda com os parâmetros atuais">
                🔄 Sincronizar Lambda
              </button>
            ` : ''}
          </div>
          <div class="code-editor-header">
            <span>auto task = tf.emplace([...](...) {</span>
          </div>
          <textarea id="node-lambda" class="form-textarea code-font" rows="8">${this.escapeHtml(node.customLambdaBody || '')}</textarea>
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

    // Modais Flutuantes de Agente (Fase 7 & 12)
    this.container.querySelector('#btn-open-node-sandbox')?.addEventListener('click', () => {
      this.bus.emit('node:open-sandbox-modal', node.id);
    });
    this.container.querySelector('#btn-open-node-model')?.addEventListener('click', () => {
      this.bus.emit('node:open-model-modal', node.id);
    });
    this.container.querySelector('#btn-open-node-tools')?.addEventListener('click', () => {
      this.bus.emit('node:open-tools-modal', node.id);
    });
    this.container.querySelector('#btn-open-node-memory')?.addEventListener('click', () => {
      this.bus.emit('node:open-memory-modal', node.id);
    });

    // Bind subflow
    this.container.querySelector('#btn-dive-subflow')?.addEventListener('click', () => {
      this.graph.diveIntoSubflow(node.id);
    });

    // Tipo
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

    // Label
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

    // Parâmetros
    this.bindParameterInputs(node, tpl);

    // Sincronizar Lambda
    this.container.querySelector('#btn-sync-lambda')?.addEventListener('click', () => {
      if (tpl) {
        const newBody = tpl.generateLambdaBody(node.parameters, node.label);
        const oldBody = node.customLambdaBody || '';
        const cmd = new UpdateNodeCommand(
          this.graph,
          node.id,
          { customLambdaBody: oldBody },
          { customLambdaBody: newBody },
          `Sincronizar lambda de "${node.label}"`
        );
        this.graph.commandManager.execute(cmd);
        const lambdaTextarea = this.container.querySelector('#node-lambda') as HTMLTextAreaElement;
        if (lambdaTextarea) lambdaTextarea.value = newBody;
      }
    });

    // Lambda
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

    // Deselect
    const deselectBtn = this.container.querySelector('#btn-deselect-node');
    deselectBtn?.addEventListener('click', () => {
      this.bus.emit('node:deselected');
    });

    // Delete
    const deleteBtn = this.container.querySelector('#btn-delete-node');
    deleteBtn?.addEventListener('click', () => {
      if (confirm(`Deseja realmente excluir o nó "${node.label}"?`)) {
        const cmd = new RemoveNodeCommand(this.graph, node.id);
        this.graph.commandManager.execute(cmd);
      }
    });
  }

  private renderParametersSection(node: INodeData, tpl?: any): string {
    const params = node.parameters || {};
    const keys = Object.keys(params);

    if (keys.length === 0 && !tpl) {
      return '';
    }

    let rowsHtml = '';

    if (tpl) {
      Object.entries(tpl.parameters).forEach(([key, def]: [string, any]) => {
        const val = params[key] !== undefined ? params[key] : def.defaultValue;
        rowsHtml += `
          <div class="param-row">
            <div class="param-label-group">
              <span class="param-name">${this.escapeHtml(def.label)}</span>
              <span class="param-desc">${this.escapeHtml(def.description)}</span>
            </div>
            <div class="param-input-wrap">
              ${def.type === 'select' ? `
                <select class="form-select param-input" data-param-key="${key}">
                  ${(def.options || []).map((opt: string) => `
                    <option value="${opt}" ${String(val) === String(opt) ? 'selected' : ''}>${opt}</option>
                  `).join('')}
                </select>
              ` : def.type === 'boolean' ? `
                <input type="checkbox" class="param-checkbox param-input" data-param-key="${key}" ${val ? 'checked' : ''} />
              ` : `
                <input type="${def.type === 'number' ? 'number' : 'text'}" 
                       step="${def.type === 'number' ? 'any' : undefined}"
                       class="form-input param-input" 
                       data-param-key="${key}" 
                       value="${this.escapeHtml(String(val))}" />
              `}
            </div>
          </div>
        `;
      });
    } else {
      keys.forEach(key => {
        const val = params[key];
        rowsHtml += `
          <div class="param-row">
            <div class="param-label-group">
              <span class="param-name">${this.escapeHtml(key)}</span>
            </div>
            <div class="param-input-wrap">
              <input type="text" class="form-input param-input" data-param-key="${key}" value="${this.escapeHtml(String(val))}" />
            </div>
          </div>
        `;
      });
    }

    return `
      <div class="inspector-section">
        <div class="section-title">⚙️ Parâmetros do Agente</div>
        <div class="params-container">
          ${rowsHtml}
        </div>
      </div>
    `;
  }

  private bindParameterInputs(node: INodeData, tpl?: any): void {
    const inputs = this.container.querySelectorAll('.param-input');
    inputs.forEach(inputEl => {
      const key = inputEl.getAttribute('data-param-key');
      if (!key) return;

      const handleChange = () => {
        let newVal: any;
        if (inputEl instanceof HTMLInputElement && inputEl.type === 'checkbox') {
          newVal = inputEl.checked;
        } else if (inputEl instanceof HTMLInputElement && inputEl.type === 'number') {
          newVal = parseFloat(inputEl.value) || 0;
        } else {
          newVal = (inputEl as HTMLInputElement | HTMLSelectElement).value;
        }

        const oldParams = { ...node.parameters };
        const newParams = { ...node.parameters, [key]: newVal };

        let extraUpdates: Partial<INodeData> = { parameters: newParams };

        // Se o nó veio de um template, atualiza o lambda automaticamente com novos parâmetros
        if (tpl) {
          const updatedLambda = tpl.generateLambdaBody(newParams, node.label);
          extraUpdates.customLambdaBody = updatedLambda;
          const lambdaEl = this.container.querySelector('#node-lambda') as HTMLTextAreaElement;
          if (lambdaEl) lambdaEl.value = updatedLambda;
        }

        const cmd = new UpdateNodeCommand(
          this.graph,
          node.id,
          { parameters: oldParams },
          extraUpdates,
          `Alterar parâmetro "${key}" em ${node.label}`
        );
        this.graph.commandManager.execute(cmd);
      };

      if (inputEl instanceof HTMLSelectElement || (inputEl instanceof HTMLInputElement && inputEl.type === 'checkbox')) {
        inputEl.addEventListener('change', handleChange);
      } else {
        inputEl.addEventListener('blur', handleChange);
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
