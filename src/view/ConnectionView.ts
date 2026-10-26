import * as THREE from 'three';
import { IEdgeData } from '../core/types.ts';
import { THEME } from './Theme.ts';
import { computeEdgeBezier, IBezierCurvePoints, IObstacleBox } from './BezierEdgeRouter.ts';

export type EdgeExecutionState = 'idle' | 'queued' | 'active' | 'completed' | 'error';

export interface IEdgeBaseStyle {
  /** Cor base da aresta em repouso (CSS hex). */
  stroke: string;
  /** Padrão de tracejado, ou null para linha sólida. */
  dashArray: string | null;
}

function cssColor(hex: number): string {
  return `#${hex.toString(16).padStart(6, '0').toUpperCase()}`;
}

/**
 * Aparência base de uma aresta, derivada exclusivamente do modelo (edgeData).
 * Fonte única para a criação/renderização SVG e para os testes.
 */
export function getEdgeBaseStyle(edgeData: IEdgeData): IEdgeBaseStyle {
  const isCall = edgeData.kind === 'call';
  const isFeedback = !isCall && !!edgeData.isFeedbackLoop;

  if (isCall) {
    return { stroke: cssColor(THEME.edge.callColor), dashArray: '7 4' };
  }
  if (isFeedback) {
    return { stroke: cssColor(THEME.edge.feedbackColor), dashArray: '8 4' };
  }
  return { stroke: cssColor(THEME.edge.defaultColor), dashArray: null };
}

/**
 * Arestrix - Estado lógico de uma aresta.
 *
 * A pintura é feita pela camada vetorial SVG no Viewport. Esta classe mantém
 * apenas o modelo (edgeData), seleção, estado de execução e a última curva
 * calculada — sem malhas Three.js duplicadas.
 */
export class ConnectionView {
  public edgeData: IEdgeData;
  public isSelected: boolean = false;
  public executionState: EdgeExecutionState = 'idle';
  public currentBezier: IBezierCurvePoints | null = null;

  constructor(edgeData: IEdgeData) {
    this.edgeData = edgeData;
  }

  /**
   * Calcula os pontos da curva Bézier cúbica entre as duas portas,
   * respeitando alças editadas (estilo Inkscape) e desvio automático de obstáculos.
   */
  updateCurve(
    startWorld: THREE.Vector3,
    endWorld: THREE.Vector3,
    obstacles: IObstacleBox[] = []
  ): IBezierCurvePoints {
    const bezier = computeEdgeBezier(startWorld, endWorld, this.edgeData, obstacles);
    this.currentBezier = bezier;
    return bezier;
  }

  getBaseStyle(): IEdgeBaseStyle {
    return getEdgeBaseStyle(this.edgeData);
  }

  setSelected(selected: boolean): void {
    this.isSelected = selected;
  }

  setExecutionState(state: EdgeExecutionState): void {
    this.executionState = state;
  }
}
