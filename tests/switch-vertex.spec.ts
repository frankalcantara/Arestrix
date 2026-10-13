import { describe, it, expect } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { GraphValidator } from '../src/core/Validator.ts';
import { GraphvizGenerator } from '../src/core/GraphvizGenerator.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { UpdateNodeCommand, RemoveEdgeCommand, CompoundCommand } from '../src/core/history/GraphCommands.ts';

function newGraph() {
  return new Graph(new EventBus());
}

describe('Vértice switch (seleção multivia)', () => {
  it('nasce com dois casos e o default sempre por último, com índices 0..N', () => {
    const g = newGraph();
    const sw = g.createNode('sw', 'Roteador', 'switch', 0, 0);
    expect(sw.outputs.map(p => p.name)).toEqual(['case 1', 'case 2', 'default']);
    expect(sw.outputs.map(p => p.conditionBranchIndex)).toEqual([0, 1, 2]);
    expect(sw.customLambdaBody).toContain('índice do caso');
  });

  it('switchOutputs preserva os ids das portas existentes e mantém o default no fim', () => {
    const existing = Graph.switchOutputs('sw', ['a', 'b']);
    const next = Graph.switchOutputs('sw', ['a', 'b', 'c'], existing);
    expect(next.slice(0, 2).map(p => p.id)).toEqual(existing.slice(0, 2).map(p => p.id));
    expect(next[next.length - 1].name).toBe('default');
    expect(next[next.length - 1].conditionBranchIndex).toBe(3);
  });

  it('não avisa se o default estiver desconectado, mas avisa de caso solto', () => {
    const g = newGraph();
    const sw = g.createNode('sw', 'Roteador', 'switch', 0, 0);
    const a = g.createNode('a', 'A', 'static', 200, 0);
    g.createEdge(sw.id, sw.outputs[0].id, a.id, a.inputs[0].id);
    const warnings = GraphValidator.validate(g).errors.filter(e => e.type === 'missing_branch');
    expect(warnings).toHaveLength(1);                       // só "case 2"; o default pode ficar solto
    expect(warnings[0].message).toContain('case 2');
  });

  it('permite ciclo que passa pelo switch e marca a aresta de retorno como feedback', () => {
    const g = newGraph();
    const work = g.createNode('work', 'Trabalho', 'static', 0, 0);
    const sw = g.createNode('sw', 'Roteador', 'switch', 200, 0);
    g.createEdge(work.id, work.outputs[0].id, sw.id, sw.inputs[0].id);
    g.createEdge(sw.id, sw.outputs[0].id, work.id, work.inputs[0].id);   // volta para o trabalho

    const back = g.getAllEdges().find(e => e.sourceNodeId === 'sw')!;
    expect(back.isFeedbackLoop).toBe(true);
    expect(GraphValidator.validate(g).isValid).toBe(true);
  });

  it('adicionar e remover caso via comandos é desfazível e leva as arestas junto', () => {
    const g = newGraph();
    const sw = g.createNode('sw', 'Roteador', 'switch', 0, 0);
    const a = g.createNode('a', 'A', 'static', 200, 0);
    const edge = g.createEdge(sw.id, sw.outputs[0].id, a.id, a.inputs[0].id);

    const old = JSON.parse(JSON.stringify(sw.outputs));
    const reduced = Graph.switchOutputs('sw', ['case 2'], [sw.outputs[1]]);   // remove o case 1
    g.commandManager.execute(new CompoundCommand('Remover caso', [
      new RemoveEdgeCommand(g, edge.id),
      new UpdateNodeCommand(g, 'sw', { outputs: old }, { outputs: reduced }, 'Remover caso')
    ]));
    expect(g.getNode('sw')!.outputs.map(p => p.name)).toEqual(['case 2', 'default']);
    expect(g.getAllEdges()).toHaveLength(0);

    g.undo();
    expect(g.getNode('sw')!.outputs.map(p => p.name)).toEqual(['case 1', 'case 2', 'default']);
    expect(g.getAllEdges()).toHaveLength(1);
  });

  it('o DOT rotula cada aresta com o nome do caso e o tipo faz roundtrip no JSON', () => {
    const g = newGraph();
    const sw = g.createNode('sw', 'Roteador', 'switch', 0, 0);
    const a = g.createNode('a', 'A', 'static', 200, 0);
    g.createEdge(sw.id, sw.outputs[1].id, a.id, a.inputs[0].id);
    expect(GraphvizGenerator.generateDot(g)).toContain('[case 2]');

    const copy = newGraph();
    copy.fromJSON(JSON.parse(JSON.stringify(g.toJSON())));
    expect(copy.getNode('sw')!.type).toBe('switch');
    expect(copy.getNode('sw')!.outputs).toHaveLength(3);
  });
});

describe('Switch sem código: configurações', () => {
  it('um switch novo nasce no modo "modelo de IA" herdando o modelo padrão do pipeline', () => {
    const g = new Graph(new EventBus());
    const sw = g.createNode('sw', 'Triagem', 'switch', 0, 0);
    expect(sw.switchSettings).toEqual({ mode: 'model', source: { kind: 'previous' }, rules: {}, instruction: '' });
    expect(sw.agentSettings?.inheritModel).toBe(true);
    expect(g.createNode('x', 'X', 'static', 0, 0).switchSettings).toBeUndefined();
  });

  it('as configurações sobrevivem ao JSON e à edição por comando desfazível', () => {
    const g = new Graph(new EventBus());
    const sw = g.createNode('sw', 'Triagem', 'switch', 0, 0);
    const before = JSON.parse(JSON.stringify(sw.switchSettings));
    const after = { mode: 'rules', source: { kind: 'file', path: 'relatorio.txt' }, rules: { [sw.outputs[0].id]: 'urgente, grave' } };
    g.commandManager.execute(new UpdateNodeCommand(g, 'sw', { switchSettings: before }, { switchSettings: after as any }, 'modo'));
    expect(g.getNode('sw')!.switchSettings!.mode).toBe('rules');

    const copy = new Graph(new EventBus());
    copy.fromJSON(JSON.parse(JSON.stringify(g.toJSON())));
    expect(copy.getNode('sw')!.switchSettings!.rules[sw.outputs[0].id]).toBe('urgente, grave');

    g.undo();
    expect(g.getNode('sw')!.switchSettings!.mode).toBe('model');
  });
});
