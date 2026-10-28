import { describe, it, expect } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { buildJunctionEdit, evaluateJunction } from '../src/core/JunctionPlanner.ts';

function outputNode(
  graph: Graph,
  id: string,
  label: string,
  destination: string,
  artifactType: 'auto' | 'markdown' | 'svg' | 'json' | 'zip' = 'auto'
) {
  const node = graph.createNode(id, label, 'output', 300, 0);
  graph.updateNode(id, { outputSettings: { ...node.outputSettings!, destinationPath: destination, artifactType } });
  return graph.getNode(id)!;
}

function chain(graph: Graph, nodes: ReturnType<Graph['createNode']>[]): void {
  for (let i = 0; i + 1 < nodes.length; i++) {
    graph.createEdge(nodes[i].id, nodes[i].outputs[0].id, nodes[i + 1].id, nodes[i + 1].inputs[0].id);
  }
}

describe('JunctionPlanner - sugestão local de junção (sem IA)', () => {
  it('sugere convergência quando as trilhas entregam na mesma pasta', () => {
    const graph = new Graph(new EventBus());
    const a1 = graph.createNode('a1', 'Montar relatório', 'static', 0, 100);
    const a2 = outputNode(graph, 'a2', 'Salvar relatório', 'exports/relatorio.md');
    const b1 = graph.createNode('b1', 'Montar anexo', 'static', 0, -100);
    const b2 = outputNode(graph, 'b2', 'Salvar anexo', 'exports/anexo.md');
    chain(graph, [a1, a2]);
    chain(graph, [b1, b2]);

    const evaluation = evaluateJunction(graph, 'pt-BR');
    expect(evaluation.shouldJoin).toBe(true);
    expect(evaluation.score).toBeGreaterThanOrEqual(2);
    expect(evaluation.rationale).toContain('mesma pasta');
    expect(evaluation.endIds.sort()).toEqual(['a2', 'b2']);
  });

  it('não sugere quando as trilhas entregam artefatos distintos em pastas distintas', () => {
    const graph = new Graph(new EventBus());
    const a = graph.createNode('a', 'Gerar vídeo', 'static', 0, 100);
    const aOut = outputNode(graph, 'a_out', 'Vídeo', 'midia/video.mp4');
    const b = graph.createNode('b', 'Gerar planilha', 'static', 0, -100);
    const bOut = outputNode(graph, 'b_out', 'Planilha', 'dados/tabela.xlsx');
    chain(graph, [a, aOut]);
    chain(graph, [b, bOut]);

    const evaluation = evaluateJunction(graph, 'pt-BR');
    expect(evaluation.shouldJoin).toBe(false);
    expect(evaluation.rationale).toContain('independentes');
  });

  it('reconhece habilidade compartilhada mesmo com entregas em pastas diferentes', () => {
    const graph = new Graph(new EventBus());
    const a = graph.createNode('a', 'Revisar rascunho', 'agent', 0, 100);
    const aOut = outputNode(graph, 'a_out', 'Rascunho revisado', 'textos/a.md');
    const b = graph.createNode('b', 'Revisar anexo', 'agent', 0, -100);
    const bOut = outputNode(graph, 'b_out', 'Anexo revisado', 'anexos/b.md');
    graph.updateNode('a', {
      agentSettings: { ...a.agentSettings!, skills: [{ id: 's1', name: 'Revisor' } as any], activeSkillId: 's1' }
    });
    graph.updateNode('b', {
      agentSettings: { ...b.agentSettings!, skills: [{ id: 's2', name: 'Revisor' } as any], activeSkillId: 's2' }
    });
    chain(graph, [a, aOut]);
    chain(graph, [b, bOut]);

    const evaluation = evaluateJunction(graph, 'pt-BR');
    expect(evaluation.shouldJoin).toBe(true);
    expect(evaluation.rationale).toContain('Revisor');
  });

  it('não sugere nada com uma única trilha', () => {
    const graph = new Graph(new EventBus());
    const a = graph.createNode('a', 'Só uma', 'static', 0, 0);
    const out = outputNode(graph, 'out', 'Entrega', 'exports/a.md');
    chain(graph, [a, out]);

    const evaluation = evaluateJunction(graph, 'pt-BR');
    expect(evaluation.shouldJoin).toBe(false);
    expect(evaluation.endIds).toEqual([]);
  });

  it('monta a junção com uma aresta por fim de trilha, sem tocar no grafo', () => {
    const graph = new Graph(new EventBus());
    const a = graph.createNode('a', 'A', 'static', 0, 100);
    const aOut = outputNode(graph, 'a_out', 'A out', 'exports/a.md');
    const b = graph.createNode('b', 'B', 'static', 0, -100);
    const bOut = outputNode(graph, 'b_out', 'B out', 'exports/b.md');
    chain(graph, [a, aOut]);
    chain(graph, [b, bOut]);

    const before = graph.getAllNodes().length;
    const edit = buildJunctionEdit(graph, ['a_out', 'b_out'], 'pt-BR');
    expect(graph.getAllNodes().length).toBe(before);   // nada aplicado ainda

    expect(edit).not.toBeNull();
    expect(edit!.node.type).toBe('join');
    expect(edit!.node.label).toBe('Junção das trilhas');
    expect(edit!.edges).toHaveLength(2);
    expect(edit!.edges.map(edge => edge.sourceNodeId).sort()).toEqual(['a_out', 'b_out']);
    expect(edit!.edges.every(edge => edge.targetNodeId === edit!.node.id)).toBe(true);
    expect(edit!.edges.every(edge => edge.kind === 'flow')).toBe(true);
    expect(edit!.node.position.x).toBeGreaterThan(aOut.position.x);
  });

  it('não sugere junção com menos de dois fins', () => {
    const graph = new Graph(new EventBus());
    const a = graph.createNode('a', 'A', 'static', 0, 0);
    expect(buildJunctionEdit(graph, ['a'], 'pt-BR')).toBeNull();
  });
});
