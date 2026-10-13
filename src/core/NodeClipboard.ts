import type { Graph } from './Graph.ts';
import type { INodeData, IPort } from './types.ts';

/**
 * Arestrix - Área de transferência interna de vértices (copiar / recortar / colar / duplicar).
 *
 * Guarda uma cópia profunda do vértice. Ao colar, gera novo id, remapeia os ids das portas
 * e desloca a posição. Arestas não são copiadas (apenas o vértice e suas configurações).
 */

const PASTE_OFFSET = 40;

let clipboard: INodeData | null = null;

export function copyNode(node: INodeData): void {
  clipboard = JSON.parse(JSON.stringify(node));
}

export function hasClipboardNode(): boolean {
  return clipboard !== null;
}

function newNodeId(graph: Graph, type: string): string {
  let id: string;
  do {
    id = `node_${type}_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 6)}`;
  } while (graph.getNode(id));
  return id;
}

function remapPort(port: IPort, oldId: string, newId: string): IPort {
  const id = port.id.startsWith(oldId) ? newId + port.id.slice(oldId.length) : `${newId}_${port.id}`;
  return { ...port, id };
}

/**
 * Cria os dados de um novo vértice a partir de um modelo, sem inseri-lo no grafo.
 */
export function cloneNodeData(graph: Graph, source: INodeData, labelSuffix: string, at?: { x: number; y: number }): INodeData {
  const copy: INodeData = JSON.parse(JSON.stringify(source));
  const id = newNodeId(graph, source.type);
  copy.inputs = copy.inputs.map(p => remapPort(p, source.id, id));
  copy.outputs = copy.outputs.map(p => remapPort(p, source.id, id));
  copy.id = id;
  copy.label = labelSuffix ? `${source.label} ${labelSuffix}` : source.label;
  copy.position = {
    x: at ? at.x : source.position.x + PASTE_OFFSET,
    y: at ? at.y : source.position.y + PASTE_OFFSET,
    z: source.position.z ?? 0
  };
  return copy;
}

/**
 * Gera um vértice a partir do conteúdo copiado. Retorna null se não houver nada copiado.
 * Cada colagem sucessiva desloca mais um passo para não empilhar vértices.
 */
let pasteCount = 0;
export function buildPastedNode(graph: Graph, labelSuffix: string): INodeData | null {
  if (!clipboard) return null;
  pasteCount++;
  const step = PASTE_OFFSET * pasteCount;
  return cloneNodeData(graph, clipboard, labelSuffix, {
    x: clipboard.position.x + step,
    y: clipboard.position.y + step
  });
}

export function resetPasteCounter(): void {
  pasteCount = 0;
}
