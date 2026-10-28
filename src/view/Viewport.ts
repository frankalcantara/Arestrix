import * as THREE from 'three';
import { CameraController } from './CameraController.ts';
import { InfiniteGrid } from './InfiniteGrid.ts';
import { NodeView } from './NodeView.ts';
import { ConnectionView, EdgeExecutionState, getEdgeBaseStyle } from './ConnectionView.ts';
import { NodeExecutionState, NodeRunOutcome, NodeRunRecord } from './SvgNodeView.ts';
import { InteractionManager } from './InteractionManager.ts';
import { Graph } from '../core/Graph.ts';
import { computeFlowRoles, computeFlowRoleMap, computeTrailAssignments } from '../core/GraphTopology.ts';
import { INodeData, IEdgeData, IGraphSchema } from '../core/types.ts';
import { EventBus } from '../core/EventBus.ts';
import { THEME } from './Theme.ts';
import { IBezierCurvePoints, IObstacleBox } from './BezierEdgeRouter.ts';
import { CanvasBackground, CANVAS_PALETTES, loadCanvasBackground, saveCanvasBackground } from './CanvasPreferences.ts';
import { APP_THEME_CHANGED_EVENT, type AppThemeChangedDetail } from './AppTheme.ts';

function sanitizeId(id: string): string {
  return id.replace(/[^a-zA-Z0-9]/g, '_');
}

export class Viewport {
  private container: HTMLElement;
  private scene: THREE.Scene;
  private camera: THREE.OrthographicCamera;
  private renderer: THREE.WebGLRenderer;
  private cameraController: CameraController;
  private grid: InfiniteGrid;
  private interactionManager: InteractionManager;

  private graph: Graph;
  private bus: EventBus;

  private nodeViews: Map<string, NodeView> = new Map();
  private connectionViews: Map<string, ConnectionView> = new Map();

  private isRunning: boolean = true;
  private frustumSize: number = 850;
  private resizeObserver!: ResizeObserver;
  private onWindowResize = (): void => this.onResize();
  private onAppThemeChanged = (event: Event): void => {
    const detail = (event as CustomEvent<AppThemeChangedDetail>).detail;
    if (detail?.theme === 'dark' || detail?.theme === 'light') {
      this.setCanvasBackground(detail.theme);
    }
  };

  // Camada Vetorial SVG Nativa para Vértices, Conexões e Alças Bézier (Inkscape-style)
  private svgLayer!: SVGSVGElement;
  private svgRoot!: SVGGElement;
  private svgEdgesLayer!: SVGGElement;
  private svgNodesLayer!: SVGGElement;
  private svgEdgeHandlesLayer!: SVGGElement;
  private svgEdgeGroups: Map<string, SVGGElement> = new Map();
  private svgEdgePaths: Map<string, SVGPathElement> = new Map();
  private svgEdgeFlowPaths: Map<string, SVGPathElement> = new Map();
  private svgEdgePackets: Map<string, SVGCircleElement> = new Map();
  private svgEdgeBadges: Map<string, { group: SVGGElement; rect: SVGRectElement; dot: SVGCircleElement; text: SVGTextElement }> = new Map();
  private svgEdgeHitPaths: Map<string, SVGPathElement> = new Map();
  private selectedEdgeId: string | null = null;
  private followExecution: boolean = false;
  private edgeAnimPhase: number = 0;
  /** Evita que telemetria atrasada reative o modo de execução após o pipeline terminar. */
  private executionActive: boolean = false;
  private canvasBackground: CanvasBackground = 'light';
  /** Liga/desliga a faixa colorida por trilha (botão "Trilhas" da barra de status). */
  private trailHighlight: boolean = false;

  constructor(container: HTMLElement, graph: Graph, bus: EventBus) {
    this.container = container;
    this.graph = graph;
    this.bus = bus;

    // 1. Cena Three.js
    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(THEME.canvasBackground);

    // 2. Camera Ortográfica com dimensões métricas invariantes a redimensionamento
    const initialW = this.container.clientWidth > 0 ? this.container.clientWidth : Math.max(window.innerWidth - 380, 800);
    const initialH = this.container.clientHeight > 0 ? this.container.clientHeight : Math.max(window.innerHeight - 120, 600);
    this.frustumSize = initialH;

    this.camera = new THREE.OrthographicCamera(
      -initialW / 2,
      initialW / 2,
      initialH / 2,
      -initialH / 2,
      0.1,
      3000
    );
    this.camera.position.set(0, 0, 1000);

    // 3. Renderizador WebGL
    this.renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
    this.renderer.setSize(initialW, initialH);
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.container.appendChild(this.renderer.domElement);

    // 3.1 Camada Vetorial SVG Nativa (Vértices e Arestas Nítidas em Qualquer Zoom)
    this.initSvgOverlay();

    // 4. Grid Infinito e Controlador de Câmera
    this.grid = new InfiniteGrid(this.scene);
    const appTheme = typeof document !== 'undefined' ? document.documentElement.dataset.theme : undefined;
    const initialCanvas: CanvasBackground = appTheme === 'dark' ? 'dark' : loadCanvasBackground();
    this.applyCanvasBackground(initialCanvas);
    if (appTheme === 'dark') saveCanvasBackground('dark');
    if (typeof document !== 'undefined') {
      document.addEventListener(APP_THEME_CHANGED_EVENT, this.onAppThemeChanged);
    }
    this.cameraController = new CameraController(this.camera, this.renderer.domElement, this.frustumSize);

    // 5. Observador de Redimensionamento do DOM para sincronizar automaticamente
    if (typeof ResizeObserver !== 'undefined') {
      this.resizeObserver = new ResizeObserver(() => {
        this.onResize();
      });
      this.resizeObserver.observe(this.container);
    }

    // 5. Gerenciador de Interações (eventos SVG + pan/zoom + rubber-band)
    this.interactionManager = new InteractionManager(
      this.cameraController,
      this.renderer.domElement,
      this.scene,
      this.graph,
      this.nodeViews,
      this.connectionViews
    );

    // 6. Subscrições ao Grafo Lógico via EventBus
    this.bindGraphEvents();

    // 7. Eventos de Janela e Resize
    window.addEventListener('resize', this.onWindowResize);

    // 8. Inicia loop de renderização
    this.animate();
  }

