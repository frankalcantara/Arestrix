import { describe, it, expect } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { GraphValidator } from '../src/core/Validator.ts';
import { EventBus } from '../src/core/EventBus.ts';

describe('Vértice Call (chamada em fluxo, Fase 4C)', () => {
  it('cria o vértice com porta de fluxo e porta call', () => {
    const graph = new Graph(new EventBus());
    const call = graph.buildNode('call', 'Chamada', 'call', 0, 0);

    expect(call.type).toBe('call');
    expect(call.inputs.map(p => p.name)).toEqual(['in']);
    expect(call.outputs.map(p => p.name)).toEqual(['out', 'call']);
    expect(call.callSettings?.source?.kind).toBe('previous');
    expect(call.callSettings?.input).toBe('');
    expect(call.customLambdaBody).toBe('');
  });

  it('aceita um Call no fluxo com exatamente um alvo chamável', () => {
    const graph = new Graph(new EventBus());
    const preparer = graph.createNode('prep', 'Preparador', 'static', 0, 0);
    const call = graph.createNode('call', 'Chamada', 'call', 200, 0);
    const specialist = graph.createNode('specialist', 'Especialista', 'agent', 200, 200);
    const after = graph.createNode('after', 'Depois', 'static', 400, 0);

    graph.createEdge(preparer.id, preparer.outputs[0].id, call.id, call.inputs[0].id);
    graph.createEdge(call.id, call.outputs[0].id, after.id, after.inputs[0].id);
    graph.addEdge({
      id: 'call_specialist',
      sourceNodeId: call.id,
      sourcePortId: call.outputs[1].id,
      targetNodeId: specialist.id,
      targetPortId: specialist.inputs[0].id,
      kind: 'call',
      callSettings: { maxCalls: 1, onError: 'fail' }
    });

    const result = GraphValidator.validate(graph);
    expect(result.errors.filter(e => e.severity === 'error')).toEqual([]);
  });

  it('rejeita um Call sem aresta de chamada', () => {
    const graph = new Graph(new EventBus());
    const before = graph.createNode('before', 'Antes', 'static', 0, 0);
    const call = graph.createNode('call', 'Chamada', 'call', 200, 0);
    graph.createEdge(before.id, before.outputs[0].id, call.id, call.inputs[0].id);

    const result = GraphValidator.validate(graph);
    expect(result.errors.some(e => e.type === 'invalid_call' && e.severity === 'error')).toBe(true);
  });

  it('rejeita um Call com mais de uma aresta de chamada', () => {
    const graph = new Graph(new EventBus());
    const call = graph.createNode('call', 'Chamada', 'call', 200, 0);
    const a = graph.createNode('a', 'Alvo A', 'agent', 0, 200);
    const b = graph.createNode('b', 'Alvo B', 'agent', 400, 200);
    for (const [id, target] of [['ca', a], ['cb', b]] as const) {
      graph.addEdge({
        id,
        sourceNodeId: call.id,
        sourcePortId: call.outputs[1].id,
        targetNodeId: target.id,
        targetPortId: target.inputs[0].id,
        kind: 'call',
        callSettings: { maxCalls: 1, onError: 'fail' }
      });
    }

    const result = GraphValidator.validate(graph);
    expect(result.errors.some(e => e.type === 'invalid_call' && e.severity === 'error')).toBe(true);
  });

  it('rejeita um Call cujo alvo também participa do fluxo', () => {
    const graph = new Graph(new EventBus());
    const call = graph.createNode('call', 'Chamada', 'call', 200, 0);
    const target = graph.createNode('target', 'Alvo', 'agent', 0, 200);
    const after = graph.createNode('after', 'Depois', 'static', 400, 200);
    graph.createEdge(target.id, target.outputs[0].id, after.id, after.inputs[0].id);
    graph.addEdge({
      id: 'call_target',
      sourceNodeId: call.id,
      sourcePortId: call.outputs[1].id,
      targetNodeId: target.id,
      targetPortId: target.inputs[0].id,
      kind: 'call',
      callSettings: { maxCalls: 1, onError: 'fail' }
    });

    const result = GraphValidator.validate(graph);
    expect(result.errors.some(e => e.type === 'invalid_call' && e.severity === 'error')).toBe(true);
  });
});
