import { Graph } from './Graph.ts';

/**
 * Papel de um vértice no fluxo, do ponto de vista de onde as trilhas começam e terminam.
 * 'both' cobre o vértice solitário, que é entrada e saída ao mesmo tempo.
 */
export type FlowRole = 'start' | 'end' | 'both';

export interface IFlowRoles {
  /** Vértices que iniciam uma trilha (nenhum fluxo chega neles). Ordenados por id. */
  starts: string[];
  /** Vértices que encerram uma trilha (nenhum fluxo sai deles). Ordenados por id. */
  ends: string[];
  /**
   * Só vale exibir os selos quando a topologia tem mais de uma entrada ou mais de uma saída.
   * Com uma única entrada e uma única saída o desenho já é inequívoco e o selo vira ruído.
   */
  ambiguous: boolean;
}

export interface ITrailAssignments {
  /** Entradas na ordem de coloração (ids ordenados). */
  entries: string[];
  /** Índice da trilha (0-based) de cada vértice alcançável a partir de uma entrada. */
  indexByNode: Map<string, number>;
}

/** Uma trilha: a entrada e todos os vértices que ela alcança, em ordem de largura (BFS). */
export interface ITrail {
  entryId: string;
  nodeIds: string[];
}

interface IFlowGraph {
  /** Alvos de chamada: não viram tarefa de fluxo. */
  callTargets: Set<string>;
  /** Arestas de fluxo já sem chamada e sem feedback, restritas a vértices agendados. */
  adjacency: Map<string, string[]>;
}

/**
 * Projeção do grafo usada por todas as análises de fluxo do Studio.
 *
 * As regras acompanham o gerador C++ (TaskflowGenerator) para nenhuma leitura mentir:
 * - arestas `call` são delegação, não precedência, e não contam como fluxo;
 * - aresta marcada como feedback fecha laço e não conta como entrada;
 * - alvo de chamada não vira tarefa de fluxo e fica fora das trilhas.
 */
function buildFlowGraph(graph: Graph): IFlowGraph {
  const edges = graph.getAllEdges();
  const callTargets = new Set<string>();
  for (const edge of edges) {
    if (edge.kind === 'call') callTargets.add(edge.targetNodeId);
  }

  const adjacency = new Map<string, string[]>();
  const connect = (from: string, to: string): void => {
    const list = adjacency.get(from);
    if (list) list.push(to);
    else adjacency.set(from, [to]);
  };

  for (const edge of edges) {
    if (edge.kind === 'call' || edge.isFeedbackLoop) continue;
    if (callTargets.has(edge.sourceNodeId) || callTargets.has(edge.targetNodeId)) continue;
    connect(edge.sourceNodeId, edge.targetNodeId);
  }

  return { callTargets, adjacency };
}

/** Calcula as pontas do fluxo do grafo ativo. */
export function computeFlowRoles(graph: Graph): IFlowRoles {
  const { callTargets, adjacency } = buildFlowGraph(graph);

  const hasIncoming = new Set<string>();
  const hasOutgoing = new Set<string>();
  for (const [source, targets] of adjacency) {
    hasOutgoing.add(source);
    for (const target of targets) hasIncoming.add(target);
  }

  const scheduled = graph.getAllNodes().filter(node => !callTargets.has(node.id));
  const starts = scheduled
    .filter(node => !hasIncoming.has(node.id))
    .map(node => node.id)
    .sort();
  const ends = scheduled
    .filter(node => !hasOutgoing.has(node.id))
    .map(node => node.id)
    .sort();

  return { starts, ends, ambiguous: starts.length > 1 || ends.length > 1 };
}

/** Papel efetivo de cada vértice quando os selos estão visíveis; vazio quando a topologia é inequívoca. */
export function computeFlowRoleMap(roles: IFlowRoles): Map<string, FlowRole> {
  const map = new Map<string, FlowRole>();
  if (!roles.ambiguous) return map;

  for (const id of roles.starts) map.set(id, 'start');
  for (const id of roles.ends) map.set(id, map.has(id) ? 'both' : 'end');
  return map;
}

/**
 * Agrupa os vértices por trilha: cada entrada leva consigo tudo o que alcança por fluxo.
 * Um vértice alcançado por mais de uma entrada fica com a primeira (ordem de id), o que mantém
 * contador, selos e cores sempre coerentes entre si.
 */
export function computeTrails(graph: Graph): ITrail[] {
  const { adjacency } = buildFlowGraph(graph);
  const { starts } = computeFlowRoles(graph);

  const trails: ITrail[] = [];
  const seen = new Set<string>();

  for (const entry of starts) {
    if (seen.has(entry)) continue;
    const nodeIds: string[] = [entry];
    seen.add(entry);

    const queue: string[] = [entry];
    while (queue.length > 0) {
      const current = queue.shift()!;
      for (const next of adjacency.get(current) ?? []) {
        if (seen.has(next)) continue;
        seen.add(next);
        nodeIds.push(next);
        queue.push(next);
      }
    }

    trails.push({ entryId: entry, nodeIds });
  }

  return trails;
}

/** Índice de trilha por vértice, para colorir o canvas. */
export function computeTrailAssignments(graph: Graph): ITrailAssignments {
  const trails = computeTrails(graph);
  const indexByNode = new Map<string, number>();
  trails.forEach((trail, index) => {
    for (const id of trail.nodeIds) {
      if (!indexByNode.has(id)) indexByNode.set(id, index);
    }
  });
  return { entries: trails.map(trail => trail.entryId), indexByNode };
}
