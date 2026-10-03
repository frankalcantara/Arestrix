import * as THREE from 'three';
import { CameraController } from './CameraController.ts';
import { NodeView } from './NodeView.ts';
import { ConnectionView } from './ConnectionView.ts';
import { Graph } from '../core/Graph.ts';
import { Line2 } from 'three/examples/jsm/lines/Line2.js';
import { LineGeometry } from 'three/examples/jsm/lines/LineGeometry.js';
import { LineMaterial } from 'three/examples/jsm/lines/LineMaterial.js';
import { THEME } from './Theme.ts';
import { globalEventBus } from '../core/EventBus.ts';

export class InteractionManager {
  private camera: THREE.OrthographicCamera;
  private cameraController: CameraController;
  private domElement: HTMLElement;
  private scene: THREE.Scene;
  private graph: Graph;

  private nodeViews: Map<string, NodeView>;
  private connectionViews: Map<string, ConnectionView>;

  private raycaster: THREE.Raycaster = new THREE.Raycaster();
  private pointer: THREE.Vector2 = new THREE.Vector2();

  // Estados de Interacao
  private draggedNodeView: NodeView | null = null;
  private dragOffset: THREE.Vector3 = new THREE.Vector3();
  private selectedNodeId: string | null = null;
  private selectedEdgeId: string | null = null;

  // Estado de Criacao de Conexao (Rubber-band)
  private isConnecting: boolean = false;
  private connectingSourceNodeId: string | null = null;
  private connectingSourcePortId: string | null = null;
  private rubberBandLine: Line2 | null = null;

  constructor(
    camera: THREE.OrthographicCamera,
    cameraController: CameraController,
    domElement: HTMLElement,
    scene: THREE.Scene,
    graph: Graph,
    nodeViews: Map<string, NodeView>,
    connectionViews: Map<string, ConnectionView>
  ) {
    this.camera = camera;
    this.cameraController = cameraController;
    this.domElement = domElement;
    this.scene = scene;
    this.graph = graph;
    this.nodeViews = nodeViews;
    this.connectionViews = connectionViews;

    this.initRubberBand();
    this.bindEvents();
  }

  private initRubberBand(): void {
    const geo = new LineGeometry();
    geo.setPositions([0, 0, 0, 0, 0, 0]);

    const mat = new LineMaterial({
      color: THEME.edge.selectedColor,
      linewidth: THEME.edge.lineWidth + 1,
      resolution: new THREE.Vector2(this.domElement.clientWidth, this.domElement.clientHeight),
      dashed: true,
      dashScale: 0.1,
      dashSize: 8,
      gapSize: 4
    });

    this.rubberBandLine = new Line2(geo, mat);
    this.rubberBandLine.visible = false;
    this.rubberBandLine.renderOrder = 999;
    this.scene.add(this.rubberBandLine);
  }

  private bindEvents(): void {
    this.domElement.addEventListener('pointerdown', this.onPointerDown.bind(this));
    window.addEventListener('pointermove', this.onPointerMove.bind(this));
    window.addEventListener('pointerup', this.onPointerUp.bind(this));
    window.addEventListener('keydown', this.onKeyDown.bind(this));
  }

  private updatePointer(e: PointerEvent): void {
    const rect = this.domElement.getBoundingClientRect();
    this.pointer.x = ((e.clientX - rect.left) / rect.width) * 2 - 1;
    this.pointer.y = -((e.clientY - rect.top) / rect.height) * 2 + 1;
  }

  private onPointerDown(e: PointerEvent): void {
    if (e.button !== 0 || this.cameraController.getIsPanning() || e.altKey) return;

    this.updatePointer(e);
    this.raycaster.setFromCamera(this.pointer, this.camera);

    const worldCursor = this.cameraController.screenToWorld(e.clientX, e.clientY);

    // 1. Checa clique em Portas (Sockets)
    const portTargets: THREE.Object3D[] = [];
    this.nodeViews.forEach(nv => {
      nv.group.traverse(obj => {
        if (obj.userData?.isPortHitbox) {
          portTargets.push(obj);
        }
      });
    });

    const portIntersects = this.raycaster.intersectObjects(portTargets, false);
    if (portIntersects.length > 0) {
      const hit = portIntersects[0].object;
      const { nodeId, portId, direction } = hit.userData;

      if (direction === 'output') {
        // Inicia rubber-banding a partir da porta de saida
        this.isConnecting = true;
        this.connectingSourceNodeId = nodeId;
        this.connectingSourcePortId = portId;
        this.rubberBandLine!.visible = true;

        const startPos = this.nodeViews.get(nodeId)!.getPortWorldPosition(portId);
        this.updateRubberBand(startPos, worldCursor);
        return;
      }
    }

    // 2. Checa clique no Corpo de Nós
    const nodeTargets: THREE.Object3D[] = [];
    this.nodeViews.forEach(nv => {
      nv.group.traverse(obj => {
        if (obj.userData?.isBody) {
          nodeTargets.push(obj);
        }
      });
    });

    const nodeIntersects = this.raycaster.intersectObjects(nodeTargets, false);
    if (nodeIntersects.length > 0) {
      const hitNodeId = nodeIntersects[0].object.userData.nodeId;
      const nv = this.nodeViews.get(hitNodeId);
      if (nv) {
        this.selectNode(hitNodeId);
        this.draggedNodeView = nv;
        this.dragOffset.set(
          nv.group.position.x - worldCursor.x,
          nv.group.position.y - worldCursor.y,
          0
        );
        return;
      }
    }

    // 3. Clique no vazio -> desselecionar
    this.selectNode(null);
    this.selectEdge(null);
  }

