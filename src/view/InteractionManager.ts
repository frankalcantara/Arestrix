import * as THREE from 'three';
import { CameraController } from './CameraController.ts';
import { NodeView } from './NodeView.ts';
import { ConnectionView } from './ConnectionView.ts';
import { Graph } from '../core/Graph.ts';
import { IEdgeControlPoints, IEdgeData, defaultCallSettings } from '../core/types.ts';
import { Line2 } from 'three/examples/jsm/lines/Line2.js';
import { LineGeometry } from 'three/examples/jsm/lines/LineGeometry.js';
import { LineMaterial } from 'three/examples/jsm/lines/LineMaterial.js';
import { THEME } from './Theme.ts';
import { globalEventBus } from '../core/EventBus.ts';
import { 
  MoveNodeCommand, 
  AddEdgeCommand, 
  RemoveNodeCommand, 
  RemoveEdgeCommand,
  UpdateEdgeCommand
} from '../core/history/GraphCommands.ts';
import { CanvasTooltip } from '../ui/CanvasTooltip.ts';
import {
  computeControlOffsetsFromCurveDrag,
  findClosestBezierParameter,
  IBezierCurvePoints,
  IObstacleBox
} from './BezierEdgeRouter.ts';

interface IBezierDragState {
  edgeId: string;
  mode: 'cp1' | 'cp2' | 'mid' | 'curve';
  t: number;
  initialCurve: IBezierCurvePoints;
  initialControlPoints: IEdgeControlPoints | undefined;
  startWorld: { x: number; y: number };
  hasMoved: boolean;
}

/**
 * Distância mínima (em pixels de tela) que o ponteiro precisa percorrer para um
 * pointerdown virar arraste. Abaixo disso o gesto é tratado como clique, evitando
 * que o tremor natural da mão mova o vértice e crie entradas de undo acidentais.
 */
const DRAG_THRESHOLD_PX = 4;

export class InteractionManager {
  private cameraController: CameraController;
  private domElement: HTMLElement;
  private scene: THREE.Scene;
  private graph: Graph;

  private nodeViews: Map<string, NodeView>;
  private connectionViews: Map<string, ConnectionView>;

  // Estados de Interacao
  private draggedNodeView: NodeView | null = null;
  private dragStartPos: { x: number; y: number } | null = null;
  private dragStartClient: { x: number; y: number } | null = null;
  private dragActive: boolean = false;
  private dragOffset: THREE.Vector3 = new THREE.Vector3();
  private selectedNodeId: string | null = null;
  private selectedEdgeId: string | null = null;
  private bezierDragState: IBezierDragState | null = null;

  // Canceladores de listeners registrados (evita handlers órfãos após dispose)
  private disposers: Array<() => void> = [];

  // Estado de Criacao de Conexao (Rubber-band)
  private isConnecting: boolean = false;
  private connectingSourceNodeId: string | null = null;
  private connectingSourcePortId: string | null = null;
  private rubberBandLine: Line2 | null = null;

  // Fase 15.2: hover com tooltip (a camada vetorial fornece os alvos)
  private tooltip: CanvasTooltip = new CanvasTooltip(120);

