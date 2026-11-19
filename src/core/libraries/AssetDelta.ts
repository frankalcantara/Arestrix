import type { IGraphSchema, INodeAgentSettings, ReasoningEffortLevel } from '../types.ts';

export interface IGraphDelta {
  nodesAdded: number;
  nodesRemoved: number;
  nodesChanged: number;
  edgesAdded: number;
  edgesRemoved: number;
}

const AGENT_SETTING_LABELS: Array<[keyof INodeAgentSettings, string]> = [
  ['sandbox', 'Sandbox'],
  ['model', 'Modelo'],
  ['codeMode', 'Modo de código'],
  ['memory', 'Memória'],
  ['compression', 'Compressão'],
  ['skills', 'Skills'],
  ['activeSkillId', 'Skill ativa'],
  ['validationRules', 'Validação de artefatos'],
  ['isSerialAcrossBatch', 'Execução serial']
];

function canonicalize(value: unknown): unknown {
  if (Array.isArray(value)) return value.map(canonicalize);
  if (value && typeof value === 'object') {
    const out: Record<string, unknown> = {};
    for (const key of Object.keys(value as Record<string, unknown>).sort()) {
      const entry = (value as Record<string, unknown>)[key];
      if (entry !== undefined) out[key] = canonicalize(entry);
    }
    return out;
  }
  return value;
}

function stable(value: unknown): string {
  if (value === undefined) return 'null';
  try {
    return JSON.stringify(canonicalize(value));
  } catch {
    return String(value);
  }
}

function modelWithoutReasoningEffort(
  model: INodeAgentSettings['model'] | undefined
): Record<string, unknown> | undefined {
  if (!model) return undefined;
  const { reasoningEffort: _effort, ...rest } = model;
  return rest;
}

function modelReasoningEffort(
  model: INodeAgentSettings['model'] | undefined
): ReasoningEffortLevel {
  return model?.reasoningEffort ?? 'medium';
}

function nodeComparable(node: IGraphSchema['nodes'][number]): string {
  const { position, ...rest } = node;
  void position;
  return stable(rest);
}

/** Diff estrutural usado no aviso de atualização antes de substituir um pipeline. */
export function diffGraphSchemas(
  current: IGraphSchema | undefined,
  next: IGraphSchema | undefined
): IGraphDelta {
  const currentNodes = new Map((current?.nodes || []).map(node => [node.id, node]));
  const nextNodes = new Map((next?.nodes || []).map(node => [node.id, node]));
  const currentEdges = new Map((current?.edges || []).map(edge => [edge.id, edge]));
  const nextEdges = new Map((next?.edges || []).map(edge => [edge.id, edge]));

  let nodesAdded = 0;
  let nodesRemoved = 0;
  let nodesChanged = 0;
  for (const [id, nextNode] of nextNodes) {
    const currentNode = currentNodes.get(id);
    if (!currentNode) nodesAdded += 1;
    else if (nodeComparable(currentNode) !== nodeComparable(nextNode)) nodesChanged += 1;
  }
  for (const id of currentNodes.keys()) {
    if (!nextNodes.has(id)) nodesRemoved += 1;
  }

  let edgesAdded = 0;
  let edgesRemoved = 0;
  for (const id of nextEdges.keys()) {
    if (!currentEdges.has(id)) edgesAdded += 1;
  }
  for (const id of currentEdges.keys()) {
    if (!nextEdges.has(id)) edgesRemoved += 1;
  }

  return { nodesAdded, nodesRemoved, nodesChanged, edgesAdded, edgesRemoved };
}

/** Lista legível das partes de `agentSettings` que mudaram. */
export function diffAgentSettings(
  current: INodeAgentSettings | undefined,
  next: INodeAgentSettings | undefined
): string[] {
  const changed: string[] = [];
  for (const [key, label] of AGENT_SETTING_LABELS) {
    if (key === 'model') {
      const currentModel = current?.model;
      const nextModel = next?.model;
      if (stable(modelWithoutReasoningEffort(currentModel)) !== stable(modelWithoutReasoningEffort(nextModel))) {
        changed.push(label);
      }
      if (modelReasoningEffort(currentModel) !== modelReasoningEffort(nextModel)) {
        changed.push('Esforço de raciocínio');
      }
      continue;
    }
    if (stable(current?.[key]) !== stable(next?.[key])) changed.push(label);
  }
  return changed;
}