  private bindGraphEvents(): void {
    this.bus.on('node:added', (node: INodeData) => this.onNodeAdded(node));
    this.bus.on('node:removed', ({ id }: { id: string }) => this.onNodeRemoved(id));
    this.bus.on('node:updated', (node: INodeData) => this.onNodeUpdated(node));
    this.bus.on('node:moved', ({ id, position }: { id: string; position: { x: number; y: number } }) => this.onNodeMoved(id, position));
    this.bus.on('edge:added', (edge: IEdgeData) => this.onEdgeAdded(edge));
    this.bus.on('edge:updated', (edge: IEdgeData) => this.onEdgeUpdated(edge));
    this.bus.on('edge:removed', (edge: IEdgeData) => this.onEdgeRemoved(edge.id));
    this.bus.on('edge:selected', (edgeId: string | null) => this.onEdgeSelected(edgeId));
    this.bus.on('graph:layout-applied', () => {
      this.refreshAllEdges();
      this.zoomToFit();
    });
    this.bus.on('graph:cleared', () => this.clearAllViews());
    this.bus.on('graph:loaded', () => this.syncFromGraph());
    this.bus.on('graph:changed', () => this.refreshFlowDecorations());
    this.bus.on('locale:changed', () => this.refreshFlowDecorations());
    this.bus.on('graph:hierarchy-changed', () => this.zoomToFit());
    this.bus.on('pipeline:compiling', () => {
      this.executionActive = false;
      this.onTelemetryReset();
      this.executionActive = true;
      this.onPipelineRunning();
    });
    this.bus.on('pipeline:running', () => {
      this.executionActive = true;
      this.onPipelineRunning();
    });
    this.bus.on('pipeline:success', () => this.onPipelineFinished(true));
    this.bus.on('pipeline:error', () => this.onPipelineFinished(false));
    this.bus.on('agent:telemetry', (ev: any) => this.onAgentTelemetry(ev));
    this.bus.on('telemetry:task-enter', (data: { taskId: string; worker?: number }) => this.onTelemetryEnter(data));
    this.bus.on('telemetry:task-exit', (data: { taskId: string; worker?: number; failed?: boolean }) => this.onTelemetryExit(data));
    this.bus.on('telemetry:reset', () => this.onTelemetryReset());
    this.bus.on('camera:zoom-changed', ({ zoom }: { zoom: number }) => {
      this.nodeViews.forEach(nv => nv.setDetailLevel(zoom));
    });
  }

  private onPipelineRunning(): void {
    this.nodeViews.forEach(nv => {
      // Um novo run invalida o registro visual do run anterior.
      nv.setRunRecord(null);
      if (nv.getExecutionState() === 'idle') {
        nv.setExecutionState('queued');
      }
    });
    this.connectionViews.forEach((conn, edgeId) => {
      if (conn.executionState === 'idle') {
        this.setEdgeExecutionState(edgeId, 'queued');
      }
    });
  }

  /**
   * Encerra a execução devolvendo todos os vértices ao modo de edição.
   * O resultado de cada vértice é preservado como um selo compacto no header
   * (sucesso/erro/não executado + duração), sem manter o vértice "apagado".
   */
  private onPipelineFinished(success: boolean): void {
    this.executionActive = false;
    const finishedAt = Date.now();

    this.nodeViews.forEach(nv => {
      const st = nv.getExecutionState();
      const durationMs = nv.getElapsedMs();

      let outcome: NodeRunOutcome | null = null;
      if (st === 'running') {
        outcome = success ? 'success' : 'error';
      } else if (st === 'completed') {
        outcome = 'success';
      } else if (st === 'error') {
        outcome = 'error';
      } else if (st === 'queued') {
        outcome = 'skipped';
      }

      nv.setRunRecord(outcome ? { outcome, durationMs, finishedAt } : null);
      nv.setExecutionState('idle');
    });

    this.connectionViews.forEach((conn, edgeId) => {
      if (conn.executionState === 'active') {
        this.setEdgeExecutionState(edgeId, success ? 'completed' : 'error');
      } else if (conn.executionState === 'queued') {
        this.setEdgeExecutionState(edgeId, 'idle');
      }
    });
  }

  /**
   * Garante que apenas o vértice atualmente em foco permaneça opaco (opacity: 1),
   * retornando qualquer vértice anteriormente ativo ao estado concluído/translúcido (opacity: 0.45)
   * e sincronizando o fluxo animado das arestas conectadas.
   */
  private focusActiveNode(activeNodeId: string): void {
    this.nodeViews.forEach(otherNv => {
      if (otherNv.nodeData.id === activeNodeId) return;
      const st = otherNv.getExecutionState();
      if (st === 'running') {
        otherNv.setExecutionState('completed');
      } else if (st === 'idle') {
        otherNv.setExecutionState('queued');
      }
    });

    this.activateEdgesForActiveNode(activeNodeId);

    const activeNv = this.nodeViews.get(activeNodeId);
    if (activeNv && this.followExecution) {
      this.cameraController.centerOn(activeNv.nodeData.position.x, activeNv.nodeData.position.y);
    }
  }

  private onAgentTelemetry(ev: any): void {
    if (!this.executionActive || !ev || !ev.nodeId) return;
    const nv = this.findNodeView(ev.nodeId);
    if (!nv) return;

    // NodeView encaminha a telemetria para o SvgNodeView (camada visual única).
    nv.handleTelemetry(ev);

    const p = ev.payload || {};
    if (
      ev.type === 'thinking' ||
      ev.type === 'tool_start' ||
      ev.type === 'tool_end' ||
      ev.type === 'rate_limit' ||
      ev.type === 'log'
    ) {
      this.focusActiveNode(nv.nodeData.id);
    } else if (ev.type === 'status') {
      const st = String(p.status || '').toLowerCase();
      if (st === 'settled' || st === 'completed' || st === 'done') {
        nv.setExecutionState('completed');
        this.completeEdgesForNode(nv.nodeData.id);
      } else if (st === 'error' || st === 'failed') {
        nv.setExecutionState('error');
        this.failEdgesForNode(nv.nodeData.id);
      } else {
        this.focusActiveNode(nv.nodeData.id);
      }
    }
  }

  private onTelemetryEnter(data: { taskId: string; worker?: number }): void {
    if (!this.executionActive) return;
    const nv = this.findNodeView(data.taskId);
    if (nv) {
      nv.setExecutionState('running');
      this.focusActiveNode(nv.nodeData.id);
    }
  }

  private onTelemetryExit(data: { taskId: string; worker?: number; failed?: boolean }): void {
    if (!this.executionActive) return;
    const nv = this.findNodeView(data.taskId);
    if (nv) {
      if (data.failed) {
        nv.setExecutionState('error');
        this.failEdgesForNode(nv.nodeData.id);
      } else {
        nv.setExecutionState('completed');
        this.completeEdgesForNode(nv.nodeData.id);
      }
    }
  }

