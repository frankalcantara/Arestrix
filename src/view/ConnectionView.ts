import * as THREE from 'three';
import { Line2 } from 'three/examples/jsm/lines/Line2.js';
import { LineMaterial } from 'three/examples/jsm/lines/LineMaterial.js';
import { LineGeometry } from 'three/examples/jsm/lines/LineGeometry.js';
import { IEdgeData } from '../core/types.ts';
import { THEME } from './Theme.ts';

export class ConnectionView {
  public edgeData: IEdgeData;
  public line: Line2;
  private geometry: LineGeometry;
  private material: LineMaterial;
  public isSelected: boolean = false;

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
   * Atualiza os pontos da curva Bézier cúbica entre as duas portas.
   */
  updateCurve(startWorld: THREE.Vector3, endWorld: THREE.Vector3): void {
    const dx = endWorld.x - startWorld.x;
    const dy = endWorld.y - startWorld.y;

    let cp1: THREE.Vector3;
    let cp2: THREE.Vector3;

    if (this.edgeData.isFeedbackLoop || dx < 40) {
      // Loopback ou conexao invertida: cria uma alca superior/inferior suave
      const curvature = Math.max(Math.abs(dy) * 0.6, 120);
      const verticalOffset = dy >= 0 ? curvature : -curvature;
      cp1 = new THREE.Vector3(startWorld.x + 80, startWorld.y + verticalOffset, 0);
      cp2 = new THREE.Vector3(endWorld.x - 80, endWorld.y + verticalOffset, 0);
    } else {
      // Conexao fluida padrao da esquerda para a direita
      const controlDist = Math.max(dx * 0.45, 60);
      cp1 = new THREE.Vector3(startWorld.x + controlDist, startWorld.y, 0);
      cp2 = new THREE.Vector3(endWorld.x - controlDist, endWorld.y, 0);
    }

    const curve = new THREE.CubicBezierCurve3(startWorld, cp1, cp2, endWorld);
    const points = curve.getPoints(36);
    const positions: number[] = [];

    points.forEach(p => {
      positions.push(p.x, p.y, p.z);
    });

    this.geometry.setPositions(positions);
    this.line.computeLineDistances();
  }

  setResolution(width: number, height: number): void {
    this.material.resolution.set(width, height);
  }

  setSelected(selected: boolean): void {
    this.isSelected = selected;
    this.material.color.setHex(
      selected 
        ? THEME.edge.selectedColor 
        : (this.edgeData.isFeedbackLoop ? THEME.edge.feedbackColor : THEME.edge.defaultColor)
    );
    this.material.linewidth = selected ? THEME.edge.lineWidth + 2 : THEME.edge.lineWidth;
  }

  dispose(): void {
    this.geometry.dispose();
    this.material.dispose();
  }
}
