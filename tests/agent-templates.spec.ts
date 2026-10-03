import { describe, it, expect, beforeEach } from 'vitest';
import * as fs from 'fs';
import * as path from 'path';
import { AgentTemplateRegistry } from '../src/core/templates/AgentTemplateRegistry.ts';
import { IAgentTemplate } from '../src/core/templates/AgentTemplateTypes.ts';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { TaskflowCppGenerator } from '../src/compiler/TaskflowCppGenerator.ts';

describe('Fase 4: Catálogo de Templates de Agentes & Sistema de Plugins Dinâmicos', () => {
  let registry: AgentTemplateRegistry;
  let graph: Graph;

  beforeEach(() => {
    registry = AgentTemplateRegistry.getInstance();
    graph = new Graph(new EventBus());
  });

  describe('1. Catálogo e Registro de Templates Built-in', () => {
    it('deve inicializar com todos os 5 templates de agentes especializados', () => {
      const templates = registry.getAllTemplates();
      expect(templates.length).toBeGreaterThanOrEqual(5);

      const ids = templates.map(t => t.id);
      expect(ids).toContain('llm_decision_agent');
      expect(ids).toContain('concurrent_queue_worker');
      expect(ids).toContain('sqlite_persistence_agent');
      expect(ids).toContain('vision_pipeline_subflow');
      expect(ids).toContain('consensus_module_agent');
    });

    it('deve agrupar os templates por categoria corretamente', () => {
      const categoryMap = registry.getTemplatesByCategory();
      expect(categoryMap.has('IA & Inferência')).toBe(true);
      expect(categoryMap.has('Concorrência & Filas')).toBe(true);
      expect(categoryMap.has('Armazenamento & Dados')).toBe(true);
      expect(categoryMap.has('Visão & Processamento')).toBe(true);
      expect(categoryMap.has('Síntese & Orquestração')).toBe(true);
    });

    it('deve recuperar templates individuais por ID', () => {
      const llmTpl = registry.getTemplate('llm_decision_agent');
      expect(llmTpl).toBeDefined();
      expect(llmTpl?.taskType).toBe('condition');
      expect(llmTpl?.parameters.confidenceThreshold).toBeDefined();

      const sqliteTpl = registry.getTemplate('sqlite_persistence_agent');
      expect(sqliteTpl).toBeDefined();
      expect(sqliteTpl?.taskType).toBe('critical');
    });
  });

  describe('2. Instanciação de Nós a partir de Templates', () => {
    it('deve instanciar Agente LLM como nó de condição com 2 ramos de saída', () => {
      const node = registry.instantiateNode('llm_decision_agent', 'Meu_Decisor_LLM', 100, 200);

      expect(node.label).toBe('Meu_Decisor_LLM');
      expect(node.type).toBe('condition');
      expect(node.templateId).toBe('llm_decision_agent');
      expect(node.position.x).toBe(100);
      expect(node.position.y).toBe(200);

      // Checa saídas de ramo condicional (ramo 0 e ramo 1)
      expect(node.outputs.length).toBe(2);
      expect(node.outputs[0].conditionBranchIndex).toBe(0);
      expect(node.outputs[1].conditionBranchIndex).toBe(1);

      // Checa parâmetros padrão
      expect(node.parameters.modelName).toBe('gemini-2.5-flash');
      expect(node.parameters.confidenceThreshold).toBe(0.85);

      // Checa lambda gerado
      expect(node.customLambdaBody).toContain('std::uniform_real_distribution');
      expect(node.customLambdaBody).toContain('ctx.last_confidence_score = confidence');
      expect(node.customLambdaBody).toContain('return 0;');
      expect(node.customLambdaBody).toContain('return 1;');
    });

    it('deve permitir sobrescrever parâmetros customizados na instanciação', () => {
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

  describe('3. Extensibilidade de Plugins Dinâmicos (Sem Recompilar o Core)', () => {
    it('deve registrar e remover novo plugin de template dinâmico via API', () => {
      const customPlugin: IAgentTemplate = {
        id: 'quantum_annealing_agent',
        name: 'Agente de Otimização Quântica',
        category: 'Plugins Customizados',
        icon: '⚛️',
        taskType: 'static',
        description: 'Resolve problemas NP-difíceis simulando recozimento quântico.',
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

    it('deve importar plugins externos a partir de JSON com validação', () => {
      const pluginJson = JSON.stringify({
        id: 'rest_api_fetcher',
        name: 'Agente REST API Poller',
        category: 'Plugins Customizados',
        icon: '🌐',
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
        endpointUrl: 'https://nodeforge.dev/metrics'
      });
      expect(node.customLambdaBody).toContain('https://nodeforge.dev/metrics');
      expect(node.customLambdaBody).toContain('API_Agent');

      registry.unregisterTemplate('rest_api_fetcher');
    });

    it('deve exportar todo o catálogo em formato JSON válido', () => {
      const json = registry.exportAllTemplatesAsJSON();
      expect(typeof json).toBe('string');
      const parsed = JSON.parse(json);
      expect(Array.isArray(parsed)).toBe(true);
      expect(parsed.length).toBeGreaterThanOrEqual(5);
    });
  });

  describe('4. Geração de Código Taskflow C++20 com Múltiplos Agentes', () => {
    it('deve compilar pipeline integrado com Fila + Visão + LLM + SQLite em C++20 válido', () => {
      // 1. Instancia os nós dos templates
      const nQueue = registry.instantiateNode('concurrent_queue_worker', '1_Fila_Buffer', -300, 0);
      const nVision = registry.instantiateNode('vision_pipeline_subflow', '2_Visao_Subflow', 0, 0);
      const nLLM = registry.instantiateNode('llm_decision_agent', '3_LLM_Decisor', 300, 0);
      const nSQLite = registry.instantiateNode('sqlite_persistence_agent', '4_SQLite_Writer', 600, 0);

      graph.addNode(nQueue);
      graph.addNode(nVision);
      graph.addNode(nLLM);
      graph.addNode(nSQLite);

      // 2. Conecta as arestas
      graph.createEdge(nQueue.id, nQueue.outputs[0].id, nVision.id, nVision.inputs[0].id);
      graph.createEdge(nVision.id, nVision.outputs[0].id, nLLM.id, nLLM.inputs[0].id);
      
      // LLM Ramo 0 (Aprovado) -> SQLite
      graph.createEdge(nLLM.id, nLLM.outputs[0].id, nSQLite.id, nSQLite.inputs[0].id);
      
      // LLM Ramo 1 (Feedback Loop) -> Volta para Fila
      graph.createEdge(nLLM.id, nLLM.outputs[1].id, nQueue.id, nQueue.inputs[0].id);

      // 3. Gera o código C++20
      const cppSource = TaskflowCppGenerator.generateSource(graph);

      // Salva em workspace_run para teste de build nativo MSVC
      fs.writeFileSync(path.resolve(process.cwd(), 'workspace_run', 'main.cpp'), cppSource, 'utf-8');

      // Headers de concorrência e estruturas
      expect(cppSource).toContain('#include <taskflow/taskflow.hpp>');
      expect(cppSource).toContain('#include <queue>');
      expect(cppSource).toContain('#include <mutex>');
      expect(cppSource).toContain('#include <unordered_map>');
      expect(cppSource).toContain('#include <syncstream>');

      // AgentContext completo
      expect(cppSource).toContain('struct AgentContext {');
      expect(cppSource).toContain('std::queue<std::string> task_queue;');
      expect(cppSource).toContain('std::mutex queue_mutex;');
      expect(cppSource).toContain('std::unordered_map<std::string, std::string> persistent_store;');
      expect(cppSource).toContain('std::mutex db_mutex;');
      expect(cppSource).toContain('double last_confidence_score');

      // Seção crítica / Semáforo Taskflow
      expect(cppSource).toContain('tf::Semaphore critical_sec(1);');

      const sLLM = nLLM.id.replace(/[^a-zA-Z0-9_]/g, '_');
      const sSQLite = nSQLite.id.replace(/[^a-zA-Z0-9_]/g, '_');

      // Nó de condição precede ramos 0 e 1
      expect(cppSource).toContain(`task_${sLLM}.precede(`);

      // Nó SQLite com acquire/release
      expect(cppSource).toContain(`task_${sSQLite}.acquire(critical_sec);`);
      expect(cppSource).toContain(`task_${sSQLite}.release(critical_sec);`);
    });
  });
});