  constructor(
    cameraController: CameraController,
    domElement: HTMLElement,
    scene: THREE.Scene,
    graph: Graph,
    nodeViews: Map<string, NodeView>,
    connectionViews: Map<string, ConnectionView>
  ) {
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
    this.listen(this.domElement, 'pointerdown', (e: PointerEvent) => this.onPointerDown(e));
    this.listen(this.domElement, 'pointercancel', () => {
      if (this.cameraController.getIsPanning()) {
        this.cameraController.stopPan();
      }
      this.finishNodeDrag();
      this.finishBezierDrag();
      this.cancelConnecting();
    });
    this.listen(window, 'pointermove', (e: PointerEvent) => this.onPointerMove(e));
    this.listen(window, 'pointerup', (e: PointerEvent) => this.onPointerUp(e));
    this.listen(window, 'keydown', (e: KeyboardEvent) => this.onKeyDown(e));

    // Tooltip: oculta em zoom (wheel) e ao sair do canvas
    this.listen(this.domElement, 'wheel', () => this.clearHover(), { passive: true });
    this.listen(this.domElement, 'pointerleave', () => this.clearHover());

    this.busOn('node:removed', ({ id }: { id: string }) => {
      if (this.selectedNodeId === id) {
        this.selectNode(null);
      }
    });

    this.busOn('edge:removed', (edge: { id: string }) => {
      if (this.selectedEdgeId === edge.id) {
        this.selectEdge(null);
      }
    });

    this.busOn('graph:cleared', () => {
      this.selectNode(null);
      this.selectEdge(null);
    });

    // Ponte de Eventos da Camada Vetorial SVG
    this.busOn('node:context-menu', ({ nodeId }: { nodeId: string }) => {
      this.clearHover();
      this.selectNode(nodeId);
    });

    this.busOn('node:select-request', ({ nodeId }: { nodeId: string }) => this.selectNode(nodeId));
    this.busOn('node:double-click', ({ nodeId }: { nodeId: string }) => this.diveIntoNode(nodeId));

    this.busOn('node:start-drag', ({ nodeId, clientX, clientY }: any) => this.startDragNode(nodeId, clientX, clientY));
    this.busOn('node:start-connecting', ({ nodeId, portId, clientX, clientY }: any) => this.startConnectingPort(nodeId, portId, clientX, clientY));
    this.busOn('node:complete-connecting', ({ nodeId, portId }: any) => this.completeConnectingPort(nodeId, portId));

    // Eventos de Edição de Curva de Bézier Estilo Inkscape
    this.busOn('edge:start-curve-drag', ({ edgeId, clientX, clientY }: any) =>
      this.startEdgeCurveDrag(edgeId, clientX, clientY)
    );
    this.busOn('edge:start-handle-drag', ({ edgeId, handle, clientX, clientY }: any) =>
      this.startEdgeHandleDrag(edgeId, handle, clientX, clientY)
    );
    this.busOn('edge:reset-control-points', ({ edgeId }: { edgeId: string }) =>
      this.resetEdgeControlPoints(edgeId)
    );
  }

  /** Registra um listener de DOM e guarda o cancelamento para o dispose. */
  private listen(
    target: EventTarget,
    type: string,
    handler: (e: any) => void,
    options?: AddEventListenerOptions
  ): void {
    target.addEventListener(type, handler, options);
    this.disposers.push(() => {
      if (typeof (target as EventTarget).removeEventListener === 'function') {
        target.removeEventListener(type, handler, options);
      }
    });
  }

  /** Registra um listener do EventBus e guarda o cancelamento para o dispose. */
  private busOn(event: string, handler: (data: any) => void): void {
    this.disposers.push(globalEventBus.on(event, handler));
  }

