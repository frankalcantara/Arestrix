import * as THREE from 'three';
import { CameraController } from './CameraController.ts';
import { InfiniteGrid } from './InfiniteGrid.ts';
import { NodeView } from './NodeView.ts';
import { ConnectionView } from './ConnectionView.ts';
import { InteractionManager } from './InteractionManager.ts';
import { Graph } from '../core/Graph.ts';
import { INodeData, IEdgeData } from '../core/types.ts';
import { EventBus } from '../core/EventBus.ts';
import { THEME } from './Theme.ts';

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
  private frustumSize: number = 1000;
  private resizeObserver!: ResizeObserver;

  constructor(container: HTMLElement, graph: Graph, bus: EventBus) {
    this.container = container;
    this.graph = graph;
    this.bus = bus;

    // 1. Cena Three.js
    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(THEME.canvasBackground);

    // 2. Camera Ortográfica com dimensões seguras (evita NaN se o contêiner estiver com 0px)
    const initialW = this.container.clientWidth > 0 ? this.container.clientWidth : Math.max(window.innerWidth - 380, 800);
    const initialH = this.container.clientHeight > 0 ? this.container.clientHeight : Math.max(window.innerHeight - 120, 600);
    const aspect = initialW / initialH;

    this.camera = new THREE.OrthographicCamera(
      (this.frustumSize * aspect) / -2,
      (this.frustumSize * aspect) / 2,
      this.frustumSize / 2,
      this.frustumSize / -2,
      0.1,
      3000
    );
    this.camera.position.set(0, 0, 1000);

    // 3. Renderizador WebGL
    this.renderer = new THREE.WebGLRenderer({ antialias: true, powerPreference: 'high-performance' });
    this.renderer.setSize(initialW, initialH);
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.container.appendChild(this.renderer.domElement);

    // 4. Grid Infinito e Controlador de Câmera
    this.grid = new InfiniteGrid(this.scene);
    this.cameraController = new CameraController(this.camera, this.renderer.domElement, this.frustumSize);

    // 5. Observador de Redimensionamento do DOM para sincronizar automaticamente
    if (typeof ResizeObserver !== 'undefined') {
      this.resizeObserver = new ResizeObserver(() => {
        this.onResize();
      });
      this.resizeObserver.observe(this.container);
    }

    // 5. Gerenciador de Interações e Raycasting
    this.interactionManager = new InteractionManager(
      this.camera,
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
    window.addEventListener('resize', this.onResize.bind(this));

    // 8. Inicia loop de renderização
    this.animate();
  }

  private bindGraphEvents(): void {
    this.bus.on('node:added', (node: INodeData) => this.onNodeAdded(node));
    this.bus.on('node:removed', ({ id }: { id: string }) => this.onNodeRemoved(id));
    this.bus.on('node:updated', (node: INodeData) => this.onNodeUpdated(node));
    this.bus.on('node:moved', ({ id, position }: { id: string; position: { x: number; y: number } }) => this.onNodeMoved(id, position));
    this.bus.on('edge:added', (edge: IEdgeData) => this.onEdgeAdded(edge));
    this.bus.on('edge:removed', (edge: IEdgeData) => this.onEdgeRemoved(edge.id));
    this.bus.on('graph:cleared', () => this.clearAllViews());
    this.bus.on('graph:loaded', () => this.syncFromGraph());
    this.bus.on('telemetry:task-enter', (data: { taskId: string; worker?: number }) => this.onTelemetryEnter(data));
    this.bus.on('telemetry:task-exit', (data: { taskId: string; worker?: number }) => this.onTelemetryExit(data));
    this.bus.on('telemetry:reset', () => this.onTelemetryReset());
  }

  private onTelemetryEnter(data: { taskId: string; worker?: number }): void {
    const nv = this.findNodeView(data.taskId);
    if (nv) {
      nv.setExecutionState('running');
    }
  }

  private onTelemetryExit(data: { taskId: string; worker?: number }): void {
    const nv = this.findNodeView(data.taskId);
    if (nv) {
      nv.setExecutionState('completed');
    }
  }

  private findNodeView(key: string): NodeView | undefined {
    let nv = this.nodeViews.get(key);
    if (!nv) {
      for (const view of this.nodeViews.values()) {
        if (view.nodeData.label === key || view.nodeData.id === key) {
          return view;
        }
      }
    }
    return nv;
  }

  private onTelemetryReset(): void {
    this.nodeViews.forEach(nv => nv.setExecutionState('idle'));
  }

  private onNodeAdded(nodeData: INodeData): void {
    if (this.nodeViews.has(nodeData.id)) return;

    const nodeView = new NodeView(nodeData);
    this.scene.add(nodeView.group);
    this.nodeViews.set(nodeData.id, nodeView);
  }

  private onNodeRemoved(nodeId: string): void {
    const nodeView = this.nodeViews.get(nodeId);
    if (nodeView) {
      this.scene.remove(nodeView.group);
      nodeView.dispose();
      this.nodeViews.delete(nodeId);
    }
  }

  private onNodeMoved(id: string, position: { x: number; y: number }): void {
    const nodeView = this.nodeViews.get(id);
    if (nodeView) {
      nodeView.updatePosition(position.x, position.y);
      this.updateConnectedEdgesForNode(id);
    }
  }

  public updateConnectedEdgesForNode(nodeId: string): void {
    const edges = this.graph.getEdgesForNode(nodeId);
    edges.forEach(edge => {
      const conn = this.connectionViews.get(edge.id);
      const src = this.nodeViews.get(edge.sourceNodeId);
      const tgt = this.nodeViews.get(edge.targetNodeId);
      if (conn && src && tgt) {
        const start = src.getPortWorldPosition(edge.sourcePortId);
        const end = tgt.getPortWorldPosition(edge.targetPortId);
        conn.updateCurve(start, end);
      }
    });
  }

  private onNodeUpdated(nodeData: INodeData): void {
    const nodeView = this.nodeViews.get(nodeData.id);
    if (nodeView) {
      if (nodeView.nodeData.type !== nodeData.type) {
        this.onNodeRemoved(nodeData.id);
        this.onNodeAdded(nodeData);
      } else {
        nodeView.updateLabel(nodeData.label);
      }
    }
  }

  private onEdgeAdded(edgeData: IEdgeData): void {
    if (this.connectionViews.has(edgeData.id)) return;

    const connView = new ConnectionView(
      edgeData,
      this.container.clientWidth,
      this.container.clientHeight
    );
    this.scene.add(connView.line);
    this.connectionViews.set(edgeData.id, connView);

    // Conectar aos pontos de mundo atuais dos nós
    const src = this.nodeViews.get(edgeData.sourceNodeId);
    const tgt = this.nodeViews.get(edgeData.targetNodeId);
    if (src && tgt) {
      const start = src.getPortWorldPosition(edgeData.sourcePortId);
      const end = tgt.getPortWorldPosition(edgeData.targetPortId);
      connView.updateCurve(start, end);
    }
  }

  private onEdgeRemoved(edgeId: string): void {
    const connView = this.connectionViews.get(edgeId);
    if (connView) {
      this.scene.remove(connView.line);
      connView.dispose();
      this.connectionViews.delete(edgeId);
    }
  }

  private clearAllViews(): void {
    this.connectionViews.forEach(cv => {
      this.scene.remove(cv.line);
      cv.dispose();
    });
    this.connectionViews.clear();

    this.nodeViews.forEach(nv => {
      this.scene.remove(nv.group);
      nv.dispose();
    });
    this.nodeViews.clear();
  }

  private syncFromGraph(): void {
    this.clearAllViews();
    this.graph.getAllNodes().forEach(n => this.onNodeAdded(n));
    this.graph.getAllEdges().forEach(e => this.onEdgeAdded(e));
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

  public selectNode(id: string | null): void {
    this.interactionManager.selectNode(id);
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

  public getCameraCenter(): THREE.Vector2 {
    return new THREE.Vector2(this.camera.position.x, this.camera.position.y);
  }

  public onResize(): void {
    const width = this.container.clientWidth > 0 ? this.container.clientWidth : Math.max(window.innerWidth - 380, 800);
    const height = this.container.clientHeight > 0 ? this.container.clientHeight : Math.max(window.innerHeight - 120, 600);

    const aspect = width / height;
    this.camera.left = (this.frustumSize * aspect) / -2;
    this.camera.right = (this.frustumSize * aspect) / 2;
    this.camera.top = this.frustumSize / 2;
    this.camera.bottom = this.frustumSize / -2;
    this.camera.updateProjectionMatrix();

    this.renderer.setSize(width, height);

    // Atualiza resolucao de todos os materiais Line2
    this.connectionViews.forEach(cv => cv.setResolution(width, height));
    this.interactionManager.setResolution(width, height);
  }

  private animate(): void {
    if (!this.isRunning) return;
    requestAnimationFrame(() => this.animate());

    const width = this.container.clientWidth || 800;
    const height = this.container.clientHeight || 600;

    // Atualiza shader do grid infinito
    this.grid.update(this.camera, width, height);

    this.renderer.render(this.scene, this.camera);
  }

  dispose(): void {
    this.isRunning = false;
    window.removeEventListener('resize', this.onResize);
    this.resizeObserver?.disconnect();
    this.clearAllViews();
    this.renderer.dispose();
  }
}
