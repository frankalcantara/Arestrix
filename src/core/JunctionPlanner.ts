import { Graph } from './Graph.ts';
import { INodeData } from './types.ts';
import { computeFlowRoles, computeTrails } from './GraphTopology.ts';

/**
 * Avaliação local (sem IA) de se as trilhas independentes devem convergir.
 *
 * É a rede de segurança do "Unir trilhas com o Assistente": quando não há modelo configurado, o
 * Studio ainda consegue propor uma junção e — mais importante — dizer por quê. Os sinais são os
 * mesmos que o briefing entrega ao modelo (artefatos, pasta, habilidade, servidor MCP, nomes),
 * só que pontuados de forma determinística e auditável.
 */
export interface IJunctionEvaluation {
  shouldJoin: boolean;
  score: number;
  /** Explicação pronta para o usuário, no idioma pedido. */
  rationale: string;
  endIds: string[];
}

export interface IJunctionEdit {
  node: INodeData;
  edges: Array<{
    id: string;
    sourceNodeId: string;
    sourcePortId: string;
    targetNodeId: string;
    targetPortId: string;
    kind: 'flow';
  }>;
}

const STOP_WORDS = new Set([
  'para', 'com', 'sem', 'dos', 'das', 'por', 'uma', 'que', 'the', 'and', 'for', 'with', 'from',
  'de', 'da', 'do', 'em', 'no', 'na', 'os', 'as', 'um', 'ao', 'à', 'e', 'o', 'a'
]);

function normalizeText(value: string): string {
  return value
    .normalize('NFD')
    .replace(/[\u0300-\u036f]/g, '')
    .toLowerCase()
    .trim();
}

function labelTokens(label: string): Set<string> {
  const tokens = normalizeText(label)
    .split(/[^a-z0-9]+/)
    .filter(token => token.length >= 4 && !STOP_WORDS.has(token));
  return new Set(tokens);
}

function fileType(path: string): string {
  const ext = normalizeText(path).split('.').pop() ?? '';
  const map: Record<string, string> = {
    md: 'markdown', markdown: 'markdown', svg: 'svg', json: 'json', pdf: 'pdf', zip: 'zip',
    png: 'imagem', jpg: 'imagem', jpeg: 'imagem', txt: 'texto', csv: 'planilha', xlsx: 'planilha'
  };
  return map[ext] ?? ext;
}

function outputPath(node: INodeData): string {
  if (node.type === 'output') return (node.outputSettings?.destinationPath ?? '').trim();
  if (node.type === 'mcp') return (node.mcpSettings?.output?.file ?? '').trim();
  return '';
}