  private activateEdgesForActiveNode(nodeId: string): void {
    // 1. Arestas que estavam ativas em nós anteriores (que já concluíram) passam para 'completed'
    this.connectionViews.forEach(conn => {
      const isIncoming = conn.edgeData.targetNodeId === nodeId;
      const isOutgoing = conn.edgeData.sourceNodeId === nodeId;
      if (!isIncoming && !isOutgoing && conn.executionState === 'active') {
        this.setEdgeExecutionState(conn.edgeData.id, 'completed');
      } else if (!isIncoming && !isOutgoing && conn.executionState === 'idle') {
        this.setEdgeExecutionState(conn.edgeData.id, 'queued');
      }
    });

    // 2. Arestas de entrada para o vértice ativo (vindo de predecessores concluídos ou fluxo direto)
    const incoming: ConnectionView[] = [];
    const outgoing: ConnectionView[] = [];
    this.connectionViews.forEach(conn => {
      if (conn.edgeData.targetNodeId === nodeId) incoming.push(conn);
      if (conn.edgeData.sourceNodeId === nodeId) outgoing.push(conn);
    });

    let matchedIncoming = false;
    incoming.forEach(conn => {
      const srcState = this.nodeViews.get(conn.edgeData.sourceNodeId)?.getExecutionState();
      if (srcState === 'completed' || srcState === 'running') {
        this.setEdgeExecutionState(conn.edgeData.id, 'active');
        matchedIncoming = true;
      }
    });
    if (!matchedIncoming) {
      incoming.forEach(conn => {
        if (!conn.edgeData.isFeedbackLoop) {
          this.setEdgeExecutionState(conn.edgeData.id, 'active');
        }
      });
    }

    // 3. Arestas de saída do vértice ativo: animam enquanto o vértice processa e transmite dados adiante
    let matchedOutgoing = false;
    outgoing.forEach(conn => {
      if (!conn.edgeData.isFeedbackLoop) {
        this.setEdgeExecutionState(conn.edgeData.id, 'active');
        matchedOutgoing = true;
      }
    });
    if (!matchedOutgoing) {
      outgoing.forEach(conn => {
        this.setEdgeExecutionState(conn.edgeData.id, 'active');
      });
    }
  }

  private completeEdgesForNode(nodeId: string): void {
    this.connectionViews.forEach(conn => {
      if (conn.edgeData.targetNodeId === nodeId || conn.edgeData.sourceNodeId === nodeId) {
        if (conn.executionState === 'active') {
          this.setEdgeExecutionState(conn.edgeData.id, 'completed');
        }
      }
    });
  }

  private failEdgesForNode(nodeId: string): void {
    this.connectionViews.forEach(conn => {
      if ((conn.edgeData.targetNodeId === nodeId || conn.edgeData.sourceNodeId === nodeId) && conn.executionState === 'active') {
        this.setEdgeExecutionState(conn.edgeData.id, 'error');
      }
    });
  }

  private findNodeView(key: string): NodeView | undefined {
    if (!key) return undefined;
    let nv = this.nodeViews.get(key);
    if (nv) return nv;

    // Normaliza chaves de execução em lote (nodeId#itemId ou itemId :: label)
    const strippedHash = key.includes('#') ? key.split('#')[0].trim() : key;
    const strippedBatch = strippedHash.includes(' :: ')
      ? strippedHash.split(' :: ').pop()!.trim()
      : strippedHash;

    nv = this.nodeViews.get(strippedBatch);
    if (nv) return nv;

    const cleanTarget = sanitizeId(strippedBatch);
    for (const view of this.nodeViews.values()) {
      const n = view.nodeData;
      if (
        n.id === strippedBatch ||
        n.label === strippedBatch ||
        n.label === key ||
        sanitizeId(n.id) === cleanTarget ||
        `task_${sanitizeId(n.id)}` === cleanTarget ||
        sanitizeId(n.label) === cleanTarget
      ) {
        return view;
      }
    }

    // Se o evento veio de um vértice interno de um Subflow/Module, destaca o Subflow pai visível
    const schemaContainsNode = (schema: IGraphSchema | undefined): boolean => {
      if (!schema || !Array.isArray(schema.nodes)) return false;
      for (const child of schema.nodes) {
        if (
          child.id === strippedBatch ||
          child.label === strippedBatch ||
          sanitizeId(child.id) === cleanTarget ||
          schemaContainsNode(child.childGraph)
        ) {
          return true;
        }
      }
      return false;
    };

    for (const view of this.nodeViews.values()) {
      if (schemaContainsNode(view.nodeData.childGraph)) {
        return view;
      }
    }

    return undefined;
  }

  private initSvgOverlay(): void {
    if (typeof document === 'undefined') return;
    const SVG_NS = 'http://www.w3.org/2000/svg';
    this.svgLayer = document.createElementNS(SVG_NS, 'svg');
    this.svgLayer.setAttribute('class', 'arestrix-canvas-svg-layer');

    this.svgRoot = document.createElementNS(SVG_NS, 'g');
    this.svgRoot.setAttribute('class', 'arestrix-svg-root');

    this.svgEdgesLayer = document.createElementNS(SVG_NS, 'g');
    this.svgEdgesLayer.setAttribute('class', 'arestrix-svg-edges');

    this.svgNodesLayer = document.createElementNS(SVG_NS, 'g');
    this.svgNodesLayer.setAttribute('class', 'arestrix-svg-nodes');

    this.svgEdgeHandlesLayer = document.createElementNS(SVG_NS, 'g');
    this.svgEdgeHandlesLayer.setAttribute('class', 'arestrix-svg-edge-handles');

    this.svgRoot.appendChild(this.svgEdgesLayer);
    this.svgRoot.appendChild(this.svgNodesLayer);
    this.svgRoot.appendChild(this.svgEdgeHandlesLayer);
    this.svgLayer.appendChild(this.svgRoot);
    this.container.appendChild(this.svgLayer);
  }

  private getObstacleBoxes(): IObstacleBox[] {
    const boxes: IObstacleBox[] = [];
    this.nodeViews.forEach(nv => {
      const size = nv.getBoundingSize();
      boxes.push({
        id: nv.nodeData.id,
        x: nv.nodeData.position.x,
        y: nv.nodeData.position.y,
        width: size.width,
        height: size.height
      });
    });
    return boxes;
  }

  private onTelemetryReset(): void {
    this.nodeViews.forEach(nv => {
      nv.setExecutionState('idle');
      nv.setRunRecord(null);
    });
    this.connectionViews.forEach((_conn, edgeId) => this.setEdgeExecutionState(edgeId, 'idle'));
  }

  private onNodeAdded(nodeData: INodeData): void {
    if (this.nodeViews.has(nodeData.id)) return;

    const nodeView = new NodeView(nodeData, this.svgNodesLayer, this.interactionManager.getTooltip());
    nodeView.setDetailLevel(this.cameraController.getZoom());
    this.nodeViews.set(nodeData.id, nodeView);
    this.refreshAllEdges();
  }

  private onNodeRemoved(nodeId: string): void {
    const nodeView = this.nodeViews.get(nodeId);
    if (nodeView) {
      nodeView.dispose();
      this.nodeViews.delete(nodeId);
      this.refreshAllEdges();
    }
  }

  private onNodeMoved(id: string, position: { x: number; y: number }): void {
    const nodeView = this.nodeViews.get(id);
    if (nodeView) {
      nodeView.updatePosition(position.x, position.y);
      this.refreshAllEdges();
    }
  }

  public updateConnectedEdgesForNode(_nodeId: string): void {
    this.refreshAllEdges();
  }

  public refreshAllEdges(): void {
    const obstacles = this.getObstacleBoxes();
    this.connectionViews.forEach((conn, edgeId) => {
      const latestEdge = this.graph.getEdge(edgeId);
      if (latestEdge) {
        conn.edgeData = latestEdge;
      }
      const src = this.nodeViews.get(conn.edgeData.sourceNodeId);
      const tgt = this.nodeViews.get(conn.edgeData.targetNodeId);
      if (src && tgt) {
        const start = src.getPortWorldPosition(conn.edgeData.sourcePortId);
        const end = tgt.getPortWorldPosition(conn.edgeData.targetPortId);
        const bezier = conn.updateCurve(start, end, obstacles);
        this.updateSvgEdgeCurve(edgeId, bezier, conn.edgeData, conn.isSelected);
        if (this.selectedEdgeId === edgeId) {
          this.renderSelectedEdgeHandles(edgeId, bezier);
        }
      }
    });
  }

