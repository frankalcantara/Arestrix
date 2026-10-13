import { Graph } from './Graph.ts';
import { IValidationResult, IValidationError } from './types.ts';
import { t } from '../i18n/index.ts';

export class GraphValidator {
  static validate(graph: Graph): IValidationResult {
    const errors: IValidationError[] = [];
    const nodes = graph.getAllNodes();
    const edges = graph.getAllEdges();

    // 1. Verificação de nós órfãos
    nodes.forEach(node => {
      const connectedEdges = graph.getEdgesForNode(node.id);
      if (connectedEdges.length === 0 && nodes.length > 1) {
        errors.push({
          type: 'orphan',
          severity: 'warning',
          message: t('validator.orphan', { label: node.label, id: node.id }),
          nodeIds: [node.id]
        });
      }
    });

    // 2. Verificação de portas em nós de condição
    nodes.filter(n => n.type === 'condition' || n.type === 'switch').forEach(condNode => {
      condNode.outputs.forEach((port, portIdx) => {
        // O 'default' do switch (última porta) pode ficar desconectado: o fluxo apenas termina ali.
        if (condNode.type === 'switch' && portIdx === condNode.outputs.length - 1) return;
        const hasConnection = edges.some(
          e => e.sourceNodeId === condNode.id && e.sourcePortId === port.id
        );
        if (!hasConnection) {
          errors.push({
            type: 'missing_branch',
            severity: 'warning',
            message: t('validator.missing_branch', { label: condNode.label, name: port.name }),
            nodeIds: [condNode.id]
          });
        }
      });
    });

    // 3. Detecção de Ciclos com Tarjan / DFS
    // No Taskflow, ciclos são permitidos SE E SOMENTE SE contêm pelo menos um nó do tipo 'condition'
    // que atue como sentinela de controle de branch para quebrar o ciclo em runtime.
    const cycles = this.detectCycles(graph);
    cycles.forEach(cycle => {
      const hasConditionNode = cycle.some(nodeId => {
        const node = graph.getNode(nodeId);
        return node && (node.type === 'condition' || node.type === 'switch');
      });

      if (!hasConditionNode) {
        // Ciclo estático fatal! O Taskflow entraria em deadlock ou compilaria um loop infinito estático.
        const cycleLabels = cycle.map(id => graph.getNode(id)?.label || id).join(' -> ');
        errors.push({
          type: 'cycle',
          severity: 'error',
          message: t('validator.cycle', { cycle: cycleLabels }),
          nodeIds: cycle
        });
      }
    });

    return {
      isValid: errors.filter(e => e.severity === 'error').length === 0,
      errors
    };
  }

  private static detectCycles(graph: Graph): string[][] {
    const nodes = graph.getAllNodes();
    const edges = graph.getAllEdges();

    const adjacency = new Map<string, string[]>();
    nodes.forEach(n => adjacency.set(n.id, []));
    edges.forEach(e => {
      if (adjacency.has(e.sourceNodeId)) {
        adjacency.get(e.sourceNodeId)!.push(e.targetNodeId);
      }
    });

    const visited = new Map<string, boolean>();
    const recStack = new Map<string, boolean>();
    const path: string[] = [];
    const foundCycles: string[][] = [];

    const dfs = (nodeId: string) => {
      visited.set(nodeId, true);
      recStack.set(nodeId, true);
      path.push(nodeId);

      const neighbors = adjacency.get(nodeId) || [];
      for (const neighbor of neighbors) {
        if (!visited.get(neighbor)) {
          dfs(neighbor);
        } else if (recStack.get(neighbor)) {
          // Ciclo encontrado! Extrai o subcaminho do ciclo
          const cycleStartIndex = path.indexOf(neighbor);
          if (cycleStartIndex !== -1) {
            foundCycles.push([...path.slice(cycleStartIndex), neighbor]);
          }
        }
      }

      recStack.set(nodeId, false);
      path.pop();
    };

    nodes.forEach(n => {
      if (!visited.get(n.id)) {
        dfs(n.id);
      }
    });

    return foundCycles;
  }
}
