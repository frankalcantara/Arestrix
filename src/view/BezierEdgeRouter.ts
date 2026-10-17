import * as THREE from 'three';
import { IEdgeControlPoints, IEdgeData } from '../core/types.ts';

export interface IObstacleBox {
  id: string;
  x: number;
  y: number;
  width: number;
  height: number;
}

export interface IBezierCurvePoints {
  start: THREE.Vector3;
  cp1: THREE.Vector3;
  cp2: THREE.Vector3;
  end: THREE.Vector3;
  midPoint: THREE.Vector3;
}

/**
 * Avalia um ponto na curva de Bézier cúbica no parâmetro t in [0, 1].
 */
export function evaluateCubicBezier(
  p0: { x: number; y: number },
  p1: { x: number; y: number },
  p2: { x: number; y: number },
  p3: { x: number; y: number },
  t: number
): THREE.Vector3 {
  const u = 1 - t;
  const b0 = u * u * u;
  const b1 = 3 * u * u * t;
  const b2 = 3 * u * t * t;
  const b3 = t * t * t;
  return new THREE.Vector3(
    b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x,
    b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y,
    0
  );
}

/**
 * Verifica se a curva de Bézier cúbica colide com alguma caixa de obstáculo fornecida.
 */
function findCollidingObstacles(
  p0: { x: number; y: number },
  p1: { x: number; y: number },
  p2: { x: number; y: number },
  p3: { x: number; y: number },
  obstacles: IObstacleBox[],
  paddingX: number = 22,
  paddingTop: number = 38,
  paddingBottom: number = 26
): IObstacleBox[] {
  if (obstacles.length === 0) return [];
  const collided = new Set<IObstacleBox>();
  const samples = 24;

  for (let i = 2; i <= samples - 2; i++) {
    const t = i / samples;
    const pt = evaluateCubicBezier(p0, p1, p2, p3, t);
    for (const obs of obstacles) {
      const halfW = obs.width / 2 + paddingX;
      const top = obs.y + obs.height / 2 + paddingTop;
      const bottom = obs.y - obs.height / 2 - paddingBottom;
      if (pt.x >= obs.x - halfW && pt.x <= obs.x + halfW && pt.y >= bottom && pt.y <= top) {
        collided.add(obs);
      }
    }
  }

  return Array.from(collided);
}

/**
 * Calcula os pontos de controle da curva de Bézier cúbica para uma aresta:
 * - Se a aresta possuir `controlPoints` editados manualmente (estilo Inkscape), aplica-os como offsets das portas.
 * - Caso contrário, calcula automaticamente a curvatura ideal desviando de vértices no caminho.
 */
