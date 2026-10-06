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
        endpointUrl: 'https://taskforge.dev/metrics'
      });
      expect(node.customLambdaBody).toContain('https://taskforge.dev/metrics');
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
});