  private onEdgeUpdated(edgeData: IEdgeData): void {
    const conn = this.connectionViews.get(edgeData.id);
    if (!conn) return;
    conn.edgeData = edgeData;
    const obstacles = this.getObstacleBoxes();
    const src = this.nodeViews.get(edgeData.sourceNodeId);
    const tgt = this.nodeViews.get(edgeData.targetNodeId);
    if (src && tgt) {
      const start = src.getPortWorldPosition(edgeData.sourcePortId);
      const end = tgt.getPortWorldPosition(edgeData.targetPortId);
      const bezier = conn.updateCurve(start, end, obstacles);
      this.updateSvgEdgeCurve(edgeData.id, bezier, edgeData, conn.isSelected);
      if (this.selectedEdgeId === edgeData.id) {
        this.renderSelectedEdgeHandles(edgeData.id, bezier);
      }
    }
  }

  private onEdgeSelected(edgeId: string | null): void {
    this.selectedEdgeId = edgeId;
    this.connectionViews.forEach((conn, id) => {
      const isSel = id === edgeId;
      if (conn.currentBezier) {
        this.updateSvgEdgeCurve(id, conn.currentBezier, conn.edgeData, isSel);
      }
    });

    if (!edgeId) {
      this.clearEdgeHandles();
      return;
    }

    const conn = this.connectionViews.get(edgeId);
    if (conn && conn.currentBezier) {
      this.renderSelectedEdgeHandles(edgeId, conn.currentBezier);
    }
  }

  private clearEdgeHandles(): void {
    if (!this.svgEdgeHandlesLayer) return;
    while (this.svgEdgeHandlesLayer.firstChild) {
      this.svgEdgeHandlesLayer.removeChild(this.svgEdgeHandlesLayer.firstChild);
    }
  }

  /**
   * Renderiza as alças de edição de curva de Bézier estilo Inkscape (tangentes cp1, cp2 e nó central mid).
   */
  private renderSelectedEdgeHandles(edgeId: string, bezier: IBezierCurvePoints): void {
    if (!this.svgEdgeHandlesLayer || typeof document === 'undefined') return;
    this.clearEdgeHandles();

    const SVG_NS = 'http://www.w3.org/2000/svg';
    const group = document.createElementNS(SVG_NS, 'g');
    group.setAttribute('class', 'arestrix-bezier-handles-group');
    group.setAttribute('data-edge-id', edgeId);

    const sx = bezier.start.x;
    const sy = -bezier.start.y;
    const cp1x = bezier.cp1.x;
    const cp1y = -bezier.cp1.y;
    const cp2x = bezier.cp2.x;
    const cp2y = -bezier.cp2.y;
    const ex = bezier.end.x;
    const ey = -bezier.end.y;
    const mx = bezier.midPoint.x;
    const my = -bezier.midPoint.y;

    // 1. Linhas de tangente tracejadas (start -> cp1 e end -> cp2)
    const line1 = document.createElementNS(SVG_NS, 'line');
    line1.setAttribute('class', 'arestrix-bezier-tangent');
    line1.setAttribute('x1', `${sx}`);
    line1.setAttribute('y1', `${sy}`);
    line1.setAttribute('x2', `${cp1x}`);
    line1.setAttribute('y2', `${cp1y}`);
    line1.setAttribute('stroke', '#3B82F6');
    line1.setAttribute('stroke-width', '1.75');
    line1.setAttribute('stroke-dasharray', '5 4');
    line1.style.pointerEvents = 'none';
    group.appendChild(line1);

    const line2 = document.createElementNS(SVG_NS, 'line');
    line2.setAttribute('class', 'arestrix-bezier-tangent');
    line2.setAttribute('x1', `${ex}`);
    line2.setAttribute('y1', `${ey}`);
    line2.setAttribute('x2', `${cp2x}`);
    line2.setAttribute('y2', `${cp2y}`);
    line2.setAttribute('stroke', '#3B82F6');
    line2.setAttribute('stroke-width', '1.75');
    line2.setAttribute('stroke-dasharray', '5 4');
    line2.style.pointerEvents = 'none';
    group.appendChild(line2);

    const bindHandleEvents = (el: SVGElement, handleType: 'cp1' | 'cp2' | 'mid') => {
      el.addEventListener('pointerdown', (e: PointerEvent) => {
        if (e.button !== 0) return;
        e.stopPropagation();
        e.preventDefault();
        this.bus.emit('edge:start-handle-drag', {
          edgeId,
          handle: handleType,
          clientX: e.clientX,
          clientY: e.clientY
        });
      });
      el.addEventListener('dblclick', (e: MouseEvent) => {
        e.stopPropagation();
        e.preventDefault();
        this.bus.emit('edge:reset-control-points', { edgeId });
      });
    };

    // 2. Alça de Controle CP1 (círculo estilo Inkscape)
    const cp1Handle = document.createElementNS(SVG_NS, 'circle');
    cp1Handle.setAttribute('class', 'arestrix-bezier-handle arestrix-bezier-handle-cp1');
    cp1Handle.setAttribute('data-handle', 'cp1');
    cp1Handle.setAttribute('data-edge-id', edgeId);
    cp1Handle.setAttribute('cx', `${cp1x}`);
    cp1Handle.setAttribute('cy', `${cp1y}`);
    cp1Handle.setAttribute('r', '7.5');
    cp1Handle.setAttribute('fill', '#FFFFFF');
    cp1Handle.setAttribute('stroke', '#2563EB');
    cp1Handle.setAttribute('stroke-width', '2.5');
    cp1Handle.style.pointerEvents = 'all';
    cp1Handle.style.cursor = 'move';
    bindHandleEvents(cp1Handle, 'cp1');
    group.appendChild(cp1Handle);

    // 3. Alça de Controle CP2 (círculo estilo Inkscape)
    const cp2Handle = document.createElementNS(SVG_NS, 'circle');
    cp2Handle.setAttribute('class', 'arestrix-bezier-handle arestrix-bezier-handle-cp2');
    cp2Handle.setAttribute('data-handle', 'cp2');
    cp2Handle.setAttribute('data-edge-id', edgeId);
    cp2Handle.setAttribute('cx', `${cp2x}`);
    cp2Handle.setAttribute('cy', `${cp2y}`);
    cp2Handle.setAttribute('r', '7.5');
    cp2Handle.setAttribute('fill', '#FFFFFF');
    cp2Handle.setAttribute('stroke', '#2563EB');
    cp2Handle.setAttribute('stroke-width', '2.5');
    cp2Handle.style.pointerEvents = 'all';
    cp2Handle.style.cursor = 'move';
    bindHandleEvents(cp2Handle, 'cp2');
    group.appendChild(cp2Handle);

    // 4. Alça Central de Curva em t=0.5 (losango estilo Inkscape para puxar a curva diretamente)
    const midHandle = document.createElementNS(SVG_NS, 'rect');
    midHandle.setAttribute('class', 'arestrix-bezier-handle arestrix-bezier-handle-mid');
    midHandle.setAttribute('data-handle', 'mid');
    midHandle.setAttribute('data-edge-id', edgeId);
    midHandle.setAttribute('x', '-5.5');
    midHandle.setAttribute('y', '-5.5');
    midHandle.setAttribute('width', '11');
    midHandle.setAttribute('height', '11');
    midHandle.setAttribute('rx', '2');
    midHandle.setAttribute('transform', `translate(${mx}, ${my}) rotate(45)`);
    midHandle.setAttribute('fill', '#2563EB');
    midHandle.setAttribute('stroke', '#FFFFFF');
    midHandle.setAttribute('stroke-width', '2');
    midHandle.style.pointerEvents = 'all';
    midHandle.style.cursor = 'grab';
    bindHandleEvents(midHandle, 'mid');
    group.appendChild(midHandle);

    this.svgEdgeHandlesLayer.appendChild(group);
  }

