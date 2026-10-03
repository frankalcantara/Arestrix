import { Graph } from '../core/Graph.ts';
import { GraphValidator } from '../core/Validator.ts';
import { Viewport } from '../view/Viewport.ts';
import { CodeModal } from './CodeModal.ts';
import { TaskflowNodeType } from '../core/types.ts';
import { PipelineRunner } from '../core/PipelineRunner.ts';
import { AddNodeCommand } from '../core/history/GraphCommands.ts';
import { IHistoryState } from '../core/history/CommandManager.ts';
import { globalEventBus } from '../core/EventBus.ts';

export class Toolbar {
  private container: HTMLElement;
  private graph: Graph;
  private viewport: Viewport;
  private codeModal: CodeModal;

  constructor(parent: HTMLElement, graph: Graph, viewport: Viewport, codeModal: CodeModal) {
    this.graph = graph;
    this.viewport = viewport;
    this.codeModal = codeModal;

    this.container = document.createElement('header');
    this.container.className = 'app-toolbar';
    parent.appendChild(this.container);

    this.render();
    this.bindKeyboardShortcuts();
  }

  private render(): void {
    this.container.innerHTML = `
      <div class="toolbar-brand">
        <div class="brand-logo">⚡</div>
        <div class="brand-text">
          <span class="brand-name">NodeForge</span>
          <span class="brand-sub">Auto Build & Run Studio</span>
        </div>
      </div>

      <div class="toolbar-separator"></div>

      <!-- HISTORICO UNDO / REDO -->
      <div class="toolbar-group">
        <button id="btn-undo" class="tool-btn btn-secondary" title="Desfazer (Ctrl+Z)" disabled>↶ Desfazer</button>
        <button id="btn-redo" class="tool-btn btn-secondary" title="Refazer (Ctrl+Y)" disabled>↷ Refazer</button>
      </div>

      <div class="toolbar-separator"></div>

      <!-- ACAO PRIMARIA NORMAL: COMPILAR E EXECUTAR -->
      <div class="toolbar-group">
        <button id="btn-run-pipeline" class="tool-btn btn-run-action" title="Compila com MSVC C++20 e executa o pipeline de agentes imediatamente (F5)">
          ▶ Executar Pipeline (F5)
        </button>
      </div>

      <div class="toolbar-separator"></div>

      <div class="toolbar-group">
        <span class="toolbar-group-label">Nós:</span>
        <button class="tool-btn btn-node-static" data-type="static">+ Estático</button>
        <button class="tool-btn btn-node-cond" data-type="condition">+ Condição</button>
        <button class="tool-btn btn-node-subflow" data-type="subflow">+ Subflow</button>
        <button class="tool-btn btn-node-module" data-type="module">+ Módulo</button>
        <button class="tool-btn btn-node-crit" data-type="critical">+ Crítico</button>
      </div>

      <div class="toolbar-separator"></div>

      <div class="toolbar-group">
        <button id="btn-validate" class="tool-btn btn-action" title="Valida a integridade topológica do DAG e checa sentinelas de loops">🔍 Validar DAG</button>
        <button id="btn-export-code" class="tool-btn btn-secondary" title="Opção avançada de engenharia: inspecionar ou exportar arquivos de código fonte">⚙️ Exportar</button>
      </div>

      <div class="toolbar-separator"></div>

      <div class="toolbar-group toolbar-group-right">
        <button id="btn-load-demo" class="tool-btn btn-secondary">💡 Exemplo de Agentes</button>
        <button id="btn-zoom-fit" class="tool-btn btn-secondary">🎯 Ajustar Vista</button>
        <button id="btn-clear-graph" class="tool-btn btn-danger-outline">🗑️ Limpar</button>
      </div>
    `;

    // Desfazer / Refazer
    const btnUndo = this.container.querySelector('#btn-undo') as HTMLButtonElement;
    const btnRedo = this.container.querySelector('#btn-redo') as HTMLButtonElement;

    btnUndo?.addEventListener('click', () => {
      this.graph.undo();
    });

    btnRedo?.addEventListener('click', () => {
      this.graph.redo();
    });

    globalEventBus.on('history:changed', (state: IHistoryState) => {
      if (btnUndo) {
        btnUndo.disabled = !state.canUndo;
        btnUndo.title = state.canUndo && state.undoDesc 
          ? `Desfazer: ${state.undoDesc} (Ctrl+Z)` 
          : 'Desfazer (Ctrl+Z)';
      }
      if (btnRedo) {
        btnRedo.disabled = !state.canRedo;
        btnRedo.title = state.canRedo && state.redoDesc 
          ? `Refazer: ${state.redoDesc} (Ctrl+Y)` 
          : 'Refazer (Ctrl+Y)';
      }
    });

    // Evento de Execução Principal (Compilar e Rodar)
    this.container.querySelector('#btn-run-pipeline')?.addEventListener('click', () => {
      this.executePipeline();
    });

    // Eventos de criacao de nos
    const nodeButtons = this.container.querySelectorAll('[data-type]');
    nodeButtons.forEach(btn => {
      btn.addEventListener('click', (e) => {
        const type = (e.currentTarget as HTMLElement).getAttribute('data-type') as TaskflowNodeType;
        this.createNewNode(type);
      });
    });

    // Validar grafo
    this.container.querySelector('#btn-validate')?.addEventListener('click', () => {
      const res = GraphValidator.validate(this.graph);
      if (res.isValid) {
        alert('✅ Grafo 100% Válido!\nTodas as dependências e sentinelas de condição do Taskflow estão corretas.');
      } else {
        const msg = res.errors.map(err => `[${err.severity.toUpperCase()}] ${err.message}`).join('\n\n');
        alert(`⚠️ Relatório de Validação:\n\n${msg}`);
      }
    });

    // Exportar codigo C++20 (Opcao secundaria de engenharia)
    this.container.querySelector('#btn-export-code')?.addEventListener('click', () => {
      this.codeModal.show();
    });

    // Carregar Exemplo de Sistema Complexo de Agentes
    this.container.querySelector('#btn-load-demo')?.addEventListener('click', () => {
      this.loadDemoComplexSystem();
    });

    // Zoom to Fit
    this.container.querySelector('#btn-zoom-fit')?.addEventListener('click', () => {
      this.viewport.zoomToFit();
    });

    // Limpar
    this.container.querySelector('#btn-clear-graph')?.addEventListener('click', () => {
      if (confirm('Deseja realmente limpar todo o fluxo de nós?')) {
        this.graph.clear();
      }
    });
  }

