import { Graph } from './Graph.ts';
import { INodeData } from './types.ts';
import { computeFlowRoles, computeTrails } from './GraphTopology.ts';

const MAX_HINT_CHARS = 90;

/**
 * Primeira linha de comentário escrita pelo autor no corpo C++ — sinal semântico do que o vértice
 * faz. Comentários gerados pelo template repetem o rótulo e são descartados.
 */
function authorHint(node: INodeData): string {
  const lines = (node.customLambdaBody ?? '')
    .split('\n')
    .map(line => line.trim())
    .filter(Boolean);
  const comment = lines.find(line => line.startsWith('//'));
  if (!comment || comment.includes(node.label)) return '';

  const text = comment.replace(/^\/\/+\s*/, '').trim();
  if (!text) return '';
  return text.length > MAX_HINT_CHARS ? `${text.slice(0, MAX_HINT_CHARS - 1)}…` : text;
}

/** Uma linha por vértice: nome, tipo e o que ele declara produzir/consumir. */
export function describeNodeForAgent(node: INodeData): string {
  const details: string[] = [];

  if (node.type === 'output') {
    const destination = node.outputSettings?.destinationPath?.trim();
    if (destination) details.push(`→ ${destination}`);
  } else if (node.type === 'mcp') {
    const server = node.mcpSettings?.server?.trim();
    const tool = node.mcpSettings?.tool?.trim();
    if (server) details.push(`servidor ${server}${tool ? `/${tool}` : ''}`);
    const file = node.mcpSettings?.output?.file?.trim();
    if (file) details.push(`→ ${file}`);
  } else if (node.type === 'agent') {
    const settings = node.agentSettings;
    const skills = settings?.skills;
    const active = skills?.find(skill => skill.id === settings?.activeSkillId) ?? skills?.[0];
    if (active?.name) details.push(`skill "${active.name}"`);
  } else if (node.type === 'condition' || node.type === 'switch') {
    if (node.outputs.length > 0) {
      details.push(`ramos: ${node.outputs.map(port => port.name).join(' | ')}`);
    }
  } else if (node.type === 'subflow' || node.type === 'module') {
    const inner = node.childGraph?.nodes?.length ?? 0;
    if (inner > 0) details.push(`${inner} vértices internos`);
  } else if (node.type === 'call') {
    details.push('chamada de agente');
  }

  const hint = authorHint(node);
  const head = `${node.label} [${node.type}]`;
  const structured = details.length > 0 ? ` · ${details.join(' · ')}` : '';
  const semantic = hint ? ` — "${hint}"` : '';
  return `${head}${structured}${semantic}`;
}

const INSTRUCTION_PT =
  'Avalie semanticamente se essas trilhas devem convergir e qual é o melhor ponto de junção: ' +
  'use os rótulos, os tipos, as habilidades e os artefatos declarados para decidir. ' +
  'Se a convergência fizer sentido, crie UM vértice do tipo join no melhor ponto e conecte a saída ' +
  'do último vértice de cada trilha a ele, sem remover nem reconectar conexões existentes. ' +
  'Se as trilhas forem independentes de propósito, não altere o grafo. ' +
  'Termine com uma frase curta explicando o critério semântico usado.';

const INSTRUCTION_EN =
  'Decide semantically whether these paths should converge and where the best junction is: ' +
  'use the labels, types, skills and declared artifacts. ' +
  'If convergence makes sense, create ONE join vertex at the best point and connect the output of ' +
  "each path's last vertex to it, without removing or rewiring existing connections. " +
  'If the paths are independent on purpose, leave the graph unchanged. ' +
  'Finish with one short sentence explaining the semantic criterion you used.';

/**
 * Briefing enviado ao Assistente quando o usuário pede uma junção.
 *
 * A enumeração das trilhas é feita aqui (determinística) para o modelo gastar o raciocínio na
 * parte semântica — o que cada trilha produz e se elas devem mesmo se encontrar.
 *
 * `parentPath` é a cadeia de ids de subflow/módulo até o grafo ativo. O Assistente edita a partir
 * da raiz, então o caminho precisa ir no pedido para as ferramentas usarem `parent_path`.
 */
export function buildJunctionPrompt(graph: Graph, locale: string, parentPath: string[] = []): string {
  const isPt = locale !== 'en-US';
  const trails = computeTrails(graph);
  const ends = new Set(computeFlowRoles(graph).ends);
  const lines: string[] = [];

  lines.push(isPt
    ? `O Studio detectou ${trails.length} trilhas independentes ${parentPath.length > 0 ? 'neste subfluxo' : 'neste grafo'}:`
    : `The Studio detected ${trails.length} independent paths ${parentPath.length > 0 ? 'inside this subflow' : 'in this graph'}:`);

  if (parentPath.length > 0) {
    const jsonPath = JSON.stringify(parentPath);
    lines.push('');
    lines.push(isPt
      ? `O grafo ativo está no subfluxo com parent_path ${jsonPath}. Passe parent_path ${jsonPath} em add_node e connect para editar o subgrafo, sem alterar o nível raiz.`
      : `The active graph is the subflow at parent_path ${jsonPath}. Pass parent_path ${jsonPath} to add_node and connect so the subgraph is edited without touching the root level.`);
  }

  trails.forEach((trail, index) => {
    const entry = graph.getNode(trail.entryId);
    const trailEnds = trail.nodeIds.filter(id => ends.has(id));
    const endLabels = trailEnds
      .map(id => graph.getNode(id)?.label ?? id)
      .join(', ');

    lines.push('');
    lines.push(isPt
      ? `Trilha ${index + 1} — início "${entry?.label ?? trail.entryId}" · ${trailEnds.length > 1 ? 'fins' : 'fim'}: ${endLabels}`
      : `Path ${index + 1} — start "${entry?.label ?? trail.entryId}" · end: ${endLabels}`);

    for (const id of trail.nodeIds) {
      const node = graph.getNode(id);
      if (node) lines.push(`  • ${describeNodeForAgent(node)}`);
    }
  });

  lines.push('');
  lines.push(isPt ? INSTRUCTION_PT : INSTRUCTION_EN);
  return lines.join('\n');
}
