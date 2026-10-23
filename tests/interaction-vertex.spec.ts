import { describe, it, expect } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { GraphValidator } from '../src/core/Validator.ts';
import { EventBus } from '../src/core/EventBus.ts';

describe('Vértice de interação humana (autoria 2A)', () => {
  it('cria o vértice com portas de fluxo e formulário padrão', () => {
    const graph = new Graph(new EventBus());
    const ask = graph.buildNode('ask', 'Pergunta', 'interaction', 0, 0);

    expect(ask.type).toBe('interaction');
    expect(ask.inputs.map(p => p.name)).toEqual(['in']);
    expect(ask.outputs.map(p => p.name)).toEqual(['out']);
    expect(ask.interactionSettings).toBeDefined();
    expect(ask.interactionSettings?.form.fields.length).toBeGreaterThan(0);
    expect(ask.interactionSettings?.channel).toBe('any');
    expect(ask.customLambdaBody).toBe('');
  });

  it('aceita uma interação no fluxo com formulário válido', () => {
    const graph = new Graph(new EventBus());
    const before = graph.createNode('before', 'Before', 'static', 0, 0);
    const ask = graph.createNode('ask', 'Pergunta', 'interaction', 200, 0);
    const after = graph.createNode('after', 'After', 'static', 400, 0);
    graph.createEdge(before.id, before.outputs[0].id, ask.id, ask.inputs[0].id);
    graph.createEdge(ask.id, ask.outputs[0].id, after.id, after.inputs[0].id);

    const result = GraphValidator.validate(graph);
    expect(result.errors.filter(e => e.severity === 'error')).toEqual([]);
  });

  it('rejeita formulário sem campos', () => {
    const graph = new Graph(new EventBus());
    const ask = graph.createNode('ask', 'Pergunta', 'interaction', 0, 0);
    if (ask.interactionSettings) ask.interactionSettings.form.fields = [];

    const result = GraphValidator.validate(graph);
    expect(result.errors.some(e => e.type === 'invalid_interaction' && e.severity === 'error')).toBe(true);
  });

  it('rejeita campo choice com menos de duas opções', () => {
    const graph = new Graph(new EventBus());
    const ask = graph.createNode('ask', 'Pergunta', 'interaction', 0, 0);
    if (ask.interactionSettings) {
      ask.interactionSettings.form.fields = [{
        id: 'tipo',
        type: 'choice',
        label: 'Tipo',
        options: [{ value: 'a', label: 'A' }]
      }];
    }

    const result = GraphValidator.validate(graph);
    expect(result.errors.some(e => e.type === 'invalid_interaction' && e.severity === 'error')).toBe(true);
  });

  it('aceita interação como alvo de chamada declarada', () => {
    const graph = new Graph(new EventBus());
    const caller = graph.createNode('caller', 'Caller', 'agent', 0, 0);
    const ask = graph.createNode('ask', 'Pergunta', 'interaction', 200, 0);
    graph.addEdge({
      id: 'call_ask',
      sourceNodeId: caller.id,
      sourcePortId: caller.outputs[0].id,
      targetNodeId: ask.id,
      targetPortId: ask.inputs[0].id,
      kind: 'call',
      callSettings: { maxCalls: 1, onError: 'fail' }
    });

    const result = GraphValidator.validate(graph);
    expect(result.errors.filter(e => e.severity === 'error')).toEqual([]);
  });

  it('rejeita interação chamável que também participa do fluxo', () => {
    const graph = new Graph(new EventBus());
    const caller = graph.createNode('caller', 'Caller', 'agent', 0, 0);
    const ask = graph.createNode('ask', 'Pergunta', 'interaction', 200, 0);
    const after = graph.createNode('after', 'After', 'static', 400, 0);
    graph.createEdge(ask.id, ask.outputs[0].id, after.id, after.inputs[0].id);
    graph.addEdge({
      id: 'call_ask',
      sourceNodeId: caller.id,
      sourcePortId: caller.outputs[0].id,
      targetNodeId: ask.id,
      targetPortId: ask.inputs[0].id,
      kind: 'call',
      callSettings: { maxCalls: 1, onError: 'fail' }
    });

    const result = GraphValidator.validate(graph);
    expect(result.errors.some(e => e.type === 'invalid_call' && e.severity === 'error')).toBe(true);
  });
});
