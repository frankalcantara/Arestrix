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

  describe('conflito de escrita entre trilhas concorrentes (aviso, não bloqueia)', () => {
    const makeOutput = (graph: Graph, id: string, destinationPath: string) => {
      const node = graph.createNode(id, id, 'output', 0, 0);
      graph.updateNode(id, {
        outputSettings: { ...node.outputSettings!, destinationPath }
      });
      return graph.getNode(id)!;
    };

    it('avisa quando dois Outputs disjuntos gravam no mesmo arquivo', () => {
      const graph = new Graph(new EventBus());
      const a = graph.createNode('a', 'A', 'static', 0, 0);
      const b = graph.createNode('b', 'B', 'static', 0, 200);
      const outA = makeOutput(graph, 'out_a', 'exports/relatorio.svg');
      const outB = makeOutput(graph, 'out_b', 'exports/relatorio.svg');
      graph.createEdge(a.id, a.outputs[0].id, outA.id, outA.inputs[0].id);
      graph.createEdge(b.id, b.outputs[0].id, outB.id, outB.inputs[0].id);

      const result = GraphValidator.validate(graph);
      const conflict = result.errors.find(e => e.type === 'resource_conflict');
      expect(conflict).toBeDefined();
      expect(conflict?.severity).toBe('warning');
      expect(conflict?.nodeIds?.sort()).toEqual(['out_a', 'out_b']);
      expect(conflict?.message).toContain('exports/relatorio.svg');
      // Nunca bloqueia: o grafo continua válido e a execução segue.
      expect(result.isValid).toBe(true);
    });

    it('não avisa quando há caminho de fluxo entre os dois (um executa depois do outro)', () => {
      const graph = new Graph(new EventBus());
      const first = makeOutput(graph, 'out_first', 'exports/dados.json');
      const second = makeOutput(graph, 'out_second', 'exports/dados.json');
      graph.createEdge(first.id, first.outputs[0].id, second.id, second.inputs[0].id);

      const result = GraphValidator.validate(graph);
      expect(result.errors.some(e => e.type === 'resource_conflict')).toBe(false);
    });

    it('não avisa quando os caminhos são diferentes', () => {
      const graph = new Graph(new EventBus());
      const a = makeOutput(graph, 'out_a', 'exports/a.svg');
      const b = makeOutput(graph, 'out_b', 'exports/b.svg');

      const result = GraphValidator.validate(graph);
      expect(result.errors.some(e => e.type === 'resource_conflict')).toBe(false);
    });

    it('avisa entre ramos irmãos de uma condição', () => {
      const graph = new Graph(new EventBus());
      const cond = graph.createNode('cond', 'Cond', 'condition', 0, 0);
      const a = makeOutput(graph, 'out_a', 'exports/saida.svg');
      const b = makeOutput(graph, 'out_b', 'exports/saida.svg');
      graph.createEdge(cond.id, cond.outputs[0].id, a.id, a.inputs[0].id);
      graph.createEdge(cond.id, cond.outputs[1].id, b.id, b.inputs[0].id);

      const result = GraphValidator.validate(graph);
      expect(result.errors.some(e => e.type === 'resource_conflict' && e.severity === 'warning')).toBe(true);
    });

    it('compara o arquivo do Output com o arquivo de saída de um vértice MCP', () => {
      const graph = new Graph(new EventBus());
      const out = makeOutput(graph, 'out_a', 'exports/entrega.md');
      const mcp = graph.createNode('mcp_a', 'MCP A', 'mcp', 0, 200);
      graph.updateNode(mcp.id, {
        mcpSettings: { ...mcp.mcpSettings!, output: { file: 'exports/entrega.md' } }
      });

      const result = GraphValidator.validate(graph);
      const conflict = result.errors.find(e => e.type === 'resource_conflict');
      expect(conflict).toBeDefined();
      expect(conflict?.nodeIds?.sort()).toEqual(['mcp_a', 'out_a']);
    });

    it('normaliza separadores e "./" antes de comparar', () => {
      const graph = new Graph(new EventBus());
      makeOutput(graph, 'out_a', 'exports\\relatorio.md');
      makeOutput(graph, 'out_b', './exports/relatorio.md');

      const result = GraphValidator.validate(graph);
      expect(result.errors.some(e => e.type === 'resource_conflict')).toBe(true);
    });

    it('não duplica o aviso para o mesmo par', () => {
      const graph = new Graph(new EventBus());
      makeOutput(graph, 'out_a', 'exports/x.md');
      makeOutput(graph, 'out_b', 'exports/x.md');

      const result = GraphValidator.validate(graph);
      expect(result.errors.filter(e => e.type === 'resource_conflict')).toHaveLength(1);
    });
  });
});

