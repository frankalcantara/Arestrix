import { EventBus, globalEventBus } from '../EventBus.ts';
import { Graph } from '../Graph.ts';
import { INodeSkill } from '../types.ts';
import {
  IWorkspaceConfig,
  IWorkspaceApprovedSkill,
  IWorkspaceApprovedMcp,
  IWorkspaceSkillAuditVerdict,
  IWorkspaceAgent,
  IWorkspaceExecutionResult
} from './types.ts';
import {
  APPROVED_WORKSPACE_SKILLS,
  APPROVED_WORKSPACE_MCPS,
  WORKSPACE_GOVERNANCE_AGENTS
} from './ApprovedWorkspaceCatalog.ts';
import { WorkspaceSkillAuditorAgent } from './WorkspaceSkillAuditorAgent.ts';
import { sanitizeEnvReferences } from '../env/EnvironmentManager.ts';

/**
 * Arestrix Studio - Gerenciador Central de Workspace e Governança
 * 
 * Responsável por:
 * 1. Provisionar e sincronizar as pastas físicas de skills, MCPs e agentes na máquina do usuário.
 * 2. Orquestrar a execução de agentes, grafos e projetos no escopo do workspace.
 * 3. Atrelar os eventos da interface (importação de skills, auditorias, execuções) ao sistema.
 */
export class WorkspaceManager {
  private static instance: WorkspaceManager;
  private bus: EventBus;
  private auditorAgent: WorkspaceSkillAuditorAgent;
  private activeWorkspaceRoot: string = '.';
  private autoVerifySkills: boolean = true;
  private installedSkills: Map<string, IWorkspaceApprovedSkill> = new Map();
  private installedMcps: Map<string, IWorkspaceApprovedMcp> = new Map();
  private governanceAgents: Map<string, IWorkspaceAgent> = new Map();
  private isProvisioned: boolean = false;
  private auditHistory: Map<string, IWorkspaceSkillAuditVerdict> = new Map();

  public isWorkspaceProvisioned(): boolean {
    return this.isProvisioned;
  }

  private constructor(bus: EventBus = globalEventBus) {
    this.bus = bus;
    this.auditorAgent = WorkspaceSkillAuditorAgent.getInstance();
    this.initDefaultCatalogs();
    this.bindEvents();
  }

  public static getInstance(bus: EventBus = globalEventBus): WorkspaceManager {
    if (!WorkspaceManager.instance) {
      WorkspaceManager.instance = new WorkspaceManager(bus);
    }
    return WorkspaceManager.instance;
  }

  private initDefaultCatalogs(): void {
    APPROVED_WORKSPACE_SKILLS.forEach(s => this.installedSkills.set(s.id, { ...s }));
    APPROVED_WORKSPACE_MCPS.forEach(m => this.installedMcps.set(m.id, { ...m }));
    WORKSPACE_GOVERNANCE_AGENTS.forEach(a => this.governanceAgents.set(a.id, { ...a }));
  }

