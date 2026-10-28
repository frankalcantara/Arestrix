import { describe, it, expect } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { buildJunctionPrompt } from '../src/core/TrailBrief.ts';

function outputNode(graph: Graph, id: string, label: string, destination: string) {
  const node = graph.createNode(id, label, 'output', 300, 0);
  graph.updateNode(id, { outputSettings: { ...node.outputSettings!, destinationPath: destination } });
  return graph.getNode(id)!;
}

function twoTrails(): Graph {
  const graph = new Graph(new EventBus());

  const a1 = graph.createNode('a1', 'Coletar fontes', 'static', 0, 120);
  const a2 = graph.createNode('a2', 'Normalizar texto', 'static', 150, 120);
  const a3 = outputNode(graph, 'a3', 'Gerar relatório', 'exports/relatorio.md');
  graph.createEdge(a1.id, a1.outputs[0].id, a2.id, a2.inputs[0].id);
  graph.createEdge(a2.id, a2.outputs[0].id, a3.id, a3.inputs[0].id);

  const b1 = graph.createNode('b1', 'Baixar imagens', 'static', 0, -120);
  const b2 = graph.createNode('b2', 'Gerar thumbnails', 'static', 150, -120);
  const b3 = outputNode(graph, 'b3', 'Publicar pacote', 'exports/pacote.zip');
  graph.createEdge(b1.id, b1.outputs[0].id, b2.id, b2.inputs[0].id);
  graph.createEdge(b2.id, b2.outputs[0].id, b3.id, b3.inputs[0].id);

  graph.updateNode(a1.id, { customLambdaBody: '// Baixa as fontes do repositório e valida o checksum\nstd::cout << "ok";' });

  return graph;
}

describe('TrailBrief - briefing de junção para o Assistente', () => {
  it('enumera as trilhas com rótulos, tipos e artefatos declarados', () => {
    const prompt = buildJunctionPrompt(twoTrails(), 'pt-BR');

    expect(prompt).toContain('2 trilhas independentes');
    expect(prompt).toContain('Trilha 1');
    expect(prompt).toContain('Trilha 2');
    expect(prompt).toContain('Coletar fontes [static]');
    expect(prompt).toContain('Gerar relatório [output] · → exports/relatorio.md');
    expect(prompt).toContain('Publicar pacote [output] · → exports/pacote.zip');
    expect(prompt).toContain('Avalie semanticamente');
  });

  it('usa o comentário escrito pelo autor como sinal semântico e descarta o boilerplate do template', () => {
    const prompt = buildJunctionPrompt(twoTrails(), 'pt-BR');

    expect(prompt).toContain('"Baixa as fontes do repositório e valida o checksum"');
    // O corpo padrão repete o rótulo e não agrega nada à avaliação.
    expect(prompt).not.toContain('Execução computacional');
  });

  it('não inclui alvo puro de chamada, que não participa do fluxo', () => {
    const graph = twoTrails();
    const specialist = graph.createNode('specialist', 'Especialista Oculto', 'agent', 600, 0);
    const caller = graph.getNode('a2')!;
    graph.addEdge({
      id: 'call_specialist',
      sourceNodeId: caller.id,
      sourcePortId: caller.outputs[0].id,
      targetNodeId: specialist.id,
      targetPortId: specialist.inputs[0].id,
      kind: 'call',
      callSettings: { maxCalls: 1, onError: 'fail' }
    });

    expect(buildJunctionPrompt(graph, 'pt-BR')).not.toContain('Especialista Oculto');
  });

  it('pede a junção sem remover nem reconectar conexões existentes', () => {
    const prompt = buildJunctionPrompt(twoTrails(), 'pt-BR');
    expect(prompt).toContain('crie UM vértice do tipo join');
    expect(prompt).toContain('sem remover nem reconectar');
    expect(prompt).toContain('Se as trilhas forem independentes de propósito, não altere o grafo');
  });

  it('fala inglês quando o Studio está em en-US', () => {
    const prompt = buildJunctionPrompt(twoTrails(), 'en-US');
    expect(prompt).toContain('2 independent paths');
    expect(prompt).toContain('Path 1');
    expect(prompt).toContain('Decide semantically');
  });

  it('leva o parent_path quando as trilhas estão dentro de um subfluxo', () => {
    const prompt = buildJunctionPrompt(twoTrails(), 'pt-BR', ['sub_revisao']);
    expect(prompt).toContain('neste subfluxo');
    expect(prompt).toContain('parent_path ["sub_revisao"]');
    expect(prompt).toContain('add_node e connect');
  });

  it('não menciona parent_path na raiz', () => {
    expect(buildJunctionPrompt(twoTrails(), 'pt-BR')).not.toContain('parent_path');
  });
});
