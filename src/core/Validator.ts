import { Graph } from './Graph.ts';
import { IValidationResult, IValidationError, IEdgeData, INodeData } from './types.ts';
import { t } from '../i18n/index.ts';
import { SkillHasher } from './skills/SkillHasher.ts';
import { SkillLedgerService } from './skills/SkillLedgerService.ts';
import { McpLedgerService } from './skills/McpLedgerService.ts';

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

    // 5. Conflito de escrita entre vertices que podem rodar ao mesmo tempo (aviso, nunca erro).
    this.validateResourceConflicts(graph, flowEdges, errors);

    // 6. Interacao humana: formulario e politica de canal validos (autoria; execucao na Fase 2B).
    this.validateInteractions(nodes, errors);

    // 7. Governanca e Validacao de Skills: bloqueia execucao se houver skills nao auditadas ou rejeitadas.
    this.validateAgentSkills(nodes, errors);

    // 8. Governanca e Validacao de Servidores MCP: bloqueia execucao se houver conectores nao homologados ou rejeitados.
    this.validateMcpServers(nodes, errors);

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
    // Vértice Call: a chamada em fluxo precisa de exatamente um alvo declarado.
    for (const node of graph.getAllNodes()) {
      if (node.type !== 'call') continue;
      const outgoing = callEdges.filter(e => e.sourceNodeId === node.id);
      if (outgoing.length !== 1) {
        errors.push({
          type: 'invalid_call',
          severity: 'error',
          message: t('validator.call_vertex_one_target', { label: node.label, id: node.id }),
          nodeIds: [node.id],
          edgeIds: outgoing.map(e => e.id)
        });
      }
    }

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

      if (source.type !== 'agent' && source.type !== 'call') {
        errors.push({
          type: 'invalid_call',
          severity: 'error',
          message: t('validator.call_source', { label: source.label, id: source.id }),
          nodeIds: [source.id],
          edgeIds: [edge.id]
        });
        continue;
      }

      if (target.type !== 'agent' && target.type !== 'interaction' &&
          target.type !== 'subflow' && target.type !== 'module') {
        errors.push({
          type: 'invalid_call',
          severity: 'error',
          message: t('validator.call_target', { label: target.label, id: target.id }),
          nodeIds: [target.id],
          edgeIds: [edge.id]
        });
        continue;
      }
      if ((target.type === 'subflow' || target.type === 'module') &&
          (!target.childGraph || !Array.isArray(target.childGraph.nodes) || target.childGraph.nodes.length === 0)) {
        errors.push({
          type: 'invalid_call',
          severity: 'error',
          message: t('validator.call_target_graph', { label: target.label, id: target.id }),
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

  /**
   * Conflito de escrita entre trilhas que podem rodar ao mesmo tempo.
   *
   * Dois vértices sem caminho de fluxo entre si podem executar em paralelo; se ambos declaram
   * gravar no mesmo arquivo, a última gravação vence sem aviso nenhum em tempo de execução.
   * A comparação usa apenas o que está declarado na configuração do vértice (destino do Output e
   * arquivo de saída do MCP) — corpo C++ livre é opaco de propósito.
   *
   * É sempre aviso: o Studio não consegue provar a intenção do autor e a execução continua
   * exatamente como antes.
   */
  private static validateResourceConflicts(
    graph: Graph,
    flowEdges: IEdgeData[],
    errors: IValidationError[]
  ): void {
    const callTargets = new Set(
      graph.getAllEdges().filter(e => e.kind === 'call').map(e => e.targetNodeId)
    );

    const writers: { node: INodeData; resource: string }[] = [];
    for (const node of graph.getAllNodes()) {
      if (callTargets.has(node.id)) continue;   // alvo puro de chamada não é tarefa de fluxo
      const resource = this.declaredWriteResource(node);
      if (resource) writers.push({ node, resource });
    }
    if (writers.length < 2) return;

    // Arestas de feedback fecham laço e não devem sugerir ordenação entre as trilhas.
    const adjacency = new Map<string, string[]>();
    for (const edge of flowEdges) {
      if (edge.isFeedbackLoop) continue;
      const list = adjacency.get(edge.sourceNodeId);
      if (list) list.push(edge.targetNodeId);
      else adjacency.set(edge.sourceNodeId, [edge.targetNodeId]);
    }

    const reaches = (from: string, to: string): boolean => {
      const seen = new Set<string>([from]);
      const stack: string[] = [from];
      while (stack.length > 0) {
        const current = stack.pop()!;
        for (const next of adjacency.get(current) ?? []) {
          if (next === to) return true;
          if (seen.has(next)) continue;
          seen.add(next);
          stack.push(next);
        }
      }
      return false;
    };

    const reported = new Set<string>();
    for (let i = 0; i < writers.length; i++) {
      for (let j = i + 1; j < writers.length; j++) {
        const first = writers[i];
        const second = writers[j];
        if (first.resource !== second.resource) continue;
        // Há caminho de fluxo entre os dois: um executa depois do outro, não há corrida.
        if (reaches(first.node.id, second.node.id) || reaches(second.node.id, first.node.id)) continue;

        const pairKey = [first.node.id, second.node.id].sort().join('\u0000');
        if (reported.has(pairKey)) continue;
        reported.add(pairKey);

        errors.push({
          type: 'resource_conflict',
          severity: 'warning',
          message: t('validator.resource_conflict', {
            first: first.node.label || first.node.id,
            second: second.node.label || second.node.id,
            path: first.resource
          }),
          nodeIds: [first.node.id, second.node.id]
        });
      }
    }
  }

  /** Arquivo que o vértice declara gravar, normalizado para comparação; null quando não grava nada. */
  private static declaredWriteResource(node: INodeData): string | null {
    let raw = '';
    if (node.type === 'output') {
      raw = node.outputSettings?.destinationPath ?? '';
    } else if (node.type === 'mcp') {
      raw = node.mcpSettings?.output?.file ?? '';
    }
    const normalized = raw
      .trim()
      .replace(/\\/g, '/')
      .replace(/^\.\//, '')
      .replace(/\/{2,}/g, '/');
    return normalized || null;
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

  private static validateAgentSkills(nodes: INodeData[], errors: IValidationError[]): void {
    const ledger = SkillLedgerService.getInstance();

    for (const node of nodes) {
      if (node.type !== 'agent') continue;
      const settings = node.agentSettings;
      if (!settings || !Array.isArray(settings.skills) || settings.skills.length === 0) continue;

      const activeSkill = settings.skills.find(s => s.id === settings.activeSkillId) || settings.skills[0];
      if (!activeSkill) continue;

      const hash = SkillHasher.computeHash(activeSkill);
      const status = ledger.getStatus(hash);

      if (status === 'REJECT') {
        errors.push({
          type: 'invalid_skill',
          severity: 'error',
          message: t('validator.skill_rejected', {
            node: node.label || node.id,
            skill: activeSkill.name || activeSkill.id,
            defaultValue: `O nó "${node.label || node.id}" utiliza a habilidade "${activeSkill.name || activeSkill.id}" que foi REJEITADA pelo Agente Auditor do Studio. Corrija as vulnerabilidades antes de executar.`
          }),
          nodeIds: [node.id]
        });
      } else if (status === 'PENDING') {
        errors.push({
          type: 'invalid_skill',
          severity: 'error',
          message: t('validator.skill_unapproved', {
            node: node.label || node.id,
            skill: activeSkill.name || activeSkill.id,
            defaultValue: `O nó "${node.label || node.id}" possui a habilidade "${activeSkill.name || activeSkill.id}" modificada ou pendente de validação. Audite-a com o Arestrix Skill Guard antes de executar.`
          }),
          nodeIds: [node.id]
        });
      }
    }
  }

  private static validateMcpServers(nodes: INodeData[], errors: IValidationError[]): void {
    const ledger = McpLedgerService.getInstance();

    for (const node of nodes) {
      // 1. Vértices do tipo 'mcp'
      if (node.type === 'mcp') {
        const serverId = node.mcpSettings?.server;
        if (!serverId || serverId.trim() === '') {
          errors.push({
            type: 'invalid_mcp',
            severity: 'warning',
            message: t('validator.mcp_missing_server', {
              node: node.label || node.id,
              defaultValue: `O nó MCP "${node.label || node.id}" não possui nenhum servidor configurado.`
            }),
            nodeIds: [node.id]
          });
          continue;
        }

        const status = ledger.getStatusByServerId(serverId);
        if (status === 'REJECT') {
          errors.push({
            type: 'invalid_mcp',
            severity: 'error',
            message: t('validator.mcp_rejected', {
              node: node.label || node.id,
              server: serverId,
              defaultValue: `O nó MCP "${node.label || node.id}" utiliza o servidor "${serverId}" que foi REJEITADO pela governança de segurança do Studio. Corrija as vulnerabilidades antes de executar.`
            }),
            nodeIds: [node.id]
          });
        } else if (status === 'PENDING') {
          errors.push({
            type: 'invalid_mcp',
            severity: 'error',
            message: t('validator.mcp_unapproved', {
              node: node.label || node.id,
              server: serverId,
              defaultValue: `O nó MCP "${node.label || node.id}" referencia o servidor "${serverId}" pendente de auditoria ou com definição modificada. Audite-o com o SkillSpector antes de executar.`
            }),
            nodeIds: [node.id]
          });
        }
      }

      // 2. Vértices do tipo 'agent' que utilizam servidores MCP em codeMode
      if (node.type === 'agent') {
        const mcpServers = node.agentSettings?.codeMode?.mcpServers;
        if (Array.isArray(mcpServers) && mcpServers.length > 0) {
          for (const serverId of mcpServers) {
            if (!serverId || serverId.trim() === '') continue;
            const status = ledger.getStatusByServerId(serverId);
            if (status === 'REJECT') {
              errors.push({
                type: 'invalid_mcp',
                severity: 'error',
                message: t('validator.mcp_rejected', {
                  node: node.label || node.id,
                  server: serverId,
                  defaultValue: `O nó de Agente "${node.label || node.id}" referencia o servidor MCP "${serverId}" que foi REJEITADO pela governança do Studio. Remova o servidor ou corrija as vulnerabilidades.`
                }),
                nodeIds: [node.id]
              });
            } else if (status === 'PENDING') {
              errors.push({
                type: 'invalid_mcp',
                severity: 'error',
                message: t('validator.mcp_unapproved', {
                  node: node.label || node.id,
                  server: serverId,
                  defaultValue: `O nó de Agente "${node.label || node.id}" referencia o servidor MCP "${serverId}" pendente de homologação. Audite-o no Studio antes de executar.`
                }),
                nodeIds: [node.id]
              });
            }
          }
        }
      }
    }
  }
}
