import { describe, it, expect, beforeEach } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { GraphValidator } from '../src/core/Validator.ts';
import { McpLedgerService } from '../src/core/skills/McpLedgerService.ts';
import { EventBus } from '../src/core/EventBus.ts';

describe('Governança MCP - Fase 4: Gatekeeper no Validator.ts (Bloqueio F5 de MCPs Não-Homologados)', () => {
  let bus: EventBus;
  let ledger: McpLedgerService;
  let graph: Graph;

  beforeEach(() => {
    bus = new EventBus();
    ledger = McpLedgerService.resetForTesting(bus);
    graph = new Graph(bus);
  });

  it('deve permitir a execução de grafo contendo nó MCP com conector nativo C++ (arestrix.arquivos)', () => {
    const nodeA = graph.createNode('node_start', 'Início', 'static', 0, 0);
    const nodeMcp = graph.createNode('node_mcp', 'Ler Arquivo Nativo', 'mcp', 200, 0);
    graph.updateNode(nodeMcp.id, {
      mcpSettings: {
        server: 'arestrix.arquivos',
        tool: 'read_file',
        arguments: { path: 'data.txt' },
        source: { kind: 'none' },
        output: {}
      }
    });
    const nodeEnd = graph.createNode('node_end', 'Fim', 'static', 400, 0);

    graph.addEdge({
      id: 'e1',
      sourceNodeId: nodeA.id,
      sourcePortId: nodeA.outputs[0].id,
      targetNodeId: nodeMcp.id,
      targetPortId: nodeMcp.inputs[0].id
    });

    graph.addEdge({
      id: 'e2',
      sourceNodeId: nodeMcp.id,
      sourcePortId: nodeMcp.outputs[0].id,
      targetNodeId: nodeEnd.id,
      targetPortId: nodeEnd.inputs[0].id
    });

    const result = GraphValidator.validate(graph);
    const mcpErrors = result.errors.filter(e => e.type === 'invalid_mcp');

    expect(mcpErrors).toHaveLength(0);
    expect(result.isValid).toBe(true);
  });

  it('deve bloquear execução (isValid: false) se nó MCP utilizar servidor rejeitado (REJECT)', () => {
    ledger.recordRejection(
      { id: 'rogue-mcp', name: 'Rogue MCP Server' },
      'Injeção de prompt detectada nas descrições de ferramentas.'
    );

    const nodeA = graph.createNode('node_start', 'Início', 'static', 0, 0);
    const nodeMcp = graph.createNode('node_mcp', 'Nó Vulnerável', 'mcp', 200, 0);
    graph.updateNode(nodeMcp.id, {
      mcpSettings: {
        server: 'rogue-mcp',
        tool: 'steal_keys',
        arguments: {},
        source: { kind: 'none' },
        output: {}
      }
    });

    graph.addEdge({
      id: 'e1',
      sourceNodeId: nodeA.id,
      sourcePortId: nodeA.outputs[0].id,
      targetNodeId: nodeMcp.id,
      targetPortId: nodeMcp.inputs[0].id
    });

    const result = GraphValidator.validate(graph);
    const mcpErrors = result.errors.filter(e => e.type === 'invalid_mcp');

    expect(result.isValid).toBe(false);
    expect(mcpErrors.length).toBeGreaterThanOrEqual(1);
    expect(mcpErrors[0].severity).toBe('error');
    expect(mcpErrors[0].message).toContain('REJEITADO');
  });

  it('deve bloquear execução se nó MCP utilizar servidor não auditado (PENDING)', () => {
    const nodeA = graph.createNode('node_start', 'Início', 'static', 0, 0);
    const nodeMcp = graph.createNode('node_mcp', 'Nó Não Auditado', 'mcp', 200, 0);
    graph.updateNode(nodeMcp.id, {
      mcpSettings: {
        server: 'unknown-third-party-server',
        tool: 'some_tool',
        arguments: {},
        source: { kind: 'none' },
        output: {}
      }
    });

    graph.addEdge({
      id: 'e1',
      sourceNodeId: nodeA.id,
      sourcePortId: nodeA.outputs[0].id,
      targetNodeId: nodeMcp.id,
      targetPortId: nodeMcp.inputs[0].id
    });

    const result = GraphValidator.validate(graph);
    const mcpErrors = result.errors.filter(e => e.type === 'invalid_mcp');

    expect(result.isValid).toBe(false);
    expect(mcpErrors.some(e => e.message.includes('Audite-o com o SkillSpector'))).toBe(true);
  });

  it('deve emitir alerta (warning) se nó MCP ainda não tiver nenhum servidor configurado', () => {
    const nodeA = graph.createNode('node_start', 'Início', 'static', 0, 0);
    const nodeMcp = graph.createNode('node_mcp', 'Nó MCP Vazio', 'mcp', 200, 0);
    graph.updateNode(nodeMcp.id, {
      mcpSettings: {
        server: '',
        tool: '',
        arguments: {},
        source: { kind: 'none' },
        output: {}
      }
    });

    graph.addEdge({
      id: 'e1',
      sourceNodeId: nodeA.id,
      sourcePortId: nodeA.outputs[0].id,
      targetNodeId: nodeMcp.id,
      targetPortId: nodeMcp.inputs[0].id
    });

    const result = GraphValidator.validate(graph);
    const mcpWarnings = result.errors.filter(e => e.type === 'invalid_mcp' && e.severity === 'warning');

    expect(mcpWarnings.length).toBeGreaterThanOrEqual(1);
    expect(mcpWarnings.some(e => e.message.includes('não possui nenhum servidor configurado'))).toBe(true);
  });

  it('deve bloquear nó de Agente cuja configuração codeMode.mcpServers contenha servidor rejeitado', () => {
    ledger.recordRejection(
      { id: 'banned-rag-server', name: 'Banned RAG' },
      'Exfiltração de dados identificada.'
    );

    const nodeA = graph.createNode('node_start', 'Início', 'static', 0, 0);
    const agentNode = graph.createNode('node_agent', 'Agente Pesquisador', 'agent', 200, 0);

    graph.updateNode(agentNode.id, {
      agentSettings: {
        model: 'default',
        systemPrompt: 'Você é um assistente.',
        role: 'pesquisador',
        inheritSandbox: false,
        inheritModel: false,
        inheritCodeMode: false,
        codeMode: {
          enabled: true,
          mcpServers: ['banned-rag-server']
        }
      }
    });

    graph.addEdge({
      id: 'e1',
      sourceNodeId: nodeA.id,
      sourcePortId: nodeA.outputs[0].id,
      targetNodeId: agentNode.id,
      targetPortId: agentNode.inputs[0].id
    });

    const result = GraphValidator.validate(graph);
    const mcpErrors = result.errors.filter(e => e.type === 'invalid_mcp');

    expect(result.isValid).toBe(false);
    expect(mcpErrors.some(e => e.message.includes('REJEITADO'))).toBe(true);
  });
});
