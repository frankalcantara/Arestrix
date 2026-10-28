import { describe, it, expect } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { computeFlowRoles, computeFlowRoleMap, computeTrailAssignments } from '../src/core/GraphTopology.ts';

/** Encadeia nós já criados: n1 -> n2 -> ... */
function chain(graph: Graph, nodes: ReturnType<Graph['createNode']>[]): void {
  for (let i = 0; i + 1 < nodes.length; i++) {
    graph.createEdge(nodes[i].id, nodes[i].outputs[0].id, nodes[i + 1].id, nodes[i + 1].inputs[0].id);
  }
}

describe('GraphTopology - pontas de trilha (selos INÍCIO/FIM)', () => {
  it('identifica duas trilhas disjuntas: duas entradas e duas saídas ambíguas', () => {
    const graph = new Graph(new EventBus());
    const a1 = graph.createNode('a1', 'A1', 'static', 0, 0);
    const a2 = graph.createNode('a2', 'A2', 'static', 200, 0);
    const a3 = graph.createNode('a3', 'A3', 'static', 400, 0);
    const b1 = graph.createNode('b1', 'B1', 'static', 0, 200);
    const b2 = graph.createNode('b2', 'B2', 'static', 200, 200);
    const b3 = graph.createNode('b3', 'B3', 'static', 400, 200);
    chain(graph, [a1, a2, a3]);
    chain(graph, [b1, b2, b3]);

    const roles = computeFlowRoles(graph);
    expect(roles.starts).toEqual(['a1', 'b1']);
    expect(roles.ends).toEqual(['a3', 'b3']);
    expect(roles.ambiguous).toBe(true);

    const map = computeFlowRoleMap(roles);
    expect(map.get('a1')).toBe('start');
    expect(map.get('b1')).toBe('start');
    expect(map.get('a3')).toBe('end');
    expect(map.get('b3')).toBe('end');
    expect(map.has('a2')).toBe(false);
  });

  it('não marca nada em uma cadeia única (topologia inequívoca)', () => {
    const graph = new Graph(new EventBus());
    const n1 = graph.createNode('n1', 'N1', 'static', 0, 0);
    const n2 = graph.createNode('n2', 'N2', 'static', 200, 0);
    const n3 = graph.createNode('n3', 'N3', 'static', 400, 0);
    chain(graph, [n1, n2, n3]);

    const roles = computeFlowRoles(graph);
    expect(roles.starts).toEqual(['n1']);
    expect(roles.ends).toEqual(['n3']);
    expect(roles.ambiguous).toBe(false);
    expect(computeFlowRoleMap(roles).size).toBe(0);
  });

  it('não marca nada quando o fork converge em um join', () => {
    const graph = new Graph(new EventBus());
    const start = graph.createNode('start', 'Start', 'fork', 0, 0);
    const left = graph.createNode('left', 'Left', 'static', 200, -100);
    const right = graph.createNode('right', 'Right', 'static', 200, 100);
    const join = graph.createNode('join', 'Join', 'join', 400, 0);

    graph.createEdge(start.id, start.outputs[0].id, left.id, left.inputs[0].id);
    graph.createEdge(start.id, start.outputs[0].id, right.id, right.inputs[0].id);
    graph.createEdge(left.id, left.outputs[0].id, join.id, join.inputs[0].id);
    graph.createEdge(right.id, right.outputs[0].id, join.id, join.inputs[0].id);

    const roles = computeFlowRoles(graph);
    expect(roles.starts).toEqual(['start']);
    expect(roles.ends).toEqual(['join']);
    expect(roles.ambiguous).toBe(false);
  });

  it('sinaliza duas saídas quando os ramos terminam em vértices diferentes', () => {
    const graph = new Graph(new EventBus());
    const cond = graph.createNode('cond', 'Cond', 'condition', 0, 0);
    const a = graph.createNode('a', 'A', 'static', 200, -100);
    const b = graph.createNode('b', 'B', 'static', 200, 100);
    graph.createEdge(cond.id, cond.outputs[0].id, a.id, a.inputs[0].id);
    graph.createEdge(cond.id, cond.outputs[1].id, b.id, b.inputs[0].id);

    const roles = computeFlowRoles(graph);
    expect(roles.starts).toEqual(['cond']);
    expect(roles.ends).toEqual(['a', 'b']);
    expect(roles.ambiguous).toBe(true);

    const map = computeFlowRoleMap(roles);
    expect(map.get('cond')).toBe('start');
    expect(map.get('a')).toBe('end');
    expect(map.get('b')).toBe('end');
  });

  it('ignora alvo puro de chamada, que não vira tarefa de fluxo', () => {
    const graph = new Graph(new EventBus());
    const caller = graph.createNode('caller', 'Caller', 'agent', 0, 0);
    const specialist = graph.createNode('specialist', 'Specialist', 'agent', 200, 0);

    graph.addEdge({
      id: 'call_caller_specialist',
      sourceNodeId: caller.id,
      sourcePortId: caller.outputs[2].id,
      targetNodeId: specialist.id,
      targetPortId: specialist.inputs[0].id,
      kind: 'call',
      callSettings: { maxCalls: 1, onError: 'fail' }
    });

    const roles = computeFlowRoles(graph);
    expect(roles.starts).toEqual(['caller']);
    expect(roles.ends).toEqual(['caller']);
    expect(roles.ambiguous).toBe(false);
    expect(computeFlowRoleMap(roles).size).toBe(0);
  });

  it('não trata a aresta de feedback como fluxo de entrada', () => {
    const graph = new Graph(new EventBus());
    const worker = graph.createNode('worker', 'Worker', 'static', 0, 0);
    const cond = graph.createNode('cond', 'Cond', 'condition', 200, 0);
    const done = graph.createNode('done', 'Done', 'static', 400, 0);

    graph.createEdge(worker.id, worker.outputs[0].id, cond.id, cond.inputs[0].id);
    graph.createEdge(cond.id, cond.outputs[0].id, done.id, done.inputs[0].id);
    const retry = graph.createEdge(cond.id, cond.outputs[1].id, worker.id, worker.inputs[0].id);
    expect(retry.isFeedbackLoop).toBe(true);

    const roles = computeFlowRoles(graph);
    expect(roles.starts).toEqual(['worker']);
    expect(roles.ends).toEqual(['done']);
    expect(roles.ambiguous).toBe(false);
  });

  describe('agrupamento por trilha (destaque do canvas)', () => {
    it('dá uma cor por trilha disjunta e mantém a ordem das entradas', () => {
      const graph = new Graph(new EventBus());
      const a1 = graph.createNode('a1', 'A1', 'static', 0, 0);
      const a2 = graph.createNode('a2', 'A2', 'static', 200, 0);
      const b1 = graph.createNode('b1', 'B1', 'static', 0, 200);
      const b2 = graph.createNode('b2', 'B2', 'static', 200, 200);
      chain(graph, [a1, a2]);
      chain(graph, [b1, b2]);

      const { entries, indexByNode } = computeTrailAssignments(graph);
      expect(entries).toEqual(['a1', 'b1']);
      expect(indexByNode.get('a1')).toBe(0);
      expect(indexByNode.get('a2')).toBe(0);
      expect(indexByNode.get('b1')).toBe(1);
      expect(indexByNode.get('b2')).toBe(1);
    });

    it('um único componente com fork e join fica todo na trilha 0', () => {
      const graph = new Graph(new EventBus());
      const start = graph.createNode('start', 'Start', 'fork', 0, 0);
      const left = graph.createNode('left', 'Left', 'static', 200, -100);
      const right = graph.createNode('right', 'Right', 'static', 200, 100);
      const join = graph.createNode('join', 'Join', 'join', 400, 0);
      graph.createEdge(start.id, start.outputs[0].id, left.id, left.inputs[0].id);
      graph.createEdge(start.id, start.outputs[0].id, right.id, right.inputs[0].id);
      graph.createEdge(left.id, left.outputs[0].id, join.id, join.inputs[0].id);
      graph.createEdge(right.id, right.outputs[0].id, join.id, join.inputs[0].id);

      const { entries, indexByNode } = computeTrailAssignments(graph);
      expect(entries).toEqual(['start']);
      expect([...indexByNode.values()]).toEqual([0, 0, 0, 0]);
    });

    it('vértice alcançado por duas entradas fica com a primeira trilha', () => {
      const graph = new Graph(new EventBus());
      const a = graph.createNode('a', 'A', 'static', 0, -100);
      const b = graph.createNode('b', 'B', 'static', 0, 100);
      const c = graph.createNode('c', 'C', 'static', 200, 0);
      graph.createEdge(a.id, a.outputs[0].id, c.id, c.inputs[0].id);
      graph.createEdge(b.id, b.outputs[0].id, c.id, c.inputs[0].id);

      const { entries, indexByNode } = computeTrailAssignments(graph);
      expect(entries).toEqual(['a', 'b']);
      expect(indexByNode.get('c')).toBe(0);
    });

    it('não colore alvo puro de chamada', () => {
      const graph = new Graph(new EventBus());
      const caller = graph.createNode('caller', 'Caller', 'agent', 0, 0);
      const specialist = graph.createNode('specialist', 'Specialist', 'agent', 200, 0);
      graph.addEdge({
        id: 'call_specialist',
        sourceNodeId: caller.id,
        sourcePortId: caller.outputs[2].id,
        targetNodeId: specialist.id,
        targetPortId: specialist.inputs[0].id,
        kind: 'call',
        callSettings: { maxCalls: 1, onError: 'fail' }
      });

      const { indexByNode } = computeTrailAssignments(graph);
      expect(indexByNode.get('caller')).toBe(0);
      expect(indexByNode.has('specialist')).toBe(false);
    });
  });
});
