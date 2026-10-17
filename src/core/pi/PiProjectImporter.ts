import { Graph } from '../Graph.ts';
import { ProjectManager } from '../ProjectManager.ts';
import { EventBus, globalEventBus } from '../EventBus.ts';
import { INodeAgentSettings, IArtifactValidationRule, INodeData } from '../types.ts';
import { 
  GenericPiProjectScanner, 
  IPiProjectScanResult, 
  IPiStageInfo, 
  IPiSkillInfo, 
  IPiMcpToolInfo 
} from './GenericPiProjectScanner.ts';
import { 
  DefaultIntelligenceService, 
  IImportAIValidationResult, 
  IAIValidatedStage 
} from '../ai/DefaultIntelligenceService.ts';

export type { IPiProjectScanResult, IPiStageInfo, IPiSkillInfo, IPiMcpToolInfo };

export class PiProjectImporter {
  private static instance: PiProjectImporter;
  private intelligenceService: DefaultIntelligenceService;

  private constructor() {
    this.intelligenceService = DefaultIntelligenceService.getInstance();
  }

  public static getInstance(): PiProjectImporter {
    if (!PiProjectImporter.instance) {
      PiProjectImporter.instance = new PiProjectImporter();
    }
    return PiProjectImporter.instance;
  }

  /**
   * Obtém lista de caminhos de projetos Pi conhecidos no disco
   */
  public async getKnownProjects(): Promise<string[]> {
    try {
      if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
        const raw = await (window as any).webui.call('get_known_pi_projects');
        if (raw) {
          const list = JSON.parse(raw);
          if (Array.isArray(list) && list.length > 0) return list;
        }
      }
    } catch (e) {
      console.warn('[PiProjectImporter] Falha ao consultar projetos conhecidos via WebUI:', e);
    }

