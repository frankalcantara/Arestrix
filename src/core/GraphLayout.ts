import { IEdgeData, INodeData } from './types.ts';

export interface IGraphLayoutOptions {
  /** Espaçamento horizontal entre colunas (centro a centro). Padrão: 420 */
  columnSpacing?: number;
  /** Espaçamento vertical entre nós na mesma coluna (centro a centro). Padrão: 230 */
  rowSpacing?: number;
  /** Número máximo de colunas por faixa horizontal antes de quebrar linha. Padrão: 4 (ou 5 em grafos ramificados) */
  maxColumnsPerRow?: number;
  /** Deslocamento vertical alternado em colunas unitárias para evitar colinearidade estrita. Padrão: 40 */
  staggerStepY?: number;
  /** Espaçamento vertical livre entre faixas (tiers) quando houver quebra de linha. Padrão: 280 */
  tierGap?: number;
}

/**
 * Algoritmo de distribuição e organização automática de vértices em 2D
 * baseado em Sugiyama (Layered DAG) com minimização de cruzamentos por baricentro,
 * quebra inteligente de linhas longas (multi-tier wrapping) e escalonamento harmônico.
 */
export class GraphLayout {
  /**
   * Detecta se um conjunto de vértices precisa de organização automática:
   * - Vértices sobrepostos / colidindo
   * - Vértices acumulados em uma linha reta horizontal ou vertical (colineares)
   * - Cadeia excessivamente esticada em uma única faixa horizontal sem distribuição 2D
   */
  public static needsAutoLayout(nodes: INodeData[], _edges: IEdgeData[] = []): boolean {
    if (nodes.length <= 1) return false;

    // 1. Verifica colisão / sobreposição direta entre quaisquer dois vértices
    for (let i = 0; i < nodes.length; i++) {
      const a = nodes[i].position;
      for (let j = i + 1; j < nodes.length; j++) {
        const b = nodes[j].position;
        if (Math.abs(a.x - b.x) < 285 && Math.abs(a.y - b.y) < 135) {
          return true;
        }
      }
    }

    if (nodes.length < 3) return false;

    let minX = Infinity;
    let maxX = -Infinity;
    let minY = Infinity;
    let maxY = -Infinity;

    for (const n of nodes) {
      if (n.position.x < minX) minX = n.position.x;
      if (n.position.x > maxX) maxX = n.position.x;
      if (n.position.y < minY) minY = n.position.y;
      if (n.position.y > maxY) maxY = n.position.y;
    }

    const spanX = maxX - minX;
    const spanY = maxY - minY;

    // 2. Vértices acumulados quase em linha reta horizontal (ex.: todos com y=0 ou y=90/10)
    if (spanY < 140) {
      return true;
    }

    // 3. Vértices acumulados em linha reta vertical
    if (spanX < 140) {
      return true;
    }

    // 4. Cadeia longa (> 4 colunas) excessivamente esticada em faixa única horizontal
    if (nodes.length >= 5 && spanX > 1800 && spanX / Math.max(spanY, 1) > 5.5) {
      return true;
    }

    return false;
  }

