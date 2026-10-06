import { Graph } from '../core/Graph.ts';
import { GraphValidator } from '../core/Validator.ts';
import { Viewport } from '../view/Viewport.ts';
import { CodeModal } from './CodeModal.ts';
import { TaskflowNodeType } from '../core/types.ts';
import { PipelineRunner } from '../core/PipelineRunner.ts';
import { AddNodeCommand } from '../core/history/GraphCommands.ts';
import { IHistoryState } from '../core/history/CommandManager.ts';
import { globalEventBus } from '../core/EventBus.ts';

import { TemplateModal } from './TemplateModal.ts';
import { GraphvizGenerator } from '../core/GraphvizGenerator.ts';

export class Toolbar {
  private container: HTMLElement;
  private graph: Graph;
  private viewport: Viewport;
  private codeModal: CodeModal;
  private templateModal: TemplateModal;

  constructor(parent: HTMLElement, graph: Graph, viewport: Viewport, codeModal: CodeModal, templateModal: TemplateModal) {
    this.graph = graph;
    this.viewport = viewport;
    this.codeModal = codeModal;
    this.templateModal = templateModal;

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
          <span class="brand-name">TaskForge</span>
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
        <button id="btn-run-pipeline" class="tool-btn btn-run-action" style="background:var(--gradient-blue); color:white;" title="Gera o código C++ e Compila (F5)">
          ▶ Build (C++)
        </button>
      </div>

      <div class="toolbar-separator"></div>

      <div class="toolbar-group">
        <span class="toolbar-group-label">Nós:</span>
        <select id="select-add-node" class="tool-btn btn-secondary" style="outline:none; cursor:pointer; min-width: 130px;">
          <option value="" disabled selected>+ Adicionar...</option>
          <option value="static">Estático (Task)</option>
          <option value="condition">Condição (Branch)</option>
          <option value="subflow">Subflow (Aninhado)</option>
          <option value="module">Módulo</option>
          <option value="critical">Crítico (Mutex)</option>
        </select>
      </div>

      <div class="toolbar-separator"></div>

      <!-- BIBLIOTECA DE TEMPLATES & PLUGINS DE AGENTES -->
      <div class="toolbar-group">
        <button id="btn-open-templates" class="tool-btn btn-templates-action" title="Abrir catálogo de templates de agentes especializados e plugins dinâmicos">
          🧩 Templates de Agentes
        </button>
      </div>

      <div class="toolbar-separator"></div>

      <div class="toolbar-group">
        <button id="btn-validate" class="tool-btn btn-action" title="Valida a integridade topológica do DAG e checa sentinelas de loops">🔍 Validar DAG</button>
        <button id="btn-export-code" class="tool-btn btn-secondary" title="Opção avançada de engenharia: inspecionar ou exportar arquivos de código fonte">⚙️ Exportar</button>
          <button id="btn-export-svg" class="tool-btn btn-secondary" title="Exportar arquitetura visual do grafo e baixar SVG usando Graphviz">🖼️ Exportar SVG</button>
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
    this.container.querySelector('#select-add-node')?.addEventListener('change', (e) => {
      const select = e.currentTarget as HTMLSelectElement;
      if (select.value) {
        this.createNewNode(select.value as TaskflowNodeType);
        select.value = ""; // Reseta o seletor após adicionar
      }
    });

    // Abrir Modal de Templates de Agentes
    this.container.querySelector('#btn-open-templates')?.addEventListener('click', () => {
      this.templateModal.show();
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


    // Exportar SVG Graphviz
    this.container.querySelector('#btn-export-svg')?.addEventListener('click', () => {
      this.exportGraphvizSVG();
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
      critical: `Shared_State_Lock_${existingCount + 1}`,
      cuda: `GPU_Kernel_${existingCount + 1}`
    };

    const node = this.graph.buildNode(id, labelMap[type], type, x, y);
    const cmd = new AddNodeCommand(this.graph, node);
    this.graph.commandManager.execute(cmd);

    // Seleciona e destaca imediatamente o nó recém-criado para abrir no Inspetor
    this.viewport.selectNode(id);
  }

  public loadDemoComplexSystem(): void {
    this.graph.clear();

    const n1 = this.graph.createNode('node_perceive', '1. Percepcao_Ambiente', 'static', -480, 80);
    const n2 = this.graph.createNode('node_state_lock', '2. Atualizar_Estado_Global', 'critical', -160, 80);
    const n3 = this.graph.createNode('node_evaluator', '3. Avaliacao_Convergencia', 'condition', 160, 80);
    const n4 = this.graph.createNode('node_cluster', '4. Decomposicao_Subflow', 'subflow', 480, 160);
    const n5 = this.graph.createNode('node_cleanup', '5. Modulo_Consolidacao', 'module', 800, 160);

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

  private exportGraphvizSVG(): void {
    const dotString = GraphvizGenerator.generateDot(this.graph);
    
    // Create an elegant viewer using d3-graphviz CDN
    const html = '<!DOCTYPE html>\n' +
'<html>\n' +
'<head>\n' +
'  <meta charset="utf-8">\n' +
'  <title>TaskForge Graphviz Export</title>\n' +
'  <style>\n' +
'    body { margin: 0; padding: 0; background: #f6f8fc; font-family: Inter, sans-serif; display: flex; flex-direction: column; height: 100vh; }\n' +
'    header { background: #ffffff; padding: 16px 24px; box-shadow: 0 1px 3px rgba(0,0,0,0.1); display: flex; justify-content: space-between; align-items: center; }\n' +
'    h1 { margin: 0; font-size: 18px; color: #0f172a; font-weight: 800; }\n' +
'    .btn { background: #3b82f6; color: white; border: none; padding: 8px 16px; border-radius: 8px; font-weight: 700; cursor: pointer; }\n' +
'    .btn:hover { background: #2563eb; }\n' +
'    #graph-container { flex: 1; overflow: auto; padding: 24px; display: flex; justify-content: center; align-items: center; }\n' +
'    svg { max-width: 100%; max-height: 100%; }\n' +
'  </style>\n' +
'</head>\n' +
'<body>\n' +
'  <header>\n' +
'    <h1>Visualizacao Graphviz (DOT)</h1>\n' +
'    <button class="btn" id="btn-download">Baixar SVG</button>\n' +
'  </header>\n' +
'  <div id="graph-container">Carregando renderizador Graphviz (WASM)...</div>\n' +
'  \n' +
'  <script src="https://d3js.org/d3.v7.min.js"></script>\n' +
'  <script src="https://unpkg.com/@hpcc-js/wasm@2.13.0/dist/index.min.js"></script>\n' +
'  <script src="https://unpkg.com/d3-graphviz@5.4.0/build/d3-graphviz.js"></script>\n' +
'  \n' +
'  <script>\n' +
'    const dotSrc = `' + dotString.replace(/`/g, '\\`') + '`;\n' +
'    \n' +
'    d3.select("#graph-container")\n' +
'      .graphviz()\n' +
'      .renderDot(dotSrc)\n' +
'      .on("end", function() {\n' +
'        document.getElementById("btn-download").addEventListener("click", () => {\n' +
'          const svgData = document.querySelector("#graph-container svg").outerHTML;\n' +
'          const blob = new Blob([svgData], { type: "image/svg+xml;charset=utf-8" });\n' +
'          const url = URL.createObjectURL(blob);\n' +
'          const a = document.createElement("a");\n' +
'          a.href = url;\n' +
'          a.download = "taskforge_architecture.svg";\n' +
'          a.click();\n' +
'          URL.revokeObjectURL(url);\n' +
'        });\n' +
'      });\n' +
'  </script>\n' +
'</body>\n' +
'</html>';

    const blob = new Blob([html], { type: 'text/html;charset=utf-8' });
    const url = URL.createObjectURL(blob);
    window.open(url, '_blank');
  }
}
