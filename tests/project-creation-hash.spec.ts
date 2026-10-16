import { describe, it, expect, beforeEach } from 'vitest';
import { sha256, hashProjectName } from '../src/core/hash.ts';
import { ProjectManager } from '../src/core/ProjectManager.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { Graph } from '../src/core/Graph.ts';
import { BackstageView } from '../src/ui/ribbon/BackstageView.ts';
import { ProjectManagerModal } from '../src/ui/modals/ProjectManagerModal.ts';

describe('Geração de ID Único com Hash e Ajuste do Formulário de Criação de Projeto', () => {
  let bus: EventBus;
  let projectManager: ProjectManager;
  let graph: Graph;

  beforeEach(() => {
    bus = new EventBus();
    projectManager = ProjectManager.getInstance(bus);
    projectManager.resetForTesting(bus);
    graph = new Graph(bus, { graphName: 'Workspace_Test', projectId: 'test_proj' });
  });

  describe('Função de Hash (SHA-256)', () => {
    it('deve calcular o hash SHA-256 determinístico correspondente aos vetores de teste NIST', () => {
      // Vetores padrão de teste
      expect(sha256('')).toBe('e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855');
      expect(sha256('abc')).toBe('ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad');
    });

    it('deve gerar ID único determinístico a partir do nome do projeto', () => {
      const name = 'Auditoria Fiscal 2026';
      const id1 = hashProjectName(name);
      const id2 = hashProjectName(name);
      expect(id1).toBe(id2);
      expect(id1).toBe(sha256(name));
      expect(id1).toHaveLength(64);
    });

    it('deve usar fallback se o nome do projeto for em branco ou vazio', () => {
      const fallbackHash = sha256('Novo Projeto');
      expect(hashProjectName('')).toBe(fallbackHash);
      expect(hashProjectName('   ')).toBe(fallbackHash);
    });
  });

  describe('Tela Criar Projeto (BackstageView - renderSectionContent)', () => {
    it('NÃO deve conter o campo "ID único do Projeto" no template de criação', () => {
      const backstage = new BackstageView(graph, {} as any, {} as any);
      (backstage as any).activeSection = 'new';
      const html = (backstage as any).renderSectionContent(projectManager.getCurrentProject(), []);

      // O campo ID único não deve existir no HTML retornado
      expect(html).not.toContain('ID Único do Projeto:');
      expect(html).not.toContain('id="backstage-new-id"');
    });

    it('deve exibir o campo com rótulo "Nome do Projeto:" e NÃO "Nome de Exibição:"', () => {
      const backstage = new BackstageView(graph, {} as any, {} as any);
      (backstage as any).activeSection = 'new';
      const html = (backstage as any).renderSectionContent(projectManager.getCurrentProject(), []);

      // Confirma que "Nome do Projeto:" está presente e "Nome de Exibição:" foi removido
      expect(html).toContain('Nome do Projeto:');
      expect(html).toContain('id="backstage-new-name"');
      expect(html).not.toContain('Nome de Exibição:');
    });

    it('deve exibir a lista de projetos recentes na tela Informações abaixo de ações rápidas de manutenção', () => {
      const backstage = new BackstageView(graph, {} as any, {} as any);
      (backstage as any).activeSection = 'info';
      const recents = [
        { projectId: 'p1', name: 'Proj Alpha', rootPath: 'C:/Alpha', lastOpened: new Date().toISOString() }
      ];
      const html = (backstage as any).renderSectionContent(projectManager.getCurrentProject(), recents);

      expect(html).toContain('Ações Rápidas de Manutenção');
      expect(html).toContain('Projetos Recentes');
      expect(html).toContain('Proj Alpha');
      expect(html).toContain('recent-project-action-select');
      expect(html).toContain('Opções ▾');
      expect(html).toContain('Abrir Projeto');
      expect(html).toContain('Gerenciar');
      expect(html).toContain('Apagar Projeto...');

      const manutencaoIdx = html.indexOf('Ações Rápidas de Manutenção');
      const recentsIdx = html.indexOf('Projetos Recentes');
      expect(recentsIdx).toBeGreaterThan(manutencaoIdx);
    });

    it('deve formatar o ID do projeto e o Diretório Físico na tela Informações com estilos anti-overflow (word-break e overflow-wrap)', () => {
      const backstage = new BackstageView(graph, {} as any, {} as any);
      (backstage as any).activeSection = 'info';
      const cur = {
        ...projectManager.getCurrentProject(),
        projectId: 'proj_hash_super_longo_que_nao_deve_vazar_do_card_de_informacoes_1234567890',
        rootPath: 'C:/Diretorio/Super/Profundo/E/Extremamente/Longo/Que/Nao/Deve/Vazar/Do/Card'
      };
      const html = (backstage as any).renderSectionContent(cur, []);

      expect(html).toContain('Informações do Projeto');
      expect(html).toContain('word-break: break-all');
      expect(html).toContain('overflow-wrap: anywhere');
      expect(html).toContain('proj_hash_super_longo');
      expect(html).toContain('Super/Profundo');
    });

    it('deve repetir a lista de projetos recentes na tela Novo Projeto com o dropdown de opções', () => {
      const backstage = new BackstageView(graph, {} as any, {} as any);
      (backstage as any).activeSection = 'new';
      const recents = [
        { projectId: 'p1', name: 'Proj Alpha', rootPath: 'C:/Alpha', lastOpened: new Date().toISOString() }
      ];
      const html = (backstage as any).renderSectionContent(projectManager.getCurrentProject(), recents);

      expect(html).toContain('Criar Novo Projeto');
      expect(html).toContain('Projetos Recentes');
      expect(html).toContain('Proj Alpha');
      expect(html).toContain('recent-project-action-select');
      expect(html).toContain('Opções ▾');
    });

    it('deve exibir na tela Abrir os projetos recentes e, na aba Grafos, os grafos do projeto atual', () => {
      const backstage = new BackstageView(graph, {} as any, {} as any);
      (backstage as any).activeSection = 'open';
      const recents = [
        { projectId: 'p1', name: 'Proj Alpha', rootPath: 'C:/Alpha', lastOpened: new Date().toISOString() }
      ];
      const currentProj = {
        ...projectManager.getCurrentProject(),
        pipelines: [
          { id: 'main', name: 'Pipeline Principal', file: 'pipelines/main.json', isPrimary: true }
        ]
      };
      const projectsHtml = (backstage as any).renderSectionContent(currentProj, recents);
      expect(projectsHtml).toContain('Proj Alpha');
      expect(projectsHtml).toContain('recent-project-action-select');
      expect(projectsHtml).toContain('data-open-tab="graphs"');

      (backstage as any).openRecentTab = 'graphs';
      const graphsHtml = (backstage as any).renderSectionContent(currentProj, recents);
      expect(graphsHtml).toContain('Pipeline Principal');
      expect(graphsHtml).toContain('data-open-action="pipeline"');
      expect(graphsHtml).not.toContain('Proj Alpha');
    });

    it('deve carregar projetos recentes logo na primeira abertura de backstage.open()', async () => {
      await projectManager.createProject({
        name: 'Projeto Existente 1',
        rootPath: 'C:/Projetos/P1'
      });
      await projectManager.createProject({
        name: 'Projeto Existente 2',
        rootPath: 'C:/Projetos/P2'
      });

      const backstage = new BackstageView(graph, {} as any, {} as any);
      await backstage.open('info');

      const html = (backstage as any).renderSectionContent(
        projectManager.getCurrentProject(),
        projectManager.listRecentProjects()
      );
      expect(html).toContain('Projeto Existente 1');
      expect(html).toContain('Projeto Existente 2');
    });
  });

  describe('Criação do Projeto com ID de Hash', () => {
    it('deve criar o projeto gerando o ID único via hash aplicado ao nome do projeto', async () => {
      const projectName = 'Controle de Estoque Inteligente';
      const generatedId = hashProjectName(projectName);

      await projectManager.createProject({
        projectId: generatedId,
        name: projectName,
        description: 'Automação logística'
      }, graph);

      const activeProject = projectManager.getCurrentProject();
      expect(activeProject.name).toBe(projectName);
      expect(activeProject.projectId).toBe(generatedId);
      expect(activeProject.projectId).toHaveLength(64);
      expect(activeProject.rootPath).toBe(`./projects/${projectName}`);
      expect(activeProject.rootPath).not.toContain(generatedId);
    });

    it('deve aplicar hash ao nome do projeto automaticamente se projectId não for fornecido', async () => {
      const projectName = 'Pipeline Agentes Autônomos';
      await projectManager.createProject({
        name: projectName
      });

      const current = projectManager.getCurrentProject();
      expect(current.name).toBe(projectName);
      expect(current.projectId).toBe(hashProjectName(projectName));
      expect(current.projectId).toHaveLength(64);
      expect(current.rootPath).toBe(`./projects/${projectName}`);
    });

    it('deve ZERAR e LIMPAR o grafo completamente ao criar um novo projeto', async () => {
      // Pré-popula o grafo com nós simulando resquício de tela anterior
      graph.createNode('node_old_1', 'Antigo Nó 1', 'agent', 0, 0);
      graph.createNode('node_old_2', 'Antigo Nó 2', 'condition', 100, 100);
      expect(graph.getAllNodes().length).toBe(2);

      // Cria um novo projeto
      await projectManager.createProject({
        name: 'Projeto Limpo do Zero'
      }, graph);

      // O grafo deve ter sido completamente zerado e limpo (0 nós e 0 arestas)
      expect(graph.getAllNodes().length).toBe(0);
      expect(graph.getAllEdges().length).toBe(0);
    });

    it('deve salvar e restaurar nós do grafo ao salvar e recarregar o projeto', async () => {
      await projectManager.createProject({
        projectId: 'proj_com_grafo',
        name: 'Projeto com Nós',
        rootPath: 'C:/Workspaces/ProjGrafo'
      }, graph);

      // Adiciona nós ao projeto
      const n1 = graph.createNode('node_novo_1', 'Agente Ingestão', 'agent', 50, 50);
      const n2 = graph.createNode('node_novo_2', 'Decisão Filtro', 'condition', 250, 50);
      graph.createEdge(n1.id, n1.outputs[0].id, n2.id, n2.inputs[0].id);

      // Salva o projeto junto com o grafo
      await projectManager.saveProject(graph);

      // Simula alteração/limpeza temporária
      graph.clear();
      expect(graph.getAllNodes().length).toBe(0);

      // Recarrega o projeto
      await projectManager.loadProject('C:/Workspaces/ProjGrafo', graph);

      // Nós e arestas devem ter sido restaurados
      expect(graph.getAllNodes().length).toBe(2);
      expect(graph.getAllEdges().length).toBe(1);
    });
  });

  describe('Modal Gerenciador de Projetos (ProjectManagerModal)', () => {
    it('NÃO deve exibir o campo Identificador Único na aba Novo Projeto', () => {
      const modal = new ProjectManagerModal(graph);
      const html = (modal as any).renderCreateTab();

      expect(html).not.toContain('Identificador Único (slug)');
      expect(html).not.toContain('id="new-proj-id"');
      expect(html).toContain('id="new-proj-name"');
      expect(html).toContain('Nome do Projeto *');
    });

    it('deve posicionar os seletores de template à esquerda dos nomes em um grid espaçoso', () => {
      const modal = new ProjectManagerModal(graph);
      const html = (modal as any).renderCreateTab();

      // Verifica se utiliza template-choice-card e template-choice-info
      expect(html).toContain('class="template-choice-card');
      expect(html).toContain('class="template-choice-title">Pipeline Vazio</span>');
      expect(html).toContain('class="template-choice-title">Pipeline Multi-Agente IA</span>');

      // Verifica que o input radio precede o título/informação do template (ficando à esquerda)
      const emptyIdx = html.indexOf('Pipeline Vazio');
      const radioEmptyIdx = html.indexOf('value="empty"');
      expect(radioEmptyIdx).toBeGreaterThan(-1);
      expect(radioEmptyIdx).toBeLessThan(emptyIdx);

      const agentsIdx = html.indexOf('Pipeline Multi-Agente IA');
      const radioAgentsIdx = html.indexOf('value="agents"');
      expect(radioAgentsIdx).toBeGreaterThan(-1);
      expect(radioAgentsIdx).toBeLessThan(agentsIdx);
    });

    it('deve carregar o campo Diretório Raiz do workspace com pasta dinâmica e botão Procurar', async () => {
      // Simula projeto recente para resolução de diretório
      await projectManager.createProject({
        name: 'Projeto de Teste Prévio',
        rootPath: 'C:/Workspaces/ProjetosRecentes/Teste1'
      });

      const modal = new ProjectManagerModal(graph);
      await (modal as any).resolveDefaultWorkspaceDir();
      const html = (modal as any).renderCreateTab();

      expect(html).toContain('Diretório Raiz do Workspace *');
      expect(html).toContain('id="new-proj-path"');
      expect(html).toContain('value="C:/Workspaces/ProjetosRecentes/Teste1"');
      expect(html).toContain('id="btn-browse-new-proj-path"');
      expect(html).toContain('Procurar...');
    });

    it('deve equipar a lista de projetos recentes com dropdown de ações (Opções ▾) contendo Abrir, Gerenciar e Apagar', () => {
      const modal = new ProjectManagerModal(graph);
      const recents = [
        {
          projectId: 'p_123',
          name: 'Projeto Auditoria Anual',
          rootPath: 'C:/Workspaces/Auditoria',
          lastOpened: new Date().toISOString()
        }
      ];
      const html = (modal as any).renderRecentsTab(recents, { projectId: 'outro', name: 'Outro' } as any);

      expect(html).toContain('recents-modal-action-select');
      expect(html).toContain('recent-project-action-select');
      expect(html).toContain('Opções ▾');
      expect(html).toContain('value="open">📂 Abrir Projeto</option>');
      expect(html).toContain('value="manage">⚙️ Gerenciar</option>');
      expect(html).toContain('value="delete">🗑️ Apagar Projeto...</option>');
    });
  });

  describe('Exclusão de Projetos e Ações do Dropdown (deleteProject)', () => {
    it('deve remover o projeto da lista de recentes ao chamar deleteProject', async () => {
      await projectManager.createProject({
        projectId: 'proj_para_apagar',
        name: 'Projeto Provisório',
        rootPath: 'C:/Projetos/Provisorio'
      });
      await projectManager.createProject({
        projectId: 'proj_fixo',
        name: 'Projeto Fixo',
        rootPath: 'C:/Projetos/Fixo'
      });

      expect(projectManager.listRecentProjects().length).toBe(2);

      const ok = await projectManager.deleteProject('proj_para_apagar', false);
      expect(ok).toBe(true);

      const remaining = projectManager.listRecentProjects();
      expect(remaining.length).toBe(1);
      expect(remaining[0].projectId).toBe('proj_fixo');
    });

    it('deve resetar o projeto ativo para o workspace padrão e limpar o grafo se o projeto apagado for o ativo', async () => {
      await projectManager.createProject({
        projectId: 'proj_ativo_apagar',
        name: 'Projeto Ativo Para Apagar',
        rootPath: 'C:/Projetos/AtivoApagar'
      }, graph);

      // Popula grafo com nó
      graph.createNode('node_ativo_1', 'Nó Ativo', 'agent', 10, 10);
      expect(graph.getAllNodes().length).toBe(1);
      expect(projectManager.getCurrentProject().projectId).toBe('proj_ativo_apagar');

      // Apaga o projeto ativo
      let eventPayload: any = null;
      bus.on('project:deleted', (data) => {
        eventPayload = data;
      });

      await projectManager.deleteProject('proj_ativo_apagar', false, graph);

      expect(eventPayload).toEqual({ projectId: 'proj_ativo_apagar', rootPath: 'C:/Projetos/AtivoApagar' });
      expect(projectManager.getCurrentProject().projectId).toBe('default-workspace');
      expect(graph.getAllNodes().length).toBe(0);
    });
  });
});
