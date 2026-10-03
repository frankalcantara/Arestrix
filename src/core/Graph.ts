import { 
  INodeData, 
  IEdgeData, 
  IGraphSchema, 
  IGraphMetadata, 
  DEFAULT_COMPILER_CONFIG,
  TaskflowNodeType,
  IPort,
  IBreadcrumbItem
} from './types.ts';
import { globalEventBus, EventBus } from './EventBus.ts';
import { CommandManager } from './history/CommandManager.ts';

interface INavigationFrame {
  schema: IGraphSchema;
  parentNodeId: string;
  parentLabel: string;
}

export class Graph {
  private nodes: Map<string, INodeData> = new Map();
  private edges: Map<string, IEdgeData> = new Map();
  private metadata: IGraphMetadata;
  private bus: EventBus;
  private navigationStack: INavigationFrame[] = [];
  public readonly commandManager: CommandManager;

  constructor(bus: EventBus = globalEventBus, initialMetadata?: Partial<IGraphMetadata>) {
    this.bus = bus;
    this.commandManager = new CommandManager(this.bus);
    this.metadata = {
      graphName: 'AgentTaskflowGraph',
      description: 'Orquestração de Agentes Concorrentes com Taskflow C++20',
      targetStandard: 'C++20',
      compilerConfig: { ...DEFAULT_COMPILER_CONFIG },
      version: '1.0.0',
      ...initialMetadata
    };
  }

  // --- Histórico de Edição (Undo / Redo) ---

  undo(): boolean {
    return this.commandManager.undo();
  }

  redo(): boolean {
    return this.commandManager.redo();
  }

  canUndo(): boolean {
    return this.commandManager.canUndo();
  }