  private onPointerMove(e: PointerEvent): void {
    if (this.cameraController.getIsPanning()) return;

    this.updatePointer(e);
    const worldCursor = this.cameraController.screenToWorld(e.clientX, e.clientY);

    // A. Drag do Nó
    if (this.draggedNodeView) {
      const newX = worldCursor.x + this.dragOffset.x;
      const newY = worldCursor.y + this.dragOffset.y;

      // Snap sutil a grade de 10px se Shift nao estiver pressionado
      const snapX = e.shiftKey ? newX : Math.round(newX / 10) * 10;
      const snapY = e.shiftKey ? newY : Math.round(newY / 10) * 10;

      this.draggedNodeView.updatePosition(snapX, snapY);
      this.graph.updateNodePosition(this.draggedNodeView.nodeData.id, snapX, snapY);
      this.updateConnectedEdges(this.draggedNodeView.nodeData.id);
      return;
    }

    // B. Conexao Rubber-band
    if (this.isConnecting && this.connectingSourceNodeId && this.connectingSourcePortId) {
      const srcNv = this.nodeViews.get(this.connectingSourceNodeId);
      if (srcNv) {
        const startPos = srcNv.getPortWorldPosition(this.connectingSourcePortId);
        this.updateRubberBand(startPos, worldCursor);
      }
    }
  }

  private onPointerUp(e: PointerEvent): void {
    if (this.draggedNodeView) {
      this.draggedNodeView = null;
    }

    if (this.isConnecting) {
      this.updatePointer(e);
      this.raycaster.setFromCamera(this.pointer, this.camera);

      const portTargets: THREE.Object3D[] = [];
      this.nodeViews.forEach(nv => {
        nv.group.traverse(obj => {
          if (obj.userData?.isPortHitbox) {
            portTargets.push(obj);
          }
        });
      });

      const portIntersects = this.raycaster.intersectObjects(portTargets, false);
      if (portIntersects.length > 0) {
        const hit = portIntersects[0].object;
        const { nodeId: targetNodeId, portId: targetPortId, direction } = hit.userData;

        // Conectar apenas se o destino for porta de entrada e nó diferente (ou loopback suportado)
        if (direction === 'input' && this.connectingSourceNodeId && this.connectingSourcePortId) {
          try {
            this.graph.createEdge(
              this.connectingSourceNodeId,
              this.connectingSourcePortId,
              targetNodeId,
              targetPortId
            );
          } catch (err) {
            console.warn('Falha ao conectar nós:', err);
          }
        }
      }

      // Finaliza estado de conexao
      this.isConnecting = false;
      this.connectingSourceNodeId = null;
      this.connectingSourcePortId = null;
      this.rubberBandLine!.visible = false;
    }
  }

  private updateRubberBand(start: THREE.Vector3, end: THREE.Vector3): void {
    const dx = end.x - start.x;
    const cp1 = new THREE.Vector3(start.x + Math.max(dx * 0.45, 40), start.y, 0);
    const cp2 = new THREE.Vector3(end.x - Math.max(dx * 0.45, 40), end.y, 0);

    const curve = new THREE.CubicBezierCurve3(start, cp1, cp2, end);
    const points = curve.getPoints(24);
    const pos: number[] = [];
    points.forEach(p => pos.push(p.x, p.y, p.z));

    this.rubberBandLine!.geometry.setPositions(pos);
    this.rubberBandLine!.computeLineDistances();
  }

  private updateConnectedEdges(nodeId: string): void {
    this.connectionViews.forEach(cv => {
      if (cv.edgeData.sourceNodeId === nodeId || cv.edgeData.targetNodeId === nodeId) {
        const srcNv = this.nodeViews.get(cv.edgeData.sourceNodeId);
        const tgtNv = this.nodeViews.get(cv.edgeData.targetNodeId);

        if (srcNv && tgtNv) {
          const start = srcNv.getPortWorldPosition(cv.edgeData.sourcePortId);
          const end = tgtNv.getPortWorldPosition(cv.edgeData.targetPortId);
          cv.updateCurve(start, end);
        }
      }
    });
  }

  selectNode(nodeId: string | null): void {
    if (this.selectedNodeId && this.selectedNodeId !== nodeId) {
      this.nodeViews.get(this.selectedNodeId)?.setSelected(false);
    }

    this.selectedNodeId = nodeId;

    if (nodeId) {
      const nv = this.nodeViews.get(nodeId);
      nv?.setSelected(true);
      globalEventBus.emit('node:selected', nv?.nodeData);
    } else {
      globalEventBus.emit('node:deselected');
    }
  }

  selectEdge(edgeId: string | null): void {
    if (this.selectedEdgeId && this.selectedEdgeId !== edgeId) {
      this.connectionViews.get(this.selectedEdgeId)?.setSelected(false);
    }

    this.selectedEdgeId = edgeId;

    if (edgeId) {
      this.connectionViews.get(edgeId)?.setSelected(true);
    }
  }

  private onKeyDown(e: KeyboardEvent): void {
    // Tecla Delete ou Backspace para remover nó selecionado
    if ((e.key === 'Delete' || e.key === 'Backspace') && !['INPUT', 'TEXTAREA'].includes((e.target as HTMLElement).tagName)) {
      if (this.selectedNodeId) {
        this.graph.removeNode(this.selectedNodeId);
        this.selectNode(null);
      }
    }
  }

  setResolution(w: number, h: number): void {
    (this.rubberBandLine?.material as LineMaterial)?.resolution.set(w, h);
  }
}
