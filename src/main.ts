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
import { GlobalDefaultsModal } from './ui/GlobalDefaultsModal.ts';
import { NodeSandboxModal } from './ui/modals/NodeSandboxModal.ts';
import { NodeModelModal } from './ui/modals/NodeModelModal.ts';
import { NodeToolsModal } from './ui/modals/NodeToolsModal.ts';
import { NodeMemoryModal } from './ui/modals/NodeMemoryModal.ts';
import { NodeSkillsModal } from './ui/modals/NodeSkillsModal.ts';
import { SkillEditorModal } from './ui/modals/SkillEditorModal.ts';
import { ProjectManager } from './core/ProjectManager.ts';
import { StatusBar } from './ui/StatusBar.ts';
import { CanvasControls } from './ui/CanvasControls.ts';

function bootstrap() {
  const appContainer = document.querySelector<HTMLDivElement>('#app');
  if (!appContainer) throw new Error('#app root element not found.');

  // 0. Inicializa gerenciador de projetos
  const projectManager = ProjectManager.getInstance(globalEventBus);
  projectManager.init();

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

  // Controles Flutuantes do Grafo / HUD do Visio
  new CanvasControls(viewportContainer, viewport, globalEventBus);

  // 5. Modais de Agentes, Code Mode e Sandbox/VM (Fase 7)
  const templateModal = new TemplateModal(graph, viewport);
  const globalDefaultsModal = new GlobalDefaultsModal(graph);
  const nodeSandboxModal = new NodeSandboxModal(graph);
  const nodeModelModal = new NodeModelModal(graph);
  const nodeToolsModal = new NodeToolsModal(graph);
  const nodeMemoryModal = new NodeMemoryModal(graph);
  const nodeSkillsModal = new NodeSkillsModal(graph);
  const skillEditorModal = new SkillEditorModal();

  // Escuta eventos dos botoes de acao rapida nos nós e no inspetor
  globalEventBus.on('node:open-sandbox-modal', (nodeId: string) => nodeSandboxModal.open(nodeId));
  globalEventBus.on('node:open-model-modal', (nodeId: string) => nodeModelModal.open(nodeId));
  globalEventBus.on('node:open-tools-modal', (nodeId: string) => nodeToolsModal.open(nodeId));
  globalEventBus.on('node:open-memory-modal', (nodeId: string) => nodeMemoryModal.open(nodeId));
  globalEventBus.on('node:open-skills-modal', (nodeId: string, initialTab?: any) => nodeSkillsModal.open(nodeId, initialTab));
  globalEventBus.on('node:open-skill-editor', (opts: any) => skillEditorModal.openSkill(opts));

  // 6. Inicializa Barra de Ferramentas Ribbon e posiciona no topo
  const toolbar = new Toolbar(appContainer, graph, viewport, codeModal, templateModal, globalDefaultsModal);
  appContainer.insertBefore(toolbar.getContainer(), mainLayout);

  // 7. Sidebar de propriedades (Inspetor / Task Pane Office) dentro de mainLayout
  new Inspector(mainLayout, graph, globalEventBus);

  // 8. Console de Execução & Build na base da área central (Docked Bottom Drawer)
  new ConsolePanel(centerArea);

  // 9. Barra de Status do Microsoft Office na base absoluta da aplicação
  new StatusBar(appContainer, graph, viewport, globalEventBus);

  // 10. Carrega o cenário de agentes demonstrativo complexo com loops de condição e seções críticas
  toolbar.loadDemoComplexSystem();

  // 9. Bridge de Telemetria de Agentes em Tempo Real (Fase 10)
  const onTelemetryHandler = (data: any) => {
    if (data && data.event) {
      globalEventBus.emit('agent:telemetry', data.event);
      globalEventBus.emit(`agent:telemetry:${data.event.nodeId}`, data.event);
    }
  };
  // @ts-ignore
  window.onArestrixTelemetry = onTelemetryHandler;
  // @ts-ignore
  window.onTaskForgeTelemetry = onTelemetryHandler;
}

// Inicializa no carregamento do DOM
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', bootstrap);
} else {
  bootstrap();
}
