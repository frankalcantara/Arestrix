import { describe, it, expect, beforeEach } from 'vitest';
import { PiProjectImporter } from '../src/core/pi/PiProjectImporter.ts';
import { ProjectManager } from '../src/core/ProjectManager.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { Graph } from '../src/core/Graph.ts';

describe('Fase 15: Importador do Pi Agent (pi-matematica)', () => {
  let bus: EventBus;
  let projectManager: ProjectManager;
  let graph: Graph;
  let importer: PiProjectImporter;

  beforeEach(() => {
    bus = new EventBus();
    projectManager = ProjectManager.getInstance(bus);
    projectManager.resetForTesting(bus);

    graph = new Graph(bus, {
      graphName: 'TestGraph',
      projectId: 'default'
    });

    importer = PiProjectImporter.getInstance();
  });

  it('deve listar caminhos de projetos conhecidos incluindo pi-matematica', async () => {
    const known = await importer.getKnownProjects();
    expect(known).toBeDefined();
    expect(Array.isArray(known)).toBe(true);
    expect(known.length).toBeGreaterThan(0);
    expect(known.some(p => p.includes('pi-matematica'))).toBe(true);
  });

  it('deve varrer a estrutura do projeto pi-matematica e retornar skills, estágios e plano', async () => {
    const scan = await importer.scanProject('C:/Projetos/frankalcantara.github.io.worktrees/pi-matematica');
    expect(scan.isValid).toBe(true);
    expect(scan.projectName).toContain('Pi Matemática');
    expect(scan.defaultProvider).toBe('deepseek');
    expect(scan.defaultModel).toBe('deepseek-v4-pro');

    // Valida detecção das 6 skills
    expect(scan.skills.length).toBe(6);
    const skillIds = scan.skills.map(s => s.id);
    expect(skillIds).toContain('redigir-math');
    expect(skillIds).toContain('criar-frontmatter');
    expect(skillIds).toContain('planejar-svg');
    expect(skillIds).toContain('criar-svg');
    expect(skillIds).toContain('validar-svg');
    expect(skillIds).toContain('animar-svg');

    // Valida detecção dos estágios do pipeline
    expect(scan.stages.length).toBe(7);
    expect(scan.stages[0].id).toBe('1-redigir');
    expect(scan.stages[0].model).toBe('deepseek-v4-pro');
    expect(scan.stages[1].model).toBe('deepseek-flash');

    // Valida artigos do currículo
    expect(scan.articles.length).toBeGreaterThan(0);
    expect(scan.articles[0]).toBe('ART-001');
  });

  it('deve sintetizar o grafo Taskflow C++20 completo ao importar o projeto', async () => {
    let batchConfiguredEvent: any = null;
    bus.on('batch:configure', (data: any) => {
      batchConfiguredEvent = data;
    });

    const scan = await importer.scanProject('C:/Projetos/frankalcantara.github.io.worktrees/pi-matematica');
    const success = await importer.importProject(scan, graph, projectManager);

    expect(success).toBe(true);

    // Valida se os 7 nós foram criados no grafo
    const nodes = graph.getAllNodes();
    expect(nodes.length).toBe(7);

    // Valida Nó 1: Redação
    const nodeRedigir = nodes.find(n => n.id === 'node_pi_redigir');
    expect(nodeRedigir).toBeDefined();
    expect(nodeRedigir?.type).toBe('agent');
    expect(nodeRedigir?.agentSettings?.model?.model).toBe('deepseek-v4-pro');
    expect(nodeRedigir?.agentSettings?.model?.skillPath).toContain('redigir-math');
    expect(nodeRedigir?.agentSettings?.isSerialAcrossBatch).toBe(true);
    expect(nodeRedigir?.agentSettings?.memory?.enabled).toBe(true);
    expect(nodeRedigir?.agentSettings?.memory?.persistent).toBe(true);
    expect(nodeRedigir?.agentSettings?.validationRules?.[0].minSizeBytes).toBe(1000);

    // Valida Nó 4: Criação de SVG
    const nodeCriarSvg = nodes.find(n => n.id === 'node_pi_criar_svg');
    expect(nodeCriarSvg).toBeDefined();
    expect(nodeCriarSvg?.agentSettings?.model?.model).toBe('deepseek-flash');
    expect(nodeCriarSvg?.agentSettings?.model?.skillPath).toContain('criar-svg');

    // Valida Nó 7: Publicação estática
    const nodePublicar = nodes.find(n => n.id === 'node_pi_publicar');
    expect(nodePublicar).toBeDefined();
    expect(nodePublicar?.type).toBe('static');
    expect(nodePublicar?.parameters?.destinationDir).toContain('matematica');

    // Valida 6 Conexões Bézier encadeando o pipeline linear
    const edges = graph.getAllEdges();
    expect(edges.length).toBe(6);

    // Valida configuração do Batch Engine
    expect(batchConfiguredEvent).toBeDefined();
    expect(batchConfiguredEvent.items.length).toBeGreaterThan(0);
    expect(batchConfiguredEvent.serializedStages).toContain('1-redigir');
    expect(batchConfiguredEvent.autoResume).toBe(true);

    // Valida atualização do ProjectManager
    const curProject = projectManager.getCurrentProject();
    expect(curProject.projectId).toBe('pi-matematica');
    expect(curProject.environment?.['PI_PROVIDER']).toBe('deepseek');
  });

  it('deve lidar graciosamente com varreduras inválidas sem corromper o grafo', async () => {
    const invalidScan: any = { isValid: false };
    const success = await importer.importProject(invalidScan, graph, projectManager);
    expect(success).toBe(false);
    expect(graph.getAllNodes().length).toBe(0);
  });
});