  canRedo(): boolean {
    return this.commandManager.canRedo();
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

  buildNode(
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

    return {
      id,
      label,
      type,
      position: { x, y, z: 0 },
      inputs,
      outputs,
      parameters: {},
      customLambdaBody: this.getDefaultLambdaBody(type, label)
    };
  }

  createNode(
    id: string, 
    label: string, 
    type: TaskflowNodeType, 
    x: number, 
    y: number
  ): INodeData {
    const node = this.buildNode(id, label, type, x, y);
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
    this.commandManager.clear();
    this.bus.emit('graph:cleared');
    this.bus.emit('graph:changed');
  }

  // --- Navegação Hierárquica e Subflows Aninhados ---

  isAtRoot(): boolean {
    return this.navigationStack.length === 0;
  }

  getBreadcrumbs(): IBreadcrumbItem[] {
    const rootName = this.navigationStack.length > 0 
      ? this.navigationStack[0].schema.metadata.graphName 
      : this.metadata.graphName;

    const items: IBreadcrumbItem[] = [
      { title: rootName }
    ];

    for (let i = 0; i < this.navigationStack.length; i++) {
      items.push({
        nodeId: this.navigationStack[i].parentNodeId,
        title: this.navigationStack[i].parentLabel
      });
    }

    return items;
  }

  diveIntoSubflow(subflowNodeId: string): boolean {
    const node = this.getNode(subflowNodeId);
    if (!node || (node.type !== 'subflow' && node.type !== 'module')) {
      return false;
    }

    // Se o nó não tiver childGraph, inicializa com template demonstrativo
    if (!node.childGraph || node.childGraph.nodes.length === 0) {
      node.childGraph = this.createDefaultSubflowTemplate(node);
    }

    // 1. Salva o snapshot do grafo atual na pilha
    const currentSnapshot = this.toJSON();
    this.navigationStack.push({
      schema: currentSnapshot,
      parentNodeId: node.id,
      parentLabel: node.label
    });

    // 2. Carrega o subgrafo como o grafo ativo no canvas
    const targetChild = node.childGraph;
    this.fromJSON(targetChild);

    this.bus.emit('graph:hierarchy-changed', {
      isAtRoot: false,
      breadcrumbs: this.getBreadcrumbs(),
      parentNodeId: node.id
    });

    return true;
  }

  navigateUp(): boolean {
    if (this.isAtRoot()) return false;

    // 1. Snapshot do subgrafo filho atual
    const currentChildSnapshot = this.toJSON();

    // 2. Desempilha o pai
    const parentFrame = this.navigationStack.pop()!;
    const parentSchema = parentFrame.schema;

    // 3. Atualiza o childGraph no nó correspondente do pai
    const parentNodeIndex = parentSchema.nodes.findIndex(n => n.id === parentFrame.parentNodeId);
    if (parentNodeIndex !== -1) {
      parentSchema.nodes[parentNodeIndex].childGraph = currentChildSnapshot;
    }

    // 4. Recarrega o pai
    this.fromJSON(parentSchema);

    this.bus.emit('graph:hierarchy-changed', {
      isAtRoot: this.isAtRoot(),
      breadcrumbs: this.getBreadcrumbs()
    });

    return true;
  }

  navigateToBreadcrumb(index: number): boolean {
    if (index < 0 || index >= this.getBreadcrumbs().length) return false;

    // Se for o nível atual, nada a fazer
    const targetStepsUp = (this.getBreadcrumbs().length - 1) - index;
    for (let i = 0; i < targetStepsUp; i++) {
      this.navigateUp();
    }
    return true;
  }

  /**
   * Constrói o schema completo do grafo raiz integrando todos os subgrafos salvos.
   */
  getRootSchema(): IGraphSchema {
    if (this.isAtRoot()) {
      return this.toJSON();
    }
    // Cria cópia profunda da raiz
    const rootSchema = JSON.parse(JSON.stringify(this.navigationStack[0].schema)) as IGraphSchema;
    
    // Atualiza a cadeia até o nível atual
    let currentTarget = rootSchema;
    for (let i = 0; i < this.navigationStack.length; i++) {
      const frame = this.navigationStack[i];
      const targetNode = currentTarget.nodes.find(n => n.id === frame.parentNodeId);
      if (targetNode) {
        if (i === this.navigationStack.length - 1) {
          targetNode.childGraph = this.toJSON();
        } else {
          targetNode.childGraph = JSON.parse(JSON.stringify(this.navigationStack[i + 1].schema));
          currentTarget = targetNode.childGraph!;
        }
      }
    }
    return rootSchema;
  }

  private createDefaultSubflowTemplate(parentNode: INodeData): IGraphSchema {
    return {
      version: '1.0.0',
      metadata: {
        graphName: `${parentNode.label}_Subflow`,
        description: `Subgrafo de decomposição dinâmica para ${parentNode.label}`,
        targetStandard: 'C++20',
        compilerConfig: { ...this.metadata.compilerConfig },
        version: '1.0.0'
      },
      nodes: [
        {
          id: `${parentNode.id}_child_1`,
          label: 'Sub_Worker_Alpha',
          type: 'static',
          position: { x: -180, y: 0, z: 0 },
          inputs: [{ id: `${parentNode.id}_c1_in`, name: 'in', direction: 'input', type: 'flow' }],
          outputs: [{ id: `${parentNode.id}_c1_out`, name: 'out', direction: 'output', type: 'flow' }],
          parameters: {},
          customLambdaBody: `std::osyncstream(std::cout) << "[${parentNode.label}] Executando Sub-Worker Alpha\\n";`
        },
        {
          id: `${parentNode.id}_child_2`,
          label: 'Sub_Worker_Beta',
          type: 'static',
          position: { x: 180, y: 0, z: 0 },
          inputs: [{ id: `${parentNode.id}_c2_in`, name: 'in', direction: 'input', type: 'flow' }],
          outputs: [{ id: `${parentNode.id}_c2_out`, name: 'out', direction: 'output', type: 'flow' }],
          parameters: {},
          customLambdaBody: `std::osyncstream(std::cout) << "[${parentNode.label}] Executando Sub-Worker Beta\\n";`
        }
      ],
      edges: [
        {
          id: `edge_${parentNode.id}_c1_to_c2`,
          sourceNodeId: `${parentNode.id}_child_1`,
          sourcePortId: `${parentNode.id}_c1_out`,
          targetNodeId: `${parentNode.id}_child_2`,
          targetPortId: `${parentNode.id}_c2_in`
        }
      ]
    };
  }
}
