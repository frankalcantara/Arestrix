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

  constructor(container: HTMLElement, graph: Graph, bus: EventBus) {
    this.container = container;
    this.graph = graph;
    this.bus = bus;

    // 1. Cena Three.js
    this.scene = new THREE.Scene();
    this.scene.background = new THREE.Color(THEME.canvasBackground);

    // 2. Camera Ortográfica
    const aspect = this.container.clientWidth / this.container.clientHeight;
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
    this.renderer.setSize(this.container.clientWidth, this.container.clientHeight);
    this.renderer.setPixelRatio(Math.min(window.devicePixelRatio, 2));
    this.container.appendChild(this.renderer.domElement);

    // 4. Grid Infinito e Controlador de Câmera
    this.grid = new InfiniteGrid(this.scene);
    this.cameraController = new CameraController(this.camera, this.renderer.domElement, this.frustumSize);

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
    this.bus.on('edge:added', (edge: IEdgeData) => this.onEdgeAdded(edge));
    this.bus.on('edge:removed', (edge: IEdgeData) => this.onEdgeRemoved(edge.id));
    this.bus.on('graph:cleared', () => this.clearAllViews());
    this.bus.on('graph:loaded', () => this.syncFromGraph());
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

  private onNodeUpdated(nodeData: INodeData): void {
    const nodeView = this.nodeViews.get(nodeData.id);
    if (nodeView) {
      nodeView.updateLabel(nodeData.label);
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
      bounds.expandByPoint(new THREE.Vector2(n.position.x - 120, n.position.y - 70));
      bounds.expandByPoint(new THREE.Vector2(n.position.x + 120, n.position.y + 70));
    });

    this.cameraController.fitToBounds(bounds, 120);
  }

  private onResize(): void {
    const width = this.container.clientWidth;
    const height = this.container.clientHeight;

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

    const width = this.container.clientWidth;
    const height = this.container.clientHeight;

    // Atualiza shader do grid infinito
    this.grid.update(this.camera, width, height);

    this.renderer.render(this.scene, this.camera);
  }

  dispose(): void {
    this.isRunning = false;
    window.removeEventListener('resize', this.onResize);
    this.clearAllViews();
    this.renderer.dispose();
  }
}
