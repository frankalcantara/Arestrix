import { ICommand } from './ICommand.ts';
import { Graph } from '../Graph.ts';
import { INodeData, IEdgeData } from '../types.ts';

/**
 * Comando para adicionar um novo nó ao grafo.
 */
export class AddNodeCommand implements ICommand {
  readonly description: string;
  private graph: Graph;
  private nodeData: INodeData;

  constructor(graph: Graph, nodeData: INodeData) {
    this.graph = graph;
    this.nodeData = { ...nodeData };
    this.description = `Adicionar Nó "${nodeData.label}"`;
  }

  execute(): void {
    this.graph.addNode(this.nodeData);
  }

  undo(): void {
    this.graph.removeNode(this.nodeData.id);
  }
}

/**
 * Comando para remover um nó (e recuperar todas as conexões ao desfazer).
 */
export class RemoveNodeCommand implements ICommand {
  readonly description: string;
  private graph: Graph;
  private nodeData: INodeData;
  private connectedEdges: IEdgeData[];

  constructor(graph: Graph, nodeId: string) {
    this.graph = graph;
    const existing = graph.getNode(nodeId);
    if (!existing) {
      throw new Error(`Não é possível criar RemoveNodeCommand: nó "${nodeId}" não existe.`);
    }
    // Clona o nó e todas as arestas conectadas antes da remoção
    this.nodeData = JSON.parse(JSON.stringify(existing));
    this.connectedEdges = JSON.parse(JSON.stringify(graph.getEdgesForNode(nodeId)));
    this.description = `Excluir Nó "${this.nodeData.label}"`;
  }

  execute(): void {
    this.graph.removeNode(this.nodeData.id);
  }

  undo(): void {
    // 1. Restaura o nó
    this.graph.addNode(this.nodeData);
    // 2. Restaura todas as arestas conectadas
    this.connectedEdges.forEach(edge => {
      try {
        this.graph.addEdge(edge);
      } catch (err) {
        console.warn(`Aviso ao restaurar aresta ${edge.id}:`, err);
      }
    });
  }
}

/**
 * Comando para mover a posição de um nó no canvas 2.5D.
 */
export class MoveNodeCommand implements ICommand {
  readonly description: string;
  private graph: Graph;
  private nodeId: string;
  private oldPos: { x: number; y: number };
  private newPos: { x: number; y: number };

  constructor(
    graph: Graph,
    nodeId: string,
    oldPos: { x: number; y: number },
    newPos: { x: number; y: number }
  ) {
    this.graph = graph;
    this.nodeId = nodeId;
    this.oldPos = { ...oldPos };
    this.newPos = { ...newPos };
    const node = graph.getNode(nodeId);
    const label = node ? node.label : nodeId;
    this.description = `Mover Nó "${label}"`;
  }

  execute(): void {
    this.graph.updateNodePosition(this.nodeId, this.newPos.x, this.newPos.y);
  }

  undo(): void {
    this.graph.updateNodePosition(this.nodeId, this.oldPos.x, this.oldPos.y);
  }
}

/**
 * Comando para criar uma nova aresta/conexão entre portas.
 */
export class AddEdgeCommand implements ICommand {
  readonly description: string;
  private graph: Graph;
  private edgeData: IEdgeData;

  constructor(graph: Graph, edgeData: IEdgeData) {
    this.graph = graph;
    this.edgeData = { ...edgeData };
    this.description = `Conectar "${edgeData.sourceNodeId}" ➔ "${edgeData.targetNodeId}"`;
  }

  execute(): void {
    this.graph.addEdge(this.edgeData);
  }

  undo(): void {
    this.graph.removeEdge(this.edgeData.id);
  }
}

/**
 * Comando para remover uma aresta/conexão.
 */
export class RemoveEdgeCommand implements ICommand {
  readonly description: string;
  private graph: Graph;
  private edgeData: IEdgeData;

  constructor(graph: Graph, edgeId: string) {
    this.graph = graph;
    const existing = graph.getEdge(edgeId);
    if (!existing) {
      throw new Error(`Não é possível criar RemoveEdgeCommand: aresta "${edgeId}" não encontrada.`);
    }
    this.edgeData = { ...existing };
    this.description = `Remover Conexão "${this.edgeData.sourceNodeId}" ➔ "${this.edgeData.targetNodeId}"`;
  }

  execute(): void {
    this.graph.removeEdge(this.edgeData.id);
  }

  undo(): void {
    this.graph.addEdge(this.edgeData);
  }
}

/**
 * Comando para atualizar propriedades de um vértice (nome, tipo, corpo lambda).
 */
export class UpdateNodeCommand implements ICommand {
  readonly description: string;
  private graph: Graph;
  private nodeId: string;
  private oldProps: Partial<INodeData>;
  private newProps: Partial<INodeData>;

  constructor(
    graph: Graph,
    nodeId: string,
    oldProps: Partial<INodeData>,
    newProps: Partial<INodeData>,
    description?: string
  ) {
    this.graph = graph;
    this.nodeId = nodeId;
    this.oldProps = { ...oldProps };
    this.newProps = { ...newProps };
    const node = graph.getNode(nodeId);
    const label = node ? node.label : nodeId;
    this.description = description || `Editar Propriedades de "${label}"`;
  }

  execute(): void {
    this.graph.updateNode(this.nodeId, this.newProps);
  }

  undo(): void {
    this.graph.updateNode(this.nodeId, this.oldProps);
  }
}

/**
 * Macro-comando que agrupa múltiplos comandos executados atomicamente.
 */
export class CompoundCommand implements ICommand {
  readonly description: string;
  private commands: ICommand[];

  constructor(description: string, commands: ICommand[]) {
    this.description = description;
    this.commands = [...commands];
  }

  execute(): void {
    for (const cmd of this.commands) {
      cmd.execute();
    }
  }

  undo(): void {
    for (let i = this.commands.length - 1; i >= 0; i--) {
      this.commands[i].undo();
    }
  }
}
