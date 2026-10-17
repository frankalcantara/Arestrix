import * as THREE from 'three';
import { Line2 } from 'three/examples/jsm/lines/Line2.js';
import { LineMaterial } from 'three/examples/jsm/lines/LineMaterial.js';
import { LineGeometry } from 'three/examples/jsm/lines/LineGeometry.js';
import { IEdgeData } from '../core/types.ts';
import { THEME } from './Theme.ts';
import { computeEdgeBezier, IBezierCurvePoints, IObstacleBox } from './BezierEdgeRouter.ts';

export type EdgeExecutionState = 'idle' | 'queued' | 'active' | 'completed' | 'error';

export class ConnectionView {
  public edgeData: IEdgeData;
  public line: Line2;
  private geometry: LineGeometry;
  private material: LineMaterial;
  public isSelected: boolean = false;
  public executionState: EdgeExecutionState = 'idle';
  public currentBezier: IBezierCurvePoints | null = null;

  constructor(edgeData: IEdgeData, screenWidth: number, screenHeight: number) {
    this.edgeData = edgeData;

    const isFeedback = !!edgeData.isFeedbackLoop;
    const color = isFeedback ? THEME.edge.feedbackColor : THEME.edge.defaultColor;

    this.material = new LineMaterial({
      color,
      linewidth: THEME.edge.lineWidth,
      resolution: new THREE.Vector2(screenWidth, screenHeight),
      dashed: isFeedback,
      dashScale: 0.1,
      dashSize: 10,
      gapSize: 5
    });

    this.geometry = new LineGeometry();
    // Posições iniciais placeholder
    this.geometry.setPositions([0, 0, 0, 10, 0, 0]);

    this.line = new Line2(this.geometry, this.material);
    this.line.computeLineDistances();
    this.line.userData = { edgeId: edgeData.id, isEdge: true };
  }

  /**
   * Atualiza os pontos da curva Bézier cúbica entre as duas portas,
   * respeitando alças editadas (estilo Inkscape) e desvio automático de obstáculos.
   */
  updateCurve(
    startWorld: THREE.Vector3,
    endWorld: THREE.Vector3,
    obstacles: IObstacleBox[] = []
  ): IBezierCurvePoints {
    const bezier = computeEdgeBezier(startWorld, endWorld, this.edgeData, obstacles);
    this.currentBezier = bezier;

    const curve = new THREE.CubicBezierCurve3(bezier.start, bezier.cp1, bezier.cp2, bezier.end);
    const points = curve.getPoints(36);
    const positions: number[] = [];

    points.forEach(p => {
      positions.push(p.x, p.y, p.z);
    });

    this.geometry.setPositions(positions);
    this.line.computeLineDistances();
    return bezier;
  }

  setResolution(width: number, height: number): void {
    this.material.resolution.set(width, height);
  }

  setSelected(selected: boolean): void {
    this.isSelected = selected;
    this.refreshAppearance();
  }

  setExecutionState(state: EdgeExecutionState): void {
    this.executionState = state;
    this.refreshAppearance();
  }

  private refreshAppearance(): void {
    if (this.isSelected) {
      this.material.color.setHex(THEME.edge.selectedColor);
      this.material.linewidth = THEME.edge.lineWidth + 2;
      return;
    }

    if (this.executionState === 'active') {
      this.material.color.setHex(0x10b981);
      this.material.linewidth = THEME.edge.lineWidth + 1.5;
    } else if (this.executionState === 'completed') {
      this.material.color.setHex(0x059669);
      this.material.linewidth = THEME.edge.lineWidth + 0.5;
    } else if (this.executionState === 'error') {
      this.material.color.setHex(0xef4444);
      this.material.linewidth = THEME.edge.lineWidth + 1;
    } else {
      this.material.color.setHex(
        this.edgeData.isFeedbackLoop ? THEME.edge.feedbackColor : THEME.edge.defaultColor
      );
      this.material.linewidth = THEME.edge.lineWidth;
    }
  }

  dispose(): void {
    this.geometry.dispose();
    this.material.dispose();
  }
}
