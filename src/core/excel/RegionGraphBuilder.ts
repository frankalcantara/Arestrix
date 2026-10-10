import { Graph } from '../Graph.ts';
import { WorkbookModel } from './WorkbookModel.ts';

const X_STEP = 340;
const Y_STEP = 130;

function slug(text: string): string {
  return text
    .normalize('NFD')
    .replace(/[̀-ͯ]/g, '')
    .replace(/[^A-Za-z0-9]+/g, '_')
    .replace(/^_+|_+$/g, '');
}

/** Camadas por caminho mais longo; ciclos entre regiões não travam (limite de iterações). */
function layers(ids: string[], deps: WorkbookModel['dependencies']): Map<string, number> {
  const layer = new Map(ids.map(id => [id, 0]));
  for (let pass = 0; pass < ids.length; pass++) {
    let changed = false;
    for (const d of deps) {
      const next = (layer.get(d.from) ?? 0) + 1;
      if (next > (layer.get(d.to) ?? 0) && next < ids.length) {
        layer.set(d.to, next);
        changed = true;
      }
    }
    if (!changed) break;
  }
  return layer;
}

/**
 * Cria no grafo um nó por região e uma aresta por dependência (a região lida alimenta a que lê).
 * Limpa o grafo antes. Devolve o mapa região → id do nó.
 */
export function buildRegionGraph(model: WorkbookModel, graph: Graph): Map<string, string> {
  graph.clear();
  const regions = model.sheets.flatMap(s => s.regions);
  const layer = layers(regions.map(r => r.id), model.dependencies);
  const rowInLayer = new Map<number, number>();
  const nodeIds = new Map<string, string>();
  const usedLabels = new Set<string>();

  regions.forEach((r, i) => {
    const sheetName = r.id.slice(0, r.id.lastIndexOf('!'));
    let label = `${slug(sheetName) || 'Aba'}_${slug(r.range)}`;
    for (let n = 2; usedLabels.has(label); n++) label = `${slug(sheetName) || 'Aba'}_${slug(r.range)}_${n}`;
    usedLabels.add(label);

    const col = layer.get(r.id) ?? 0;
    const row = rowInLayer.get(col) ?? 0;
    rowInLayer.set(col, row + 1);

    const nodeId = `node_xl_${i}`;
    graph.createNode(nodeId, label, 'static', col * X_STEP, row * Y_STEP);
    nodeIds.set(r.id, nodeId);
  });

  for (const d of model.dependencies) {
    const from = nodeIds.get(d.from);
    const to = nodeIds.get(d.to);
    if (!from || !to) continue;
    const src = graph.getNode(from)!;
    const dst = graph.getNode(to)!;
    graph.createEdge(from, src.outputs[0].id, to, dst.inputs[0].id);
  }
  return nodeIds;
}
