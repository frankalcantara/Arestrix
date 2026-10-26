import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import * as THREE from 'three';
import { Graph } from '../src/core/Graph.ts';
import { EventBus, globalEventBus } from '../src/core/EventBus.ts';
import { InteractionManager } from '../src/view/InteractionManager.ts';

/**
 * Regressões de interação do canvas: limiar de arraste (clique não move o vértice)
 * e ponte de duplo clique do vértice SVG para entrar em Subflow/Módulo.
 */
describe('InteractionManager - arraste e duplo clique', () => {
  let originalWindow: any;
  let windowHandlers: Record<string, Function[]>;
  let manager: InteractionManager;
  let graph: Graph;

  function emitWindow(type: string, event: any): void {
    (windowHandlers[type] || []).forEach(handler => handler(event));
  }

  function makePointerEvent(clientX: number, clientY: number, extra: Record<string, any> = {}): any {
    return {
      clientX,
      clientY,
      button: 0,
      shiftKey: false,
      altKey: false,
      pointerId: 1,
      target: null,
      stopPropagation: () => {},
      preventDefault: () => {},
      ...extra
    };
  }

  beforeEach(() => {
    originalWindow = (globalThis as any).window;
    windowHandlers = {};
    (globalThis as any).window = {
      addEventListener: (type: string, handler: Function) => {
        (windowHandlers[type] ||= []).push(handler);
      },
      removeEventListener: (type: string, handler: Function) => {
        windowHandlers[type] = (windowHandlers[type] || []).filter(h => h !== handler);
      }
    };

    graph = new Graph(new EventBus());
    graph.addNode({
      id: 'a',
      type: 'agent',
      label: 'A',
      position: { x: 100, y: 100 },
      inputs: [],
      outputs: [],
      parameters: {}
    });

    const nodeData = graph.getNode('a')!;
    const group = new THREE.Group();
    group.position.set(nodeData.position.x, nodeData.position.y, 0);

    const nodeView: any = {
      nodeData,
      group,
      updatePosition: (x: number, y: number) => {
        nodeData.position.x = x;
        nodeData.position.y = y;
        group.position.set(x, y, 0);
      },
      getBoundingSize: () => ({ width: 290, height: 160 }),
      getPortWorldPosition: () => new THREE.Vector3(),
      setSelected: () => {},
      isDetailVisible: () => true
    };

    const domElement: any = {
      addEventListener: () => {},
      removeEventListener: () => {},
      clientWidth: 800,
      clientHeight: 600,
      style: {},
      getBoundingClientRect: () => ({ left: 0, top: 0, width: 800, height: 600 })
    };

    // Mundo = cliente neste teste, o que torna o snap de 10px determinístico.
    const cameraController: any = {
      screenToWorld: (x: number, y: number) => new THREE.Vector3(x, y, 0),
      getIsPanning: () => false,
      stopPan: () => {}
    };

    manager = new InteractionManager(
      new THREE.OrthographicCamera(),
      cameraController,
      domElement,
      new THREE.Scene(),
      graph,
      new Map([['a', nodeView]]) as any,
      new Map()
    );
  });

  afterEach(() => {
    manager.dispose();
    (globalThis as any).window = originalWindow;
  });

  it('não move o vértice quando o ponteiro apenas treme durante o clique', () => {
    manager.startDragNode('a', 0, 0);

    emitWindow('pointermove', makePointerEvent(1, 1));
    emitWindow('pointermove', makePointerEvent(3, 2));
    emitWindow('pointerup', makePointerEvent(3, 2));

    expect(graph.getNode('a')!.position).toEqual({ x: 100, y: 100 });
    expect(graph.canUndo()).toBe(false);
  });

  it('move o vértice após vencer o limiar e registra undo uma única vez', () => {
    manager.startDragNode('a', 0, 0);

    // Antes do limiar: nada acontece.
    emitWindow('pointermove', makePointerEvent(2, 2));
    expect(graph.getNode('a')!.position).toEqual({ x: 100, y: 100 });

    // Cruza o limiar (4px) e reancora o offset: ainda sem salto visual.
    emitWindow('pointermove', makePointerEvent(10, 0));
    expect(graph.getNode('a')!.position).toEqual({ x: 100, y: 100 });

    // Movimento efetivo depois do limiar.
    emitWindow('pointermove', makePointerEvent(20, 0));
    expect(graph.getNode('a')!.position).toEqual({ x: 110, y: 100 });

    emitWindow('pointerup', makePointerEvent(20, 0));
    expect(graph.canUndo()).toBe(true);

    graph.undo();
    expect(graph.getNode('a')!.position).toEqual({ x: 100, y: 100 });
  });

  it('encerra o arraste quando o pointerup se perde (nenhum botão pressionado)', () => {
    manager.startDragNode('a', 0, 0);

    // Cruza o limiar (reancora) e em seguida move de fato.
    emitWindow('pointermove', makePointerEvent(10, 0, { buttons: 1 }));
    emitWindow('pointermove', makePointerEvent(20, 0, { buttons: 1 }));
    expect(graph.getNode('a')!.position).toEqual({ x: 110, y: 100 });

    // O pointerup foi consumido em outra camada: o próximo move chega sem botão.
    emitWindow('pointermove', makePointerEvent(40, 0, { buttons: 0 }));
    expect(graph.getNode('a')!.position).toEqual({ x: 110, y: 100 });
    expect(graph.canUndo()).toBe(true);

    // Movimentos seguintes não devem mais arrastar o vértice.
    emitWindow('pointermove', makePointerEvent(60, 0, { buttons: 1 }));
    expect(graph.getNode('a')!.position).toEqual({ x: 110, y: 100 });
  });

  it('cancela a conexão quando o pointerup não é do botão primário', () => {
    globalEventBus.emit('node:start-connecting', { nodeId: 'a', portId: 'out', clientX: 0, clientY: 0 });
    emitWindow('pointerup', makePointerEvent(0, 0, { button: 2 }));

    expect(graph.getAllEdges()).toHaveLength(0);
  });

  it('entra no Subflow quando o duplo clique do vértice SVG é encaminhado', () => {
    graph.addNode({
      id: 'sub',
      type: 'subflow',
      label: 'Subflow',
      position: { x: 300, y: 100 },
      inputs: [],
      outputs: [],
      parameters: {}
    });

    expect(graph.isAtRoot()).toBe(true);
    globalEventBus.emit('node:double-click', { nodeId: 'sub' });

    expect(graph.isAtRoot()).toBe(false);
    expect(graph.getNode('sub')).toBeUndefined();
  });

  it('sincroniza a seleção do canvas ao receber node:select-request', () => {
    globalEventBus.emit('node:select-request', { nodeId: 'a' });

    // selectNode emite node:selected com o INodeData e permite Delete/atalhos.
    expect(manager.getSelectedNodeId()).toBe('a');
  });

  it('remove os listeners de janela no dispose', () => {
    expect((windowHandlers['pointermove'] || []).length).toBe(1);
    manager.dispose();
    expect((windowHandlers['pointermove'] || []).length).toBe(0);
    expect((windowHandlers['pointerup'] || []).length).toBe(0);
  });
});
