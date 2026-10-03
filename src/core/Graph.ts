import { 
  INodeData, 
  IEdgeData, 
  IGraphSchema, 
  IGraphMetadata, 
  DEFAULT_COMPILER_CONFIG,
  TaskflowNodeType,
  IPort
} from './types.ts';
import { globalEventBus, EventBus } from './EventBus.ts';

export class Graph {
  private nodes: Map<string, INodeData> = new Map();
  private edges: Map<string, IEdgeData> = new Map();
  private metadata: IGraphMetadata;
  private bus: EventBus;

  constructor(bus: EventBus = globalEventBus, initialMetadata?: Partial<IGraphMetadata>) {
    this.bus = bus;
    this.metadata = {
      graphName: 'AgentTaskflowGraph',
      description: 'Orquestração de Agentes Concorrentes com Taskflow C++20',
      targetStandard: 'C++20',
      compilerConfig: { ...DEFAULT_COMPILER_CONFIG },
      version: '1.0.0',
      ...initialMetadata
    };
  }

  // --- Operações com Nós ---

  addNode(node: INodeData): void {
    if (this.nodes.has(node.id)) {
      throw new Error(`Node with id "${node.id}" already exists.`);
    }
    this.nodes.set(node.id, { ...node });
    this.bus.emit('node:added', node);
    this.bus.emit('graph:changed');
  }

  createNode(
    id: string, 
    label: string, 
    type: TaskflowNodeType, 
    x: number, 
    y: number
  ): INodeData {
    const inputs: IPort[] = [
      { id: `${id}_in_0`, name: 'in', direction: 'input', type: 'flow' }
    ];

    let outputs: IPort[] = [];
    if (type === 'condition') {
      outputs = [
        { id: `${id}_out_0`, name: 'branch 0', direction: 'output', type: 'flow', conditionBranchIndex: 0 },
        { id: `${id}_out_1`, name: 'branch 1', direction: 'output', type: 'flow', conditionBranchIndex: 1 }
      ];
    } else {
      outputs = [
        { id: `${id}_out_0`, name: 'out', direction: 'output', type: 'flow' }
      ];
    }

    const node: INodeData = {
      id,
      label,
      type,
      position: { x, y, z: 0 },
      inputs,
      outputs,
      parameters: {},
      customLambdaBody: this.getDefaultLambdaBody(type, label)
    };

    this.addNode(node);
    return node;
  }

  private getDefaultLambdaBody(type: TaskflowNodeType, label: string): string {
    switch (type) {
      case 'condition':
        return `// Decisão de branch do agente ${label}\n// Retorna 0 para ramo 0, 1 para ramo 1\nreturn (rand() % 2);`;
      case 'subflow':
        return `// Subfluxo gerado dinamicamente para ${label}\nauto sub_a = sf.emplace([]() { std::cout << "Subtask A\\n"; }).name("SubA");\nauto sub_b = sf.emplace([]() { std::cout << "Subtask B\\n"; }).name("SubB");\nsub_a.precede(sub_b);`;
      case 'module':
        return `// Módulo reutilizável composto de outro subgrafo`;
      case 'critical':
        return `// Seção crítica com acesso exclusivo a recursos compartilhados\nstd::cout << "Acesso exclusivo seguro pelo agente ${label}\\n";`;
      case 'static':
      default:
        return `// Execução computacional determinística do agente: ${label}\nstd::cout << "Executando agente: ${label}\\n";`;
    }
  }

  updateNode(id: string, partial: Partial<INodeData>): void {
    const existing = this.nodes.get(id);
    if (!existing) return;

    const updated = { ...existing, ...partial };
    this.nodes.set(id, updated);
    this.bus.emit('node:updated', updated);
    this.bus.emit('graph:changed');
  }

  updateNodePosition(id: string, x: number, y: number): void {
    const node = this.nodes.get(id);
    if (node) {
      node.position.x = x;
      node.position.y = y;
      this.bus.emit('node:moved', { id, position: node.position });
    }
  }

  removeNode(id: string): void {
    if (!this.nodes.has(id)) return;

    // Remover arestas conectadas a este nó
    const connectedEdges = this.getEdgesForNode(id);
    connectedEdges.forEach(edge => this.removeEdge(edge.id));

    this.nodes.delete(id);
    this.bus.emit('node:removed', { id });
    this.bus.emit('graph:changed');
  }