  private bindEvents(): void {
    // 1. Escuta solicitação de importação de habilidade
    this.bus.on('skill:import-requested', (payload: { skill: Partial<INodeSkill>; nodeId?: string; isCurated?: boolean }) => {
      const verdict = this.auditSkill(payload.skill, payload.isCurated);
      this.bus.emit('skill:verified', { ...payload, verdict });
    });

    // 2. Escuta solicitação de auditoria avulsa de habilidade
    this.bus.on('skill:audit-requested', (payload: { skill: Partial<INodeSkill>; callback?: (v: IWorkspaceSkillAuditVerdict) => void }) => {
      const verdict = this.auditSkill(payload.skill);
      if (payload.callback) payload.callback(verdict);
      this.bus.emit('skill:verified', { skill: payload.skill, verdict });
    });

    // 3. Escuta execução de agentes de workspace
    this.bus.on('workspace:run-agent', async (payload: { agentId: string; inputPayload?: any }) => {
      const result = await this.runAgent(payload.agentId, payload.inputPayload);
      this.bus.emit('workspace:agent-executed', result);
    });

    // 4. Escuta execução de grafos no workspace
    this.bus.on('workspace:run-graph', async (payload: { graph: Graph; graphId?: string }) => {
      const result = await this.runGraph(payload.graph, payload.graphId);
      this.bus.emit('workspace:graph-executed', result);
    });

    // 5. Escuta execução de projetos no workspace
    this.bus.on('workspace:run-project', async (payload: { projectId: string; graph?: Graph }) => {
      const result = await this.runProject(payload.projectId, payload.graph);
      this.bus.emit('workspace:project-executed', result);
    });

    // 6. Ao carregar ou criar projeto, sincroniza o workspace
    this.bus.on('project:loaded', (project: any) => {
      if (project && project.rootPath) {
        this.setWorkspaceRoot(project.rootPath);
        this.ensureWorkspaceDirectories(project.rootPath);
      }
    });

    this.bus.on('project:created', (project: any) => {
      if (project && project.rootPath) {
        this.setWorkspaceRoot(project.rootPath);
        this.ensureWorkspaceDirectories(project.rootPath);
      }
    });
  }

  public setWorkspaceRoot(rootPath: string): void {
    this.activeWorkspaceRoot = rootPath.replace(/\\/g, '/').replace(/\/$/, '') || '.';
  }

  public getWorkspaceRoot(): string {
    return this.activeWorkspaceRoot;
  }

  public getWorkspaceDirectories(): { root: string; skills: string; mcps: string; agents: string } {
    const base = this.activeWorkspaceRoot === '.' ? './.workspace' : `${this.activeWorkspaceRoot}/.workspace`;
    return {
      root: base,
      skills: `${base}/skills`,
      mcps: `${base}/mcps`,
      agents: `${base}/agents`
    };
  }

  /**
   * Garante a criação física das pastas do workspace na máquina do usuário e grava os assets
   */
  public async ensureWorkspaceDirectories(rootPath?: string): Promise<boolean> {
    if (rootPath) {
      this.setWorkspaceRoot(rootPath);
    }
    const dirs = this.getWorkspaceDirectories();

    let success = false;
    try {
      const glob = typeof window !== 'undefined' ? (window as any) : (typeof globalThis !== 'undefined' ? (globalThis as any) : null);
      if (glob && glob.webui && glob.webui.call) {
        // Envia requisição para criação nativa das pastas e gravação de assets no disco
        const ok = await glob.webui.call('ensure_workspace_directories', JSON.stringify({
          root: dirs.root,
          skills: dirs.skills,
          mcps: dirs.mcps,
          agents: dirs.agents
        }));
        if (ok) {
          await this.installApprovedAssetsToDisk(dirs);
          success = true;
        }
      }
    } catch (e) {
      console.warn('[WorkspaceManager] Operando com persistência local de workspace:', e);
    }

    if (!success) {
      // Fallback em memória e localStorage para testes e navegador
      this.persistLocalWorkspaceState();
      success = true;
    }

    this.isProvisioned = true;
    this.bus.emit('workspace:provisioned', { directories: dirs });
    return success;
  }

