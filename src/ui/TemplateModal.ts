import { AgentTemplateRegistry } from '../core/templates/AgentTemplateRegistry.ts';
import { IAgentTemplate } from '../core/templates/AgentTemplateTypes.ts';
import { Graph } from '../core/Graph.ts';
import { Viewport } from '../view/Viewport.ts';
import { AddNodeCommand } from '../core/history/GraphCommands.ts';
import { ModalWindowManager, IModalWindowController } from './modals/ModalWindowManager.ts';

export class TemplateModal {
  private overlay: HTMLElement;
  private graph: Graph;
  private viewport: Viewport;
  private registry: AgentTemplateRegistry;
  private windowController?: IModalWindowController;
  private activeCategory: string = 'Todos';

  constructor(graph: Graph, viewport: Viewport) {
    this.graph = graph;
    this.viewport = viewport;
    this.registry = AgentTemplateRegistry.getInstance();

    this.overlay = document.createElement('div');
    this.overlay.className = 'modal-overlay hidden';
    document.body.appendChild(this.overlay);

    this.render();
  }

  public show(): void {
    this.activeCategory = 'Todos';
    this.render();
    this.overlay.classList.remove('hidden');
    this.windowController?.bringToFront();
  }

  public hide(): void {
    this.overlay.classList.add('hidden');
  }

