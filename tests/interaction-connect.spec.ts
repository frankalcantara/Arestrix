import { describe, it, expect, beforeAll, afterAll, beforeEach } from 'vitest';
import * as THREE from 'three';
import { Graph } from '../src/core/Graph.ts';
import { EventBus, globalEventBus } from '../src/core/EventBus.ts';
import { InteractionManager } from '../src/view/InteractionManager.ts';

// Gesto das portas SVG: pointerdown na saida emite node:start-connecting e pointerup na entrada
// emite node:complete-connecting (com stopPropagation, entao o onPointerUp da janela nao roda).
describe('Conexao entre vertices pelas portas SVG', () => {
  let originalWindow: any;
  let graph: Graph;

  function addNode(id: string): void {
    graph.addNode({
      id,
      type: 'agent',
      label: id,
      position: { x: 0, y: 0 },
      inputs: [{ id: 'in', name: 'in', direction: 'input', type: 'flow' }],
      outputs: [{ id: 'out', name: 'out', direction: 'output', type: 'flow' }]
    } as any);
  }

  function drag(fromNode: string, toNode: string): void {
    globalEventBus.emit('node:start-connecting', { nodeId: fromNode, portId: 'out', clientX: 0, clientY: 0 });
    globalEventBus.emit('node:complete-connecting', { nodeId: toNode, portId: 'in' });
  }

  beforeAll(() => {
    originalWindow = (globalThis as any).window;
    (globalThis as any).window = { addEventListener: () => {} };
  });

  afterAll(() => {
    (globalThis as any).window = originalWindow;
  });

  beforeEach(() => {
    graph = new Graph(new EventBus());
    addNode('a');
    addNode('b');
    const fakeView = { getPortWorldPosition: () => new THREE.Vector3() };
    const domElement: any = {
      addEventListener: () => {},
      clientWidth: 800,
      clientHeight: 600,
      style: {},
      getBoundingClientRect: () => ({ left: 0, top: 0, width: 800, height: 600 })
    };
    const cameraController: any = { screenToWorld: () => new THREE.Vector3(), getIsPanning: () => false };
    new InteractionManager(
      new THREE.OrthographicCamera(),
      cameraController,
      domElement,
      new THREE.Scene(),
      graph,
      new Map([['a', fakeView], ['b', fakeView]]) as any,
      new Map()
    );
  });

  it('soltar na porta de entrada cria a aresta no grafo e ela pode ser desfeita e refeita', () => {
    drag('a', 'b');
    const edges = graph.getAllEdges();
    expect(edges).toHaveLength(1);
    expect(edges[0]).toMatchObject({ sourceNodeId: 'a', sourcePortId: 'out', targetNodeId: 'b', targetPortId: 'in' });

    expect(graph.undo()).toBe(true);
    expect(graph.getAllEdges()).toHaveLength(0);
    expect(graph.redo()).toBe(true);
    expect(graph.getAllEdges()).toHaveLength(1);
  });

  it('nao conecta um vertice a ele mesmo nem sem um arrasto iniciado', () => {
    drag('a', 'a');
    globalEventBus.emit('node:complete-connecting', { nodeId: 'b', portId: 'in' });
    expect(graph.getAllEdges()).toHaveLength(0);
  });
});
