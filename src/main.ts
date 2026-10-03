import './style.css';
import { Graph } from './core/Graph.ts';
import { globalEventBus } from './core/EventBus.ts';
import { Viewport } from './view/Viewport.ts';
import { Toolbar } from './ui/Toolbar.ts';
import { Inspector } from './ui/Inspector.ts';
import { CodeModal } from './ui/CodeModal.ts';
import { ConsolePanel } from './ui/ConsolePanel.ts';

function bootstrap() {
  const appContainer = document.querySelector<HTMLDivElement>('#app');
  if (!appContainer) throw new Error('#app root element not found.');

  // 1. Instância do Grafo Central com compilador padrão MSVC C++20
  const graph = new Graph(globalEventBus, {
    graphName: 'AgentComplexOrchestrator',
    description: 'Pipeline Concorrente de Agentes e Decisao com Taskflow C++20',
    targetStandard: 'C++20',
    compilerConfig: {
      compiler: 'msvc',
      cppStandard: 'C++20',
      buildSystem: 'cmake',
      taskflowIncludePath: 'include',
      outputBinaryName: 'agent_pipeline',
      enableOpenMP: false,
      enableCuda: false,
      extraFlags: ['/std:c++20', '/EHsc', '/O2']
    }
  });

  // 2. Modais
  const codeModal = new CodeModal(graph);

  // 3. Estrutura do Layout DOM
  const mainLayout = document.createElement('main');
  mainLayout.className = 'main-layout';

  const centerArea = document.createElement('div');
  centerArea.className = 'center-area';

  const viewportContainer = document.createElement('div');
  viewportContainer.className = 'viewport-container';
  centerArea.appendChild(viewportContainer);

  mainLayout.appendChild(centerArea);

  // 4. Inicializa Viewport WebGL (Three.js)
  const viewport = new Viewport(viewportContainer, graph, globalEventBus);

  // 5. Inicializa Barra de Ferramentas
  const toolbar = new Toolbar(appContainer, graph, viewport, codeModal);

  // 6. Insere layout principal e sidebar de propriedades
  appContainer.appendChild(mainLayout);
  new Inspector(mainLayout, graph, globalEventBus);

  // 7. Console de Execução & Build na base da área central
  new ConsolePanel(centerArea);

  // 8. Carrega o cenário de agentes demonstrativo complexo com loops de condição e seções críticas
  toolbar.loadDemoComplexSystem();
}

// Inicializa no carregamento do DOM
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', bootstrap);
} else {
  bootstrap();
}