  public executePipeline(): void {
    PipelineRunner.run(this.graph);
  }

  private bindKeyboardShortcuts(): void {
    window.addEventListener('keydown', (e: KeyboardEvent) => {
      // F5 ou Ctrl+Enter dispara o build e run automático
      if (e.key === 'F5' || (e.ctrlKey && e.key === 'Enter')) {
        e.preventDefault();
        this.executePipeline();
      }
    });
  }

  public getContainer(): HTMLElement {
    return this.container;
  }

  private createNewNode(type: TaskflowNodeType): void {
    const existingCount = this.graph.getAllNodes().length;
    const id = `node_${Date.now()}`;
    
    // Posiciona no centro visível da visão atual do usuário com offset escalonado
    const center = this.viewport.getCameraCenter();
    const offset = (existingCount % 6) * 35;
    const x = Math.round((center.x + offset) / 10) * 10;
    const y = Math.round((center.y - offset) / 10) * 10;

    const labelMap: Record<TaskflowNodeType, string> = {
      static: `Agente_Worker_${existingCount + 1}`,
      condition: `Decisor_Stochastic_${existingCount + 1}`,
      subflow: `Dynamic_Cluster_${existingCount + 1}`,
      module: `Module_Pipeline_${existingCount + 1}`,
      critical: `Shared_State_Lock_${existingCount + 1}`
    };

    const node = this.graph.buildNode(id, labelMap[type], type, x, y);
    const cmd = new AddNodeCommand(this.graph, node);
    this.graph.commandManager.execute(cmd);

    // Seleciona e destaca imediatamente o nó recém-criado para abrir no Inspetor
    this.viewport.selectNode(id);
  }

  public loadDemoComplexSystem(): void {
    this.graph.clear();

    const n1 = this.graph.createNode('node_perceive', '1. Percepcao_Ambiente', 'static', -400, 100);
    const n2 = this.graph.createNode('node_state_lock', '2. Atualizar_Estado_Global', 'critical', -150, 100);
    const n3 = this.graph.createNode('node_evaluator', '3. Avaliacao_Convergencia', 'condition', 120, 100);
    const n4 = this.graph.createNode('node_cluster', '4. Decomposicao_Subflow', 'subflow', 400, 180);
    const n5 = this.graph.createNode('node_cleanup', '5. Modulo_Consolidacao', 'module', 680, 180);

    // Conexoes
    this.graph.createEdge(n1.id, n1.outputs[0].id, n2.id, n2.inputs[0].id);
    this.graph.createEdge(n2.id, n2.outputs[0].id, n3.id, n3.inputs[0].id);

    // Condicao: Branch 0 -> Sucesso (vai para Subflow n4)
    this.graph.createEdge(n3.id, n3.outputs[0].id, n4.id, n4.inputs[0].id);

    // Condicao: Branch 1 -> Feedback Loop! Volta para n1 para nova iteracao
    this.graph.createEdge(n3.id, n3.outputs[1].id, n1.id, n1.inputs[0].id);

    // Subflow -> Modulo final
    this.graph.createEdge(n4.id, n4.outputs[0].id, n5.id, n5.inputs[0].id);

    // Ajusta a vista para enquadrar perfeitamente o grafo
    setTimeout(() => {
      this.viewport.zoomToFit();
    }, 100);
  }
}
