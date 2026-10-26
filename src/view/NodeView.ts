import * as THREE from 'three';
import { INodeData } from '../core/types.ts';
import { THEME } from './Theme.ts';
import { globalEventBus } from '../core/EventBus.ts';
import { SvgNodeView, NodeExecutionState, NodeRunRecord } from './SvgNodeView.ts';
import { CanvasTooltip } from '../ui/CanvasTooltip.ts';

/**
 * Arestrix - Vértice lógico do grafo.
 *
 * A partir da migração para a camada vetorial, TODO o desenho do vértice
 * (corpo, header, tipografia, portas, barra de ações e telemetria) é feito
 * exclusivamente pelo SvgNodeView. Esta classe guarda apenas:
 * - estado lógico (seleção, execução, nível de detalhe);
 * - a geometria/âncoras usadas pelo roteamento de arestas e pelo InteractionManager.
 *
 * Não há mais malhas Three.js de vértice: manter as duas representações era a
 * origem de áreas duplicadas (ex.: pílula de status desenhada duas vezes).
 */
export class NodeView {
  public nodeData: INodeData;
  public svgNodeView: SvgNodeView;
  public isSelected: boolean = false;

  private width: number;
  private height: number;
  private detailVisible: boolean = true;

  /** Altura do cartão em função do número de portas (fonte única para layout e enquadramento). */
  static computeHeight(nodeData: Pick<INodeData, 'inputs' | 'outputs'>): number {
    return SvgNodeView.computeHeight(nodeData);
  }

  constructor(nodeData: INodeData, svgContainer: SVGGElement, tooltip?: CanvasTooltip) {
    this.nodeData = nodeData;
    this.width = THEME.nodeBody.width;
    this.height = NodeView.computeHeight(nodeData);
    this.svgNodeView = new SvgNodeView(
      nodeData,
      svgContainer,
      tooltip ?? new CanvasTooltip(),
      globalEventBus
    );
  }

  /** Atualiza a posição lógica e o transform da camada vetorial. */
  updatePosition(x: number, y: number): void {
    this.svgNodeView.updatePosition(x, y);
  }

  updateLabel(newLabel: string): void {
    this.svgNodeView.updateLabel(newLabel);
  }

  setSelected(selected: boolean): void {
    this.isSelected = selected;
    this.svgNodeView.setSelected(selected);
  }

  getExecutionState(): NodeExecutionState {
    return this.svgNodeView.getExecutionState();
  }

  setExecutionState(state: NodeExecutionState, detail?: string): void {
    this.svgNodeView.setExecutionState(state, detail);
  }

  /** Tempo da execução atual (running) ou da última execução concluída. */
  getElapsedMs(): number {
    return this.svgNodeView.getElapsedMs();
  }

  /** Selo persistente com o resultado da última execução (não altera o modo de edição). */
  setRunRecord(record: NodeRunRecord | null): void {
    this.svgNodeView.setRunRecord(record);
  }

  getRunRecord(): NodeRunRecord | null {
    return this.svgNodeView.getRunRecord();
  }

  /**
   * Nível de detalhe por zoom: abaixo do limiar o SvgNodeView oculta rótulos de
   * portas, barra de ações e pílula de status.
   */
  setDetailLevel(zoom: number): void {
    const visible = zoom >= THEME.lod.detailZoomThreshold;
    if (visible === this.detailVisible) return;
    this.detailVisible = visible;
    this.svgNodeView.setDetailLevel(zoom);
  }

  isDetailVisible(): boolean {
    return this.detailVisible;
  }

  getBoundingSize(): { width: number; height: number } {
    return { width: this.width, height: this.height };
  }

  /**
   * Posição mundial da porta. A geometria vem do SvgNodeView (fonte única),
   * convertida para o sistema de coordenadas do mundo (Y para cima).
   */
  getPortWorldPosition(portId: string): THREE.Vector3 {
    const local = this.svgNodeView.getPortPosition(portId);
    if (!local) {
      return new THREE.Vector3(this.nodeData.position.x, this.nodeData.position.y, 0);
    }
    return new THREE.Vector3(
      this.nodeData.position.x + local.x,
      this.nodeData.position.y - local.y,
      0
    );
  }

  /** A telemetria é inteiramente renderizada pela camada vetorial. */
  handleTelemetry(ev: any): void {
    this.svgNodeView.handleTelemetry(ev);
  }

  dispose(): void {
    this.svgNodeView.dispose();
  }
}