  getNode(id: string): INodeData | undefined {
    return this.nodes.get(id);
  }

  getAllNodes(): INodeData[] {
    return Array.from(this.nodes.values());
  }

  // --- Operações com Arestas ---

  addEdge(edge: IEdgeData): void {
    if (this.edges.has(edge.id)) {
      throw new Error(`Edge with id "${edge.id}" already exists.`);
    }

    const source = this.nodes.get(edge.sourceNodeId);
    const target = this.nodes.get(edge.targetNodeId);

    if (!source || !target) {
      throw new Error(`Cannot add edge: source or target node not found.`);
    }

    // Identificar se a aresta é um loop condicional
    if (source.type === 'condition') {
      const sourcePort = source.outputs.find(p => p.id === edge.sourcePortId);
      if (sourcePort && sourcePort.conditionBranchIndex !== undefined) {
        // Se a aresta retorna para o próprio nó ou nó antecedente, marca como loopback
        edge.isFeedbackLoop = this.isFeedbackEdge(edge.sourceNodeId, edge.targetNodeId);
      }
    }

    this.edges.set(edge.id, { ...edge });
    this.bus.emit('edge:added', edge);
    this.bus.emit('graph:changed');
  }

  createEdge(
    sourceNodeId: string, 
    sourcePortId: string, 
    targetNodeId: string, 
    targetPortId: string
  ): IEdgeData {
    const id = `edge_${sourceNodeId}_${sourcePortId}_to_${targetNodeId}_${targetPortId}`;
    
    // Evita duplicatas exatas
    if (this.edges.has(id)) {
      return this.edges.get(id)!;
    }

    const edge: IEdgeData = {
      id,
      sourceNodeId,
      sourcePortId,
      targetNodeId,
      targetPortId
    };

    this.addEdge(edge);
    return edge;
  }

  removeEdge(id: string): void {
    const edge = this.edges.get(id);
    if (!edge) return;

    this.edges.delete(id);
    this.bus.emit('edge:removed', edge);
    this.bus.emit('graph:changed');
  }

  getEdge(id: string): IEdgeData | undefined {
    return this.edges.get(id);
  }

  getAllEdges(): IEdgeData[] {
    return Array.from(this.edges.values());
  }

  getEdgesForNode(nodeId: string): IEdgeData[] {
    return this.getAllEdges().filter(
      e => e.sourceNodeId === nodeId || e.targetNodeId === nodeId
    );
  }

  // --- Checagem de Feedback Loop ---
  private isFeedbackEdge(sourceId: string, targetId: string): boolean {
    if (sourceId === targetId) return true;
    
    // Busca se existe caminho de targetId para sourceId (ciclo)
    const visited = new Set<string>();
    const stack = [targetId];

    while (stack.length > 0) {
      const current = stack.pop()!;
      if (current === sourceId) return true;
      if (!visited.has(current)) {
        visited.add(current);
        const outgoing = this.getAllEdges().filter(e => e.sourceNodeId === current);
        outgoing.forEach(e => stack.push(e.targetNodeId));
      }
    }
    return false;
  }

  // --- Metadados e Compilador ---

  getMetadata(): IGraphMetadata {
    return { ...this.metadata };
  }

  updateMetadata(partial: Partial<IGraphMetadata>): void {
    this.metadata = { ...this.metadata, ...partial };
    this.bus.emit('metadata:updated', this.metadata);
    this.bus.emit('graph:changed');
  }

  // --- Serialização JSON ---

  toJSON(): IGraphSchema {
    return {
      version: '1.0.0',
      metadata: this.getMetadata(),
      nodes: this.getAllNodes(),
      edges: this.getAllEdges()
    };
  }

  fromJSON(schema: IGraphSchema): void {
    this.clear();
    this.metadata = { ...schema.metadata };

    schema.nodes.forEach(node => {
      this.nodes.set(node.id, { ...node });
    });

    schema.edges.forEach(edge => {
      this.edges.set(edge.id, { ...edge });
    });

    this.bus.emit('graph:loaded', schema);
    this.bus.emit('graph:changed');
  }

  clear(): void {
    const nodeIds = Array.from(this.nodes.keys());
    nodeIds.forEach(id => this.removeNode(id));
    this.edges.clear();
    this.bus.emit('graph:cleared');
    this.bus.emit('graph:changed');
  }
}
