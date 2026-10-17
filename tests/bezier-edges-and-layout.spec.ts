import { describe, it, expect } from 'vitest';
import * as THREE from 'three';
import { Graph } from '../src/core/Graph.ts';
import { GraphLayout } from '../src/core/GraphLayout.ts';
import {
  computeEdgeBezier,
  computeControlOffsetsFromCurveDrag,
  evaluateCubicBezier,
  findClosestBezierParameter,
  IObstacleBox
} from '../src/view/BezierEdgeRouter.ts';
import { UpdateEdgeCommand } from '../src/core/history/GraphCommands.ts';

describe('Curvas de Bézier Editáveis (Estilo Inkscape) e Desvio de Obstáculos', () => {
  it('deve desviar automaticamente de um vértice intermediário no caminho da aresta', () => {
    const start = new THREE.Vector3(-260, 0, 0);
    const end = new THREE.Vector3(260, 0, 0);
    const obstacles: IObstacleBox[] = [
      { id: 'node_src', x: -400, y: 0, width: 280, height: 150 },
      { id: 'node_mid', x: 0, y: 0, width: 280, height: 150 },
      { id: 'node_tgt', x: 400, y: 0, width: 280, height: 150 }
    ];

    const curve = computeEdgeBezier(
      start,
      end,
      { sourceNodeId: 'node_src', targetNodeId: 'node_tgt' },
      obstacles
    );

    // O ponto médio da curva (t=0.5) não pode passar por dentro do vértice intermediário (|y| > 75)
    expect(Math.abs(curve.midPoint.y)).toBeGreaterThan(90);
  });

  it('deve respeitar fielmente os controlPoints customizados (cp1Offset, cp2Offset)', () => {
    const start = new THREE.Vector3(-200, 20, 0);
    const end = new THREE.Vector3(200, -40, 0);

    const curve = computeEdgeBezier(
      start,
      end,
      {
        sourceNodeId: 'a',
        targetNodeId: 'b',
        controlPoints: {
          cp1Offset: { x: 120, y: 180 },
          cp2Offset: { x: -110, y: 160 }
        }
      },
      []
    );

    expect(curve.cp1.x).toBe(-80);
    expect(curve.cp1.y).toBe(200);
    expect(curve.cp2.x).toBe(90);
    expect(curve.cp2.y).toBe(120);
  });

  it('deve calcular novos offsets de controle ao arrastar diretamente um ponto da curva (Direct Curve Drag)', () => {
    const start = new THREE.Vector3(0, 0, 0);
    const end = new THREE.Vector3(300, 0, 0);
    const initial = computeEdgeBezier(start, end, { sourceNodeId: 'a', targetNodeId: 'b' }, []);

    const targetPoint = { x: 150, y: 140 };
    const t = findClosestBezierParameter(initial, { x: 150, y: 0 });
    const offsets = computeControlOffsetsFromCurveDrag(initial, t, targetPoint);

    const updated = computeEdgeBezier(
      start,
      end,
      { sourceNodeId: 'a', targetNodeId: 'b', controlPoints: offsets },
      []
    );

    const ptAtT = evaluateCubicBezier(updated.start, updated.cp1, updated.cp2, updated.end, t);
    expect(ptAtT.x).toBeCloseTo(targetPoint.x, 0);
    expect(ptAtT.y).toBeCloseTo(targetPoint.y, 0);
  });

  it('deve suportar edição, persistência JSON e Undo/Redo de alças Bézier na aresta', () => {
    const graph = new Graph();
    const n1 = graph.createNode('n1', 'Origem', 'static', -300, 0);
    const n2 = graph.createNode('n2', 'Destino', 'static', 300, 0);
    const edge = graph.createEdge(n1.id, n1.outputs[0].id, n2.id, n2.inputs[0].id);

    expect(graph.getEdge(edge.id)?.controlPoints).toBeUndefined();

    const customControls = {
      cp1Offset: { x: 140, y: 210 },
      cp2Offset: { x: -140, y: 190 }
    };

    const cmd = new UpdateEdgeCommand(graph, edge.id, undefined, customControls);
    graph.commandManager.execute(cmd);

    expect(graph.getEdge(edge.id)?.controlPoints).toEqual(customControls);

    // Verifica serialização JSON
    const schema = graph.toJSON();
    expect(schema.edges[0].controlPoints).toEqual(customControls);

    // Undo restaura curva automática (controlPoints = undefined)
    graph.undo();
    expect(graph.getEdge(edge.id)?.controlPoints).toBeUndefined();

    // Redo reaplica as alças customizadas
    graph.redo();
    expect(graph.getEdge(edge.id)?.controlPoints).toEqual(customControls);
  });
});