  public setNodeExecutionState(nodeId: string, state: NodeExecutionState, detail: string = ''): void {
    const nodeView = this.nodeViews.get(nodeId);
    if (nodeView) nodeView.setExecutionState(state, detail);
  }

  /** Aplica/limpa o registro da última execução sem alterar o estado de edição do vértice. */
  public setNodeRunRecord(nodeId: string, record: NodeRunRecord | null): void {
    this.nodeViews.get(nodeId)?.setRunRecord(record);
  }

  public setEdgeExecutionState(edgeId: string, state: EdgeExecutionState): void {
    const conn = this.connectionViews.get(edgeId);
    if (!conn) return;
    conn.setExecutionState(state);
    this.applySvgEdgeAppearance(edgeId, conn.edgeData, conn.isSelected, state);
  }

  public getEdgeExecutionState(edgeId: string): EdgeExecutionState {
    return this.connectionViews.get(edgeId)?.executionState ?? 'idle';
  }

  public resetExecutionVisuals(): void {
    this.onTelemetryReset();
  }

  public setFollowExecution(enabled: boolean): void {
    this.followExecution = enabled;
    this.bus.emit('canvas:follow-execution', this.followExecution);
  }

  public isFollowingExecution(): boolean {
    return this.followExecution;
  }

  public toggleFollowExecution(): boolean {
    this.followExecution = !this.followExecution;
    this.bus.emit('canvas:follow-execution', this.followExecution);
    return this.followExecution;
  }

  private getActiveEdgeAccentAndLabel(edgeData: IEdgeData): {
    stroke: string;
    fillBg: string;
    textFill: string;
    label: string;
  } {
    const tgtSvg = this.nodeViews.get(edgeData.targetNodeId)?.svgNodeView ?? null;
    const srcSvg = this.nodeViews.get(edgeData.sourceNodeId)?.svgNodeView ?? null;
    const activeNode =
      tgtSvg?.getExecutionState() === 'running'
        ? tgtSvg
        : srcSvg?.getExecutionState() === 'running'
          ? srcSvg
          : tgtSvg || srcSvg;

    const kind = activeNode?.getActivityKind() ?? 'running';
    const detail = activeNode?.getActivityDetail() ?? '';
    const elapsedSec = ((activeNode?.getElapsedMs() ?? 0) / 1000).toFixed(1);

    if (kind === 'thinking') {
      return {
        stroke: '#6366F1',
        fillBg: '#EEF2FF',
        textFill: '#3730A3',
        label: `Pensando · ${elapsedSec}s`
      };
    }
    if (kind === 'tool_call') {
      const shortTool = detail ? detail.slice(0, 10) : 'Tool';
      return {
        stroke: '#0284C7',
        fillBg: '#E0F2FE',
        textFill: '#075985',
        label: `${shortTool} · ${elapsedSec}s`
      };
    }
    if (kind === 'rate_limit') {
      return {
        stroke: '#F59E0B',
        fillBg: '#FEF3C7',
        textFill: '#92400E',
        label: `Aguardando · ${elapsedSec}s`
      };
    }
    return {
      stroke: '#10B981',
      fillBg: '#ECFDF5',
      textFill: '#065F46',
      label: `Em fluxo · ${elapsedSec}s`
    };
  }

  private applySvgEdgeAppearance(
    edgeId: string,
    edgeData: IEdgeData,
    isSelected: boolean,
    execState: EdgeExecutionState
  ): void {
    const path = this.svgEdgePaths.get(edgeId);
    const flowPath = this.svgEdgeFlowPaths.get(edgeId);
    const packet = this.svgEdgePackets.get(edgeId);
    const badge = this.svgEdgeBadges.get(edgeId);
    const group = this.svgEdgeGroups.get(edgeId);
    if (!path) return;

    const activeInfo = execState === 'active' ? this.getActiveEdgeAccentAndLabel(edgeData) : null;
    const baseStyle = getEdgeBaseStyle(edgeData);

    if (isSelected) {
      path.setAttribute('stroke', '#2563EB');
      path.setAttribute('stroke-width', '4');
      path.setAttribute('opacity', '1');
    } else if (execState === 'active' && activeInfo) {
      path.setAttribute('stroke', activeInfo.stroke);
      path.setAttribute('stroke-width', '5');
      path.setAttribute('opacity', '0.28');
    } else if (execState === 'completed') {
      path.setAttribute('stroke', '#10B981');
      path.setAttribute('stroke-width', '3.5');
      path.setAttribute('opacity', '0.85');
    } else if (execState === 'error') {
      path.setAttribute('stroke', '#EF4444');
      path.setAttribute('stroke-width', '3.5');
      path.setAttribute('opacity', '1');
    } else if (execState === 'queued') {
      path.setAttribute('stroke', '#94A3B8');
      path.setAttribute('stroke-width', '2.5');
      path.setAttribute('opacity', '0.45');
    } else {
      path.setAttribute('stroke', baseStyle.stroke);
      path.setAttribute('stroke-width', '3');
      path.setAttribute('opacity', '1');
    }

    // O tracejado acompanha o modelo (call/feedback), inclusive sob seleção/execução.
    if (baseStyle.dashArray) {
      path.setAttribute('stroke-dasharray', baseStyle.dashArray);
    } else {
      path.removeAttribute('stroke-dasharray');
    }

    if (flowPath) {
      if (execState === 'active' && activeInfo) {
        flowPath.style.display = 'block';
        flowPath.setAttribute('class', 'arestrix-svg-edge-flow flow-active');
        flowPath.setAttribute('stroke', activeInfo.stroke);
        flowPath.setAttribute('stroke-width', '3.5');
      } else {
        flowPath.style.display = 'none';
        flowPath.setAttribute('class', 'arestrix-svg-edge-flow');
      }
    }

    if (packet) {
      if (execState === 'active' && activeInfo) {
        packet.style.display = 'block';
        packet.setAttribute('fill', activeInfo.stroke);
      } else {
        packet.style.display = 'none';
      }
    }

    if (badge) {
      if (execState === 'active' && activeInfo) {
        badge.group.style.display = 'block';
        const w = Math.max(98, activeInfo.label.length * 6.2 + 24);
        badge.rect.setAttribute('x', `${(-w / 2).toFixed(1)}`);
        badge.rect.setAttribute('width', `${w.toFixed(1)}`);
        badge.rect.setAttribute('fill', activeInfo.fillBg);
        badge.rect.setAttribute('stroke', activeInfo.stroke);
        badge.dot.setAttribute('cx', `${(-w / 2 + 11).toFixed(1)}`);
        badge.dot.setAttribute('fill', activeInfo.stroke);
        badge.text.setAttribute('x', '5');
        badge.text.setAttribute('fill', activeInfo.textFill);
        badge.text.textContent = activeInfo.label;
      } else if (execState === 'completed') {
        badge.group.style.display = 'block';
        const w = 78;
        badge.rect.setAttribute('x', `${-w / 2}`);
        badge.rect.setAttribute('width', `${w}`);
        badge.rect.setAttribute('fill', '#ECFDF5');
        badge.rect.setAttribute('stroke', '#6EE7B7');
        badge.dot.setAttribute('cx', `${-w / 2 + 11}`);
        badge.dot.setAttribute('fill', '#10B981');
        badge.text.setAttribute('x', '5');
        badge.text.setAttribute('fill', '#047857');
        badge.text.textContent = '✓ Fluxo OK';
      } else if (execState === 'error') {
        badge.group.style.display = 'block';
        const w = 68;
        badge.rect.setAttribute('x', `${-w / 2}`);
        badge.rect.setAttribute('width', `${w}`);
        badge.rect.setAttribute('fill', '#FEF2F2');
        badge.rect.setAttribute('stroke', '#FCA5A5');
        badge.dot.setAttribute('cx', `${-w / 2 + 11}`);
        badge.dot.setAttribute('fill', '#EF4444');
        badge.text.setAttribute('x', '5');
        badge.text.setAttribute('fill', '#B91C1C');
        badge.text.textContent = '✕ Falha';
      } else {
        badge.group.style.display = 'none';
      }
    }

    if (group) {
      const classes = ['arestrix-svg-edge-group', `edge-state-${execState}`];
      if (isSelected) classes.push('selected');
      if (edgeData.controlPoints) classes.push('custom-bezier');
      group.setAttribute('class', classes.join(' '));
    }
  }