function directoryOf(path: string): string {
  const normalized = path.replace(/\\/g, '/').replace(/^\.\//, '');
  const slash = normalized.lastIndexOf('/');
  return slash > 0 ? normalized.slice(0, slash) : '';
}

function artifactTypeOf(node: INodeData): string {
  if (node.type === 'output') {
    const declared = node.outputSettings?.artifactType;
    if (declared && declared !== 'auto') return declared;
  }
  return fileType(outputPath(node));
}

function activeSkillName(node: INodeData): string {
  const settings = node.agentSettings;
  const skills = settings?.skills;
  if (!skills || skills.length === 0) return '';
  const active = skills.find(skill => skill.id === settings?.activeSkillId) ?? skills[0];
  return (active?.name ?? '').trim();
}

interface ITrailFacts {
  endIds: string[];
  path: string;
  directory: string;
  type: string;
  skill: string;
  mcpServer: string;
  tokens: Set<string>;
}

function collectFacts(graph: Graph): ITrailFacts[] {
  const trails = computeTrails(graph);
  const ends = new Set(computeFlowRoles(graph).ends);

  return trails.map(trail => {
    const nodes = trail.nodeIds
      .map(id => graph.getNode(id))
      .filter((node): node is INodeData => Boolean(node));
    const trailEnds = nodes.filter(node => ends.has(node.id));

    const tokens = new Set<string>();
    const skills = new Set<string>();
    const mcpServers = new Set<string>();
    for (const node of nodes) {
      for (const token of labelTokens(node.label)) tokens.add(token);
      const skill = activeSkillName(node);
      if (skill) skills.add(skill);
      if (node.type === 'mcp' && node.mcpSettings?.server) mcpServers.add(node.mcpSettings.server);
    }

    const last = trailEnds[trailEnds.length - 1] ?? nodes[nodes.length - 1];
    return {
      endIds: trailEnds.map(node => node.id),
      path: last ? outputPath(last) : '',
      directory: last ? directoryOf(outputPath(last)) : '',
      type: last ? artifactTypeOf(last) : '',
      skill: [...skills][0] ?? '',
      mcpServer: [...mcpServers][0] ?? '',
      tokens
    };
  });
}

function sharedValues(values: string[]): string[] {
  const counts = new Map<string, number>();
  for (const value of values) {
    if (!value) continue;
    counts.set(value, (counts.get(value) ?? 0) + 1);
  }
  return [...counts.entries()].filter(([, count]) => count > 1).map(([value]) => value);
}

/** Avalia se as trilhas compartilham sinais semânticos suficientes para justificar uma junção. */
export function evaluateJunction(graph: Graph, locale: string = 'pt-BR'): IJunctionEvaluation {
  const isPt = locale !== 'en-US';
  const facts = collectFacts(graph);
  const endIds = facts.flatMap(trail => trail.endIds);

  if (facts.length < 2) {
    return {
      shouldJoin: false,
      score: 0,
      rationale: isPt
        ? 'O grafo não tem duas trilhas independentes para unir.'
        : 'The graph has no two independent paths to merge.',
      endIds: []
    };
  }

  const reasons: string[] = [];
  let score = 0;

  const directories = sharedValues(facts.map(trail => trail.directory));
  if (directories.length > 0) {
    score += 2;
    reasons.push(isPt ? `entregam na mesma pasta (${directories[0]})` : `they deliver to the same folder (${directories[0]})`);
  }

  const types = sharedValues(facts.map(trail => trail.type));
  if (types.length > 0) {
    score += 2;
    reasons.push(isPt ? `produzem o mesmo tipo de artefato (${types[0]})` : `they produce the same artifact type (${types[0]})`);
  }

  const skills = sharedValues(facts.map(trail => trail.skill));
  if (skills.length > 0) {
    score += 2;
    reasons.push(isPt ? `usam a mesma habilidade ("${skills[0]}")` : `they use the same skill ("${skills[0]}")`);
  }

  const servers = sharedValues(facts.map(trail => trail.mcpServer));
  if (servers.length > 0) {
    score += 2;
    reasons.push(isPt ? `chamam o mesmo servidor MCP ("${servers[0]}")` : `they call the same MCP server ("${servers[0]}")`);
  }

  const tokenCounts = new Map<string, number>();
  for (const trail of facts) {
    for (const token of trail.tokens) tokenCounts.set(token, (tokenCounts.get(token) ?? 0) + 1);
  }
  const sharedToken = [...tokenCounts.entries()].find(([, count]) => count > 1)?.[0];
  if (sharedToken) {
    score += 1;
    reasons.push(isPt ? `compartilham o termo "${sharedToken}" nos nomes` : `they share the term "${sharedToken}" in their names`);
  }

  const distinctDirectories = new Set(facts.map(trail => trail.directory).filter(Boolean));
  const distinctTypes = new Set(facts.map(trail => trail.type).filter(Boolean));
  if (distinctDirectories.size === facts.length && distinctTypes.size === facts.length) {
    score -= 2;
  }

  const shouldJoin = score >= 2;
  let rationale: string;
  if (shouldJoin) {
    const list = reasons.slice(0, 3).join(isPt ? '; ' : '; ');
    rationale = isPt
      ? `Sugestão local: as trilhas devem convergir porque ${list}. A junção entra depois do último vértice de cada trilha.`
      : `Local suggestion: the paths should converge because ${list}. The junction goes after the last vertex of each path.`;
  } else {
    rationale = isPt
      ? 'Sugestão local: as trilhas parecem independentes — não há pasta, tipo de artefato, habilidade, servidor ou termo em comum que justifique uni-las. Nada foi alterado.'
      : 'Local suggestion: the paths look independent — no shared folder, artifact type, skill, server or term justifies merging them. Nothing was changed.';
  }

  return { shouldJoin, score, rationale, endIds };
}

/** Constrói o vértice de junção e as arestas que ligam cada fim de trilha a ele (sem tocar no grafo). */
export function buildJunctionEdit(graph: Graph, endIds: string[], locale: string = 'pt-BR'): IJunctionEdit | null {
  const isPt = locale !== 'en-US';
  const ends = endIds
    .map(id => graph.getNode(id))
    .filter((node): node is INodeData => Boolean(node));
  if (ends.length < 2) return null;

  const ports = ends.map(node => node.outputs.find(port => port.type === 'flow') ?? node.outputs[0]);
  if (ports.some(port => !port)) return null;

  let joinId = 'join_trilhas';
  let suffix = 2;
  while (graph.getNode(joinId)) {
    joinId = `join_trilhas_${suffix++}`;
  }

  const label = isPt ? 'Junção das trilhas' : 'Path junction';
  const x = Math.max(...ends.map(node => node.position.x)) + 360;
  const y = ends.reduce((sum, node) => sum + node.position.y, 0) / ends.length;
  const node = graph.buildNode(joinId, label, 'join', x, y);

  const targetPortId = node.inputs[0].id;
  const edges = ends.map((end, index) => {
    const sourcePort = ports[index]!;
    return {
      id: `edge_${end.id}_${sourcePort.id}_to_${joinId}_${targetPortId}`,
      sourceNodeId: end.id,
      sourcePortId: sourcePort.id,
      targetNodeId: joinId,
      targetPortId,
      kind: 'flow' as const
    };
  });

  return { node, edges };
}