describe('Algoritmo de Distribuição de Vértices (Sugiyama 2D Auto-Layout)', () => {
  it('deve detectar vértices acumulados em linha reta ou sobrepostos via needsAutoLayout', () => {
    const graph = new Graph();
    const n1 = graph.createNode('n1', 'Etapa 1', 'agent', 0, 0);
    const n2 = graph.createNode('n2', 'Etapa 2', 'agent', 600, 0);
    const n3 = graph.createNode('n3', 'Etapa 3', 'agent', 1200, 0);
    const n4 = graph.createNode('n4', 'Etapa 4', 'agent', 1800, 0);

    expect(GraphLayout.needsAutoLayout([n1, n2, n3, n4])).toBe(true);
  });

  it('deve distribuir cadeias longas em múltiplas faixas 2D sem sobreposição e sem colinearidade estrita', () => {
    const graph = new Graph();
    const nodes = [];
    for (let i = 1; i <= 8; i++) {
      // Todos acumulados na linha y = 0
      nodes.push(graph.createNode(`n${i}`, `Agente ${i}`, 'agent', (i - 1) * 200, 0));
    }
    for (let i = 0; i < nodes.length - 1; i++) {
      graph.createEdge(nodes[i].id, nodes[i].outputs[0].id, nodes[i + 1].id, nodes[i + 1].inputs[0].id);
    }

    graph.autoLayout(true);

    const updatedNodes = graph.getAllNodes();
    // Nenhum par de vértices pode colidir
    for (let i = 0; i < updatedNodes.length; i++) {
      for (let j = i + 1; j < updatedNodes.length; j++) {
        const a = updatedNodes[i].position;
        const b = updatedNodes[j].position;
        const overlaps = Math.abs(a.x - b.x) < 285 && Math.abs(a.y - b.y) < 135;
        expect(overlaps).toBe(false);
      }
    }

    // Como são 8 nós em sequência, deve ter quebrado em 2 faixas horizontais (spanY > 250)
    const ys = updatedNodes.map(n => n.position.y);
    const spanY = Math.max(...ys) - Math.min(...ys);
    expect(spanY).toBeGreaterThan(250);

    // Undo deve restaurar as posições originais (y = 0)
    graph.undo();
    expect(graph.getAllNodes().every(n => n.position.y === 0)).toBe(true);

    // Redo reaplica o layout 2D
    graph.redo();
    const redoYs = graph.getAllNodes().map(n => n.position.y);
    expect(Math.max(...redoYs) - Math.min(...redoYs)).toBeGreaterThan(250);
  });

  it('deve aplicar auto-layout automaticamente ao carregar um JSON com vértices em linha reta (fromJSON)', () => {
    const rawGraph = new Graph();
    const a = rawGraph.createNode('a', 'Início', 'static', 0, 0);
    const b = rawGraph.createNode('b', 'Triagem', 'switch', 600, 0);
    const c1 = rawGraph.createNode('c1', 'Ramo 1', 'agent', 1200, 0);
    const c2 = rawGraph.createNode('c2', 'Ramo 2', 'agent', 1800, 0);
    const d = rawGraph.createNode('d', 'Fim', 'static', 2400, 0);

    rawGraph.createEdge(a.id, a.outputs[0].id, b.id, b.inputs[0].id);
    rawGraph.createEdge(b.id, b.outputs[0].id, c1.id, c1.inputs[0].id);
    rawGraph.createEdge(b.id, b.outputs[1].id, c2.id, c2.inputs[0].id);
    rawGraph.createEdge(c1.id, c1.outputs[0].id, d.id, d.inputs[0].id);
    rawGraph.createEdge(c2.id, c2.outputs[0].id, d.id, d.inputs[0].id);

    const collinearSchema = rawGraph.toJSON();

    const targetGraph = new Graph();
    targetGraph.fromJSON(collinearSchema);

    // c1 (conectado à porta 0 superior) deve ficar acima (+Y) de c2 (conectado à porta 1 inferior)
    const loadedC1 = targetGraph.getNode('c1')!;
    const loadedC2 = targetGraph.getNode('c2')!;
    expect(loadedC1.position.x).toBe(loadedC2.position.x);
    expect(loadedC1.position.y).toBeGreaterThan(loadedC2.position.y);
    expect(GraphLayout.needsAutoLayout(targetGraph.getAllNodes(), targetGraph.getAllEdges())).toBe(false);
  });
});
