import { describe, it, expect, beforeEach } from 'vitest';
import { GenericPiProjectScanner, IPiProjectScanResult } from '../src/core/pi/GenericPiProjectScanner.ts';
import { EnvironmentManager } from '../src/core/env/EnvironmentManager.ts';
import { DefaultIntelligenceService } from '../src/core/ai/DefaultIntelligenceService.ts';
import { PiProjectImporter } from '../src/core/pi/PiProjectImporter.ts';
import { ProjectManager } from '../src/core/ProjectManager.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { Graph } from '../src/core/Graph.ts';

describe('Importação Genérica de Projetos Pi & Modelo Padrão de Inteligência', () => {
  let bus: EventBus;
  let projectManager: ProjectManager;
  let graph: Graph;
  let importer: PiProjectImporter;
  let envManager: EnvironmentManager;
  let intelligenceService: DefaultIntelligenceService;

  beforeEach(() => {
    bus = new EventBus();
    projectManager = ProjectManager.getInstance(bus);
    projectManager.resetForTesting(bus);

    graph = new Graph(bus, {
      graphName: 'GenericPiGraph',
      projectId: 'default'
    });

    importer = PiProjectImporter.getInstance();
    envManager = EnvironmentManager.getInstance();
    intelligenceService = DefaultIntelligenceService.getInstance();
    intelligenceService.setCustomApiCaller(undefined);
  });

  describe('1. Gestão Estrita de Segredos e Variáveis de Ambiente (EnvironmentManager)', () => {
    it('nunca deve armazenar segredos em arquivos ou código e deve ler de variáveis de ambiente do SO', async () => {
      // Configura temporariamente no process.env para testar leitura
      process.env['TEST_SECRET_KEY'] = 'secret-test-token-123';
      const val = await envManager.getEnvVar('TEST_SECRET_KEY');
      expect(val).toBe('secret-test-token-123');

      // Limpeza
      delete process.env['TEST_SECRET_KEY'];
      const emptyVal = await envManager.getEnvVar('TEST_SECRET_KEY');
      expect(emptyVal).toBeNull();
    });

    it('deve fornecer instruções completas de configuração para Windows quando a chave estiver ausente', () => {
      const guidance = envManager.getGuidance('DEEPSEEK_API_KEY');
      expect(guidance.keyName).toBe('DEEPSEEK_API_KEY');
      expect(guidance.powershellCmd).toContain('[System.Environment]::SetEnvironmentVariable');
      expect(guidance.powershellCmd).toContain('DEEPSEEK_API_KEY');
      expect(guidance.cmdCmd).toContain('setx DEEPSEEK_API_KEY');
      expect(guidance.guiInstructions.length).toBeGreaterThan(3);
      expect(guidance.guiInstructions[0]).toContain('sysdm.cpl');
    });
  });

  describe('2. Modelo Padrão do Arestrix (DefaultIntelligenceService)', () => {
    it('deve sintetizar um prompt rico e detalhado com todas as skills, etapas e ferramentas MCP descobertas', () => {
      const mockScan: IPiProjectScanResult = {
        isValid: true,
        rootPath: 'C:/Projetos/QualquerProjetoPi',
        projectName: 'Pipeline Genérico de Processamento',
        defaultProvider: 'deepseek',
        defaultModel: 'deepseek-v4-pro',
        defaultTools: ['write', 'edit'],
        skills: [
          { id: 'extrator', name: 'Extrator de Dados', path: '.agents/extrator/SKILL.md', description: 'Extrai dados brutos' },
          { id: 'transformador', name: 'Transformador', path: '.agents/transformador/SKILL.md', description: 'Aplica transformações' }
        ],
        stages: [
          { id: 'step-1', name: 'Extração', model: 'deepseek-v4-pro', skillPath: '.agents/extrator/SKILL.md', prompt: 'Extraia os registros' },
          { id: 'step-2', name: 'Transformação', model: 'deepseek-flash', skillPath: '.agents/transformador/SKILL.md', prompt: 'Transforme os registros' }
        ],
        mcpTools: [
          { name: 'db_connector', type: 'server', description: 'Servidor MCP Postgres' }
        ],
        orchestrationType: 'script',
        workItems: ['LOTE-001', 'LOTE-002']
      };

      const prompt = intelligenceService.buildPiAnalysisPrompt(mockScan);
      expect(prompt).toContain('Pipeline Genérico de Processamento');
      expect(prompt).toContain('Extrator de Dados');
      expect(prompt).toContain('Transformador');
      expect(prompt).toContain('db_connector');
      expect(prompt).toContain('deepseek-v4-pro');
      expect(prompt).toContain('LOTE-001');
    });

    it('deve usar o caller mockado para simular a resposta da IA e refinar a topologia', async () => {
      const mockScan: IPiProjectScanResult = {
        isValid: true,
        rootPath: 'C:/Projetos/QualquerProjetoPi',
        projectName: 'Auditor de Código',
        defaultProvider: 'deepseek',
        defaultModel: 'deepseek-v4-pro',
        defaultTools: [],
        skills: [
          { id: 'linter', name: 'Linter', path: '.agents/linter/SKILL.md' },
          { id: 'security', name: 'Security Scanner', path: '.agents/security/SKILL.md' }
        ],
        stages: [
          { id: '1-lint', name: 'Linter', model: 'deepseek-flash', prompt: 'Rode o linter' },
          { id: '2-sec', name: 'Segurança', model: 'deepseek-v4-pro', prompt: 'Analise vulnerabilidades' }
        ],
        mcpTools: [],
        orchestrationType: 'agent_pool'
      };

      // Injeta mock do Modelo Padrão
      intelligenceService.setCustomApiCaller(async (_key, _prompt) => {
        return JSON.stringify({
          confidenceScore: 98,
          summary: 'Auditoria de código genérica validada com sucesso pelo Modelo Padrão.',
          verifiedStages: [
            {
              id: '1-lint',
              label: '1. Verificação Estática (Linter)',
              nodeType: 'agent',
              role: 'Linter do Código',
              model: 'deepseek-flash',
              promptTemplate: 'Rode o linter rigoroso',
              isDeterministic: false
            },
            {
              id: '2-sec',
              label: '2. Análise de Segurança',
              nodeType: 'agent',
              role: 'Auditor de Vulnerabilidades',
              model: 'deepseek-v4-pro',
              promptTemplate: 'Inspecione dependências e CVEs',
              isDeterministic: false
            }
          ],
          verifiedEdges: [
            { fromNodeId: '1-lint', toNodeId: '2-sec', type: 'flow' }
          ],
          auditFindings: ['Pipeline modular e isolado com sucesso.']
        });
      });

      const result = await intelligenceService.analyzeAndValidatePiImport(mockScan);
      expect(result.isSuccess).toBe(true);
      expect(result.confidenceScore).toBe(98);
      expect(result.verifiedStages.length).toBe(2);
      expect(result.verifiedStages[0].label).toContain('Verificação Estática');
      expect(result.verifiedEdges.length).toBe(1);
    });

    it('deve usar o caller injetado mesmo sem chave de API no ambiente', async () => {
      const previousKey = process.env['DEEPSEEK_API_KEY'];
      delete process.env['DEEPSEEK_API_KEY'];

      try {
        intelligenceService.setCustomApiCaller(async () => JSON.stringify({
          confidenceScore: 77,
          summary: 'Caller injetado sem chave',
          verifiedStages: [
            {
              id: 's1',
              label: 'Etapa única',
              nodeType: 'agent',
              role: 'Executor',
              promptTemplate: 'Execute'
            }
          ],
          verifiedEdges: [],
          auditFindings: []
        }));

        const result = await intelligenceService.analyzeAndValidatePiImport({
          isValid: true,
          rootPath: 'C:/tmp/pi-sem-chave',
          projectName: 'Sem chave',
          defaultProvider: 'deepseek',
          defaultModel: 'deepseek-chat',
          defaultTools: [],
          skills: [],
          stages: [{ id: 's1', name: 'Etapa', model: 'deepseek-chat', prompt: 'execute' }],
          mcpTools: [],
          orchestrationType: 'script'
        });

        expect(result.isSuccess).toBe(true);
        expect(result.confidenceScore).toBe(77);
        expect(result.verifiedStages).toHaveLength(1);
      } finally {
        if (previousKey !== undefined) process.env['DEEPSEEK_API_KEY'] = previousKey;
        intelligenceService.setCustomApiCaller(undefined);
      }
    });
  });

  describe('3. Importação Genérica de Qualquer Projeto Pi no Arestrix Studio', () => {
    it('deve sintetizar nós e arestas dinâmicos para um projeto Pi arbitrário sem estágios pré-fixados', async () => {
      const genericScan: IPiProjectScanResult = {
        isValid: true,
        rootPath: 'C:/Projetos/QualquerProjetoNovo',
        projectName: 'Tradutor Multilíngue',
        defaultProvider: 'deepseek',
        defaultModel: 'deepseek-chat',
        defaultTools: ['read', 'write'],
        skills: [
          { id: 'ingles', name: 'Tradutor EN', path: '.agents/ingles/SKILL.md' },
          { id: 'alemao', name: 'Tradutor DE', path: '.agents/alemao/SKILL.md' },
          { id: 'publicador', name: 'Publicador Markdown', path: '.agents/publicador/SKILL.md' }
        ],
        stages: [
          { id: 'step_en', name: 'Traduzir para Inglês', model: 'deepseek-chat', prompt: 'Traduza o texto para EN' },
          { id: 'step_de', name: 'Traduzir para Alemão', model: 'deepseek-chat', prompt: 'Traduza o texto para DE' },
          { id: 'step_save', name: 'Salvar Arquivos', model: 'taskflow_static', prompt: 'Salva os arquivos gerados', isDeterministic: true }
        ],
        mcpTools: [],
        orchestrationType: 'declarative',
        workItems: ['DOC-001', 'DOC-002', 'DOC-003', 'DOC-004']
      };

      const success = await importer.importProject(genericScan, graph, projectManager, bus);
      expect(success).toBe(true);

      const nodes = graph.getAllNodes();
      expect(nodes.length).toBe(3);

      // Verifica IDs e tipos dinâmicos
      const enNode = nodes.find(n => n.label.includes('Inglês'));
      expect(enNode).toBeDefined();
      expect(enNode?.type).toBe('agent');
      expect(enNode?.agentSettings?.model?.model).toBe('deepseek-chat');

      const saveNode = nodes.find(n => n.label.includes('Salvar'));
      expect(saveNode).toBeDefined();
      expect(saveNode?.type).toBe('static');

      // Verifica conexões sequenciais encadeando os 3 nós
      const edges = graph.getAllEdges();
      expect(edges.length).toBe(2);

      // Verifica atualização do ProjectManager
      const current = projectManager.getCurrentProject();
      expect(current.name).toBe('Tradutor Multilíngue');
      expect(current.projectId).toBe('tradutor-multilingue');
    });

    it('deve extrair etapas dinâmicas de código JavaScript arbitrário de orquestração', () => {
      const code = `
        const ETAPAS = [
          { id: 'etapa_coleta', nome: 'Coleta de Métricas', modelo: 'deepseek-flash', skill: '.agents/coleta/SKILL.md', prompt: (id) => \`Colete métricas de \${id}\` },
          { id: 'etapa_relatorio', nome: 'Geração do Relatório', modelo: 'deepseek-v4-pro', skill: '.agents/relatorio/SKILL.md', prompt: (id) => \`Gere PDF de \${id}\` }
        ];
      `;

      const stages = GenericPiProjectScanner.extractStagesFromScript(code, [], 'deepseek-v4-pro');
      expect(stages.length).toBe(2);
      expect(stages[0].id).toBe('etapa_coleta');
      expect(stages[0].name).toBe('Coleta de Métricas');
      expect(stages[0].model).toBe('deepseek-flash');
      expect(stages[1].id).toBe('etapa_relatorio');
      expect(stages[1].model).toBe('deepseek-v4-pro');
    });
  });
});
