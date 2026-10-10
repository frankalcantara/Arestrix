import { describe, it, expect, beforeEach } from 'vitest';
import { PiProjectImporter } from '../src/core/pi/PiProjectImporter.ts';
import { ProjectManager } from '../src/core/ProjectManager.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { Graph } from '../src/core/Graph.ts';

describe('Importador Genérico de Projetos Pi (PiProjectImporter)', () => {
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

  it('deve listar caminhos de projetos conhecidos de forma segura', async () => {
    const known = await importer.getKnownProjects();
    expect(known).toBeDefined();
    expect(Array.isArray(known)).toBe(true);
  });

  it('deve varrer a estrutura de um projeto genérico e retornar skills, estágios e catálogo de tarefas', async () => {
    const scan = await importer.scanProject('C:/Projetos/AgenteGenerico');
    expect(scan.isValid).toBe(true);
    expect(scan.projectName).toBeDefined();
    expect(scan.defaultProvider).toBe('deepseek');
    expect(scan.defaultModel).toBe('deepseek-v4-pro');

    // Valida detecção de skills e estágios
    expect(scan.skills.length).toBeGreaterThan(0);
    expect(scan.stages.length).toBeGreaterThan(0);
    expect(scan.stages[0].model).toBe('deepseek-v4-pro');
  });

  it('deve sintetizar o grafo Taskflow C++20 completo ao importar o projeto', async () => {
    let batchConfiguredEvent: any = null;
    bus.on('batch:configure', (data: any) => {
      batchConfiguredEvent = data;
    });

    const scan = await importer.scanProject('C:/Projetos/AgenteGenerico');
    const success = await importer.importProject(scan, graph, projectManager);

    expect(success).toBe(true);

    // Valida se os nós foram criados no grafo
    const nodes = graph.getAllNodes();
    expect(nodes.length).toBeGreaterThan(0);

    // Valida que o primeiro nó é um agente com modelo deepseek
    const firstAgent = nodes.find(n => n.type === 'agent');
    expect(firstAgent).toBeDefined();
    expect(firstAgent?.agentSettings?.model?.model).toBeDefined();

    // Valida conexões encadeando o pipeline
    const edges = graph.getAllEdges();
    expect(edges.length).toBeGreaterThanOrEqual(1);

    // Valida configuração do Batch Engine se houver itens
    if (scan.workItems && scan.workItems.length > 0) {
      expect(batchConfiguredEvent).toBeDefined();
    }

    // Valida atualização do ProjectManager dinâmico
    const curProject = projectManager.getCurrentProject();
    expect(curProject.projectId).toBeDefined();
    expect(curProject.projectId).toContain('generico');
  });

  it('deve lidar graciosamente com varreduras inválidas sem corromper o grafo', async () => {
    const invalidScan: any = { isValid: false };
    const success = await importer.importProject(invalidScan, graph, projectManager);
    expect(success).toBe(false);
    expect(graph.getAllNodes().length).toBe(0);
  });
});