  private updateSvgEdgeCurve(
    edgeId: string,
    bezier: IBezierCurvePoints,
    edgeData: IEdgeData,
    isSelected: boolean = false
  ): void {
    const path = this.svgEdgePaths.get(edgeId);
    if (!path) return;

    const d = `M ${bezier.start.x} ${-bezier.start.y} C ${bezier.cp1.x} ${-bezier.cp1.y}, ${bezier.cp2.x} ${-bezier.cp2.y}, ${bezier.end.x} ${-bezier.end.y}`;
    path.setAttribute('d', d);

    const flowPath = this.svgEdgeFlowPaths.get(edgeId);
    if (flowPath) {
      flowPath.setAttribute('d', d);
    }

    const hitPath = this.svgEdgeHitPaths.get(edgeId);
    if (hitPath) {
      hitPath.setAttribute('d', d);
    }

    const badge = this.svgEdgeBadges.get(edgeId);
    if (badge) {
      badge.group.setAttribute('transform', `translate(${bezier.midPoint.x}, ${-bezier.midPoint.y})`);
    }

    const packet = this.svgEdgePackets.get(edgeId);
    if (packet) {
      packet.setAttribute('cx', `${bezier.midPoint.x}`);
      packet.setAttribute('cy', `${-bezier.midPoint.y}`);
    }

    const execState = this.connectionViews.get(edgeId)?.executionState ?? 'idle';
    this.applySvgEdgeAppearance(edgeId, edgeData, isSelected, execState);
  }

  private onNodeUpdated(nodeData: INodeData): void {
    const nodeView = this.nodeViews.get(nodeData.id);
    if (nodeView) {
      const portsOf = (n: INodeData) => JSON.stringify([n.inputs.map(p => [p.id, p.name]), n.outputs.map(p => [p.id, p.name])]);
      if (nodeView.nodeData.type !== nodeData.type || portsOf(nodeView.nodeData) !== portsOf(nodeData)) {
        const wasSelected = nodeView.isSelected;
        this.onNodeRemoved(nodeData.id);
        this.onNodeAdded(nodeData);
        if (wasSelected) this.nodeViews.get(nodeData.id)?.setSelected(true);
        this.refreshAllEdges();
      } else {
        nodeView.updateLabel(nodeData.label);
      }
    }
  }

