import { describe, it, expect, beforeEach } from 'vitest';
import { PiProjectImporter } from '../src/core/pi/PiProjectImporter.ts';
import { GenericPiProjectScanner } from '../src/core/pi/GenericPiProjectScanner.ts';
import { GitHubSkillsRegistry } from '../src/core/skills/GitHubSkillsRegistry.ts';
import { ProjectManager } from '../src/core/ProjectManager.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { Graph } from '../src/core/Graph.ts';

describe('Topologia Concorrente (Fork/Join) e Ingestão de Skills no Pi Import', () => {
  let bus: EventBus;
  let projectManager: ProjectManager;
  let graph: Graph;
  let importer: PiProjectImporter;

  beforeEach(() => {
    bus = new EventBus();
    projectManager = ProjectManager.getInstance(bus);
    projectManager.resetForTesting(bus);

    graph = new Graph(bus, {
      graphName: 'ConcurrentImportTest',
      projectId: 'default'
    });

    importer = PiProjectImporter.getInstance();
  });

  it('deve extrair conteúdo Markdown completo do SKILL.md durante a varredura estática', async () => {
    const scan = await GenericPiProjectScanner.scan('C:/Projetos/ProjetoTestePi');
    expect(scan.isValid).toBe(true);
    expect(scan.skills.length).toBeGreaterThan(0);

    // Todas as skills devem possuir markdownContent completo e systemPrompt
    for (const skill of scan.skills) {
      expect(skill.markdownContent).toBeDefined();
      expect(skill.markdownContent?.length).toBeGreaterThan(20);
      expect(skill.markdownContent).toContain('#');
      expect(skill.systemPrompt).toBeDefined();
      expect(skill.tools).toBeDefined();
    }
  });

  it('deve sintetizar um DAG concorrente com nós fork e join ao invés de um pipeline linear', async () => {
    const scan = await importer.scanProject('C:/Projetos/ProjetoTestePi');
    const success = await importer.importProject(scan, graph, projectManager);
    expect(success).toBe(true);

    const nodes = graph.getAllNodes();
    expect(nodes.length).toBeGreaterThan(5);

    // 1. Deve possuir nó do tipo Fork (Despachante Concorrente)
    const forkNode = nodes.find(n => n.type === 'fork');
    expect(forkNode).toBeDefined();
    expect(forkNode?.label).toContain('Despachante');

    // 2. Deve possuir nó do tipo Join (Barreira de Sincronização)
    const joinNode = nodes.find(n => n.type === 'join');
    expect(joinNode).toBeDefined();
    expect(joinNode?.label).toContain('Sincronização');

    // 3. Deve possuir arestas paralelas partindo do Fork
    const edges = graph.getAllEdges();
    const forkOutputs = edges.filter(e => e.sourceNodeId === forkNode?.id);
    expect(forkOutputs.length).toBeGreaterThanOrEqual(2); // Dispara ao menos 2 ramos paralelos

    // 4. Deve possuir arestas convergindo na barreira Join
    const joinInputs = edges.filter(e => e.targetNodeId === joinNode?.id);
    expect(joinInputs.length).toBeGreaterThanOrEqual(2); // Convergência de múltiplos ramos

    // 5. O nó final deve ser a publicação determinística sucedendo o Join
    const publishNode = nodes.find(n => n.id.includes('publicar') || (n.type === 'static' && n.label.includes('Publicação')));
    expect(publishNode).toBeDefined();
    const joinToPublish = edges.find(e => e.sourceNodeId === joinNode?.id && e.targetNodeId === publishNode?.id);
    expect(joinToPublish).toBeDefined();
  });

  it('deve anexar objetos INodeSkill completos às propriedades dos nós de agente', async () => {
    const scan = await importer.scanProject('C:/Projetos/ProjetoTestePi');
    await importer.importProject(scan, graph, projectManager);

    const agentNodes = graph.getAllNodes().filter(n => n.type === 'agent');
    expect(agentNodes.length).toBeGreaterThan(0);

    for (const agent of agentNodes) {
      expect(agent.agentSettings).toBeDefined();
      expect(agent.agentSettings?.skills).toBeDefined();
      expect(agent.agentSettings?.skills?.length).toBeGreaterThan(0);

      const activeSkill = agent.agentSettings?.skills?.[0];
      expect(activeSkill).toBeDefined();
      expect(activeSkill?.name).toBeDefined();
      expect(activeSkill?.markdownContent).toBeDefined();
      expect(activeSkill?.markdownContent).toContain('#');
      expect(activeSkill?.systemPrompt).toBeDefined();
      expect(activeSkill?.tools).toBeDefined();
      expect(agent.agentSettings?.activeSkillId).toBe(activeSkill?.id);
    }
  });

  it('deve gerenciar o catálogo de skills do GitHub com licenças permissivas e repositórios customizáveis', () => {
    const registry = GitHubSkillsRegistry.getInstance();
    const repos = registry.getRepositories();
    expect(repos.length).toBeGreaterThanOrEqual(4);

    // Todos os repos padrão devem ter licença permissiva (MIT ou Apache-2.0)
    for (const repo of repos) {
      expect(['MIT', 'Apache-2.0', 'BSD-3-Clause']).toContain(repo.license);
      expect(repo.isEnabled).toBe(true);
    }

    const defaultSkills = registry.getDefaultSkills();
    expect(defaultSkills.length).toBeGreaterThanOrEqual(5);

    // Testa conversão para INodeSkill
    const firstGitHubSkill = defaultSkills[0];
    const nodeSkill = registry.convertToNodeSkill(firstGitHubSkill);
    expect(nodeSkill.id).toBe(firstGitHubSkill.id);
    expect(nodeSkill.markdownContent).toBe(firstGitHubSkill.markdownContent);
    expect(nodeSkill.systemPrompt).toBe(firstGitHubSkill.systemPrompt);
    expect(nodeSkill.license).toBe(firstGitHubSkill.license);

    // Testa adição e toggle de novo repositório
    const added = registry.addRepository('community/extra-skills', 'Extra Community Skills', 'Habilidades adicionais', 'MIT');
    expect(added).toBe(true);
    expect(registry.getRepositories().some(r => r.repo === 'community/extra-skills')).toBe(true);

    // Toggle para desativar
    const newRepo = registry.getRepositories().find(r => r.repo === 'community/extra-skills');
    if (newRepo) {
      registry.toggleRepository(newRepo.id, false);
      expect(registry.getRepositories().find(r => r.id === newRepo.id)?.isEnabled).toBe(false);
      registry.removeRepository(newRepo.id);
    }
  });
});
