import { describe, it, expect, beforeEach } from 'vitest';
import * as fs from 'fs';
import * as path from 'path';
import { AgentTemplateRegistry } from '../src/core/templates/AgentTemplateRegistry.ts';
import { IAgentTemplate } from '../src/core/templates/AgentTemplateTypes.ts';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';

describe('Fase 4: Catalogo de Templates de Agentes & Sistema de Plugins Dinamicos', () => {
  let registry: AgentTemplateRegistry;
  let graph: Graph;

  beforeEach(() => {
    registry = AgentTemplateRegistry.getInstance();
    graph = new Graph(new EventBus());
  });

  describe('1. Catalogo e Registro de Templates Built-in', () => {
    it('deve inicializar com todos os 6 templates de agentes especializados', () => {
      const templates = registry.getAllTemplates();
      expect(templates.length).toBeGreaterThanOrEqual(6);
      
      const ids = templates.map(t => t.id);
      expect(ids).toContain('consensus_module_agent');
      expect(ids).toContain('llm_decision_agent');
      expect(ids).toContain('concurrent_queue_worker');
      expect(ids).toContain('sqlite_persistence_agent');
      expect(ids).toContain('vision_pipeline_subflow');
      expect(ids).toContain('cuda_kernel_agent');
    });

    it('deve recuperar template especifico por ID', () => {
      const tpl = registry.getTemplate('llm_decision_agent');
      expect(tpl).toBeDefined();
      expect(tpl?.name).toBe('Agente LLM / Prompt Decisor');
      expect(tpl?.taskType).toBe('condition');
    });
  });

  describe('2. Instanciacao de Nos a partir de Templates', () => {
    it('deve instanciar Agente LLM como no de condicao com 2 ramos de saida', () => {
      const node = registry.instantiateNode('llm_decision_agent', 'Meu_Decisor_LLM', 100, 200);

      expect(node.label).toBe('Meu_Decisor_LLM');
      expect(node.type).toBe('condition');
      expect(node.templateId).toBe('llm_decision_agent');
      expect(node.position.x).toBe(100);
      expect(node.position.y).toBe(200);

      // Checa saidas de ramo condicional (ramo 0 e ramo 1)
      expect(node.outputs.length).toBe(2);
      expect(node.outputs[0].conditionBranchIndex).toBe(0);
      expect(node.outputs[1].conditionBranchIndex).toBe(1);

      // Checa parametros padrao
      expect(node.parameters.modelName).toBe('gemini-2.5-flash');
      expect(node.parameters.confidenceThreshold).toBe(0.85);

      // Checa lambda gerado
      expect(node.customLambdaBody).toContain('std::uniform_real_distribution');
      expect(node.customLambdaBody).toContain('ctx.last_confidence_score = confidence');
      expect(node.customLambdaBody).toContain('return 0;');
      expect(node.customLambdaBody).toContain('return 1;');
    });

    it('deve permitir sobrescrever parametros customizados na instanciacao', () => {
      const node = registry.instantiateNode('concurrent_queue_worker', 'Worker_Custom', 0, 0, {
        batchSize: 128,
        workerTag: 'GPU-Worker-99'
      });

      expect(node.type).toBe('static');
      expect(node.parameters.batchSize).toBe(128);
      expect(node.parameters.workerTag).toBe('GPU-Worker-99');
      expect(node.customLambdaBody).toContain('GPU-Worker-99');
      expect(node.customLambdaBody).toContain('128');
    });

    it('deve instanciar Agente SQLite protegido por CriticalSection', () => {
      const node = registry.instantiateNode('sqlite_persistence_agent', 'DB_Writer');
      expect(node.type).toBe('critical');
      expect(node.parameters.journalMode).toBe('WAL');
      expect(node.customLambdaBody).toContain('ctx.persistent_store');
      expect(node.customLambdaBody).toContain('ctx.db_mutex');
    });
  });

  describe('3. Extensibilidade de Plugins Dinamicos (Sem Recompilar o Core)', () => {
    it('deve registrar e remover novo plugin de template dinamico via API', () => {
      const customPlugin: IAgentTemplate = {
        id: 'quantum_annealing_agent',
        name: 'Agente de Otimizacao Quantica',
        category: 'Plugins Customizados',
        icon: '??',
        taskType: 'static',
        description: 'Resolve problemas NP-dificeis simulando recozimento quantico.',
        inputs: [{ name: 'hamiltonian_in', type: 'data' }],
        outputs: [{ name: 'ground_state_out', type: 'data' }],
        parameters: {
          spinCount: { label: 'Spins', type: 'number', defaultValue: 128, description: 'Qtd de spins' }
        },
        isCustomPlugin: true,
        generateLambdaBody: (params, label) => `// ${label} Spins: ${params.spinCount}`
      };

      registry.registerTemplate(customPlugin);
      expect(registry.getTemplate('quantum_annealing_agent')).toBeDefined();

      const instantiated = registry.instantiateNode('quantum_annealing_agent', 'Quantum_1');
      expect(instantiated.customLambdaBody).toContain('Quantum_1 Spins: 128');

      // Limpeza
      const removed = registry.unregisterTemplate('quantum_annealing_agent');
      expect(removed).toBe(true);
      expect(registry.getTemplate('quantum_annealing_agent')).toBeUndefined();
    });

    it('deve importar plugins externos a partir de JSON com validacao', () => {
      const pluginJson = JSON.stringify({
        id: 'rest_api_fetcher',
        name: 'Agente REST API Poller',
        category: 'Plugins Customizados',
        icon: '??',
        taskType: 'static',
        description: 'Faz polling HTTP concorrente e envia eventos.',
        inputs: [{ name: 'trigger', type: 'flow' }],
        outputs: [{ name: 'json_payload', type: 'data' }],
        parameters: {
          endpointUrl: { label: 'Endpoint', type: 'string', defaultValue: 'https://api.local/v1', description: 'URL alvo' }
        },
        lambdaBodyTemplate: '// Chamada para ${params.endpointUrl} por ${label}'
      });

      const result = registry.importTemplatesFromJSON(pluginJson);
      expect(result.success).toBe(1);
      expect(result.errors.length).toBe(0);

      const tpl = registry.getTemplate('rest_api_fetcher');
      expect(tpl).toBeDefined();

      const node = registry.instantiateNode('rest_api_fetcher', 'API_Agent', 0, 0, {
        endpointUrl: 'https://arestrix.dev/metrics'
      });
      expect(node.customLambdaBody).toContain('https://arestrix.dev/metrics');
      expect(node.customLambdaBody).toContain('API_Agent');

      registry.unregisterTemplate('rest_api_fetcher');
    });

    it('deve exportar todo o catalogo em formato JSON valido', () => {
      const json = registry.exportAllTemplatesAsJSON();
      expect(typeof json).toBe('string');
      const parsed = JSON.parse(json);
      expect(Array.isArray(parsed)).toBe(true);
      expect(parsed.length).toBeGreaterThanOrEqual(6);
    });
  });

  describe('4. Agentes prontos e round-trip de agentSettings', () => {
    it('deve listar agentes embutidos na categoria Agentes', () => {
      const agent = registry.getTemplate('agent_document_analyst');
      expect(agent).toBeDefined();
      expect(agent?.taskType).toBe('agent');
      expect(agent?.category).toBe('Agentes');
      expect(agent?.agentSettings?.model?.promptTemplate).toContain('analista');
      expect(agent?.agentSettings?.skills?.length).toBe(1);
    });

    it('deve preservar agentSettings ao instanciar e isolar o template', () => {
      const node = registry.instantiateNode('agent_document_analyst', 'Meu_Analista');
      expect(node.type).toBe('agent');
      expect(node.presetId).toBe('agent_document_analyst');
      expect(node.presetLocked).toBe(true);
      expect(node.agentSettings?.model?.promptTemplate).toContain('analista');
      expect(node.agentSettings?.skills?.[0]?.id).toBe('builtin-document-analysis');

      node.agentSettings!.model!.promptTemplate = 'prompt alterado';
      expect(registry.getTemplate('agent_document_analyst')?.agentSettings?.model?.promptTemplate)
        .not.toBe('prompt alterado');
    });

    it('deve salvar um agente customizado com configuração completa e detectar duplicata', () => {
      const node = registry.instantiateNode('agent_content_reviewer', 'Revisor_Custom');
      node.agentSettings!.model!.promptTemplate = 'Prompt específico do revisor customizado';

      const saved = registry.saveNodeAsTemplate(node);
      expect(saved.status).toBe('created');
      if (saved.status !== 'created') return;
      expect(saved.template.category).toBe('Agentes');
      expect(saved.template.presetLocked).toBe(true);
      expect(saved.template.agentSettings?.model?.promptTemplate).toBe('Prompt específico do revisor customizado');

      const restored = registry.instantiateNode(saved.template.id, 'Revisor_Restaurado');
      expect(restored.presetLocked).toBe(true);
      expect(restored.agentSettings?.model?.promptTemplate).toBe('Prompt específico do revisor customizado');
      expect(restored.agentSettings?.skills?.length).toBe(1);

      expect(registry.saveNodeAsTemplate({ ...node, id: 'outro_agente' }).status).toBe('duplicate');
      registry.unregisterTemplate(saved.template.id);
    });

    it('deve exportar e importar agentSettings sem perder a configuração', () => {
      const json = registry.exportTemplateAsJSON('agent_research_synthesizer');
      expect(json).not.toBeNull();
      const serialized = JSON.parse(json!);
      expect(serialized.agentSettings?.model?.promptTemplate).toContain('pesquisador');

      const importedJson = JSON.stringify({ ...serialized, id: 'imported_research_agent' });
      const result = registry.importTemplatesFromJSON(importedJson);
      expect(result.success).toBe(1);
      const imported = registry.instantiateNode('imported_research_agent', 'Pesquisador_Importado');
      expect(imported.type).toBe('agent');
      expect(imported.customLambdaBody).toBe('');
      expect(imported.presetLocked).toBe(true);
      expect(imported.agentSettings?.model?.promptTemplate).toContain('pesquisador');
      registry.unregisterTemplate('imported_research_agent');
    });

    it('deve empacotar um pipeline como agente visual e preservar o grafo', () => {
      graph.addNode({
        id: 'a',
        label: 'Entrada',
        type: 'agent',
        position: { x: 0, y: 0 },
        inputs: [{ id: 'a_in', name: 'in', direction: 'input', type: 'flow' }],
        outputs: [{ id: 'a_out', name: 'out', direction: 'output', type: 'flow' }],
        parameters: {}
      });
      graph.addNode({
        id: 'b',
        label: 'Saída',
        type: 'output',
        position: { x: 200, y: 0 },
        inputs: [{ id: 'b_in', name: 'data_in', direction: 'input', type: 'data' }],
        outputs: [{ id: 'b_out', name: 'out', direction: 'output', type: 'flow' }],
        parameters: {}
      });
      graph.createEdge('a', 'a_out', 'b', 'b_in');

      const pipeline = registry.saveGraphAsPipelineTemplate(graph, 'Pipeline de Teste');
      expect(pipeline.assetKind).toBe('pipeline');
      expect(pipeline.taskType).toBe('subflow');
      expect(pipeline.presentation).toBe('agent');
      expect(pipeline.presetLocked).toBe(true);
      expect(pipeline.pipelineInterface?.input.name).toBe('Entrada');
      expect(pipeline.pipelineInterface?.outputs[0]?.name).toBe('Saída');
      expect(pipeline.childGraph?.nodes?.length).toBe(2);

      const serialized = JSON.parse(registry.exportTemplateAsJSON(pipeline.id)!);
      expect(serialized.assetKind).toBe('pipeline');
      expect(serialized.childGraph.nodes.length).toBe(2);
      expect(serialized.pipelineInterface.outputs[0].name).toBe('Saída');
      expect(serialized.presentation).toBe('agent');

      const imported = registry.importTemplatesFromJSON(JSON.stringify({
        ...serialized,
        id: 'imported_pipeline_agent'
      }));
      expect(imported.success).toBe(1);
      const template = registry.getTemplate('imported_pipeline_agent');
      expect(template?.assetKind).toBe('pipeline');
      expect(template?.childGraph?.nodes?.length).toBe(2);
      registry.unregisterTemplate('imported_pipeline_agent');
    });

    it('deve importar um pipeline de outro projeto e rejeitar JSON estruturalmente inválido', () => {
      graph.addNode({
        id: 'x',
        label: 'Agente',
        type: 'agent',
        position: { x: 0, y: 0 },
        inputs: [],
        outputs: [],
        parameters: {}
      });
      const imported = registry.importGraphAsPipelineTemplate(graph.toJSON(), 'Pipeline Externo');
      expect(imported.assetKind).toBe('pipeline');
      expect(imported.name).toBe('Pipeline Externo');
      expect(imported.childGraph?.nodes?.length).toBe(1);
      registry.unregisterTemplate(imported.id);

      expect(() => registry.importGraphAsPipelineTemplate({} as any, 'Inválido'))
        .toThrow(/nodes e edges/);

      const withCall = graph.toJSON();
      withCall.edges.push({ id: 'call-1', sourceNodeId: 'x', sourcePortId: 'x_out', targetNodeId: 'x', targetPortId: 'x_in', kind: 'call' });
      expect(() => registry.importGraphAsPipelineTemplate(withCall, 'Com chamada'))
        .toThrow(/arestas de chamada/);

      const withNested = graph.toJSON();
      withNested.nodes.push({
        id: 'nested', label: 'Nested', type: 'subflow', position: { x: 0, y: 0 },
        inputs: [], outputs: [], parameters: {},
        childGraph: { version: '1.0.0', metadata: {} as any, nodes: [{ id: 'inner' } as any], edges: [] }
      });
      expect(() => registry.importGraphAsPipelineTemplate(withNested, 'Com subflow'))
        .toThrow(/subflows aninhados/);
    });
  });
});

describe('Salvar vértice como template', () => {
  it('inclui uma vez e rejeita vértice equivalente', async () => {
    const { AgentTemplateRegistry } = await import('../src/core/templates/AgentTemplateRegistry.ts');
    const reg = new AgentTemplateRegistry();
    const node = reg.instantiateNode('cuda_kernel_agent', 'MeuKernel', 0, 0);
    expect(reg.saveNodeAsTemplate(node).status).toBe('duplicate');

    const custom = { ...node, label: 'Meu', customLambdaBody: '// custom\nstd::cout << "Meu";' };
    expect(reg.saveNodeAsTemplate(custom).status).toBe('created');
    expect(reg.saveNodeAsTemplate({ ...custom, id: 'outro' }).status).toBe('duplicate');
  });
});
