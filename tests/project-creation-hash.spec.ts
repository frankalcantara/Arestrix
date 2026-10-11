import { describe, it, expect, beforeEach } from 'vitest';
import { sha256, hashProjectName } from '../src/core/hash.ts';
import { ProjectManager } from '../src/core/ProjectManager.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { Graph } from '../src/core/Graph.ts';
import { BackstageView } from '../src/ui/ribbon/BackstageView.ts';

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
      expect(activeProject.rootPath).toBe(`./projects/${generatedId}`);
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
    });
  });
});