export function computeEdgeBezier(
  startWorld: THREE.Vector3,
  endWorld: THREE.Vector3,
  edgeData: Pick<IEdgeData, 'sourceNodeId' | 'targetNodeId' | 'isFeedbackLoop' | 'controlPoints'>,
  obstacles: IObstacleBox[] = []
): IBezierCurvePoints {
  // 1. Se houver pontos de controle customizados pelo usuário, respeita-os fielmente
  if (edgeData.controlPoints) {
    const cp1 = new THREE.Vector3(
      startWorld.x + edgeData.controlPoints.cp1Offset.x,
      startWorld.y + edgeData.controlPoints.cp1Offset.y,
      0
    );
    const cp2 = new THREE.Vector3(
      endWorld.x + edgeData.controlPoints.cp2Offset.x,
      endWorld.y + edgeData.controlPoints.cp2Offset.y,
      0
    );
    const midPoint = evaluateCubicBezier(startWorld, cp1, cp2, endWorld, 0.5);
    return { start: startWorld.clone(), cp1, cp2, end: endWorld.clone(), midPoint };
  }

  const dx = endWorld.x - startWorld.x;
  const dy = endWorld.y - startWorld.y;

  let cp1: THREE.Vector3;
  let cp2: THREE.Vector3;

  const isBackwardOrLoop = !!edgeData.isFeedbackLoop || dx < 60;

  if (isBackwardOrLoop) {
    // Conexão de retorno (feedback loop) ou quebra de linha (row wrap):
    const minX = Math.min(startWorld.x, endWorld.x) - 80;
    const maxX = Math.max(startWorld.x, endWorld.x) + 80;

    const corridorObstacles = obstacles.filter(
      o => o.x + o.width / 2 >= minX && o.x - o.width / 2 <= maxX
    );

    const outExtent = Math.max(120, Math.min(Math.abs(dx) * 0.25 + 80, 240));
    const avgY = (startWorld.y + endWorld.y) / 2;

    // Se houver desnível vertical amplo (ex.: transição entre linhas do layout) e não for loop explícito,
    // verifica se uma curva em S pelo corredor intermediário passa livre de obstáculos
    const midCand1 = new THREE.Vector3(startWorld.x + outExtent, startWorld.y + dy * 0.48, 0);
    const midCand2 = new THREE.Vector3(endWorld.x - outExtent, endWorld.y - dy * 0.48, 0);
    const otherObstacles = corridorObstacles.filter(
      o => o.id !== edgeData.sourceNodeId && o.id !== edgeData.targetNodeId
    );

    if (
      !edgeData.isFeedbackLoop &&
      Math.abs(dy) >= 180 &&
      findCollidingObstacles(startWorld, midCand1, midCand2, endWorld, otherObstacles).length === 0
    ) {
      cp1 = midCand1;
      cp2 = midCand2;
    } else if (corridorObstacles.length > 0) {
      const maxTopY = Math.max(...corridorObstacles.map(o => o.y + o.height / 2 + 44));
      const minBottomY = Math.min(...corridorObstacles.map(o => o.y - o.height / 2 - 36));

      const distToTop = Math.abs(maxTopY - avgY);
      const distToBottom = Math.abs(avgY - minBottomY);

      // Prefere contornar por baixo quando a distância for similar (evita cruzar as barras de ação no topo)
      const routeBelow = distToBottom <= distToTop + 40;
      const targetPeakY = routeBelow ? minBottomY - 50 : maxTopY + 50;
      const cpY = (targetPeakY - 0.125 * (startWorld.y + endWorld.y)) / 0.75;

      cp1 = new THREE.Vector3(startWorld.x + outExtent, cpY, 0);
      cp2 = new THREE.Vector3(endWorld.x - outExtent, cpY, 0);
    } else {
      const curvature = Math.max(Math.abs(dy) * 0.65, 220);
      const verticalOffset = dy > 20 ? curvature : -curvature;
      cp1 = new THREE.Vector3(startWorld.x + outExtent, startWorld.y + verticalOffset, 0);
      cp2 = new THREE.Vector3(endWorld.x - outExtent, endWorld.y + verticalOffset, 0);
    }
  } else {
    // Conexão fluida da esquerda para a direita
    const controlDist = Math.min(Math.max(dx * 0.44, 70), 340);
    cp1 = new THREE.Vector3(startWorld.x + controlDist, startWorld.y, 0);
    cp2 = new THREE.Vector3(endWorld.x - controlDist, endWorld.y, 0);

    // Verifica se existem vértices intermediários no caminho da curva padrão
    const intermediateObstacles = obstacles.filter(
      o =>
        o.id !== edgeData.sourceNodeId &&
        o.id !== edgeData.targetNodeId &&
        o.x + o.width / 2 >= startWorld.x + 10 &&
        o.x - o.width / 2 <= endWorld.x - 10
    );

    if (intermediateObstacles.length > 0) {
      const hits = findCollidingObstacles(startWorld, cp1, cp2, endWorld, intermediateObstacles);
      if (hits.length > 0) {
        const maxTopY = Math.max(...hits.map(o => o.y + o.height / 2 + 46));
        const minBottomY = Math.min(...hits.map(o => o.y - o.height / 2 - 38));
        const avgY = (startWorld.y + endWorld.y) / 2;

        const targetTopPeak = maxTopY + 36;
        const targetBottomPeak = minBottomY - 36;

        const cpYTop = (targetTopPeak - 0.125 * (startWorld.y + endWorld.y)) / 0.75;
        const cpYBottom = (targetBottomPeak - 0.125 * (startWorld.y + endWorld.y)) / 0.75;

        const topCand1 = new THREE.Vector3(startWorld.x + controlDist, cpYTop, 0);
        const topCand2 = new THREE.Vector3(endWorld.x - controlDist, cpYTop, 0);
        const botCand1 = new THREE.Vector3(startWorld.x + controlDist, cpYBottom, 0);
        const botCand2 = new THREE.Vector3(endWorld.x - controlDist, cpYBottom, 0);

        const topHits = findCollidingObstacles(startWorld, topCand1, topCand2, endWorld, intermediateObstacles).length;
        const botHits = findCollidingObstacles(startWorld, botCand1, botCand2, endWorld, intermediateObstacles).length;

        let useTop: boolean;
        if (topHits !== botHits) {
          useTop = topHits < botHits;
        } else {
          useTop = Math.abs(targetTopPeak - avgY) < Math.abs(avgY - targetBottomPeak);
        }

        if (useTop) {
          cp1 = topCand1;
          cp2 = topCand2;
        } else {
          cp1 = botCand1;
          cp2 = botCand2;
        }
      }
    }
  }

  const midPoint = evaluateCubicBezier(startWorld, cp1, cp2, endWorld, 0.5);
  return {
    start: startWorld.clone(),
    cp1,
    cp2,
    end: endWorld.clone(),
    midPoint
  };
}