  /**
   * O canvas WebGL só recebe pointerdown em área vazia: vértices, portas, botões
   * de ação e arestas são interceptados pela camada vetorial SVG, que publica os
   * eventos de alto nível no EventBus (node:start-drag, node:start-connecting...).
   */
  private onPointerDown(e: PointerEvent): void {
    if (e.button !== 0 || this.cameraController.getIsPanning() || e.altKey) return;

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

    const worldCursor = this.cameraController.screenToWorld(e.clientX, e.clientY);

    // 0. Drag de Alça / Curva de Bézier Estilo Inkscape
    if (this.bezierDragState) {
      // Mesma rede de segurança do arraste de vértice: não continua editando a
      // curva se o botão já foi solto e o pointerup se perdeu.
      if (typeof e.buttons === 'number' && e.buttons === 0) {
        this.finishBezierDrag();
        return;
      }

      const dist = Math.hypot(
        worldCursor.x - this.bezierDragState.startWorld.x,
        worldCursor.y - this.bezierDragState.startWorld.y
      );
      if (!this.bezierDragState.hasMoved && dist < 2) {
        return;
      }
      this.bezierDragState.hasMoved = true;

      const { edgeId, mode, t, initialCurve } = this.bezierDragState;
      let newControls: IEdgeControlPoints;

      if (mode === 'cp1') {
        newControls = {
          cp1Offset: {
            x: Math.round(worldCursor.x - initialCurve.start.x),
            y: Math.round(worldCursor.y - initialCurve.start.y)
          },
          cp2Offset: {
            x: Math.round(initialCurve.cp2.x - initialCurve.end.x),
            y: Math.round(initialCurve.cp2.y - initialCurve.end.y)
          }
        };
      } else if (mode === 'cp2') {
        newControls = {
          cp1Offset: {
            x: Math.round(initialCurve.cp1.x - initialCurve.start.x),
            y: Math.round(initialCurve.cp1.y - initialCurve.start.y)
          },
          cp2Offset: {
            x: Math.round(worldCursor.x - initialCurve.end.x),
            y: Math.round(worldCursor.y - initialCurve.end.y)
          }
        };
      } else {
        newControls = computeControlOffsetsFromCurveDrag(initialCurve, t, worldCursor);
      }

      this.graph.updateEdge(edgeId, { controlPoints: newControls });
      return;
    }

    // A. Drag do Nó
    if (this.draggedNodeView) {
      // Rede de segurança: se o pointerup se perdeu (ex.: consumido por outra camada
      // ou solto fora da janela), encerra o arraste em vez de deixar o vértice colado
      // ao cursor para sempre.
      if (typeof e.buttons === 'number' && e.buttons === 0) {
        this.finishNodeDrag();
        return;
      }

      // Só inicia o movimento após vencer o limiar de arraste: um clique com
      // pequeno tremor não deve deslocar o vértice nem gerar undo acidental.
      if (!this.dragActive) {
        const start = this.dragStartClient;
        if (start && Math.hypot(e.clientX - start.x, e.clientY - start.y) < DRAG_THRESHOLD_PX) {
          return;
        }
        this.dragActive = true;
        // Reancora o offset no ponto em que o arraste realmente começou,
        // evitando o "salto" do vértice ao cruzar o limiar.
        this.dragOffset.set(
          this.draggedNodeView.nodeData.position.x - worldCursor.x,
          this.draggedNodeView.nodeData.position.y - worldCursor.y,
          0
        );
      }

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
  }

  private clearHover(): void {
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

    this.finishBezierDrag();

    this.finishNodeDrag();

    // A conclusão sobre uma porta de entrada chega via SVG (node:complete-connecting).
    // Aqui apenas encerramos o gesto caso o ponteiro tenha sido solto no vazio.
    if (this.isConnecting) {
      this.cancelConnecting();
    }
  }

  /** Finaliza a edição de curva Bézier, registrando um único UpdateEdgeCommand no undo. */
  private finishBezierDrag(): void {
    if (!this.bezierDragState) return;
    const { edgeId, initialControlPoints, hasMoved } = this.bezierDragState;
    this.bezierDragState = null;
    if (!hasMoved) return;

    const currentEdge = this.graph.getEdge(edgeId);
    if (!currentEdge) return;
    const cmd = new UpdateEdgeCommand(
      this.graph,
      edgeId,
      initialControlPoints,
      currentEdge.controlPoints
    );
    this.graph.commandManager.record(cmd);
  }

  /**
   * Finaliza o arraste do vértice, registrando um único MoveNodeCommand no undo.
   * Idempotente: pode ser chamado no pointerup e também na guarda de "botão solto"
   * de onPointerMove, caso o pointerup tenha sido consumido/stopPropagation em outro alvo.
   */
  private finishNodeDrag(): void {
    if (!this.draggedNodeView) return;

    const finalX = this.draggedNodeView.nodeData.position.x;
    const finalY = this.draggedNodeView.nodeData.position.y;
    if (this.dragActive && this.dragStartPos && (finalX !== this.dragStartPos.x || finalY !== this.dragStartPos.y)) {
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
    this.dragStartClient = null;
    this.dragActive = false;
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
    const obstacles: IObstacleBox[] = [];
    this.nodeViews.forEach(nv => {
      const size = nv.getBoundingSize();
      obstacles.push({
        id: nv.nodeData.id,
        x: nv.nodeData.position.x,
        y: nv.nodeData.position.y,
        width: size.width,
        height: size.height
      });
    });

    this.connectionViews.forEach(cv => {
      if (cv.edgeData.sourceNodeId === nodeId || cv.edgeData.targetNodeId === nodeId) {
        const srcNv = this.nodeViews.get(cv.edgeData.sourceNodeId);
        const tgtNv = this.nodeViews.get(cv.edgeData.targetNodeId);

        if (srcNv && tgtNv) {
          const start = srcNv.getPortWorldPosition(cv.edgeData.sourcePortId);
          const end = tgtNv.getPortWorldPosition(cv.edgeData.targetPortId);
          cv.updateCurve(start, end, obstacles);
        }
      }
    });
  }

  selectNode(nodeId: string | null): void {
    if (nodeId && this.selectedEdgeId) {
      this.selectEdge(null);
    }

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
    if (edgeId && this.selectedNodeId) {
      this.selectNode(null);
    }

    if (this.selectedEdgeId && this.selectedEdgeId !== edgeId) {
      this.connectionViews.get(this.selectedEdgeId)?.setSelected(false);
    }

    this.selectedEdgeId = edgeId;

    if (edgeId) {
      this.connectionViews.get(edgeId)?.setSelected(true);
    }

    globalEventBus.emit('edge:selected', edgeId);
  }

  public getSelectedEdgeId(): string | null {
    return this.selectedEdgeId;
  }

  public getSelectedNodeId(): string | null {
    return this.selectedNodeId;
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

  /** Entra no subflow/módulo clicado duas vezes (evento emitido pelo vértice SVG). */
  private diveIntoNode(nodeId: string): void {
    const nodeData = this.graph.getNode(nodeId);
    if (nodeData && (nodeData.type === 'subflow' || nodeData.type === 'module')) {
      this.graph.diveIntoSubflow(nodeId);
    }
  }

  public startDragNode(nodeId: string, clientX: number, clientY: number): void {
    const nv = this.nodeViews.get(nodeId);
    if (!nv) return;
    this.beginNodeDrag(nv, clientX, clientY);
  }

  /**
   * Prepara o arraste de um vértice. O movimento só é aplicado em onPointerMove
   * depois de vencer DRAG_THRESHOLD_PX, mantendo o clique puro como seleção.
   */
  private beginNodeDrag(nv: NodeView, clientX: number, clientY: number): void {
    const worldCursor = this.cameraController.screenToWorld(clientX, clientY);
    this.selectNode(nv.nodeData.id);
    this.draggedNodeView = nv;
    this.dragStartPos = { x: nv.nodeData.position.x, y: nv.nodeData.position.y };
    this.dragStartClient = { x: clientX, y: clientY };
    this.dragActive = false;
    this.dragOffset.set(
      nv.nodeData.position.x - worldCursor.x,
      nv.nodeData.position.y - worldCursor.y,
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
    if (this.connectingSourceNodeId !== targetNodeId) {
      this.connectToPort(targetNodeId, targetPortId);
    }
    this.cancelConnecting();
  }

  // execute() adiciona a aresta ao grafo e a registra para desfazer; record() so registraria,
  // e a aresta nunca apareceria.
  private connectToPort(targetNodeId: string, targetPortId: string): void {
    if (!this.connectingSourceNodeId || !this.connectingSourcePortId) return;
    try {
      const sourceNode = this.graph.getNode(this.connectingSourceNodeId);
      const sourcePort = sourceNode?.outputs.find(p => p.id === this.connectingSourcePortId);
      const edgeData: IEdgeData = {
        id: `edge_${Date.now()}_${Math.random().toString(36).substr(2, 4)}`,
        sourceNodeId: this.connectingSourceNodeId,
        sourcePortId: this.connectingSourcePortId,
        targetNodeId,
        targetPortId
      };
      if (sourcePort?.type === 'call') {
        edgeData.kind = 'call';
        edgeData.callSettings = defaultCallSettings();
      }
      const cmd = new AddEdgeCommand(this.graph, edgeData);
      this.graph.commandManager.execute(cmd);
    } catch (err) {
      console.warn('Falha ao conectar nós:', err);
    }
  }

  public cancelConnecting(): void {
    this.isConnecting = false;
    this.connectingSourceNodeId = null;
    this.connectingSourcePortId = null;
    if (this.rubberBandLine) this.rubberBandLine.visible = false;
  }

  public startEdgeCurveDrag(edgeId: string, clientX: number, clientY: number): void {
    const conn = this.connectionViews.get(edgeId);
    const edge = this.graph.getEdge(edgeId);
    if (!conn || !edge || !conn.currentBezier) return;

    this.clearHover();
    this.selectEdge(edgeId);

    const worldCursor = this.cameraController.screenToWorld(clientX, clientY);
    const t = findClosestBezierParameter(conn.currentBezier, worldCursor);

    this.bezierDragState = {
      edgeId,
      mode: 'curve',
      t,
      initialCurve: {
        start: conn.currentBezier.start.clone(),
        cp1: conn.currentBezier.cp1.clone(),
        cp2: conn.currentBezier.cp2.clone(),
        end: conn.currentBezier.end.clone(),
        midPoint: conn.currentBezier.midPoint.clone()
      },
      initialControlPoints: edge.controlPoints
        ? {
            cp1Offset: { ...edge.controlPoints.cp1Offset },
            cp2Offset: { ...edge.controlPoints.cp2Offset }
          }
        : undefined,
      startWorld: { x: worldCursor.x, y: worldCursor.y },
      hasMoved: false
    };
  }

  public startEdgeHandleDrag(
    edgeId: string,
    handle: 'cp1' | 'cp2' | 'mid',
    clientX: number,
    clientY: number
  ): void {
    const conn = this.connectionViews.get(edgeId);
    const edge = this.graph.getEdge(edgeId);
    if (!conn || !edge || !conn.currentBezier) return;

    this.clearHover();
    this.selectEdge(edgeId);

    const worldCursor = this.cameraController.screenToWorld(clientX, clientY);
    this.bezierDragState = {
      edgeId,
      mode: handle,
      t: 0.5,
      initialCurve: {
        start: conn.currentBezier.start.clone(),
        cp1: conn.currentBezier.cp1.clone(),
        cp2: conn.currentBezier.cp2.clone(),
        end: conn.currentBezier.end.clone(),
        midPoint: conn.currentBezier.midPoint.clone()
      },
      initialControlPoints: edge.controlPoints
        ? {
            cp1Offset: { ...edge.controlPoints.cp1Offset },
            cp2Offset: { ...edge.controlPoints.cp2Offset }
          }
        : undefined,
      startWorld: { x: worldCursor.x, y: worldCursor.y },
      hasMoved: false
    };
  }

  public resetEdgeControlPoints(edgeId: string): void {
    const edge = this.graph.getEdge(edgeId);
    if (!edge || !edge.controlPoints) return;

    const cmd = new UpdateEdgeCommand(
      this.graph,
      edgeId,
      edge.controlPoints,
      undefined,
      `Restaurar Curva Automática (${edgeId})`
    );
    this.graph.commandManager.execute(cmd);
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
    this.disposers.forEach(off => off());
    this.disposers = [];
  }
}
