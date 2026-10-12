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
import { 
  MoveNodeCommand, 
  AddEdgeCommand, 
  RemoveNodeCommand, 
  RemoveEdgeCommand 
} from '../core/history/GraphCommands.ts';
import { getNodeAction, NodeActionType } from './NodeActions.ts';
import { CanvasTooltip } from '../ui/CanvasTooltip.ts';

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
  private dragStartPos: { x: number; y: number } | null = null;
  private dragOffset: THREE.Vector3 = new THREE.Vector3();
  private selectedNodeId: string | null = null;
  private selectedEdgeId: string | null = null;

  // Estado de Criacao de Conexao (Rubber-band)
  private isConnecting: boolean = false;
  private connectingSourceNodeId: string | null = null;
  private connectingSourcePortId: string | null = null;
  private rubberBandLine: Line2 | null = null;

  // Fase 15.2: hover com tooltip e destaque de botões de ação
  private tooltip: CanvasTooltip = new CanvasTooltip(350);
  private hoveredActionNode: NodeView | null = null;

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
    this.domElement.addEventListener('dblclick', this.onDoubleClick.bind(this));
    this.domElement.addEventListener('pointercancel', () => {
      if (this.cameraController.getIsPanning()) {
        this.cameraController.stopPan();
      }
    });
    window.addEventListener('pointermove', this.onPointerMove.bind(this));
    window.addEventListener('pointerup', this.onPointerUp.bind(this));
    window.addEventListener('keydown', this.onKeyDown.bind(this));

    // Tooltip: oculta em zoom (wheel) e ao sair do canvas
    this.domElement.addEventListener('wheel', () => this.clearHover(), { passive: true });
    this.domElement.addEventListener('pointerleave', () => this.clearHover());

    globalEventBus.on('node:removed', ({ id }: { id: string }) => {
      if (this.selectedNodeId === id) {
        this.selectNode(null);
      }
    });

    globalEventBus.on('graph:cleared', () => {
      this.selectNode(null);
      this.selectEdge(null);
    });

    // Ponte de Eventos da Camada Vetorial SVG
    globalEventBus.on('node:start-drag', ({ nodeId, clientX, clientY }: any) => this.startDragNode(nodeId, clientX, clientY));
    globalEventBus.on('node:start-connecting', ({ nodeId, portId, clientX, clientY }: any) => this.startConnectingPort(nodeId, portId, clientX, clientY));
    globalEventBus.on('node:complete-connecting', ({ nodeId, portId }: any) => this.completeConnectingPort(nodeId, portId));
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

    // 0. Checa clique em Botões de Ação do Nó (Fase 15.2: catálogo com 5 ações)
    const actionTargets: THREE.Object3D[] = [];
    this.nodeViews.forEach(nv => {
      if (nv.group.visible && nv.isDetailVisible()) {
        nv.group.traverseVisible(obj => {
          if (obj.userData?.isActionHitbox) {
            actionTargets.push(obj);
          }
        });
      }
    });

    const actionIntersects = this.raycaster.intersectObjects(actionTargets, false);
    if (actionIntersects.length > 0) {
      const hit = actionIntersects[0].object;
      const { nodeId, actionType } = hit.userData;
      this.clearHover();
      this.selectNode(nodeId);
      const actionDef = getNodeAction(actionType);
      if (actionDef) {
        globalEventBus.emit(actionDef.event, nodeId);
      }
      return;
    }

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
        this.dragStartPos = { x: nv.nodeData.position.x, y: nv.nodeData.position.y };
        this.dragOffset.set(
          nv.group.position.x - worldCursor.x,
          nv.group.position.y - worldCursor.y,
          0
        );
        return;
      }
    }

    // 3. Clique no vazio -> desselecionar e iniciar Pan do Canvas
    e.preventDefault();
    this.selectNode(null);
    this.selectEdge(null);
    this.cameraController.startPan(e.clientX, e.clientY);
    try {
      this.domElement.setPointerCapture(e.pointerId);
    } catch {
      // Ignora erro se ambiente de teste não suportar pointer capture
    }
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
      return;
    }

    // C. Cursor Hover feedback & Tooltip (Fase 15.2)
    this.raycaster.setFromCamera(this.pointer, this.camera);
    const interactiveTargets: THREE.Object3D[] = [];
    this.nodeViews.forEach(nv => {
      if (nv.group.visible) {
        nv.group.traverseVisible(obj => {
          if (
            obj.userData?.isActionHitbox ||
            obj.userData?.isPortHitbox ||
            (obj.userData?.isBody && obj.userData?.tooltip)
          ) {
            interactiveTargets.push(obj);
          }
        });
      }
    });

    const hits = this.raycaster.intersectObjects(interactiveTargets, false);
    if (hits.length > 0) {
      this.domElement.style.cursor = 'pointer';
      const hit = hits[0].object;
      const u = hit.userData;

      // Se for ação da barra, destaca o botão
      if (u.isActionHitbox) {
        const nv = this.nodeViews.get(u.nodeId);
        if (nv && this.hoveredActionNode !== nv) {
          if (this.hoveredActionNode) this.hoveredActionNode.setHoveredAction(null);
          this.hoveredActionNode = nv;
        }
        nv?.setHoveredAction(u.actionType as NodeActionType);
      } else if (this.hoveredActionNode) {
        this.hoveredActionNode.setHoveredAction(null);
        this.hoveredActionNode = null;
      }

      // Exibe tooltip com rótulo e descrição
      if (u.tooltip) {
        const worldPos = new THREE.Vector3();
        hit.getWorldPosition(worldPos);
        const screenPos = this.cameraController.worldToScreen(worldPos);
        const key = `${u.nodeId || ''}_${u.actionType || u.portId || 'body'}`;
        this.tooltip.show(
          key,
          { title: u.tooltip, description: u.tooltipDescription },
          screenPos.x,
          screenPos.y
        );
      } else {
        this.tooltip.hide();
      }
    } else {
      this.domElement.style.cursor = 'default';
      this.clearHover();
    }
  }

  private clearHover(): void {
    if (this.hoveredActionNode) {
      this.hoveredActionNode.setHoveredAction(null);
      this.hoveredActionNode = null;
    }
    this.tooltip.hide();
  }

  private onPointerUp(e: PointerEvent): void {
    if (this.cameraController.getIsPanning()) {
      this.cameraController.stopPan();
      try {
        this.domElement.releasePointerCapture(e.pointerId);
      } catch {
        // Ignora erro se ambiente de teste não suportar
      }
    }

    if (this.draggedNodeView) {
      const finalX = this.draggedNodeView.nodeData.position.x;
      const finalY = this.draggedNodeView.nodeData.position.y;
      if (this.dragStartPos && (finalX !== this.dragStartPos.x || finalY !== this.dragStartPos.y)) {
        const cmd = new MoveNodeCommand(
          this.graph,
          this.draggedNodeView.nodeData.id,
          this.dragStartPos,
          { x: finalX, y: finalY }
        );
        this.graph.commandManager.record(cmd);
      }
      this.draggedNodeView = null;
      this.dragStartPos = null;
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
            const edgeId = `edge_${Date.now()}_${Math.random().toString(36).substr(2, 4)}`;
            const edgeData = {
              id: edgeId,
              sourceNodeId: this.connectingSourceNodeId,
              sourcePortId: this.connectingSourcePortId,
              targetNodeId,
              targetPortId,
              type: 'flow' as const
            };
            const cmd = new AddEdgeCommand(this.graph, edgeData);
            this.graph.commandManager.execute(cmd);
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
    const isEditingText = ['INPUT', 'TEXTAREA'].includes((e.target as HTMLElement).tagName);

    // Ctrl+Z (Desfazer / Undo)
    if ((e.ctrlKey || e.metaKey) && (e.key === 'z' || e.key === 'Z') && !e.shiftKey) {
      if (!isEditingText) {
        e.preventDefault();
        this.graph.undo();
        return;
      }
    }

    // Ctrl+Y ou Ctrl+Shift+Z (Refazer / Redo)
    if (((e.ctrlKey || e.metaKey) && (e.key === 'y' || e.key === 'Y')) ||
        ((e.ctrlKey || e.metaKey) && e.shiftKey && (e.key === 'z' || e.key === 'Z'))) {
      if (!isEditingText) {
        e.preventDefault();
        this.graph.redo();
        return;
      }
    }

    // Tecla Delete ou Backspace para remover nó ou aresta selecionada
    if ((e.key === 'Delete' || e.key === 'Backspace') && !isEditingText) {
      if (this.selectedNodeId) {
        const cmd = new RemoveNodeCommand(this.graph, this.selectedNodeId);
        this.graph.commandManager.execute(cmd);
        this.selectNode(null);
      } else if (this.selectedEdgeId) {
        const cmd = new RemoveEdgeCommand(this.graph, this.selectedEdgeId);
        this.graph.commandManager.execute(cmd);
        this.selectEdge(null);
      }
    }

    // Tecla Escape para voltar ao nível pai se estiver dentro de um subflow
    if (e.key === 'Escape') {
      if (!this.graph.isAtRoot()) {
        this.graph.navigateUp();
      }
    }
  }

  private onDoubleClick(e: MouseEvent): void {
    if (e.button !== 0) return;
    this.updatePointer(e as any);
    this.raycaster.setFromCamera(this.pointer, this.camera);

    const nodeTargets: THREE.Object3D[] = [];
    this.nodeViews.forEach(nv => {
      nv.group.traverse(obj => {
        if (obj.userData?.isBody) {
          nodeTargets.push(obj);
        }
      });
    });

    const intersects = this.raycaster.intersectObjects(nodeTargets, false);
    if (intersects.length > 0) {
      const hitNodeId = intersects[0].object.userData.nodeId;
      const nodeData = this.graph.getNode(hitNodeId);
      if (nodeData && (nodeData.type === 'subflow' || nodeData.type === 'module')) {
        this.graph.diveIntoSubflow(hitNodeId);
      }
    }
  }

  public startDragNode(nodeId: string, clientX: number, clientY: number): void {
    const nv = this.nodeViews.get(nodeId);
    if (!nv) return;
    const worldCursor = this.cameraController.screenToWorld(clientX, clientY);
    this.selectNode(nodeId);
    this.draggedNodeView = nv;
    this.dragStartPos = { x: nv.nodeData.position.x, y: nv.nodeData.position.y };
    this.dragOffset.set(
      nv.group.position.x - worldCursor.x,
      nv.group.position.y - worldCursor.y,
      0
    );
  }

  public startConnectingPort(nodeId: string, portId: string, clientX: number, clientY: number): void {
    const worldCursor = this.cameraController.screenToWorld(clientX, clientY);
    this.isConnecting = true;
    this.connectingSourceNodeId = nodeId;
    this.connectingSourcePortId = portId;
    if (this.rubberBandLine) this.rubberBandLine.visible = true;
    const nv = this.nodeViews.get(nodeId);
    if (nv) {
      const startPos = nv.getPortWorldPosition(portId);
      this.updateRubberBand(startPos, worldCursor);
    }
  }

  public completeConnectingPort(targetNodeId: string, targetPortId: string): void {
    if (!this.isConnecting || !this.connectingSourceNodeId || !this.connectingSourcePortId) return;
    if (this.connectingSourceNodeId === targetNodeId) {
      this.cancelConnecting();
      return;
    }
    const edgeId = `edge_${Date.now()}`;
    const cmd = new AddEdgeCommand(this.graph, {
      id: edgeId,
      sourceNodeId: this.connectingSourceNodeId,
      sourcePortId: this.connectingSourcePortId,
      targetNodeId,
      targetPortId
    });
    this.graph.commandManager.record(cmd);
    this.cancelConnecting();
  }

  public cancelConnecting(): void {
    this.isConnecting = false;
    this.connectingSourceNodeId = null;
    this.connectingSourcePortId = null;
    if (this.rubberBandLine) this.rubberBandLine.visible = false;
  }

  setResolution(w: number, h: number): void {
    (this.rubberBandLine?.material as LineMaterial)?.resolution.set(w, h);
  }

  getTooltip(): CanvasTooltip {
    return this.tooltip;
  }

  dispose(): void {
    this.clearHover();
    this.tooltip.dispose();
  }
}
