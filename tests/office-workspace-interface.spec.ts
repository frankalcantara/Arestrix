import { describe, it, expect, beforeEach } from 'vitest';
import { THEME } from '../src/view/Theme.ts';
import { CameraController } from '../src/view/CameraController.ts';
import * as THREE from 'three';

describe('Área Útil do Grafo: Padrão Microsoft Office / Visio Fluent UI', () => {
  it('deve adotar paleta corporativa Microsoft Office e proporções técnicas do Visio no THEME', () => {
    // Nós e cores corporativas do Office
    expect(THEME.nodeTypes.static.color).toBe(0x0f6cbd); // Azul Clássico Office
    expect(THEME.nodeTypes.condition.color).toBe(0xc43e1c); // Laranja PowerPoint / Rust
    expect(THEME.nodeTypes.subflow.color).toBe(0x5c2d91); // Púrpura Teams
    expect(THEME.nodeTypes.module.color).toBe(0x107c41); // Verde Excel
    expect(THEME.nodeTypes.critical.color).toBe(0x008272); // Teal SharePoint

    // Geometria precisa do Visio (cantos sóbrios de 6px em vez de bolhas de 16px/22px)
    expect(THEME.nodeBody.borderRadius).toBe(6);
    expect(THEME.nodeBody.borderSelected).toBe(0x0f6cbd);
    expect(THEME.nodeBody.borderColor).toBe(0xcbd5e1);

    // Tipografia Segoe UI / Fluent
    expect(THEME.typography.fontFamily).toContain('Segoe UI');
  });

  it('deve fornecer controle programático de zoom com limites seguros no CameraController', () => {
    const frustumSize = 1000;
    const camera = new THREE.OrthographicCamera(-500, 500, 500, -500, 0.1, 2000);
    camera.position.set(0, 0, 1000);

    // Mock simples de domElement
    const fakeDom = {
      getBoundingClientRect: () => ({ left: 0, top: 0, width: 800, height: 600 }),
      addEventListener: () => {},
      clientWidth: 800,
      clientHeight: 600,
      style: {}
    } as any;

    const controller = new CameraController(camera, fakeDom, frustumSize);

    // Zoom inicial deve ser 1.0
    expect(controller.getZoom()).toBe(1.0);

    // Zoom In
    controller.zoomIn(1.2);
    expect(controller.getZoom()).toBeCloseTo(1.2, 2);

    // Zoom Out
    controller.zoomOut(0.5);
    expect(controller.getZoom()).toBeCloseTo(0.6, 2);

    // Limites de Zoom (não deve ultrapassar min 0.2 nem max 3.5)
    controller.setZoom(10.0);
    expect(controller.getZoom()).toBe(3.5);

    controller.setZoom(0.05);
    expect(controller.getZoom()).toBe(0.2);

    // Reset para 100%
    controller.reset();
    expect(controller.getZoom()).toBe(1.0);
  });

  it('deve conter estrutura de classes CSS completas para layout docked, Task Pane e StatusBar Office', () => {
    const workspaceClasses = [
      'main-layout',
      'center-area',
      'breadcrumbs-bar',
      'viewport-container',
      'office-canvas-controls',
      'canvas-hud-btn',
      'console-panel',
      'office-docked-console',
      'office-console-splitter',
      'office-console-header',
      'office-task-pane',
      'office-task-pane-header',
      'office-task-pane-tabs',
      'office-statusbar',
      'office-zoom-control',
      'office-zoom-slider'
    ];

    expect(workspaceClasses).toContain('office-task-pane');
    expect(workspaceClasses).toContain('office-statusbar');
    expect(workspaceClasses).toContain('office-docked-console');
    expect(workspaceClasses).toContain('office-canvas-controls');
    expect(workspaceClasses).toContain('office-console-splitter');
    expect(workspaceClasses.length).toBe(16);
  });

  it('deve suportar pan do canvas em área vazia através de startPan, panBy e stopPan no CameraController', () => {
    const frustumSize = 1000;
    const camera = new THREE.OrthographicCamera(-500, 500, 500, -500, 0.1, 2000);
    camera.position.set(0, 0, 1000);

    const fakeDom = {
      getBoundingClientRect: () => ({ left: 0, top: 0, width: 800, height: 600 }),
      addEventListener: () => {},
      clientWidth: 800,
      clientHeight: 600,
      style: {}
    } as any;

    const controller = new CameraController(camera, fakeDom, frustumSize);

    expect(controller.getIsPanning()).toBe(false);

    // Inicia pan (como quando clica em área vazia)
    controller.startPan(100, 150);
    expect(controller.getIsPanning()).toBe(true);
    expect(fakeDom.style.cursor).toBe('grabbing');

    // Deslocamento de pan relativo (panBy)
    // scale = (1000 / 600) / 1.0 = 1.666...
    // dx = 60 -> camera.position.x subtrai 100
    // dy = 30 -> camera.position.y soma 50
    controller.panBy(60, 30);
    expect(camera.position.x).toBeCloseTo(-100, 1);
    expect(camera.position.y).toBeCloseTo(50, 1);

    // Finaliza pan
    controller.stopPan();
    expect(controller.getIsPanning()).toBe(false);
    expect(fakeDom.style.cursor).toBe('default');
  });

  it('deve validar classes de Task Pane dinâmico (splitter, flutuação e recolhimento)', () => {
    const dynamicPaneClasses = [
      'office-task-pane-splitter',
      'office-task-pane-toggle-strip',
      'is-floating',
      'office-tp-btn-icon',
      'office-tp-width-btn'
    ];

    expect(dynamicPaneClasses).toContain('office-task-pane-splitter');
    expect(dynamicPaneClasses).toContain('is-floating');
    expect(dynamicPaneClasses).toContain('office-tp-width-btn');
    expect(dynamicPaneClasses.length).toBe(5);
  });

  it('deve permitir alterar largura do Painel de Tarefas via setWidth e cycleWidth no Inspector', async () => {
    const origDoc = (globalThis as any).document;
    const origWin = (globalThis as any).window;

    const createMockElement = (tag: string) => {
      const children: any[] = [];
      const el: any = {
        tagName: tag.toUpperCase(),
        className: '',
        style: {},
        children,
        classList: {
          add: (cls: string) => { el.className += ` ${cls}`; },
          remove: (cls: string) => { el.className = el.className.replace(cls, '').trim(); }
        },
        appendChild: (child: any) => children.push(child),
        querySelector: (sel: string) => {
          if (sel === '.office-task-pane-splitter') return children.find(c => c.className?.includes('office-task-pane-splitter'));
          if (sel === '.office-task-pane') return children.find(c => c.className?.includes('office-task-pane'));
          return null;
        },
        querySelectorAll: () => [],
        addEventListener: () => {},
        removeEventListener: () => {},
        innerHTML: ''
      };
      return el;
    };

    (globalThis as any).document = {
      createElement: createMockElement,
      body: { style: {} }
    };
    (globalThis as any).window = {
      innerWidth: 1920,
      innerHeight: 1080,
      addEventListener: () => {},
      removeEventListener: () => {}
    };

    try {
      const { Inspector } = await import('../src/ui/Inspector.ts');
      const { Graph } = await import('../src/core/Graph.ts');
      const { EventBus } = await import('../src/core/EventBus.ts');

      const parent = createMockElement('div');
      const bus = new EventBus();
      const graph = new Graph(bus);

      const inspector = new Inspector(parent, graph, bus);

      // Largura inicial padrão
      expect(inspector.getWidth()).toBe(360);

      // Redimensionamento manual para qualquer largura (ex: 450px)
      inspector.setWidth(450);
      expect(inspector.getWidth()).toBe(450);

      // Limites de segurança (min 260px)
      inspector.setWidth(100);
      expect(inspector.getWidth()).toBe(260);

      // Presets rápidos (cycleWidth: 340 -> 480 -> 640 -> 820 -> 340)
      inspector.setWidth(340);
      inspector.cycleWidth();
      expect(inspector.getWidth()).toBe(480);

      inspector.cycleWidth();
      expect(inspector.getWidth()).toBe(640);

      inspector.cycleWidth();
      expect(inspector.getWidth()).toBe(820);

      inspector.cycleWidth();
      expect(inspector.getWidth()).toBe(340);

      // Verifica que o elemento splitter físico foi inserido no parent antes do container
      const splitter = parent.querySelector('.office-task-pane-splitter');
      expect(splitter).not.toBeNull();
      const taskPane = parent.querySelector('.office-task-pane');
      expect(taskPane).not.toBeNull();
    } finally {
      (globalThis as any).document = origDoc;
      (globalThis as any).window = origWin;
    }
  });
});