  /**
   * Calcula novas coordenadas 2D (x, y) para todos os vértices do grafo
   * no espaço de mundo do Arestrix (+X para a direita, +Y para cima).
   */
  public static computePositions(
    nodes: INodeData[],
    edges: IEdgeData[],
    options: IGraphLayoutOptions = {}
  ): Map<string, { x: number; y: number }> {
    const result = new Map<string, { x: number; y: number }>();
    if (nodes.length === 0) return result;

    if (nodes.length === 1) {
      result.set(nodes[0].id, { x: 0, y: 0 });
      return result;
    }

    const colSpacing = options.columnSpacing ?? 420;
    const baseRowSpacing = options.rowSpacing ?? 250;
    const staggerY = options.staggerStepY ?? 75;
    const tierGap = options.tierGap ?? 280;

    const nodeMap = new Map<string, INodeData>();
    nodes.forEach(n => nodeMap.set(n.id, n));

    // --- FASE 1: Construção do DAG (quebra de ciclos / feedback loops) ---
    const forwardAdj = new Map<string, Array<{ targetId: string; portOrder: number }>>();
    const reverseAdj = new Map<string, Array<{ sourceId: string; portOrder: number }>>();
    nodes.forEach(n => {
      forwardAdj.set(n.id, []);
      reverseAdj.set(n.id, []);
    });

    // Ordena arestas colocando loops explícitos por último
    const sortedEdges = [...edges].sort((a, b) => {
      const aLoop = a.isFeedbackLoop ? 1 : 0;
      const bLoop = b.isFeedbackLoop ? 1 : 0;
      return aLoop - bLoop;
    });

    const canReach = (startId: string, goalId: string): boolean => {
      if (startId === goalId) return true;
      const visited = new Set<string>([startId]);
      const queue: string[] = [startId];
      while (queue.length > 0) {
        const curr = queue.shift()!;
        const nexts = forwardAdj.get(curr) || [];
        for (const nxt of nexts) {
          if (nxt.targetId === goalId) return true;
          if (!visited.has(nxt.targetId)) {
            visited.add(nxt.targetId);
            queue.push(nxt.targetId);
          }
        }
      }
      return false;
    };

    for (const edge of sortedEdges) {
      if (!nodeMap.has(edge.sourceNodeId) || !nodeMap.has(edge.targetNodeId)) continue;
      if (edge.sourceNodeId === edge.targetNodeId) continue;
      if (edge.isFeedbackLoop) continue;

      // Evita fechar ciclo no DAG de camadas
      if (canReach(edge.targetNodeId, edge.sourceNodeId)) {
        continue;
      }

      const srcNode = nodeMap.get(edge.sourceNodeId)!;
      const portIdx = srcNode.outputs.findIndex(p => p.id === edge.sourcePortId);
      const portOrder = portIdx >= 0 ? portIdx : 0;

      forwardAdj.get(edge.sourceNodeId)!.push({ targetId: edge.targetNodeId, portOrder });
      reverseAdj.get(edge.targetNodeId)!.push({ sourceId: edge.sourceNodeId, portOrder });
    }

    // --- FASE 2: Atribuição de Camadas Topológicas (Longest-Path Ranking) ---
    const inDegree = new Map<string, number>();
    nodes.forEach(n => inDegree.set(n.id, (reverseAdj.get(n.id) || []).length));

    const rank = new Map<string, number>();
    nodes.forEach(n => rank.set(n.id, 0));

    const queue: string[] = [];
    nodes.forEach(n => {
      if ((inDegree.get(n.id) || 0) === 0) {
        queue.push(n.id);
      }
    });

    const visitedOrder: string[] = [];
    while (queue.length > 0) {
      const u = queue.shift()!;
      visitedOrder.push(u);
      const uRank = rank.get(u) || 0;
      for (const edge of forwardAdj.get(u) || []) {
        const v = edge.targetId;
        if ((rank.get(v) || 0) < uRank + 1) {
          rank.set(v, uRank + 1);
        }
        inDegree.set(v, (inDegree.get(v) || 1) - 1);
        if (inDegree.get(v) === 0) {
          queue.push(v);
        }
      }
    }

    // Caso restem nós não visitados, atribui rank incremental
    for (const n of nodes) {
      if (!visitedOrder.includes(n.id)) {
        rank.set(n.id, 0);
      }
    }

    // Compactação de fontes: aproxima nós sem entrada (rank 0) de seus sucessores diretos
    for (let i = visitedOrder.length - 1; i >= 0; i--) {
      const u = visitedOrder[i];
      const preds = reverseAdj.get(u) || [];
      const succs = forwardAdj.get(u) || [];
      if (preds.length === 0 && succs.length > 0) {
        const minSuccRank = Math.min(...succs.map(s => rank.get(s.targetId) || 0));
        if (minSuccRank > 1) {
          rank.set(u, minSuccRank - 1);
        }
      }
    }

    // Agrupa nós por rank e compacta ranks vazios
    const rawRanks = Array.from(new Set(Array.from(rank.values()))).sort((a, b) => a - b);
    const layers: string[][] = rawRanks.map(() => []);
    const rankToLayerIdx = new Map<number, number>();
    rawRanks.forEach((r, idx) => rankToLayerIdx.set(r, idx));

    nodes.forEach(n => {
      const r = rank.get(n.id) || 0;
      const lIdx = rankToLayerIdx.get(r) || 0;
      layers[lIdx].push(n.id);
    });

    // --- FASE 3: Minimização de Cruzamentos (Heurística do Baricentro com Ordem de Portas) ---
    const orderInLayer = new Map<string, number>();
    const updateLayerOrders = () => {
      for (const layer of layers) {
        layer.forEach((id, idx) => orderInLayer.set(id, idx));
      }
    };
    updateLayerOrders();

    for (let sweep = 0; sweep < 4; sweep++) {
      // Varredura da esquerda para a direita (usa predecessores e índice da porta de saída)
      for (let l = 1; l < layers.length; l++) {
        const layer = layers[l];
        const barycenters = new Map<string, number>();
        layer.forEach((u, idx) => {
          const preds = reverseAdj.get(u) || [];
          if (preds.length === 0) {
            barycenters.set(u, idx);
          } else {
            const sum = preds.reduce((acc, p) => {
              const predPos = orderInLayer.get(p.sourceId) ?? 0;
              return acc + predPos + p.portOrder * 0.35;
            }, 0);
            barycenters.set(u, sum / preds.length);
          }
        });
        layer.sort((a, b) => (barycenters.get(a) ?? 0) - (barycenters.get(b) ?? 0));
        updateLayerOrders();
      }

      // Varredura da direita para a esquerda (usa sucessores)
      for (let l = layers.length - 2; l >= 0; l--) {
        const layer = layers[l];
        const barycenters = new Map<string, number>();
        layer.forEach((u, idx) => {
          const succs = forwardAdj.get(u) || [];
          if (succs.length === 0) {
            barycenters.set(u, idx);
          } else {
            const sum = succs.reduce((acc, s) => {
              const succPos = orderInLayer.get(s.targetId) ?? 0;
              return acc + succPos - s.portOrder * 0.15;
            }, 0);
            barycenters.set(u, sum / succs.length);
          }
        });
        layer.sort((a, b) => (barycenters.get(a) ?? 0) - (barycenters.get(b) ?? 0));
        updateLayerOrders();
      }
    }

    // --- FASE 4: Atribuição de Coordenadas 2D com Quebra de Linha (Multi-Tier) e Escalonamento ---
    const maxLayerSize = Math.max(...layers.map(l => l.length), 1);
    const maxCols = options.maxColumnsPerRow ?? (maxLayerSize <= 2 ? 4 : 5);

    // Divide as colunas (layers) em faixas horizontais (tiers) de até maxCols colunas
    const tiers: string[][][] = [];
    for (let i = 0; i < layers.length; i += maxCols) {
      tiers.push(layers.slice(i, i + maxCols));
    }

    const estimateNodeHeight = (nodeId: string): number => {
      const n = nodeMap.get(nodeId);
      if (!n) return 150;
      const maxPorts = Math.max(n.inputs?.length || 0, n.outputs?.length || 0, 1);
      return 125 + maxPorts * 26;
    };

    // Calcula altura total de cada tier para empilhá-los de cima (+Y) para baixo (-Y)
    const tierHeights: number[] = tiers.map(tierCols => {
      let maxColHeight = 180;
      for (const col of tierCols) {
        if (col.length === 1) {
          const h = estimateNodeHeight(col[0]) + staggerY * 2;
          if (h > maxColHeight) maxColHeight = h;
        } else {
          const totalNodesH = col.reduce((acc, id) => acc + estimateNodeHeight(id), 0);
          const colH = totalNodesH + (col.length - 1) * Math.max(baseRowSpacing - 140, 80);
          if (colH > maxColHeight) maxColHeight = colH;
        }
      }
      return maxColHeight;
    });

    const rawPositions = new Map<string, { x: number; y: number }>();
    let currentTierCenterY = 0;

    for (let tIdx = 0; tIdx < tiers.length; tIdx++) {
      const tierCols = tiers[tIdx];
      if (tIdx > 0) {
        const prevHalf = tierHeights[tIdx - 1] / 2;
        const currHalf = tierHeights[tIdx] / 2;
        currentTierCenterY -= prevHalf + tierGap + currHalf;
      }

      const numColsInTier = tierCols.length;
      const tierWidth = (numColsInTier - 1) * colSpacing;
      const startX = -tierWidth / 2;

      for (let cIdx = 0; cIdx < numColsInTier; cIdx++) {
        const col = tierCols[cIdx];
        const x = startX + cIdx * colSpacing;

        if (col.length === 1) {
          // Escalonamento vertical harmônico para que nós consecutivos nunca fiquem na mesma reta Y
          const staggerOffset =
            numColsInTier > 1 ? (cIdx % 2 === 0 ? staggerY : -staggerY) : 0;
          rawPositions.set(col[0], {
            x,
            y: currentTierCenterY + staggerOffset
          });
        } else {
          // Distribui múltiplos nós da coluna de cima (+Y) para baixo (-Y) respeitando a altura de cada nó
          const heights = col.map(id => estimateNodeHeight(id));
          const verticalGap = Math.max(baseRowSpacing - 140, 85);
          const totalColSpan =
            heights.reduce((a, b) => a + b, 0) + (col.length - 1) * verticalGap;

          let cursorTopY = currentTierCenterY + totalColSpan / 2;
          for (let rIdx = 0; rIdx < col.length; rIdx++) {
            const nodeH = heights[rIdx];
            const nodeCenterY = cursorTopY - nodeH / 2;
            rawPositions.set(col[rIdx], { x, y: nodeCenterY });
            cursorTopY -= nodeH + verticalGap;
          }
        }
      }
    }

    // Centraliza todo o conjunto ao redor de (0, 0) e alinha na grade de 10px
    let minX = Infinity;
    let maxX = -Infinity;
    let minY = Infinity;
    let maxY = -Infinity;

    for (const pos of rawPositions.values()) {
      if (pos.x < minX) minX = pos.x;
      if (pos.x > maxX) maxX = pos.x;
      if (pos.y < minY) minY = pos.y;
      if (pos.y > maxY) maxY = pos.y;
    }

    const centerX = (minX + maxX) / 2;
    const centerY = (minY + maxY) / 2;

    for (const [nodeId, pos] of rawPositions.entries()) {
      const snappedX = Math.round((pos.x - centerX) / 10) * 10;
      const snappedY = Math.round((pos.y - centerY) / 10) * 10;
      result.set(nodeId, { x: snappedX, y: snappedY });
    }

    return result;
  }
}
