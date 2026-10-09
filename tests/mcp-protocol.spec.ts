import { describe, it, expect, beforeEach } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { UpdateNodeCommand } from '../src/core/history/GraphCommands.ts';
import { IMcpServerConfig, IMcpToolDefinition, ICodeModeConfig } from '../src/core/types.ts';

describe('Model Context Protocol (MCP) Integration (Fase 13)', () => {
  let eventBus: EventBus;
  let graph: Graph;

  beforeEach(() => {
    eventBus = new EventBus();
    graph = new Graph(eventBus, {
      graphName: 'MCP_Test_Pipeline',
      description: 'Pipeline de teste de conectores MCP',
      targetStandard: 'C++20',
      compilerConfig: {
        compiler: 'msvc',
        cppStandard: 'C++20',
        buildSystem: 'cmake',
        taskflowIncludePath: 'include',
        outputBinaryName: 'mcp_pipeline',
        enableOpenMP: false,
        enableCuda: false,
        extraFlags: []
      }
    });
  });

  it('deve herdar ferramentas MCP padrão dos padrões globais do pipeline', () => {
    // Configura padrões globais com ferramentas MCP
    graph.updateGlobalDefaults({
      codeMode: {
        allowedTools: ['read', 'write'],
        customScripts: [],
        allowedMcpTools: ['read_file', 'fetch_url']
      }
    });

    const node = graph.buildNode('node_mcp_std', 'Agente MCP Padrão', 'agent', 0, 0);
    graph.addNode(node);

    const retrieved = graph.getNode('node_mcp_std')!;
    expect(retrieved.agentSettings?.inheritCodeMode).toBe(true);

    const defaults = graph.getGlobalDefaults();
    expect(defaults.codeMode.allowedMcpTools).toContain('read_file');
    expect(defaults.codeMode.allowedMcpTools).toContain('fetch_url');
  });

  it('deve permitir customização seletiva de ferramentas MCP por nó de agente', () => {
    const node = graph.buildNode('node_custom_mcp', 'Agente de Análise com MCP Especializado', 'agent', 100, 50);
    node.agentSettings = {
      inheritSandbox: true,
      inheritModel: true,
      inheritCodeMode: false,
      codeMode: {
        allowedTools: ['read'],
        customScripts: [],
        allowedMcpTools: ['list_directory', 'query_sql'],
        mcpServers: ['mock-filesystem', 'mock-database']
      }
    };
    graph.addNode(node);

    const retrieved = graph.getNode('node_custom_mcp')!;
    expect(retrieved.agentSettings?.inheritCodeMode).toBe(false);
    expect(retrieved.agentSettings?.codeMode?.allowedMcpTools).toEqual(['list_directory', 'query_sql']);
    expect(retrieved.agentSettings?.codeMode?.mcpServers).toEqual(['mock-filesystem', 'mock-database']);
  });

  it('deve validar tipos e estruturas de configuração de servidores MCP (stdio e sse)', () => {
    const stdioServer: IMcpServerConfig = {
      id: 'local-fs',
      name: 'Filesystem Server',
      transport: 'stdio',
      command: 'npx',
      args: ['-y', '@modelcontextprotocol/server-filesystem', '.'],
      isEnabled: true,
      cachedTools: [
        {
          name: 'read_file',
          description: 'Lê arquivo',
          serverId: 'local-fs',
          inputSchema: { type: 'object', properties: { path: { type: 'string' } } }
        }
      ]
    };

    const sseServer: IMcpServerConfig = {
      id: 'remote-sse',
      name: 'Remote SSE Gateway',
      transport: 'sse',
      url: 'http://localhost:8080/sse',
      isEnabled: false,
      cachedTools: []
    };

    expect(stdioServer.transport).toBe('stdio');
    expect(stdioServer.cachedTools?.length).toBe(1);
    expect(stdioServer.cachedTools?.[0].name).toBe('read_file');

    expect(sseServer.transport).toBe('sse');
    expect(sseServer.url).toBe('http://localhost:8080/sse');
    expect(sseServer.isEnabled).toBe(false);
  });

  it('deve preservar configurações de ferramentas MCP na serialização e deserialização do grafo', () => {
    const node = graph.buildNode('node_mcp_1', 'Pesquisador Web MCP', 'agent', 200, 200);
    node.agentSettings = {
      inheritSandbox: true,
      inheritModel: true,
      inheritCodeMode: false,
      codeMode: {
        allowedTools: ['read', 'bash'],
        customScripts: ['python test.py'],
        allowedMcpTools: ['fetch_url', 'web_search'],
        mcpServers: ['remote-fetch']
      }
    };
    graph.addNode(node);

    const serializedSchema = graph.toJSON();
    expect(serializedSchema).toBeDefined();

    // Novo grafo a partir do JSON exportado
    const restoredGraph = new Graph(new EventBus());
    restoredGraph.fromJSON(serializedSchema);

    const restoredNode = restoredGraph.getNode('node_mcp_1');
    expect(restoredNode).toBeDefined();
    expect(restoredNode?.agentSettings?.inheritCodeMode).toBe(false);
    expect(restoredNode?.agentSettings?.codeMode?.allowedMcpTools).toContain('fetch_url');
    expect(restoredNode?.agentSettings?.codeMode?.allowedMcpTools).toContain('web_search');
    expect(restoredNode?.agentSettings?.codeMode?.mcpServers).toContain('remote-fetch');
  });

  it('deve suportar desfazer e refazer (Undo / Redo) em alterações de ferramentas MCP', () => {
    const node = graph.buildNode('node_undo_mcp', 'Agente Reversível MCP', 'agent', 0, 0);
    graph.addNode(node);

    const initialSettings = node.agentSettings;

    const modifiedSettings = {
      ...initialSettings,
      inheritCodeMode: false,
      codeMode: {
        allowedTools: ['write'] as ('write' | 'edit' | 'read' | 'bash' | 'validate_svg')[],
        customScripts: [],
        allowedMcpTools: ['write_file']
      }
    };

    const cmd = new UpdateNodeCommand(
      graph,
      node.id,
      { agentSettings: initialSettings },
      { agentSettings: modifiedSettings },
      'Adicionar permissão MCP write_file'
    );
    graph.commandManager.execute(cmd);

    // Estado após execução
    let updatedNode = graph.getNode(node.id);
    expect(updatedNode?.agentSettings?.inheritCodeMode).toBe(false);
    expect(updatedNode?.agentSettings?.codeMode?.allowedMcpTools).toEqual(['write_file']);

    // Desfazer (Undo)
    graph.commandManager.undo();
    updatedNode = graph.getNode(node.id);
    expect(updatedNode?.agentSettings?.inheritCodeMode).toBe(true);
    expect(updatedNode?.agentSettings?.codeMode?.allowedMcpTools).toBeUndefined();

    // Refazer (Redo)
    graph.commandManager.redo();
    updatedNode = graph.getNode(node.id);
    expect(updatedNode?.agentSettings?.inheritCodeMode).toBe(false);
    expect(updatedNode?.agentSettings?.codeMode?.allowedMcpTools).toEqual(['write_file']);
  });
});