  private onEdgeAdded(edgeData: IEdgeData): void {
    if (this.connectionViews.has(edgeData.id)) return;

    const connView = new ConnectionView(edgeData);
    this.connectionViews.set(edgeData.id, connView);

    // Conecta aresta no SVG com área de clique expandida e suporte a alças estilo Inkscape
    if (this.svgEdgesLayer && typeof document !== 'undefined') {
      const SVG_NS = 'http://www.w3.org/2000/svg';
      const edgeGroup = document.createElementNS(SVG_NS, 'g');
      edgeGroup.setAttribute('class', 'arestrix-svg-edge-group edge-state-idle');
      edgeGroup.setAttribute('data-edge-id', edgeData.id);

      const hitPath = document.createElementNS(SVG_NS, 'path');
      hitPath.setAttribute('class', 'arestrix-svg-edge-hit');
      hitPath.setAttribute('id', `svg-edge-hit-${edgeData.id}`);
      hitPath.setAttribute('stroke', 'transparent');
      hitPath.setAttribute('stroke-width', '18');
      hitPath.setAttribute('fill', 'none');
      hitPath.style.pointerEvents = 'stroke';
      hitPath.style.cursor = 'grab';

      hitPath.addEventListener('pointerdown', (e: PointerEvent) => {
        if (e.button !== 0 || e.altKey) return;
        e.stopPropagation();
        e.preventDefault();
        this.bus.emit('edge:start-curve-drag', {
          edgeId: edgeData.id,
          clientX: e.clientX,
          clientY: e.clientY
        });
      });

      hitPath.addEventListener('dblclick', (e: MouseEvent) => {
        e.stopPropagation();
        e.preventDefault();
        this.bus.emit('edge:reset-control-points', { edgeId: edgeData.id });
      });

      const path = document.createElementNS(SVG_NS, 'path');
      path.setAttribute('class', 'arestrix-svg-edge');
      path.setAttribute('id', `svg-edge-${edgeData.id}`);
      const baseStyle = getEdgeBaseStyle(edgeData);
      path.setAttribute('stroke', baseStyle.stroke);
      path.setAttribute('stroke-width', '3');
      path.setAttribute('stroke-linecap', 'round');
      path.setAttribute('fill', 'none');
      path.style.pointerEvents = 'none';
      if (baseStyle.dashArray) path.setAttribute('stroke-dasharray', baseStyle.dashArray);

      // Camada de pulso de fluxo animado ao longo da curva Bézier
      const flowPath = document.createElementNS(SVG_NS, 'path');
      flowPath.setAttribute('class', 'arestrix-svg-edge-flow');
      flowPath.setAttribute('id', `svg-edge-flow-${edgeData.id}`);
      flowPath.setAttribute('stroke', '#10B981');
      flowPath.setAttribute('stroke-width', '3.5');
      flowPath.setAttribute('stroke-linecap', 'round');
      flowPath.setAttribute('stroke-dasharray', '10 12');
      flowPath.setAttribute('fill', 'none');
      flowPath.style.pointerEvents = 'none';
      flowPath.style.display = 'none';

      // Partícula (orb) vetorial que percorre a curva Bézier continuamente durante a execução
      const packet = document.createElementNS(SVG_NS, 'circle');
      packet.setAttribute('class', 'arestrix-svg-edge-packet');
      packet.setAttribute('id', `svg-edge-packet-${edgeData.id}`);
      packet.setAttribute('r', '5.5');
      packet.setAttribute('fill', '#10B981');
      packet.setAttribute('stroke', '#FFFFFF');
      packet.setAttribute('stroke-width', '2');
      packet.style.pointerEvents = 'none';
      packet.style.display = 'none';

      // Pílula de informação limpa no ponto médio da aresta (zero text-shadow / zero blur)
      const badgeGroup = document.createElementNS(SVG_NS, 'g');
      badgeGroup.setAttribute('class', 'arestrix-svg-edge-badge');
      badgeGroup.setAttribute('id', `svg-edge-badge-${edgeData.id}`);
      badgeGroup.style.pointerEvents = 'none';
      badgeGroup.style.display = 'none';

      const badgeRect = document.createElementNS(SVG_NS, 'rect');
      badgeRect.setAttribute('class', 'arestrix-svg-edge-badge-bg');
      badgeRect.setAttribute('x', '-52');
      badgeRect.setAttribute('y', '-10');
      badgeRect.setAttribute('width', '104');
      badgeRect.setAttribute('height', '20');
      badgeRect.setAttribute('rx', '10');
      badgeRect.setAttribute('ry', '10');
      badgeRect.setAttribute('fill', '#ECFDF5');
      badgeRect.setAttribute('stroke', '#10B981');
      badgeRect.setAttribute('stroke-width', '1.3');

      const badgeDot = document.createElementNS(SVG_NS, 'circle');
      badgeDot.setAttribute('class', 'arestrix-svg-edge-badge-dot');
      badgeDot.setAttribute('cx', '-41');
      badgeDot.setAttribute('cy', '0');
      badgeDot.setAttribute('r', '3.5');
      badgeDot.setAttribute('fill', '#10B981');

      const badgeText = document.createElementNS(SVG_NS, 'text');
      badgeText.setAttribute('class', 'arestrix-svg-edge-badge-text');
      badgeText.setAttribute('x', '5');
      badgeText.setAttribute('y', '3.5');
      badgeText.setAttribute('text-anchor', 'middle');
      badgeText.setAttribute('fill', '#065F46');
      badgeText.setAttribute('font-family', 'Inter, system-ui, sans-serif');
      badgeText.setAttribute('font-size', '9.5');
      badgeText.setAttribute('font-weight', '700');
      badgeText.textContent = 'Em fluxo · 0.0s';

      badgeGroup.appendChild(badgeRect);
      badgeGroup.appendChild(badgeDot);
      badgeGroup.appendChild(badgeText);

      edgeGroup.appendChild(hitPath);
      edgeGroup.appendChild(path);
      edgeGroup.appendChild(flowPath);
      edgeGroup.appendChild(packet);
      edgeGroup.appendChild(badgeGroup);
      this.svgEdgesLayer.appendChild(edgeGroup);

      this.svgEdgeGroups.set(edgeData.id, edgeGroup);
      this.svgEdgeHitPaths.set(edgeData.id, hitPath);
      this.svgEdgePaths.set(edgeData.id, path);
      this.svgEdgeFlowPaths.set(edgeData.id, flowPath);
      this.svgEdgePackets.set(edgeData.id, packet);
      this.svgEdgeBadges.set(edgeData.id, {
        group: badgeGroup,
        rect: badgeRect,
        dot: badgeDot,
        text: badgeText
      });
    }

    // Conectar aos pontos de mundo atuais dos nós com desvio de obstáculos
    const src = this.nodeViews.get(edgeData.sourceNodeId);
    const tgt = this.nodeViews.get(edgeData.targetNodeId);
    if (src && tgt) {
      const obstacles = this.getObstacleBoxes();
      const start = src.getPortWorldPosition(edgeData.sourcePortId);
      const end = tgt.getPortWorldPosition(edgeData.targetPortId);
      const bezier = connView.updateCurve(start, end, obstacles);
      this.updateSvgEdgeCurve(edgeData.id, bezier, edgeData, connView.isSelected);
    }
  }

  private onEdgeRemoved(edgeId: string): void {
    if (this.selectedEdgeId === edgeId) {
      this.selectedEdgeId = null;
      this.clearEdgeHandles();
    }

    const connView = this.connectionViews.get(edgeId);
    if (connView) {
      this.connectionViews.delete(edgeId);
    }

    const edgeGroup = this.svgEdgeGroups.get(edgeId);
    if (edgeGroup && edgeGroup.parentNode) {
      edgeGroup.parentNode.removeChild(edgeGroup);
    }
    this.svgEdgeGroups.delete(edgeId);
    this.svgEdgeHitPaths.delete(edgeId);
    this.svgEdgePaths.delete(edgeId);
    this.svgEdgeFlowPaths.delete(edgeId);
    this.svgEdgePackets.delete(edgeId);
    this.svgEdgeBadges.delete(edgeId);
  }

  private clearAllViews(): void {
    this.selectedEdgeId = null;
    this.clearEdgeHandles();

    this.connectionViews.clear();

    this.svgEdgeGroups.forEach(g => {
      if (g.parentNode) g.parentNode.removeChild(g);
    });
    this.svgEdgeGroups.clear();
    this.svgEdgeHitPaths.clear();
    this.svgEdgePaths.clear();
    this.svgEdgeFlowPaths.clear();
    this.svgEdgePackets.clear();
    this.svgEdgeBadges.clear();

    this.nodeViews.forEach(nv => {
      nv.dispose();
    });
    this.nodeViews.clear();
  }

  private syncFromGraph(): void {
    this.clearAllViews();
    this.graph.getAllNodes().forEach(n => this.onNodeAdded(n));
    this.graph.getAllEdges().forEach(e => this.onEdgeAdded(e));
    this.refreshFlowDecorations();
    this.refreshAllEdges();
  }