    // Nenhum candidato hardcoded
    return [];
  }

  /**
   * Varre e analisa estaticamente o diretório de QUALQUER projeto Pi de forma genérica
   */
  public async scanProject(dirPath: string): Promise<IPiProjectScanResult> {
    try {
      if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
        const raw = await (window as any).webui.call('scan_pi_project', dirPath);
        if (raw) {
          const res = JSON.parse(raw) as IPiProjectScanResult;
          if (res && res.isValid && res.stages && res.stages.length > 0) {
            return res;
          }
        }
      }
    } catch (e) {
      console.warn('[PiProjectImporter] WebUI scanner indisponível, usando GenericPiProjectScanner:', e);
    }

    return GenericPiProjectScanner.scan(dirPath);
  }

  /**
   * Executa a validação e refinamento semântico por IA usando o Modelo Padrão do Arestrix
   */
  public async validateWithDefaultIntelligence(scan: IPiProjectScanResult): Promise<IImportAIValidationResult> {
    return this.intelligenceService.analyzeAndValidatePiImport(scan);
  }

  /**
   * Importa o projeto Pi analisado e validado, sintetizando dinamicamente o grafo no Arestrix
   */
  public async importProject(
    scan: IPiProjectScanResult,
    graph: Graph,
    projectManager: ProjectManager = ProjectManager.getInstance(globalEventBus),
    bus?: EventBus,
    aiValidation?: IImportAIValidationResult
  ): Promise<boolean> {
    if (!scan || !scan.isValid) return false;
    const activeBus = bus || (graph as any).bus || globalEventBus;

    // 1. Obtém a validação da IA (ou executa agora se não foi passada previamente)
    const validation = aiValidation || await this.validateWithDefaultIntelligence(scan);
    const stagesToCreate = validation.verifiedStages && validation.verifiedStages.length > 0
      ? validation.verifiedStages
      : this.convertScanStagesToValidated(scan.stages, scan.defaultModel);

    // 2. Cria o manifesto do projeto dinâmico no ProjectManager
    const projectId = scan.projectName
      .toLowerCase()
      .normalize('NFD')
      .replace(/[\u0300-\u036f]/g, '')
      .replace(/[^a-z0-9_-]/g, '-')
      .replace(/-+/g, '-')
      .replace(/^-|-$/g, '') || 'projeto-pi';
    await projectManager.createProject({
      projectId,
      name: scan.projectName,
      version: '1.0.0',
      description: validation.summary || `Pipeline de Agentes importado do projeto Pi "${scan.projectName}"`,
      rootPath: scan.rootPath,
      pipelines: [
        {
          id: 'main',
          name: 'Pipeline Principal Pi',
          file: 'pipelines/main.json',
          isPrimary: true,
          description: `Pipeline com ${stagesToCreate.length} etapas gerado pelo Arestrix Studio`
        }
      ],
      database: {
        type: 'sqlite',
        filePath: 'storage/database.sqlite'
      },
      environment: {
        PI_PROVIDER: scan.defaultProvider || 'deepseek',
        PI_DEFAULT_MODEL: scan.defaultModel || 'deepseek-v4-pro',
        DEST_POSTS_DIR: scan.destinationPostsDir || '',
        DEST_DIAGRAMS_DIR: scan.destinationDiagramsDir || ''
      }
    }, graph);

    // 3. Limpa o grafo atual para injetar o novo pipeline dinâmico
    graph.clear();

    // 4. Monta os nós dinamicamente distribuídos no Canvas com layout 2D não-linear
    const createdNodes: Map<string, INodeData> = new Map();

    // Identifica ramos concorrentes para cálculo de coordenadas 2D
    const contentStagesList = stagesToCreate.filter(s => 
      s.nodeType === 'agent' && (s.id.includes('redigir') || s.id.includes('frontmatter') || s.id.includes('conteudo') || s.id.includes('texto') || s.id.startsWith('1-') || s.id.startsWith('2-'))
    );
    const visualStagesList = stagesToCreate.filter(s => 
      s.nodeType === 'agent' && (s.id.includes('svg') || s.id.includes('diagram') || s.id.includes('imagem') || s.id.includes('visual') || s.id.includes('animar') || s.id.startsWith('3-') || s.id.startsWith('4') || s.id.startsWith('5-'))
    );
    const hasParallelBranches = contentStagesList.length > 0 && visualStagesList.length > 0;

    let contentIdx = 0;
    let visualIdx = 0;

    stagesToCreate.forEach((st, idx) => {
      let x = 0;
      let y = 50;

      if (st.nodeType === 'fork') {
        x = -450;
        y = 50;
      } else if (st.nodeType === 'join') {
        x = 900;
        y = 50;
      } else if (hasParallelBranches && (st.id.includes('redigir') || contentStagesList.some(c => c.id === st.id))) {
        // Ramo A: Redação / Conteúdo (Posicionado acima, y = 200)
        x = -150 + (contentIdx++) * 300;
        y = 200;
      } else if (hasParallelBranches && (st.id.includes('svg') || visualStagesList.some(v => v.id === st.id))) {
        // Ramo B: Pipeline de Diagramas e SVGs (Posicionado abaixo, y = -100)
        x = -250 + (visualIdx++) * 280;
        y = -100;
      } else if (st.isDeterministic && (st.id.includes('publicar') || st.id.includes('exportar'))) {
        x = 1200;
        y = 50;
      } else {
        // Distribuição balanceada horizontal padrão
        const total = stagesToCreate.length;
        x = Math.round((idx - (total - 1) / 2) * 260);
        y = (idx % 2 === 0) ? 90 : 10;
      }

      // Normaliza o ID para permitir compatibilidade canônica ('1-redigir' -> 'node_pi_redigir')
      const cleanId = st.id.replace(/^[0-9]+[a-z]?-/, '').replace(/[^a-zA-Z0-9_]/g, '_');
      const nodeId = `node_pi_${cleanId}`;
      const nodeType = st.nodeType || (st.isDeterministic ? 'static' : 'agent');

      const node = graph.createNode(nodeId, st.label, nodeType, x, y);

      if (nodeType === 'agent') {
        // Ingestão completa das habilidades (skills) físicas do agente
        const matchingSkill = scan.skills.find(s => 
          (st.skillPath && s.path === st.skillPath) ||
          s.id === st.id ||
          st.id.includes(s.id) ||
          s.id.includes(st.id) ||
          st.label.toLowerCase().includes(s.name.toLowerCase())
        );

        let nodeSkills: any[] = [];
        let activeSkillId: string | undefined = undefined;

        if (matchingSkill) {
          const nodeSkill = {
            id: matchingSkill.id,
            name: matchingSkill.name,
            description: matchingSkill.description,
            markdownContent: matchingSkill.markdownContent || `# ${matchingSkill.name}\n\n${matchingSkill.description}`,
            systemPrompt: matchingSkill.systemPrompt || matchingSkill.description,
            tools: matchingSkill.tools || ['write', 'edit', 'read'],
            license: 'MIT',
            sourceUrl: matchingSkill.path,
            updatedAt: new Date().toISOString()
          };
          nodeSkills = [nodeSkill];
          activeSkillId = nodeSkill.id;
        } else {
          // Cria skill padrão formatada baseada no papel/prompt do agente
          const defaultSkill = {
            id: `skill-${cleanId}`,
            name: st.label,
            description: st.role || `Habilidade especializada para ${st.label}`,
            markdownContent: `# ${st.label}\n\n${st.promptTemplate || 'Instruções operacionais do agente'}\n\n## Regras de Execução\n- Execute as tarefas com rigor e precisão.\n- Valide os artefatos gerados.`,
            systemPrompt: st.promptTemplate || `Você é o agente responsável por ${st.label}.`,
            tools: ['write', 'edit', 'read'],
            license: 'MIT',
            updatedAt: new Date().toISOString()
          };
          nodeSkills = [defaultSkill];
          activeSkillId = defaultSkill.id;
        }

        node.agentSettings = this.createAgentSettings(
          st.model || scan.defaultModel || 'deepseek-v4-pro',
          st.skillPath || '',
          st.promptTemplate,
          st.validationRules || [],
          idx === 0, // Primeira etapa serializada por padrão
          nodeSkills,
          activeSkillId
        );
        graph.updateNode(node.id, { agentSettings: node.agentSettings });
      } else if (nodeType === 'static') {
        node.parameters = {
          action: st.id,
          description: st.promptTemplate,
          destinationDir: scan.destinationPostsDir || 'outputs/posts'
        };
        graph.updateNode(node.id, { parameters: node.parameters });
      }

      createdNodes.set(st.id, node);
      createdNodes.set(nodeId, node);
      createdNodes.set(`node_${st.id.replace(/[^a-zA-Z0-9_]/g, '_')}`, node);
    });

    // 5. Conecta as arestas verificadas (ou sequenciais se não especificadas)
    if (validation.verifiedEdges && validation.verifiedEdges.length > 0) {
      validation.verifiedEdges.forEach(edge => {
        const srcNode = createdNodes.get(edge.fromNodeId) || createdNodes.get(`node_${edge.fromNodeId}`);
        const tgtNode = createdNodes.get(edge.toNodeId) || createdNodes.get(`node_${edge.toNodeId}`);

        if (srcNode && tgtNode && srcNode.outputs.length > 0 && tgtNode.inputs.length > 0) {
          graph.createEdge(srcNode.id, srcNode.outputs[0].id, tgtNode.id, tgtNode.inputs[0].id);
        }
      });
    } else {
      // Conexão sequencial de fallback
      const nodesList = Array.from(new Set(createdNodes.values()));
      for (let i = 0; i < nodesList.length - 1; i++) {
        const src = nodesList[i];
        const tgt = nodesList[i + 1];
        if (src.outputs.length > 0 && tgt.inputs.length > 0) {
          graph.createEdge(src.id, src.outputs[0].id, tgt.id, tgt.inputs[0].id);
        }
      }
    }

    // 5.1 Distribui os vértices automaticamente em 2D (Sugiyama) caso estejam em linha reta ou sobrepostos
    graph.autoLayout(false);

    // 6. Configura e emite os parâmetros para o Batch Engine
    const workItems = (validation.batchSettings?.workItems && validation.batchSettings.workItems.length > 0)
      ? validation.batchSettings.workItems
      : (scan.workItems && scan.workItems.length > 0 ? scan.workItems : ['ITEM-001', 'ITEM-002']);

    activeBus.emit('batch:configure', {
      items: workItems,
      serializedStages: [stagesToCreate[0]?.id || 'etapa_1'],
      maxConcurrency: validation.batchSettings?.suggestedConcurrency || scan.maxConcurrency || 2,
      cooldownMs: validation.batchSettings?.cooldownMs || scan.cooldownMs || 2000,
      stateFilePath: validation.batchSettings?.stateFile || scan.stateFilePath || 'pipeline-state.json',
      autoResume: true
    });

    activeBus.emit('project:changed', projectManager.getCurrentProject());
    return true;
  }

  private convertScanStagesToValidated(stages: IPiStageInfo[], defaultModel: string): IAIValidatedStage[] {
    return stages.map(st => ({
      id: st.id,
      label: st.name,
      nodeType: st.isDeterministic ? 'static' : 'agent',
      role: `Agente para ${st.name}`,
      model: st.model || defaultModel,
      skillPath: st.skillPath,
      promptTemplate: st.prompt,
      validationRules: st.validationTarget ? [{
        targetPathPattern: st.validationTarget,
        minSizeBytes: st.minSizeBytes || 1000
      }] : undefined,
      isDeterministic: st.isDeterministic
    }));
  }

  private createAgentSettings(
    model: string,
    skillPath: string,
    prompt: string,
    validationRules: IArtifactValidationRule[] = [],
    isSerialAcrossBatch: boolean = false,
    skills: any[] = [],
    activeSkillId?: string
  ): INodeAgentSettings {
    return {
      inheritSandbox: false,
      sandbox: {
        isolationLevel: 'container',
        network: 'egress_llm_only',
        maxMemoryMB: 2048,
        timeoutSeconds: 3600,
        readOnlyMounts: ['.agents/'],
        readWriteMounts: ['output/']
      },
      inheritModel: false,
      model: {
        provider: 'deepseek',
        model,
        skillPath,
        promptTemplate: prompt,
        temperature: 0.2
      },
      inheritCodeMode: false,
      codeMode: {
        allowedTools: ['write', 'edit', 'read'],
        customScripts: []
      },
      inheritMemory: false,
      memory: {
        enabled: true,
        persistent: true,
        domains: ['pipeline', 'agents']
      },
      skills,
      activeSkillId,
      validationRules,
      isSerialAcrossBatch
    };
  }

  /**
   * Cria resultado de fallback seguro em caso de ambiente restrito
   */
  public createFallbackScanResult(dirPath: string): IPiProjectScanResult {
    return GenericPiProjectScanner.scan(dirPath) as any;
  }
}