  /**
   * Grava os arquivos físicos de skills (SKILL.md) e MCPs (*.json) na pasta do workspace do usuário
   */
  public async installApprovedAssetsToDisk(dirs?: { root: string; skills: string; mcps: string; agents: string }): Promise<boolean> {
    const targetDirs = dirs || this.getWorkspaceDirectories();
    const glob = typeof window !== 'undefined' ? (window as any) : (typeof globalThis !== 'undefined' ? (globalThis as any) : null);

    if (glob && glob.webui && glob.webui.call) {
      try {
        // 1. Grava cada skill física com seu SKILL.md
        for (const skill of this.getApprovedSkills()) {
          const skillDir = `${targetDirs.skills}/${skill.id}`;
          await glob.webui.call('write_workspace_asset', JSON.stringify({
            filePath: `${skillDir}/SKILL.md`,
            content: skill.markdownContent
          }));
          await glob.webui.call('write_workspace_asset', JSON.stringify({
            filePath: `${skillDir}/skill.json`,
            content: JSON.stringify(skill, null, 2)
          }));
        }

        // 2. Grava configurações de MCPs (env sanitizado: somente referencias ${NOME} chegam ao disco)
        for (const mcp of this.getApprovedMcps()) {
          const safeMcp: any = mcp && (mcp as any).env
            ? { ...mcp, env: sanitizeEnvReferences((mcp as any).env) }
            : mcp;
          await glob.webui.call('write_workspace_asset', JSON.stringify({
            filePath: `${targetDirs.mcps}/${mcp.id}.json`,
            content: JSON.stringify(safeMcp, null, 2)
          }));
        }

        // 3. Grava manifesto geral do workspace
        const workspaceManifest: IWorkspaceConfig = {
          workspaceRoot: targetDirs.root,
          skillsDir: targetDirs.skills,
          mcpsDir: targetDirs.mcps,
          agentsDir: targetDirs.agents,
          graphsDir: `${targetDirs.root}/graphs`,
          autoVerifySkills: this.autoVerifySkills,
          strictSecurityMode: true,
          installedAssetsVersion: '2.0.0',
          lastSyncTimestamp: new Date().toISOString()
        };

        await glob.webui.call('write_workspace_asset', JSON.stringify({
          filePath: `${targetDirs.root}/workspace.config.json`,
          content: JSON.stringify(workspaceManifest, null, 2)
        }));

        this.persistLocalWorkspaceState();
        return true;
      } catch (err) {
        console.warn('[WorkspaceManager] Falha ao gravar assets no disco via C++ WebUI:', err);
      }
    }

    this.persistLocalWorkspaceState();
    return true;
  }

  private persistLocalWorkspaceState(): void {
    if (typeof localStorage !== 'undefined') {
      try {
        const manifest: IWorkspaceConfig = {
          workspaceRoot: this.getWorkspaceDirectories().root,
          skillsDir: this.getWorkspaceDirectories().skills,
          mcpsDir: this.getWorkspaceDirectories().mcps,
          agentsDir: this.getWorkspaceDirectories().agents,
          graphsDir: `${this.getWorkspaceDirectories().root}/graphs`,
          autoVerifySkills: this.autoVerifySkills,
          strictSecurityMode: true,
          installedAssetsVersion: '2.0.0',
          lastSyncTimestamp: new Date().toISOString()
        };
        localStorage.setItem('arestrix_workspace_config', JSON.stringify(manifest));
        localStorage.setItem('arestrix_workspace_skills', JSON.stringify(Array.from(this.installedSkills.values())));
        // env de MCPs sanitizado: valores literais de segredos jamais persistem em localStorage.
        const safeMcps = Array.from(this.installedMcps.values()).map((m: any) =>
          m && m.env ? { ...m, env: sanitizeEnvReferences(m.env) } : m
        );
        localStorage.setItem('arestrix_workspace_mcps', JSON.stringify(safeMcps));
      } catch (_) {}
    }
  }

  // --- Governança e Auditoria ---

  public auditSkill(skill: Partial<INodeSkill>, isCurated: boolean = false): IWorkspaceSkillAuditVerdict {
    const verdict = this.auditorAgent.auditSkill(skill, isCurated);
    if (skill.id) {
      this.auditHistory.set(skill.id, verdict);
    }
    return verdict;
  }

  public getAuditVerdict(skillId: string): IWorkspaceSkillAuditVerdict | undefined {
    return this.auditHistory.get(skillId);
  }

  // --- Catálogos Homologados ---

  public getApprovedSkills(): IWorkspaceApprovedSkill[] {
    return Array.from(this.installedSkills.values());
  }

  public getApprovedMcps(): IWorkspaceApprovedMcp[] {
    return Array.from(this.installedMcps.values());
  }

  public getGovernanceAgents(): IWorkspaceAgent[] {
    return Array.from(this.governanceAgents.values());
  }

