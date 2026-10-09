import { describe, it, expect, beforeEach } from 'vitest';
import { ProjectManager } from '../src/core/ProjectManager.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { Graph } from '../src/core/Graph.ts';
import { IProjectManifest } from '../src/core/types.ts';

describe('Fase 14: Conceito de Projetos & Workspaces Isolados', () => {
  let bus: EventBus;
  let projectManager: ProjectManager;
  let graph: Graph;

  beforeEach(() => {
    bus = new EventBus();
    projectManager = ProjectManager.getInstance(bus);
    projectManager.resetForTesting(bus);

    graph = new Graph(bus, {
      graphName: 'GrafoInicial',
      projectId: 'default'
    });
  });

  it('deve inicializar com um manifesto de workspace padrão estruturado', () => {
    const cur = projectManager.getCurrentProject();
    expect(cur).toBeDefined();
    expect(cur.projectId).toBe('default-workspace');
    expect(cur.name).toBe('Workspace Padrão');
    expect(cur.pipelines.length).toBeGreaterThan(0);
    expect(cur.pipelines[0].isPrimary).toBe(true);
    expect(cur.database?.type).toBe('sqlite');
  });

  it('deve criar um novo projeto com diretório, pipelines e sincronizar com o grafo', async () => {
    let eventReceived: IProjectManifest | null = null;
    bus.on('project:created', (proj: IProjectManifest) => {
      eventReceived = proj;
    });

    const manifestInput = {
      projectId: 'auditoria_fiscal_2026',
      name: 'Auditoria Fiscal Corporativa',
      description: 'Pipeline de auditoria de balancetes e apuração',
      rootPath: 'C:/Arestrix/Projetos/AuditoriaFiscal',
      environment: {
        'DATABASE_URL': 'sqlite:///storage/audit.sqlite',
        'MODO_EXECUCAO': 'auditoria'
      }
    };

    const success = await projectManager.createProject(manifestInput, graph);
    expect(success).toBe(true);

    const active = projectManager.getCurrentProject();
    expect(active.projectId).toBe('auditoria_fiscal_2026');
    expect(active.name).toBe('Auditoria Fiscal Corporativa');
    expect(active.rootPath).toBe('C:/Arestrix/Projetos/AuditoriaFiscal');
    expect(active.pipelines.length).toBe(1);
    expect(active.pipelines[0].file).toBe('pipelines/main.json');
    expect(active.environment?.['MODO_EXECUCAO']).toBe('auditoria');

    // Verifica sincronização com o grafo central
    expect(graph.getProjectId()).toBe('auditoria_fiscal_2026');
    expect(graph.getMetadata().graphName).toBe('Auditoria Fiscal Corporativa');
    expect(eventReceived).not.toBeNull();
  });

  it('deve manter histórico de projetos recentes em ordem MRU sem duplicações', async () => {
    await projectManager.createProject({
      projectId: 'proj_alpha',
      name: 'Projeto Alpha',
      rootPath: 'C:/Projetos/Alpha'
    });

    await projectManager.createProject({
      projectId: 'proj_beta',
      name: 'Projeto Beta',
      rootPath: 'C:/Projetos/Beta'
    });

    let recents = projectManager.listRecentProjects();
    expect(recents.length).toBe(2);
    expect(recents[0].projectId).toBe('proj_beta');
    expect(recents[1].projectId).toBe('proj_alpha');

    // Ao reabrir o Projeto Alpha, ele deve ir para o topo
    await projectManager.loadProject('C:/Projetos/Alpha');
    recents = projectManager.listRecentProjects();
    expect(recents.length).toBe(2);
    expect(recents[0].projectId).toBe('proj_alpha');
    expect(recents[1].projectId).toBe('proj_beta');
  });

  it('deve permitir atualizar metadados do projeto ativo e refletir no grafo', async () => {
    await projectManager.createProject({
      projectId: 'proj_update',
      name: 'Nome Antigo',
      rootPath: 'C:/Projetos/Update'
    }, graph);

    projectManager.updateProjectMetadata({
      name: 'Nome Renovado 2.0',
      description: 'Nova descrição executiva'
    }, graph);

    const cur = projectManager.getCurrentProject();
    expect(cur.name).toBe('Nome Renovado 2.0');
    expect(cur.description).toBe('Nova descrição executiva');
    expect(graph.getMetadata().graphName).toBe('Nome Renovado 2.0');
  });

  it('deve isolar o escopo de memória do Blackboard ao alternar entre projetos', async () => {
    // Configura projeto 1
    await projectManager.createProject({
      projectId: 'cliente_hospital',
      name: 'Hospital São Lucas',
      rootPath: 'C:/Workspaces/Hospital'
    }, graph);
    expect(graph.getProjectId()).toBe('cliente_hospital');

    // Alterna para projeto 2
    await projectManager.createProject({
      projectId: 'cliente_banco',
      name: 'Banco Investimento',
      rootPath: 'C:/Workspaces/Banco'
    }, graph);
    expect(graph.getProjectId()).toBe('cliente_banco');

    // Ao recarregar o projeto 1, o projectId do grafo é restaurado
    await projectManager.loadProject('C:/Workspaces/Hospital', graph);
    expect(graph.getProjectId()).toBe('cliente_hospital');
  });
});
