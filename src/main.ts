import './style.css';
import { Graph } from './core/Graph.ts';
import { globalEventBus } from './core/EventBus.ts';
import { Viewport } from './view/Viewport.ts';
import { Toolbar } from './ui/Toolbar.ts';
import { Inspector } from './ui/Inspector.ts';
import { CodeModal } from './ui/CodeModal.ts';
import { ConsolePanel } from './ui/ConsolePanel.ts';
import { BreadcrumbsBar } from './ui/BreadcrumbsBar.ts';
import { TemplateModal } from './ui/TemplateModal.ts';

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

  // Barra de Breadcrumbs hierárquica
  new BreadcrumbsBar(centerArea, graph, globalEventBus);

  const viewportContainer = document.createElement('div');
  viewportContainer.className = 'viewport-container';
  centerArea.appendChild(viewportContainer);

  mainLayout.appendChild(centerArea);

  // Insere mainLayout no DOM antes de instanciar o Viewport para garantir dimensões não nulas
  appContainer.appendChild(mainLayout);

  // 4. Inicializa Viewport WebGL (Three.js) com dimensões reais já computadas pelo DOM
  const viewport = new Viewport(viewportContainer, graph, globalEventBus);

  // 5. Modal de Templates de Agentes & Plugins
  const templateModal = new TemplateModal(graph, viewport);

  // 6. Inicializa Barra de Ferramentas e posiciona no topo
  const toolbar = new Toolbar(appContainer, graph, viewport, codeModal, templateModal);
  appContainer.insertBefore(toolbar.getContainer(), mainLayout);

  // 6. Sidebar de propriedades (Inspetor) dentro de mainLayout
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