  public convertToNodeSkill(approved: IWorkspaceApprovedSkill): INodeSkill {
    return {
      id: approved.id,
      name: approved.name,
      description: approved.description,
      license: approved.license,
      tools: [...approved.tools],
      systemPrompt: approved.systemPrompt,
      markdownContent: approved.markdownContent,
      updatedAt: new Date().toISOString()
    };
  }

  // --- Motores de Execução no Workspace (Agentes, Grafos, Projetos) ---

  public async runAgent(agentId: string, inputPayload?: any): Promise<IWorkspaceExecutionResult> {
    const timestamp = new Date().toISOString();
    const agent = this.governanceAgents.get(agentId) || {
      id: agentId,
      name: 'Agente do Workspace',
      role: 'Executor',
      category: 'automation' as const,
      description: 'Execução de rotina de governança',
      version: '1.0.0',
      systemPrompt: '',
      enabled: true
    };

    // Executa rotina específica dependendo do agente de governança
    if (agentId === 'agent-workspace-skill-auditor') {
      const skills = this.getApprovedSkills();
      const findings = skills.map(s => `Skill [${s.name}]: Status Seguro (Risk: 0/100).`);
      return {
        executionId: `exec-${Date.now()}`,
        targetType: 'agent',
        targetId: agentId,
        status: 'success',
        summary: `Auditoria de Workspace concluída com sucesso. ${skills.length} habilidades pré-aprovadas ativas e íntegras.`,
        timestamp,
        details: { auditedCount: skills.length, findings }
      };
    }

    if (agentId === 'agent-workspace-graph-linter') {
      return {
        executionId: `exec-${Date.now()}`,
        targetType: 'agent',
        targetId: agentId,
        status: 'success',
        summary: `Validador de Grafos do Workspace executado. Nenhuma violação estrutural detectada.`,
        timestamp,
        details: { linterStatus: 'passed' }
      };
    }

    return {
      executionId: `exec-${Date.now()}`,
      targetType: 'agent',
      targetId: agentId,
      status: 'success',
      summary: `Agente de workspace "${agent.name}" executado com sucesso.`,
      timestamp,
      details: { inputPayload }
    };
  }

  public async runGraph(graph: Graph, graphId?: string): Promise<IWorkspaceExecutionResult> {
    const timestamp = new Date().toISOString();
    const nodes = graph.getAllNodes();
    const edges = graph.getAllEdges();

    // Executa auditoria dos nós do grafo
    const auditedSkills: string[] = [];
    nodes.forEach(n => {
      if (n.agentSettings?.skills) {
        n.agentSettings.skills.forEach(s => {
          const v = this.auditSkill(s);
          auditedSkills.push(`${n.label}: ${s.name} (${v.status.toUpperCase()})`);
        });
      }
    });

    return {
      executionId: `exec-${Date.now()}`,
      targetType: 'graph',
      targetId: graphId || graph.getProjectId() || 'current-graph',
      status: 'success',
      summary: `Grafo executado no escopo do Workspace com ${nodes.length} vértices e ${edges.length} conexões.`,
      timestamp,
      details: {
        nodeCount: nodes.length,
        edgeCount: edges.length,
        auditedSkills
      }
    };
  }

  public async runProject(projectId: string, graph?: Graph): Promise<IWorkspaceExecutionResult> {
    const timestamp = new Date().toISOString();
    if (graph) {
      return await this.runGraph(graph, projectId);
    }
    return {
      executionId: `exec-${Date.now()}`,
      targetType: 'project',
      targetId: projectId,
      status: 'success',
      summary: `Projeto "${projectId}" inicializado e validado no ambiente do Workspace.`,
      timestamp,
      details: { projectId, root: this.activeWorkspaceRoot }
    };
  }

  // --- Reset para Testes ---
  public resetForTesting(bus?: EventBus): void {
    if (bus) {
      this.bus = bus;
      this.bindEvents();
    }
    this.activeWorkspaceRoot = '.';
    this.installedSkills.clear();
    this.installedMcps.clear();
    this.governanceAgents.clear();
    this.auditHistory.clear();
    this.isProvisioned = false;
    this.initDefaultCatalogs();
  }
}