  private render(): void {
    const categories = ['Todos', 'IA & Inferência', 'Concorrência & Filas', 'Armazenamento & Dados', 'Visão & Processamento', 'Síntese & Orquestração', 'Plugins Customizados'];
    const allTemplates = this.registry.getAllTemplates();
    const filtered = this.activeCategory === 'Todos'
      ? allTemplates
      : allTemplates.filter(t => t.category === this.activeCategory);

    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: 'Biblioteca de Templates de Agentes & Plugins',
      subtitle: 'Tarefas especializadas e componentes desacoplados para Taskflow C++20',
      icon: '🧩',
      closeBtnId: 'btn-close-template-modal'
    });

    this.overlay.innerHTML = `
      <div class="modal-dialog template-modal-dialog">
        ${titleBarHtml}

        <!-- Barra de Ações Rápidas -->
        <div class="template-action-bar">
          <div class="template-action-left">
            <button id="btn-preset-pipeline" class="btn btn-preset-action" title="Cria um pipeline integrado com todos os 5 agentes conectados">
              ⚡ Instanciar Arquitetura Completa de Agentes
            </button>
          </div>
          <div class="template-action-right">
            <button id="btn-import-plugin" class="btn btn-secondary-pill" title="Importa um plugin de agente a partir de um arquivo JSON">
              🔌 Importar Plugin (.json)
            </button>
            <button id="btn-export-catalog" class="btn btn-secondary-pill" title="Exporta todos os templates registrados para JSON">
              💾 Exportar Catálogo
            </button>
            <input type="file" id="plugin-file-input" accept=".json" style="display: none;" />
          </div>
        </div>

        <!-- Filtros por Categoria -->
        <div class="template-category-tabs">
          ${categories.map(cat => {
            const count = cat === 'Todos' 
              ? allTemplates.length 
              : allTemplates.filter(t => t.category === cat).length;
            const activeClass = this.activeCategory === cat ? 'active' : '';
            return `<button class="category-pill ${activeClass}" data-cat="${cat}">
              ${cat} <span class="cat-count">(${count})</span>
            </button>`;
          }).join('')}
        </div>

        <!-- Grid de Cards de Templates -->
        <div class="template-modal-body">
          <div class="templates-grid">
            ${filtered.length === 0 ? `
              <div class="templates-empty">
                <div class="empty-icon">🔍</div>
                <div class="empty-title">Nenhum template nesta categoria</div>
                <div class="empty-desc">Use o botão "Importar Plugin" acima para adicionar novos agentes dinamicamente.</div>
              </div>
            ` : filtered.map(tpl => this.renderTemplateCard(tpl)).join('')}
          </div>
        </div>

        <div class="modal-footer">
          <div class="footer-info">
            💡 Os nós instanciados herdam portas de dados, lambdas C++20 otimizados e suporte a histórico <strong>Desfazer (Ctrl+Z)</strong>.
          </div>
          <button id="btn-close-modal-bottom" class="btn btn-secondary">Fechar</button>
        </div>
      </div>
    `;

    const dialog = this.overlay.querySelector('.modal-dialog') as HTMLElement;
    if (dialog) {
      this.windowController = ModalWindowManager.attach({
        dialog,
        minWidth: 640,
        minHeight: 480,
        onClose: () => this.hide()
      });
    }

    this.bindEvents();
  }

  private renderTemplateCard(tpl: IAgentTemplate): string {
    const paramKeys = Object.keys(tpl.parameters);
    const paramSummary = paramKeys.length > 0 
      ? paramKeys.slice(0, 3).join(', ') + (paramKeys.length > 3 ? '...' : '')
      : 'Nenhum';

    return `
      <div class="template-card" data-template-id="${tpl.id}">
        <div class="template-card-header">
          <div class="template-icon-badge">${tpl.icon}</div>
          <div class="template-info">
            <div class="template-name">${this.escapeHtml(tpl.name)}</div>
            <div class="template-badges">
              <span class="inspector-badge badge-${tpl.taskType}">${tpl.taskType.toUpperCase()}</span>
              ${tpl.isCustomPlugin ? '<span class="plugin-custom-badge">PLUGIN DINÂMICO</span>' : ''}
            </div>
          </div>
        </div>

        <p class="template-desc">${this.escapeHtml(tpl.description)}</p>

        <div class="template-meta-row">
          <div class="meta-item">
            <span class="meta-label">Entradas:</span>
            <span class="meta-value">${tpl.inputs.length} porta(s)</span>
          </div>
          <div class="meta-item">
            <span class="meta-label">Saídas:</span>
            <span class="meta-value">${tpl.outputs.length} porta(s)</span>
          </div>
          <div class="meta-item">
            <span class="meta-label">Parâmetros:</span>
            <span class="meta-value" title="${paramSummary}">${paramKeys.length} config(s)</span>
          </div>
        </div>

        <div class="template-code-preview hidden" id="preview-${tpl.id}">
          <pre class="code-font">${this.escapeHtml(tpl.generateLambdaBody(this.getDefaultParams(tpl), tpl.name))}</pre>
        </div>

        <div class="template-card-footer">
          <button class="btn-toggle-code" data-preview-id="preview-${tpl.id}">
            👁️ Ver C++20
          </button>
          <button class="btn btn-primary btn-insert-node" data-insert-id="${tpl.id}">
            ➕ Inserir no Grafo
          </button>
        </div>
      </div>
    `;
  }

  private getDefaultParams(tpl: IAgentTemplate): Record<string, any> {
    const res: Record<string, any> = {};
    Object.entries(tpl.parameters).forEach(([k, def]) => {
      res[k] = def.defaultValue;
    });
    return res;
  }

  private bindEvents(): void {
    // Fechar
    this.overlay.querySelector('#btn-close-template-modal')?.addEventListener('click', () => this.hide());
    this.overlay.querySelector('#btn-close-modal-bottom')?.addEventListener('click', () => this.hide());
    this.overlay.addEventListener('click', (e) => {
      if (e.target === this.overlay) this.hide();
    });

    // Filtros de Categoria
    this.overlay.querySelectorAll('.category-pill').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const cat = (e.currentTarget as HTMLElement).getAttribute('data-cat');
        if (cat) {
          this.activeCategory = cat;
          this.render();
        }
      });
    });

    // Inserir nó individual
    this.overlay.querySelectorAll('.btn-insert-node').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const tplId = (e.currentTarget as HTMLElement).getAttribute('data-insert-id');
        if (tplId) {
          this.insertTemplateNode(tplId);
          this.hide();
        }
      });
    });

    // Toggle Preview de Código
    this.overlay.querySelectorAll('.btn-toggle-code').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const targetId = (e.currentTarget as HTMLElement).getAttribute('data-preview-id');
        if (targetId) {
          const previewEl = this.overlay.querySelector(`#${targetId}`);
          if (previewEl) {
            previewEl.classList.toggle('hidden');
            const isHidden = previewEl.classList.contains('hidden');
            (e.currentTarget as HTMLElement).textContent = isHidden ? '👁️ Ver C++20' : '✕ Ocultar C++20';
          }
        }
      });
    });

    // Preset Arquitetura Completa
    this.overlay.querySelector('#btn-preset-pipeline')?.addEventListener('click', () => {
      this.instantiateCompleteMultiAgentPreset();
      this.hide();
    });

    // Exportar Catálogo
    this.overlay.querySelector('#btn-export-catalog')?.addEventListener('click', () => {
      const json = this.registry.exportAllTemplatesAsJSON();
      const blob = new Blob([json], { type: 'application/json' });
      const url = URL.createObjectURL(blob);
      const a = document.createElement('a');
      a.href = url;
      a.download = 'arestrix_agent_templates.json';
      a.click();
      URL.revokeObjectURL(url);
    });

    // Importar Plugin
    const fileInput = this.overlay.querySelector('#plugin-file-input') as HTMLInputElement;
    this.overlay.querySelector('#btn-import-plugin')?.addEventListener('click', () => {
      fileInput?.click();
    });

    fileInput?.addEventListener('change', () => {
      const file = fileInput.files?.[0];
      if (file) {
        const reader = new FileReader();
        reader.onload = (e) => {
          const content = e.target?.result as string;
          if (content) {
            const res = this.registry.importTemplatesFromJSON(content);
            if (res.errors.length > 0) {
              alert(`Avisos ao importar:\n\n${res.errors.join('\n')}`);
            } else {
              alert(`✅ Sucesso! ${res.success} plugin(s) de agentes carregado(s) com sucesso.`);
            }
            this.activeCategory = 'Plugins Customizados';
            this.render();
          }
        };
        reader.readAsText(file);
      }
    });
  }

  private insertTemplateNode(templateId: string): void {
    const center = this.viewport.getCameraCenter();
    const existingCount = this.graph.getAllNodes().length;
    const offset = (existingCount % 5) * 40;
    const x = Math.round((center.x + offset) / 10) * 10;
    const y = Math.round((center.y - offset) / 10) * 10;

    const node = this.registry.instantiateNode(templateId, undefined, x, y);
    const cmd = new AddNodeCommand(this.graph, node);
    this.graph.commandManager.execute(cmd);

    // Seleciona o nó inserido
    this.viewport.selectNode(node.id);
  }

  public instantiateCompleteMultiAgentPreset(): void {
    this.graph.clear();

    // 1. Agente de Fila Concorrente (Produtor/Consumidor)
    const nQueue = this.registry.instantiateNode('concurrent_queue_worker', '1. Fila_Buffer_ThreadSafe', -500, 60);
    this.graph.addNode(nQueue);

    // 2. Agente de Visão (Decomposição em Subflow)
    const nVision = this.registry.instantiateNode('vision_pipeline_subflow', '2. Visao_Subflow_Paralelo', -150, 60);
    this.graph.addNode(nVision);

    // 3. Agente LLM (Decisor Estocástico com Ramo 0 e Ramo 1)
    const nLLM = this.registry.instantiateNode('llm_decision_agent', '3. LLM_Prompt_Decisor', 200, 60);
    this.graph.addNode(nLLM);

    // 4. Agente SQLite (Persistência com CriticalSection)
    const nSQLite = this.registry.instantiateNode('sqlite_persistence_agent', '4. SQLite_Persistencia_ACID', 580, 140);
    this.graph.addNode(nSQLite);

    // 5. Agente de Consenso (Módulo Encapsulado)
    const nConsensus = this.registry.instantiateNode('consensus_module_agent', '5. Modulo_Consenso_Final', 920, 140);
    this.graph.addNode(nConsensus);

    // Conexões:
    // Fila -> Visão
    this.graph.createEdge(nQueue.id, nQueue.outputs[0].id, nVision.id, nVision.inputs[0].id);

    // Visão -> LLM
    this.graph.createEdge(nVision.id, nVision.outputs[0].id, nLLM.id, nLLM.inputs[0].id);

    // LLM Ramo 0 (Aprovado) -> SQLite
    this.graph.createEdge(nLLM.id, nLLM.outputs[0].id, nSQLite.id, nSQLite.inputs[0].id);

    // LLM Ramo 1 (Re-tentativa / Feedback Loop) -> Volta para Fila Concorrente!
    this.graph.createEdge(nLLM.id, nLLM.outputs[1].id, nQueue.id, nQueue.inputs[0].id);

    // SQLite -> Módulo de Consenso
    this.graph.createEdge(nSQLite.id, nSQLite.outputs[0].id, nConsensus.id, nConsensus.inputs[0].id);

    setTimeout(() => {
      this.viewport.zoomToFit();
    }, 150);
  }

  private escapeHtml(text: string): string {
    return text
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
}