  /**
   * Selos INÍCIO/FIM nos vértices onde as trilhas começam e terminam.
   *
   * Só aparecem quando a topologia é ambígua (mais de uma entrada ou mais de uma saída): é o
   * caso em que o desenho sozinho não revela que duas trilhas disjuntas rodam em paralelo. Com
   * uma entrada e uma saída o fluxo já é óbvio e o selo viraria ruído.
   */
  private refreshFlowDecorations(): void {
    const roles = computeFlowRoleMap(computeFlowRoles(this.graph));
    const assignments = this.trailHighlight
      ? computeTrailAssignments(this.graph).indexByNode
      : null;
    this.nodeViews.forEach((nodeView, id) => {
      nodeView.setFlowRole(roles.get(id) ?? null);
      nodeView.setTrailHighlight(assignments ? assignments.get(id) ?? null : null);
    });
  }

  /** Liga/desliga as cores por trilha; devolve o novo estado. */
  public toggleTrailHighlight(): boolean {
    this.trailHighlight = !this.trailHighlight;
    this.refreshFlowDecorations();
    return this.trailHighlight;
  }

  public isTrailHighlightEnabled(): boolean {
    return this.trailHighlight;
  }

  public zoomToFit(): void {
    const nodes = this.graph.getAllNodes();
    if (nodes.length === 0) {
      this.cameraController.reset();
      return;
    }

    const bounds = new THREE.Box2();
    nodes.forEach(n => {
      bounds.expandByPoint(new THREE.Vector2(n.position.x - 145, n.position.y - 80));
      bounds.expandByPoint(new THREE.Vector2(n.position.x + 145, n.position.y + 80));
    });

    this.cameraController.fitToBounds(bounds, 80);
  }

  public autoLayout(): void {
    this.graph.autoLayout(true);
  }

  public selectNode(id: string | null): void {
    this.interactionManager.selectNode(id);
  }

  public selectEdge(id: string | null): void {
    this.interactionManager.selectEdge(id);
  }

  /** Seleciona o vértice e centraliza a câmera nele (usado pela busca no projeto). */
  public focusNode(id: string): boolean {
    const node = this.graph.getNode(id);
    if (!node) return false;
    this.cameraController.centerOn(node.position.x, node.position.y);
    this.selectNode(id);
    return true;
  }

  public getCameraController(): CameraController {
    return this.cameraController;
  }

  public toggleGrid(): boolean {
    return this.grid.toggleVisible();
  }

  public isGridVisible(): boolean {
    return this.grid.isVisible();
  }

  /** Fundo atual do canvas: claro (padrão) ou preto. */
  public getCanvasBackground(): CanvasBackground {
    return this.canvasBackground;
  }

  /**
   * Configuração persistida do canvas. O valor fica em localStorage e é
   * reaplicado ao iniciar o Studio; não altera o grafo/projeto.
   */
  public setCanvasBackground(background: CanvasBackground): void {
    this.applyCanvasBackground(background);
    saveCanvasBackground(background);
    this.bus.emit('canvas:background-changed', background);
  }

  public toggleCanvasBackground(): CanvasBackground {
    const next: CanvasBackground = this.canvasBackground === 'dark' ? 'light' : 'dark';
    this.setCanvasBackground(next);
    return next;
  }

  private applyCanvasBackground(background: CanvasBackground): void {
    const palette = CANVAS_PALETTES[background];
    this.canvasBackground = background;
    this.scene.background = new THREE.Color(palette.background);
    this.grid.setTheme(palette.background, palette.gridMajor, palette.gridMinor);
    this.container.classList.toggle('canvas-bg-dark', background === 'dark');
    this.container.style.backgroundColor = palette.containerCss;
  }

  public getCameraCenter(): THREE.Vector2 {
    return new THREE.Vector2(this.camera.position.x, this.camera.position.y);
  }

  public onResize(): void {
    const width = this.container.clientWidth > 0 ? this.container.clientWidth : Math.max(window.innerWidth - 380, 800);
    const height = this.container.clientHeight > 0 ? this.container.clientHeight : Math.max(window.innerHeight - 120, 600);

    this.frustumSize = height;
    this.cameraController.setFrustumSize(height);

    this.camera.left = -width / 2;
    this.camera.right = width / 2;
    this.camera.top = height / 2;
    this.camera.bottom = -height / 2;
    this.camera.updateProjectionMatrix();

    this.renderer.setSize(width, height);

    // O rubber-band (única linha Three.js restante) precisa da nova resolução.
    this.interactionManager.setResolution(width, height);
  }

  private animateActiveEdges(): void {
    this.edgeAnimPhase = (this.edgeAnimPhase + 0.015) % 1;
    const t = this.edgeAnimPhase;
    const u = 1 - t;
    const dashOffset = -((this.edgeAnimPhase * 88) % 22);

    this.connectionViews.forEach((conn, edgeId) => {
      if (conn.executionState !== 'active') return;

      const flowPath = this.svgEdgeFlowPaths.get(edgeId);
      if (flowPath) {
        flowPath.setAttribute('stroke-dashoffset', dashOffset.toFixed(1));
      }

      const packet = this.svgEdgePackets.get(edgeId);
      const bz = conn.currentBezier;
      if (packet && bz) {
        const bx =
          u * u * u * bz.start.x +
          3 * u * u * t * bz.cp1.x +
          3 * u * t * t * bz.cp2.x +
          t * t * t * bz.end.x;
        const by =
          u * u * u * bz.start.y +
          3 * u * u * t * bz.cp1.y +
          3 * u * t * t * bz.cp2.y +
          t * t * t * bz.end.y;
        packet.setAttribute('cx', bx.toFixed(1));
        packet.setAttribute('cy', (-by).toFixed(1));
      }

      // Atualiza pílula de status/tempo em tempo real na aresta ativa
      this.applySvgEdgeAppearance(edgeId, conn.edgeData, conn.isSelected, 'active');
    });
  }

  private animate(): void {
    if (!this.isRunning) return;
    requestAnimationFrame(() => this.animate());

    const width = this.container.clientWidth || 800;
    const height = this.container.clientHeight || 600;

    // Atualiza shader do grid infinito
    this.grid.update(this.camera, width, height);

    // Anima partículas e fluxo vetorial nas arestas ativas
    this.animateActiveEdges();

    // Sincroniza matriz da camada SVG vetorial com a câmera Three.js (escala pura invariante a resize)
    if (this.svgRoot) {
      const scale = this.camera.zoom;
      const cx = width / 2 - this.camera.position.x * scale;
      const cy = height / 2 + this.camera.position.y * scale;
      this.svgRoot.setAttribute('transform', `matrix(${scale}, 0, 0, ${scale}, ${cx}, ${cy})`);
    }

    this.renderer.render(this.scene, this.camera);
  }

  dispose(): void {
    this.isRunning = false;
    window.removeEventListener('resize', this.onWindowResize);
    if (typeof document !== 'undefined') {
      document.removeEventListener(APP_THEME_CHANGED_EVENT, this.onAppThemeChanged);
    }
    this.resizeObserver?.disconnect();
    this.clearAllViews();
    this.interactionManager.dispose();
    this.cameraController.dispose();
    if (this.svgLayer?.parentNode) {
      this.svgLayer.parentNode.removeChild(this.svgLayer);
    }
    this.renderer.dispose();
  }
}
