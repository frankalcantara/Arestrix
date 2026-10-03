import { describe, it, expect } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { 
  AddNodeCommand, 
  RemoveNodeCommand, 
  MoveNodeCommand, 
  AddEdgeCommand, 
  RemoveEdgeCommand, 
  UpdateNodeCommand,
  CompoundCommand 
} from '../src/core/history/GraphCommands.ts';
import { CommandManager } from '../src/core/history/CommandManager.ts';

describe('Histórico de Edição (Undo / Redo) - Padrão Command', () => {
  it('deve adicionar um nó e desfazê-lo / refazê-lo com sucesso', () => {
    const graph = new Graph();
    expect(graph.getAllNodes().length).toBe(0);
    expect(graph.canUndo()).toBe(false);

    const nodeData = graph.buildNode('node_1', 'Agente_Worker', 'static', 100, 200);
    const cmd = new AddNodeCommand(graph, nodeData);

    graph.commandManager.execute(cmd);
    expect(graph.getAllNodes().length).toBe(1);
    expect(graph.getNode('node_1')).toBeDefined();
    expect(graph.canUndo()).toBe(true);
    expect(graph.canRedo()).toBe(false);

    // Desfazer (Undo)
    const undid = graph.undo();
    expect(undid).toBe(true);
    expect(graph.getAllNodes().length).toBe(0);
    expect(graph.getNode('node_1')).toBeUndefined();
    expect(graph.canUndo()).toBe(false);
    expect(graph.canRedo()).toBe(true);

    // Refazer (Redo)
    const redid = graph.redo();
    expect(redid).toBe(true);
    expect(graph.getAllNodes().length).toBe(1);
    expect(graph.getNode('node_1')).toBeDefined();
    expect(graph.canUndo()).toBe(true);
    expect(graph.canRedo()).toBe(false);
  });

  it('deve desfazer e refazer o movimento de um nó', () => {
    const graph = new Graph();
    const node = graph.createNode('node_move', 'Agente_Movel', 'static', 50, 50);

    const moveCmd = new MoveNodeCommand(
      graph,
      node.id,
      { x: 50, y: 50 },
      { x: 250, y: 350 }
    );

    // Simula registro apos arrasto
    moveCmd.execute();
    graph.commandManager.record(moveCmd);

    expect(graph.getNode(node.id)!.position.x).toBe(250);
    expect(graph.getNode(node.id)!.position.y).toBe(350);

    // Desfazer movimento
    graph.undo();
    expect(graph.getNode(node.id)!.position.x).toBe(50);
    expect(graph.getNode(node.id)!.position.y).toBe(50);

    // Refazer movimento
    graph.redo();
    expect(graph.getNode(node.id)!.position.x).toBe(250);
    expect(graph.getNode(node.id)!.position.y).toBe(350);
  });

  it('deve adicionar, desfazer e refazer uma conexão (aresta)', () => {
    const graph = new Graph();
    const a = graph.createNode('a', 'Agente A', 'static', 0, 0);
    const b = graph.createNode('b', 'Agente B', 'static', 200, 0);

    const edgeData = {
      id: 'edge_ab',
      sourceNodeId: a.id,
      sourcePortId: a.outputs[0].id,
      targetNodeId: b.id,
      targetPortId: b.inputs[0].id,
      type: 'flow' as const
    };

    const cmd = new AddEdgeCommand(graph, edgeData);
    graph.commandManager.execute(cmd);

    expect(graph.getAllEdges().length).toBe(1);
    expect(graph.getEdge('edge_ab')).toBeDefined();

    // Desfazer
    graph.undo();
    expect(graph.getAllEdges().length).toBe(0);
    expect(graph.getEdge('edge_ab')).toBeUndefined();

    // Refazer
    graph.redo();
    expect(graph.getAllEdges().length).toBe(1);
    expect(graph.getEdge('edge_ab')).toBeDefined();
  });

  it('deve remover nó com conexões e restaurar tanto o nó quanto todas as arestas no Undo', () => {
    const graph = new Graph();
    const n1 = graph.createNode('n1', 'Origem', 'static', 0, 0);
    const n2 = graph.createNode('n2', 'Intermediario', 'static', 150, 0);
    const n3 = graph.createNode('n3', 'Destino', 'static', 300, 0);

    graph.createEdge(n1.id, n1.outputs[0].id, n2.id, n2.inputs[0].id);
    graph.createEdge(n2.id, n2.outputs[0].id, n3.id, n3.inputs[0].id);

    expect(graph.getAllNodes().length).toBe(3);
    expect(graph.getAllEdges().length).toBe(2);

    // Remove nó intermediário (que possui 2 conexões)
    const removeCmd = new RemoveNodeCommand(graph, n2.id);
    graph.commandManager.execute(removeCmd);

    expect(graph.getAllNodes().length).toBe(2);
    expect(graph.getNode('n2')).toBeUndefined();
    expect(graph.getAllEdges().length).toBe(0); // Ambas as arestas conectadas a n2 foram removidas

    // Desfazer a exclusão: deve restaurar o nó E suas conexões
    graph.undo();
    expect(graph.getAllNodes().length).toBe(3);
    expect(graph.getNode('n2')).toBeDefined();
    expect(graph.getAllEdges().length).toBe(2);

    // Refazer a exclusão
    graph.redo();
    expect(graph.getAllNodes().length).toBe(2);
    expect(graph.getNode('n2')).toBeUndefined();
    expect(graph.getAllEdges().length).toBe(0);
  });

  it('deve atualizar propriedades de um nó e restaurar no Undo/Redo', () => {
    const graph = new Graph();
    const node = graph.createNode('n_prop', 'Nome_Original', 'static', 0, 0);

    const updateCmd = new UpdateNodeCommand(
      graph,
      node.id,
      { label: 'Nome_Original', customLambdaBody: 'std::cout << "Original";' },
      { label: 'Nome_Novo', customLambdaBody: 'std::cout << "Alterado";' }
    );
    graph.commandManager.execute(updateCmd);

    expect(graph.getNode(node.id)!.label).toBe('Nome_Novo');
    expect(graph.getNode(node.id)!.customLambdaBody).toBe('std::cout << "Alterado";');

    graph.undo();
    expect(graph.getNode(node.id)!.label).toBe('Nome_Original');
    expect(graph.getNode(node.id)!.customLambdaBody).toBe('std::cout << "Original";');

    graph.redo();
    expect(graph.getNode(node.id)!.label).toBe('Nome_Novo');
    expect(graph.getNode(node.id)!.customLambdaBody).toBe('std::cout << "Alterado";');
  });

  it('deve limpar a pilha de redo quando uma nova ação for executada', () => {
    const graph = new Graph();
    const n1 = graph.buildNode('n1', 'Nó 1', 'static', 0, 0);
    const n2 = graph.buildNode('n2', 'Nó 2', 'static', 100, 0);
    const n3 = graph.buildNode('n3', 'Nó 3', 'static', 200, 0);

    graph.commandManager.execute(new AddNodeCommand(graph, n1));
    graph.commandManager.execute(new AddNodeCommand(graph, n2));

    expect(graph.canUndo()).toBe(true);
    expect(graph.canRedo()).toBe(false);

    graph.undo(); // Desfez n2
    expect(graph.canRedo()).toBe(true);

    // Nova ação n3 deve descartar a ramificação futura de redo
    graph.commandManager.execute(new AddNodeCommand(graph, n3));
    expect(graph.canRedo()).toBe(false);

    expect(graph.getAllNodes().map(n => n.id)).toEqual(['n1', 'n3']);
  });

  it('deve suportar CompoundCommand (macro-operações em lote)', () => {
    const graph = new Graph();
    const n1 = graph.buildNode('n1', 'N1', 'static', 0, 0);
    const n2 = graph.buildNode('n2', 'N2', 'static', 100, 0);

    const compound = new CompoundCommand('Adicionar lote', [
      new AddNodeCommand(graph, n1),
      new AddNodeCommand(graph, n2)
    ]);

    graph.commandManager.execute(compound);
    expect(graph.getAllNodes().length).toBe(2);

    graph.undo();
    expect(graph.getAllNodes().length).toBe(0);

    graph.redo();
    expect(graph.getAllNodes().length).toBe(2);
  });
});
