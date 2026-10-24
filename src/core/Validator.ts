import { Graph } from './Graph.ts';
import { IValidationResult, IValidationError, IEdgeData, INodeData } from './types.ts';
import { t } from '../i18n/index.ts';

export class GraphValidator {
  static validate(graph: Graph): IValidationResult {
    const errors: IValidationError[] = [];
    const nodes = graph.getAllNodes();
    const edges = graph.getAllEdges();

    // Chamadas nao sao dependencias de fluxo: validamos os dois grafos separadamente.
    const flowEdges = edges.filter(e => (e.kind ?? 'flow') !== 'call');
    const callEdges = edges.filter(e => e.kind === 'call');

    // 1. Verificação de nós órfãos (qualquer aresta conta, inclusive chamada).
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

    // 2. Verificação de portas em nós de condição (somente arestas de fluxo).
    nodes.filter(n => n.type === 'condition' || n.type === 'switch').forEach(condNode => {
      condNode.outputs.forEach((port, portIdx) => {
        // O 'default' do switch (última porta) pode ficar desconectado: o fluxo apenas termina ali.
        if (condNode.type === 'switch' && portIdx === condNode.outputs.length - 1) return;
        const hasConnection = flowEdges.some(
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

    // 3. Detecção de ciclos de FLUXO com Tarjan / DFS.
    // No Taskflow, ciclos são permitidos SE E SOMENTE SE contêm pelo menos um nó do tipo
    // 'condition'/'switch' que atue como sentinela de controle de branch.
    const cycles = this.detectCycles(graph, flowEdges);
    cycles.forEach(cycle => {
      const hasConditionNode = cycle.some(nodeId => {
        const node = graph.getNode(nodeId);
        return node && (node.type === 'condition' || node.type === 'switch');
      });

      if (!hasConditionNode) {
        const cycleLabels = cycle.map(id => graph.getNode(id)?.label || id).join(' -> ');
        errors.push({
          type: 'cycle',
          severity: 'error',
          message: t('validator.cycle', { cycle: cycleLabels }),
          nodeIds: cycle
        });
      }
    });

    // 4. Grafo de chamadas: alvos declarados, sem misturar fluxo e chamada no v1.
    this.validateCalls(graph, flowEdges, callEdges, errors);

    // 5. Interacao humana: formulario e politica de canal validos (autoria; execucao na Fase 2B).
    this.validateInteractions(nodes, errors);

    return {
      isValid: errors.filter(e => e.severity === 'error').length === 0,
      errors
    };
  }

  private static validateCalls(
    graph: Graph,
    flowEdges: IEdgeData[],
    callEdges: IEdgeData[],
    errors: IValidationError[]
  ): void {
    for (const edge of callEdges) {
      const source = graph.getNode(edge.sourceNodeId);
      const target = graph.getNode(edge.targetNodeId);

      if (!source || !target) {
        errors.push({
          type: 'invalid_call',
          severity: 'error',
          message: t('validator.call_missing', { id: edge.id }),
          edgeIds: [edge.id]
        });
        continue;
      }

      if (source.type !== 'agent') {
        errors.push({
          type: 'invalid_call',
          severity: 'error',
          message: t('validator.call_source', { label: source.label, id: source.id }),
          nodeIds: [source.id],
          edgeIds: [edge.id]
        });
        continue;
      }

      if (target.type !== 'agent' && target.type !== 'interaction') {
        errors.push({
          type: 'invalid_call',
          severity: 'error',
          message: t('validator.call_target', { label: target.label, id: target.id }),
          nodeIds: [target.id],
          edgeIds: [edge.id]
        });
        continue;
      }

      const maxCalls = edge.callSettings?.maxCalls ?? 1;
      if (maxCalls < 1) {
        errors.push({
          type: 'invalid_call',
          severity: 'error',
          message: t('validator.call_max', { from: source.label, to: target.label }),
          nodeIds: [source.id, target.id],
          edgeIds: [edge.id]
        });
      }

      // v1: o chamavel nao participa do fluxo (evita execucao dupla silenciosa).
      const targetInFlow = flowEdges.some(
        f => f.sourceNodeId === target.id || f.targetNodeId === target.id
      );
      if (targetInFlow) {
        errors.push({
          type: 'invalid_call',
          severity: 'error',
          message: t('validator.call_target_scheduled', { label: target.label, id: target.id }),
          nodeIds: [target.id],
          edgeIds: [edge.id]
        });
      }

      const duplicateFlow = flowEdges.some(
        f => f.sourceNodeId === edge.sourceNodeId && f.targetNodeId === edge.targetNodeId
      );
      if (duplicateFlow) {
        errors.push({
          type: 'invalid_call',
          severity: 'error',
          message: t('validator.call_duplicate', { from: source.label, to: target.label }),
          nodeIds: [source.id, target.id],
          edgeIds: [edge.id]
        });
      }
    }

    // v1: ciclos de chamada sao erro ate existir um limite de profundidade aplicado no runtime.
    const callCycles = this.detectCycles(graph, callEdges);
    callCycles.forEach(cycle => {
      const labels = cycle.map(id => graph.getNode(id)?.label || id).join(' -> ');
      errors.push({
        type: 'call_cycle',
        severity: 'error',
        message: t('validator.call_cycle', { cycle: labels }),
        nodeIds: cycle
      });
    });
  }

  private static validateInteractions(nodes: INodeData[], errors: IValidationError[]): void {
    for (const node of nodes) {
      if (node.type !== 'interaction') continue;
      const settings = node.interactionSettings;
      if (!settings || !settings.form) {
        errors.push({
          type: 'invalid_interaction',
          severity: 'error',
          message: t('validator.interaction_missing', { label: node.label, id: node.id }),
          nodeIds: [node.id]
        });
        continue;
      }

      const fields = Array.isArray(settings.form.fields) ? settings.form.fields : [];
      const schemaSource = settings.schemaSource ?? 'static';
      if (schemaSource !== 'static' && schemaSource !== 'vertex') {
        errors.push({
          type: 'invalid_interaction',
          severity: 'error',
          message: t('validator.interaction_schema_source', { label: node.label }),
          nodeIds: [node.id]
        });
      }
      if (settings.generator?.fallback && !['fail', 'free_text'].includes(settings.generator.fallback)) {
        errors.push({
          type: 'invalid_interaction',
          severity: 'error',
          message: t('validator.interaction_fallback', { label: node.label }),
          nodeIds: [node.id]
        });
      }
      if (schemaSource === 'static' && fields.length === 0) {
        errors.push({
          type: 'invalid_interaction',
          severity: 'error',
          message: t('validator.interaction_empty_form', { label: node.label }),
          nodeIds: [node.id]
        });
      }

      const seen = new Set<string>();
      const fieldsToValidate = schemaSource === 'static' ? fields : [];
      for (const field of fieldsToValidate) {
        if (!field.id || seen.has(field.id)) {
          errors.push({
            type: 'invalid_interaction',
            severity: 'error',
            message: t('validator.interaction_field_id', { label: node.label }),
            nodeIds: [node.id]
          });
        } else {
          seen.add(field.id);
        }
        if (!field.label) {
          errors.push({
            type: 'invalid_interaction',
            severity: 'error',
            message: t('validator.interaction_field_label', { label: node.label, id: field.id || '?' }),
            nodeIds: [node.id]
          });
        }
        if (field.type === 'choice' || field.type === 'multi_choice') {
          const options = field.options ?? [];
          if (options.length < 2) {
            errors.push({
              type: 'invalid_interaction',
              severity: 'error',
              message: t('validator.interaction_options', { label: node.label, id: field.id || '?' }),
              nodeIds: [node.id]
            });
          }
        }
      }

      if (!['studio', 'runner', 'cli', 'any'].includes(settings.channel)) {
        errors.push({
          type: 'invalid_interaction',
          severity: 'error',
          message: t('validator.interaction_channel', { label: node.label }),
          nodeIds: [node.id]
        });
      }
      if (settings.timeoutSeconds < 0) {
        errors.push({
          type: 'invalid_interaction',
          severity: 'error',
          message: t('validator.interaction_timeout', { label: node.label }),
          nodeIds: [node.id]
        });
      }
    }
  }

  private static detectCycles(graph: Graph, edges: IEdgeData[]): string[][] {
    const nodes = graph.getAllNodes();

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
