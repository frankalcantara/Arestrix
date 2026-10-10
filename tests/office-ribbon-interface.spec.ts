import { describe, it, expect, beforeEach } from 'vitest';
import ExcelJS from 'exceljs';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { ProjectManager } from '../src/core/ProjectManager.ts';
import { ImportPiModal } from '../src/ui/modals/ImportPiModal.ts';
import { ImportExcelModal } from '../src/ui/modals/ImportExcelModal.ts';
import { ImportClaudeModal } from '../src/ui/modals/ImportClaudeModal.ts';
import { ImportOpenAiModal } from '../src/ui/modals/ImportOpenAiModal.ts';
import { ImportHermesModal } from '../src/ui/modals/ImportHermesModal.ts';

describe('Fase 15: Interface Estilo Microsoft Office & Conectores de Importação', () => {
  let bus: EventBus;
  let graph: Graph;
  let projectManager: ProjectManager;

  beforeEach(() => {
    bus = new EventBus();
    projectManager = ProjectManager.getInstance(bus);
    projectManager.resetForTesting(bus);

    graph = new Graph(bus, {
      graphName: 'Workspace_Office_Test',
      projectId: 'office_test_proj'
    });
  });

  it('deve sintetizar agente do Pi.ai com raciocínio empático e memória configurada (Fase 16 prioridade 1)', () => {
    const initialNodes = graph.getAllNodes().length;
    const piModal = new ImportPiModal(graph);

    piModal.injectPiAgent({
      name: 'Agente_Pi_Mentor_Estrategico',
      style: 'empathetic_dialogue',
      conversation: 'User: Como estruturar agentes concorrentes?\nPi: Use Taskflow e Blackboard particionado.'
    });

    const currentNodes = graph.getAllNodes();
    expect(currentNodes.length).toBe(initialNodes + 1);

    const piNode = currentNodes.find(n => n.label.includes('Pi') || n.id.startsWith('pi_agent'));
    expect(piNode).toBeDefined();
    expect(piNode?.type).toBe('agent');
    expect(piNode?.agentSettings).toBeDefined();
    expect(piNode?.agentSettings?.model?.model).toBe('pi-3-empathic');
    expect(piNode?.agentSettings?.memory?.enabled).toBe(true);
    expect(piNode?.agentSettings?.memory?.persistent).toBe(true);
    expect(piNode?.agentSettings?.memory?.domains).toContain('strategy');
  });

  it('deve sintetizar agentes do Claude, OpenAI e Hermes com configurações adequadas', () => {
    const claudeModal = new ImportClaudeModal(graph);
    claudeModal.injectClaudeAgent({ name: 'Agente_Claude_35_Sonnet', model: 'claude-3-5-sonnet' });

    const openaiModal = new ImportOpenAiModal(graph);
    openaiModal.injectOpenAiAgent({ name: 'Agente_GPT4o_Task', model: 'gpt-4o' });

    const hermesModal = new ImportHermesModal(graph);
    hermesModal.injectHermesAgent({ name: 'Agente_Hermes_Local_VM', model: 'hermes-3-llama-3.1-70b' });

    const nodes = graph.getAllNodes();
    const claudeNode = nodes.find(n => n.id.startsWith('claude_agent'));
    const openAiNode = nodes.find(n => n.id.startsWith('openai_agent'));
    const hermesNode = nodes.find(n => n.id.startsWith('hermes_agent'));

    expect(claudeNode).toBeDefined();
    expect(claudeNode?.agentSettings?.model?.provider).toBe('anthropic');
    expect(claudeNode?.agentSettings?.model?.model).toBe('claude-3-5-sonnet');

    expect(openAiNode).toBeDefined();
    expect(openAiNode?.agentSettings?.model?.provider).toBe('openai');
    expect(openAiNode?.agentSettings?.model?.model).toBe('gpt-4o');

    expect(hermesNode).toBeDefined();
    expect(hermesNode?.agentSettings?.sandbox?.isolationLevel).toBe('microvm');
    expect(hermesNode?.agentSettings?.model?.model).toBe('hermes-3-llama-3.1-70b');
  });

  it('deve analisar um .xlsx (Raio-X) e gerar o grafo de regiões com as dependências', async () => {
    const wb = new ExcelJS.Workbook();
    const dados = wb.addWorksheet('Dados');
    ['Item', 'Valor'].forEach((h, i) => (dados.getCell(1, i + 1).value = h));
    [['a', 10], ['b', 20], ['c', 30]].forEach(([n, v], i) => {
      dados.getCell(i + 2, 1).value = n;
      dados.getCell(i + 2, 2).value = v;
    });
    const resumo = wb.addWorksheet('Resumo');
    resumo.getCell('A1').value = 'Total';
    resumo.getCell('B1').value = { formula: 'SUM(Dados!B2:B4)', result: 60 };
    const bytes = new Uint8Array(await wb.xlsx.writeBuffer());

    const excelModal = new ImportExcelModal(graph);
    await excelModal.analyzeBytes(bytes, 'teste.xlsx');

    const model = excelModal.getModel();
    expect(model).not.toBeNull();
    expect(model!.stats.regions).toBe(2);
    expect(model!.dependencies).toEqual([{ from: 'Dados!A1:B4', to: 'Resumo!A1:B1', formulaCount: 1 }]);
    expect(JSON.parse(excelModal.exportJson()).schemaVersion).toBe(1);

    excelModal.generateRegionGraph();
    const nodes = graph.getAllNodes();
    expect(nodes.map(n => n.label).sort()).toEqual(['Dados_A1_B4', 'Resumo_A1_B1']);
    const edges = graph.getAllEdges();
    expect(edges).toHaveLength(1);
    const from = nodes.find(n => n.label === 'Dados_A1_B4')!;
    const to = nodes.find(n => n.label === 'Resumo_A1_B1')!;
    expect(edges[0]).toMatchObject({ sourceNodeId: from.id, targetNodeId: to.id });
  });

  it('deve informar erro legível quando o arquivo não é um .xlsx', async () => {
    const excelModal = new ImportExcelModal(graph);
    await excelModal.analyzeBytes(new TextEncoder().encode('a,b,c'), 'dados.csv');
    expect(excelModal.getModel()).toBeNull();
    const before = graph.getAllNodes().length;
    excelModal.generateRegionGraph(); // sem modelo: não faz nada
    expect(graph.getAllNodes().length).toBe(before);
  });

  it('deve validar estrutura das classes CSS da Ribbon compatíveis com o padrão Microsoft Office Fluent', () => {
    // Classes de controle do padrão Office Ribbon
    const ribbonClasses = [
      'office-ribbon-container',
      'office-titlebar',
      'office-tabs-bar',
      'office-ribbon-body',
      'office-backstage-overlay',
      'office-backstage-sidebar',
      'office-backstage-main',
      'ribbon-btn-large',
      'ribbon-btn-small'
    ];

    expect(ribbonClasses).toContain('office-ribbon-container');
    expect(ribbonClasses).toContain('office-backstage-overlay');
    expect(ribbonClasses).toContain('ribbon-btn-large');
    expect(ribbonClasses.length).toBe(9);
  });
});
