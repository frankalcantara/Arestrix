import { describe, it, expect } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { GraphValidator } from '../src/core/Validator.ts';
import { EventBus } from '../src/core/EventBus.ts';

describe('Validação Topológica e Regras de Grafo Taskflow', () => {
  it('deve validar um grafo acíclico simples com sucesso', () => {
    const bus = new EventBus();
    const graph = new Graph(bus);

    const a = graph.createNode('node_a', 'Agente_A', 'static', 0, 0);
    const b = graph.createNode('node_b', 'Agente_B', 'static', 200, 0);

    graph.createEdge(a.id, a.outputs[0].id, b.id, b.inputs[0].id);

    const result = GraphValidator.validate(graph);
    expect(result.isValid).toBe(true);
    expect(result.errors.filter(e => e.severity === 'error')).toHaveLength(0);
  });

  it('deve rejeitar e apontar erro em ciclos estáticos proibidos (sem condição)', () => {
    const bus = new EventBus();
    const graph = new Graph(bus);

    const a = graph.createNode('node_a', 'Agente_A', 'static', 0, 0);
    const b = graph.createNode('node_b', 'Agente_B', 'static', 200, 0);

    // A -> B
    graph.createEdge(a.id, a.outputs[0].id, b.id, b.inputs[0].id);
    // B -> A (Ciclo estático proibido!)
    graph.createEdge(b.id, b.outputs[0].id, a.id, a.inputs[0].id);

    const result = GraphValidator.validate(graph);
    expect(result.isValid).toBe(false);
    const cycleError = result.errors.find(e => e.type === 'cycle');
    expect(cycleError).toBeDefined();
    expect(cycleError?.severity).toBe('error');
  });

  it('deve permitir loops/ciclos estocásticos quando controlados por nó de condição (Taskflow)', () => {
    const bus = new EventBus();
    const graph = new Graph(bus);

    const worker = graph.createNode('worker', 'Worker_Agent', 'static', 0, 0);
    const evaluator = graph.createNode('evaluator', 'Evaluator', 'condition', 250, 0);
    const finish = graph.createNode('finish', 'Finish_Agent', 'static', 500, 0);

    // worker -> evaluator
    graph.createEdge(worker.id, worker.outputs[0].id, evaluator.id, evaluator.inputs[0].id);
    // evaluator branch 0 -> finish
    graph.createEdge(evaluator.id, evaluator.outputs[0].id, finish.id, finish.inputs[0].id);
    // evaluator branch 1 -> loopback para worker (tentar novamente)
    graph.createEdge(evaluator.id, evaluator.outputs[1].id, worker.id, worker.inputs[0].id);

    const result = GraphValidator.validate(graph);
    // Como o ciclo é mediado por um nó do tipo 'condition', não deve ser considerado ciclo estático fatal!
    const fatalCycles = result.errors.filter(e => e.type === 'cycle' && e.severity === 'error');
    expect(fatalCycles).toHaveLength(0);
    expect(result.isValid).toBe(true);
  });

  it('deve alertar sobre ramos de condição desconectados', () => {
    const bus = new EventBus();
    const graph = new Graph(bus);

    const evaluator = graph.createNode('evaluator', 'Evaluator', 'condition', 0, 0);
    const target = graph.createNode('target', 'Target', 'static', 200, 0);

    // Conecta apenas branch 0, deixando branch 1 solto
    graph.createEdge(evaluator.id, evaluator.outputs[0].id, target.id, target.inputs[0].id);

    const result = GraphValidator.validate(graph);
    const missingBranch = result.errors.find(e => e.type === 'missing_branch');
    expect(missingBranch).toBeDefined();
    expect(missingBranch?.severity).toBe('warning');
  });

  describe('grafo de chamadas (kind = call)', () => {
    const addCall = (graph: Graph, from: ReturnType<Graph['createNode']>, to: ReturnType<Graph['createNode']>, maxCalls = 1) => {
      graph.addEdge({
        id: `call_${from.id}_${to.id}`,
        sourceNodeId: from.id,
        sourcePortId: from.outputs[0].id,
        targetNodeId: to.id,
        targetPortId: to.inputs[0].id,
        kind: 'call',
        callSettings: { maxCalls, onError: 'fail' }
      });
    };

    it('aceita um agente chamável sem arestas de fluxo', () => {
      const graph = new Graph(new EventBus());
      const caller = graph.createNode('caller', 'Caller', 'agent', 0, 0);
      const specialist = graph.createNode('specialist', 'Specialist', 'agent', 200, 0);

      addCall(graph, caller, specialist);

      const result = GraphValidator.validate(graph);
      expect(result.isValid).toBe(true);
      expect(result.errors.filter(e => e.severity === 'error')).toHaveLength(0);
    });

    it('rejeita chamada saindo de um vértice que não é agente', () => {
      const graph = new Graph(new EventBus());
      const source = graph.createNode('source', 'Source', 'static', 0, 0);
      const target = graph.createNode('target', 'Target', 'agent', 200, 0);

      addCall(graph, source, target);

      const result = GraphValidator.validate(graph);
      expect(result.errors.some(e => e.type === 'invalid_call' && e.severity === 'error')).toBe(true);
    });

    it('rejeita chamada para um vértice que não é agente no v1', () => {
      const graph = new Graph(new EventBus());
      const caller = graph.createNode('caller', 'Caller', 'agent', 0, 0);
      const target = graph.createNode('target', 'Target', 'static', 200, 0);

      addCall(graph, caller, target);

      const result = GraphValidator.validate(graph);
      expect(result.errors.some(e => e.type === 'invalid_call' && e.severity === 'error')).toBe(true);
    });

    it('rejeita um chamável que também participa do fluxo', () => {
      const graph = new Graph(new EventBus());
      const caller = graph.createNode('caller', 'Caller', 'agent', 0, 0);
      const target = graph.createNode('target', 'Target', 'agent', 200, 0);

      graph.createEdge(caller.id, caller.outputs[0].id, target.id, target.inputs[0].id);
      addCall(graph, caller, target);

      const result = GraphValidator.validate(graph);
      expect(result.errors.some(e => e.type === 'invalid_call' && e.severity === 'error')).toBe(true);
    });

    it('rejeita maxCalls menor que 1', () => {
      const graph = new Graph(new EventBus());
      const caller = graph.createNode('caller', 'Caller', 'agent', 0, 0);
      const target = graph.createNode('target', 'Target', 'agent', 200, 0);

      addCall(graph, caller, target, 0);

      const result = GraphValidator.validate(graph);
      expect(result.errors.some(e => e.type === 'invalid_call' && e.severity === 'error')).toBe(true);
    });

    it('rejeita ciclo de chamadas no v1', () => {
      const graph = new Graph(new EventBus());
      const a = graph.createNode('a', 'A', 'agent', 0, 0);
      const b = graph.createNode('b', 'B', 'agent', 200, 0);

      addCall(graph, a, b);
      addCall(graph, b, a);

      const result = GraphValidator.validate(graph);
      expect(result.errors.some(e => e.type === 'call_cycle' && e.severity === 'error')).toBe(true);
    });
  });
});
