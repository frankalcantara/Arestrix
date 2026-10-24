import { 
  INodeData, 
  IEdgeData, 
  IGraphSchema, 
  IGraphMetadata, 
  DEFAULT_COMPILER_CONFIG,
  IGlobalPipelineDefaults,
  DEFAULT_PIPELINE_DEFAULTS,
  DEFAULT_NODE_MEMORY_CONFIG,
  TaskflowNodeType,
  IPort,
  IEdgeControlPoints,
  IBreadcrumbItem, defaultSwitchSettings, defaultMcpSettings, defaultOutputSettings,
  defaultInteractionSettings, defaultCallVertexSettings } from './types.ts';
import { globalEventBus, EventBus } from './EventBus.ts';
import { CommandManager } from './history/CommandManager.ts';
import { GraphLayout, IGraphLayoutOptions } from './GraphLayout.ts';
import { AutoLayoutCommand } from './history/GraphCommands.ts';

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
      globalDefaults: JSON.parse(JSON.stringify(DEFAULT_PIPELINE_DEFAULTS)),
      ...initialMetadata
    };
  }


  // --- Configuracoes Globais do Pipeline de Agentes ---

  getProjectId(): string {
    if (!this.metadata.projectId) {
      this.metadata.projectId = this.metadata.graphName.toLowerCase().replace(/[^a-z0-9_-]/g, '_');
    }
    return this.metadata.projectId;
  }

  setProjectId(id: string): void {
    this.metadata.projectId = id;
    this.bus.emit('graph:project-changed', id);
  }

  getGlobalDefaults(): IGlobalPipelineDefaults {
    if (!this.metadata.globalDefaults) {
      this.metadata.globalDefaults = JSON.parse(JSON.stringify(DEFAULT_PIPELINE_DEFAULTS));
    }
    return this.metadata.globalDefaults!;
  }

  updateGlobalDefaults(defaults: Partial<IGlobalPipelineDefaults>): void {
    const current = this.getGlobalDefaults();
    this.metadata.globalDefaults = {
      ...current,
      ...defaults,
      sandbox: { ...current.sandbox, ...(defaults.sandbox || {}) },
      model: { ...current.model, ...(defaults.model || {}) },
      codeMode: { ...current.codeMode, ...(defaults.codeMode || {}) },
      memory: { ...(current.memory || DEFAULT_NODE_MEMORY_CONFIG), ...(defaults.memory || {}) }
    };
    this.bus.emit('graph:defaults-updated', this.metadata.globalDefaults);
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
    
    // NOVO: Espelha o estado imediatamente no backend C++
    try {
      // @ts-ignore
      if (typeof webui !== 'undefined') {
        // @ts-ignore
        webui.call("add_node", JSON.stringify(node));
      }
    } catch (e) {
      console.warn("WebUI C++ backend not detected, running pure local.");
    }

    this.nodes.set(node.id, { ...node });
    this.bus.emit('node:added', node);
    this.bus.emit('graph:changed');
  }

  /**
   * Portas de saída de um switch: uma por caso, mais o 'default' sempre por último.
   * O índice do caso é a posição na lista (conditionBranchIndex), o mesmo valor que o corpo C++ devolve.
   */
  static switchOutputs(id: string, caseNames: string[], existing: IPort[] = []): IPort[] {
    const ports: IPort[] = caseNames.map((name, i) => ({
      id: existing[i]?.id ?? `${id}_out_${i}`,
      name,
      direction: 'output',
      type: 'flow',
      conditionBranchIndex: i
    }));
    ports.push({
      id: `${id}_out_default`,
      name: 'default',
      direction: 'output',
      type: 'flow',
      conditionBranchIndex: caseNames.length
    });
    return ports;
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
    } else if (type === 'switch') {
      outputs = Graph.switchOutputs(id, ['case 1', 'case 2']);
    } else if (type === 'agent') {
      inputs.push({ id: `${id}_in_prompt`, name: 'prompt_in', direction: 'input', type: 'data', dataType: 'std::string' });
      outputs = [
        { id: `${id}_out_0`, name: 'out', direction: 'output', type: 'flow' },
        { id: `${id}_out_data`, name: 'artifact_out', direction: 'output', type: 'data', dataType: 'std::string' },
        { id: `${id}_out_call`, name: 'call', direction: 'output', type: 'call' }
      ];
    } else if (type === 'output') {
      inputs.push({ id: `${id}_in_data`, name: 'data_in', direction: 'input', type: 'data', dataType: 'std::string' });
      outputs = [
        { id: `${id}_out_0`, name: 'out', direction: 'output', type: 'flow' }
      ];
    } else if (type === 'interaction') {
      outputs = [
        { id: `${id}_out_0`, name: 'out', direction: 'output', type: 'flow' }
      ];
    } else if (type === 'call') {
      outputs = [
        { id: `${id}_out_0`, name: 'out', direction: 'output', type: 'flow' },
        { id: `${id}_out_call`, name: 'call', direction: 'output', type: 'call' }
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
      customLambdaBody: this.getDefaultLambdaBody(type, label),
      agentSettings: (type === 'agent' || type === 'switch') ? {
        inheritSandbox: true,
        inheritModel: true,
        inheritCodeMode: true,
        inheritMemory: true
      } : undefined,
      switchSettings: type === 'switch' ? defaultSwitchSettings('model') : undefined,
      mcpSettings: type === 'mcp' ? defaultMcpSettings() : undefined,
      outputSettings: type === 'output' ? defaultOutputSettings() : undefined,
      interactionSettings: type === 'interaction' ? defaultInteractionSettings(label) : undefined,
      callSettings: type === 'call' ? defaultCallVertexSettings() : undefined
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
      case 'mcp':
        return '';   // sem código: o gerador chama o cliente MCP nativo com as configurações do vértice
      case 'switch':
        return `// Switch ${label}: devolva o índice do caso (0, 1, 2...).\n// O último caso é o "default": qualquer valor fora do intervalo vai para ele.\nreturn 0;`;
      case 'condition':
        return `// Decisão de branch do agente ${label}\n// Retorna 0 para ramo 0, 1 para ramo 1\nreturn (rand() % 2);`;
      case 'subflow':
        return `// Subfluxo gerado dinamicamente para ${label}\nauto sub_a = sf.emplace([]() { std::cout << "Subtask A\\n"; }).name("SubA");\nauto sub_b = sf.emplace([]() { std::cout << "Subtask B\\n"; }).name("SubB");\nsub_a.precede(sub_b);`;
      case 'module':
        return `// Módulo reutilizável composto de outro subgrafo`;
      case 'critical':
        return `// Seção crítica com acesso exclusivo a recursos compartilhados\nstd::cout << "Acesso exclusivo seguro pelo agente ${label}\\n";`;
      case 'cuda':
        return `// Kernel CUDA acelerado na GPU: ${label}\nstd::cout << "[CUDA: ${label}] Executando kernel na GPU...\\n";`;
      case 'agent':
        return `// Agente Autônomo Code Mode: ${label}\n// Invocação em sandbox/VM isolada com streaming de eventos\nstd::cout << "[Agent: ${label}] Disparando no sandbox isolado...\\n";`;
      case 'interaction':
        return '';   // sem código: o canal humano é tratado pelo gerador/runtime (Fase 2B)
      case 'call':
        return '';   // sem código: o gerador emite runFlowCall com o alvo da aresta call
      case 'output':
        return `// Coletor de Entrega / Artefato: ${label}\n// Em runtime, este vértice registra e disponibiliza o artefato produzido na aba Artefatos e na pasta de saída.\nstd::cout << "[OUTPUT: ${label}] Entregável registrado com sucesso!\\n";`;
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

    try {
      // @ts-ignore
      if (typeof webui !== 'undefined') webui.call("remove_node", id);
    } catch(e) {}

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

  getNodes(): INodeData[] {
    return this.getAllNodes();
  }

  // --- Operações com Arestas ---

  addEdge(edge: IEdgeData): void {
    if (this.edges.has(edge.id)) {
      throw new Error(`Edge with id "${edge.id}" already exists.`);
    }

    // NOVO: Espelha a aresta no C++
    try {
      // @ts-ignore
      if (typeof webui !== 'undefined') {
        // @ts-ignore
        webui.call("add_edge", JSON.stringify(edge));
      }
    } catch (e) {}

    const source = this.nodes.get(edge.sourceNodeId);
    const target = this.nodes.get(edge.targetNodeId);

    if (!source || !target) {
      throw new Error(`Cannot add edge: source or target node not found.`);
    }

    // Identificar se a aresta é um loop condicional. Chamadas (kind = call) não são fluxo
    // e nunca viram feedback.
    if ((edge.kind ?? 'flow') !== 'call' && (source.type === 'condition' || source.type === 'switch')) {
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

    try {
      // @ts-ignore
      if (typeof webui !== 'undefined') webui.call("remove_edge", id);
    } catch(e) {}

    this.edges.delete(id);
    this.bus.emit('edge:removed', edge);
    this.bus.emit('graph:changed');
  }

  updateEdge(id: string, partial: Partial<IEdgeData>): void {
    const existing = this.edges.get(id);
    if (!existing) return;

    const updated: IEdgeData = { ...existing, ...partial };
    if ('controlPoints' in partial && partial.controlPoints === undefined) {
      delete updated.controlPoints;
    }

    this.edges.set(id, updated);
    this.bus.emit('edge:updated', updated);
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

  // --- Organização Automática de Vértices (Auto-Layout Sugiyama 2D) ---

  autoLayout(
    recordHistory: boolean = true,
    options?: IGraphLayoutOptions
  ): Map<string, { x: number; y: number }> {
    const allNodes = this.getAllNodes();
    const allEdges = this.getAllEdges();
    if (allNodes.length === 0) return new Map();

    const newPositions = GraphLayout.computePositions(allNodes, allEdges, options);

    if (recordHistory) {
      const oldPositions = new Map<string, { x: number; y: number }>();
      allNodes.forEach(n => oldPositions.set(n.id, { x: n.position.x, y: n.position.y }));

      const oldEdgeControls = new Map<string, IEdgeControlPoints | undefined>();
      allEdges.forEach(e => {
        if (e.controlPoints) {
          oldEdgeControls.set(e.id, {
            cp1Offset: { ...e.controlPoints.cp1Offset },
            cp2Offset: { ...e.controlPoints.cp2Offset }
          });
        }
      });

      this.commandManager.execute(
        new AutoLayoutCommand(this, oldPositions, newPositions, oldEdgeControls)
      );
    } else {
      for (const [id, pos] of newPositions.entries()) {
        this.updateNodePosition(id, pos.x, pos.y);
      }
      for (const edge of allEdges) {
        if (edge.controlPoints) {
          this.updateEdge(edge.id, { controlPoints: undefined });
        }
      }
    }

    this.bus.emit('graph:layout-applied', newPositions);
    this.bus.emit('graph:changed');
    return newPositions;
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
        const outgoing = this.getAllEdges().filter(
          e => e.sourceNodeId === current && (e.kind ?? 'flow') !== 'call'
        );
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

  fromJSON(schema: IGraphSchema, options?: { autoLayoutIfNeeded?: boolean }): void {
    this.clear();
    this.metadata = { ...schema.metadata };

    schema.nodes.forEach(node => {
      this.nodes.set(node.id, {
        ...node,
        position: { ...node.position }
      });
    });

    schema.edges.forEach(edge => {
      this.edges.set(edge.id, { ...edge });
    });

    const shouldCheckAutoLayout = options?.autoLayoutIfNeeded !== false;
    if (shouldCheckAutoLayout) {
      const loadedNodes = this.getAllNodes();
      const loadedEdges = this.getAllEdges();
      if (GraphLayout.needsAutoLayout(loadedNodes, loadedEdges)) {
        const distributed = GraphLayout.computePositions(loadedNodes, loadedEdges);
        for (const [id, pos] of distributed.entries()) {
          const n = this.nodes.get(id);
          if (n) {
            n.position.x = pos.x;
            n.position.y = pos.y;
          }
        }
      }
    }

    this.bus.emit('graph:loaded', this.toJSON());
    this.bus.emit('graph:changed');
  }

  clear(): void {
    try {
      // @ts-ignore
      if (typeof webui !== 'undefined') webui.call("clear_graph");
    } catch(e) {}
    
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