/**
 * Encontra o parâmetro t in [0.15, 0.85] na curva de Bézier mais próximo de um ponto no mundo.
 */
export function findClosestBezierParameter(
  curve: IBezierCurvePoints,
  worldPoint: { x: number; y: number }
): number {
  let bestT = 0.5;
  let bestDistSq = Infinity;
  const steps = 40;

  for (let i = 6; i <= steps - 6; i++) {
    const t = i / steps;
    const pt = evaluateCubicBezier(curve.start, curve.cp1, curve.cp2, curve.end, t);
    const dx = pt.x - worldPoint.x;
    const dy = pt.y - worldPoint.y;
    const dSq = dx * dx + dy * dy;
    if (dSq < bestDistSq) {
      bestDistSq = dSq;
      bestT = t;
    }
  }

  return bestT;
}

/**
 * Calcula os novos offsets de controle (cp1Offset, cp2Offset) ao arrastar diretamente um ponto t da curva
 * (estilo Inkscape Direct Curve Drag), fazendo a curva passar pelo novo ponto desejado.
 */
export function computeControlOffsetsFromCurveDrag(
  initialCurve: IBezierCurvePoints,
  t: number,
  targetWorldPoint: { x: number; y: number }
): IEdgeControlPoints {
  const clampedT = Math.max(0.15, Math.min(0.85, t));
  const u = 1 - clampedT;

  const ptOnInitial = evaluateCubicBezier(
    initialCurve.start,
    initialCurve.cp1,
    initialCurve.cp2,
    initialCurve.end,
    clampedT
  );

  const deltaX = targetWorldPoint.x - ptOnInitial.x;
  const deltaY = targetWorldPoint.y - ptOnInitial.y;

  // Coeficientes de Bernstein cúbicos para P1 e P2
  const c1 = 3 * u * u * clampedT;
  const c2 = 3 * u * clampedT * clampedT;
  const denom = c1 * c1 + c2 * c2;

  const w1 = denom > 1e-6 ? c1 / denom : 4 / 3;
  const w2 = denom > 1e-6 ? c2 / denom : 4 / 3;

  const newCp1X = initialCurve.cp1.x + w1 * deltaX;
  const newCp1Y = initialCurve.cp1.y + w1 * deltaY;
  const newCp2X = initialCurve.cp2.x + w2 * deltaX;
  const newCp2Y = initialCurve.cp2.y + w2 * deltaY;

  return {
    cp1Offset: {
      x: Math.round(newCp1X - initialCurve.start.x),
      y: Math.round(newCp1Y - initialCurve.start.y)
    },
    cp2Offset: {
      x: Math.round(newCp2X - initialCurve.end.x),
      y: Math.round(newCp2Y - initialCurve.end.y)
    }
  };
}
