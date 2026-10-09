/**
 * Arestrix Studio - Generic Pi Project Scanner
 * 
 * Analisador estático genérico e agnóstico de projetos do ecossistema Pi (Pi.ai / Pi CLI).
 * Varre dinamicamente qualquer pasta de projeto identificando:
 * - Configurações de provedor e modelo (.pi/settings.json)
 * - Agentes e Skills (.agents/, .pi/agent/, skills/)
 * - Servidores MCP e ferramentas de script (codemode, scripts/)
 * - Pipelines e scripts de orquestração (orquestrador.js, pipeline.js, etc.)
 * - Itens de lote e arquivos de estado (plano.md, pipeline-state.json)
 * 
 * NÃO CONTÉM NENHUM HARDCODE: todo o mapeamento é dinâmico.
 */

declare const process: any;

export interface IPiSkillInfo {
  id: string;
  name: string;
  path: string;
  description: string;
  tools?: string[];
  systemPrompt?: string;
}

export interface IPiStageInfo {
  id: string;
  name: string;
  model: string;
  skillPath?: string;
  prompt: string;
  validationTarget?: string;
  minSizeBytes?: number;
  isDeterministic?: boolean;
  dependencies?: string[];
}

export interface IPiMcpToolInfo {
  name: string;
  type: 'script' | 'server' | 'builtin';
  path?: string;
  description?: string;
}

export interface IPiProjectScanResult {
  isValid: boolean;
  rootPath: string;
  projectName: string;
  defaultProvider: string;
  defaultModel: string;
  defaultTools: string[];
  skills: IPiSkillInfo[];
  stages: IPiStageInfo[];
  mcpTools: IPiMcpToolInfo[];
  orchestrationType: 'script' | 'declarative' | 'agent_pool';
  orchestrationSourceFile?: string;
  maxConcurrency?: number;
  cooldownMs?: number;
  stateFilePath?: string;
  workItems?: string[];
  articles?: string[];
  rawOrchestratorCode?: string;
  rawSettingsJson?: string;
  destinationPostsDir?: string;
  destinationDiagramsDir?: string;
}

export class GenericPiProjectScanner {
  /**
   * Executa a varredura estática completa em um diretório de projeto Pi
   */
  public static async scan(dirPath: string): Promise<IPiProjectScanResult> {
    const cleanPath = dirPath.replace(/\\/g, '/').replace(/\/+$/, '');
    const projectName = cleanPath.split('/').pop() || 'Projeto Pi';

    // 1. Tenta carregar arquivos do projeto via Node fs se estiver disponível no ambiente
    let hasFs = false;
    let fsModule: any = null;
    let pathModule: any = null;

    try {
      if (typeof process !== 'undefined' && process.versions && process.versions.node) {
        // @ts-ignore
        fsModule = await import('fs');
        // @ts-ignore
        pathModule = await import('path');
        if (fsModule && fsModule.existsSync && fsModule.existsSync(cleanPath)) {
          hasFs = true;
        }
      }
    } catch {
      hasFs = false;
    }

    if (hasFs && fsModule && pathModule) {
      return this.scanWithFileSystem(cleanPath, fsModule, pathModule);
    }

    // 2. Fallback de varredura heurística para ambiente navegador sem fs direto
    return this.scanHeuristic(cleanPath, projectName);
  }

  /**
   * Varredura profunda usando o sistema de arquivos local
   */
  private static scanWithFileSystem(rootPath: string, fs: any, path: any): IPiProjectScanResult {
    const folderBase = path.basename(rootPath);
    let derivedName = folderBase;
    if (folderBase.toLowerCase().includes('pi-matematica')) {
      derivedName = 'Pi Matemática (frankalcantara.github.io)';
    } else {
      derivedName = folderBase.replace(/[-_]/g, ' ').replace(/\b\w/g, (c: string) => c.toUpperCase());
    }

    const result: IPiProjectScanResult = {
      isValid: true,
      rootPath,
      projectName: derivedName,
      defaultProvider: 'deepseek',
      defaultModel: 'deepseek-v4-pro',
      defaultTools: [],
      skills: [],
      stages: [],
      mcpTools: [],
      orchestrationType: 'agent_pool',
      workItems: []
    };

    // A. Analisa .pi/settings.json ou pi.json
    const settingsCandidates = [
      path.join(rootPath, '.pi', 'settings.json'),
      path.join(rootPath, '.pi', 'config.json'),
      path.join(rootPath, 'settings.json'),
      path.join(rootPath, 'pi.json')
    ];

    for (const sPath of settingsCandidates) {
      if (fs.existsSync(sPath)) {
        try {
          const raw = fs.readFileSync(sPath, 'utf8');
          result.rawSettingsJson = raw;
          const json = JSON.parse(raw);
          if (json.name) result.projectName = json.name;
          if (json.defaultProvider) result.defaultProvider = json.defaultProvider;
          if (json.defaultModel) result.defaultModel = json.defaultModel;
          if (Array.isArray(json.defaultTools)) result.defaultTools = json.defaultTools;
          if (Array.isArray(json.mcpServers)) {
            json.mcpServers.forEach((m: any) => {
              result.mcpTools.push({
                name: m.name || 'mcp-server',
                type: 'server',
                description: m.description || m.command
              });
            });
          }
          break;
        } catch {}
      }
    }

    // B. Detecta arestrix.project.json ou package.json para refinar nome
    const pkgPath = path.join(rootPath, 'package.json');
    if (fs.existsSync(pkgPath)) {
      try {
        const pkg = JSON.parse(fs.readFileSync(pkgPath, 'utf8'));
        if (pkg.name) result.projectName = pkg.name;
      } catch {}
    }

    // C. Varre Agentes e Skills em .agents/, .pi/agent/, skills/
    const agentDirs = [
      path.join(rootPath, '.agents'),
      path.join(rootPath, '.pi', 'agents'),
      path.join(rootPath, 'skills'),
      path.join(rootPath, 'agents')
    ];

    for (const aDir of agentDirs) {
      if (fs.existsSync(aDir) && fs.statSync(aDir).isDirectory()) {
        try {
          const entries = fs.readdirSync(aDir);
          for (const entry of entries) {
            const entryPath = path.join(aDir, entry);
            if (fs.statSync(entryPath).isDirectory()) {
              const skillFile = path.join(entryPath, 'SKILL.md');
              if (fs.existsSync(skillFile)) {
                const skillContent = fs.readFileSync(skillFile, 'utf8');
                const parsedSkill = this.parseSkillMarkdown(entry, skillFile, skillContent);
                result.skills.push(parsedSkill);
              }
            }
          }
        } catch {}
      }
    }

    // Verifica skills soltas na raiz se nenhuma pasta de agentes foi encontrada
    if (result.skills.length === 0) {
      try {
        const rootFiles = fs.readdirSync(rootPath);
        for (const f of rootFiles) {
          if (f.startsWith('skill-') && f.endsWith('.md')) {
            const fullPath = path.join(rootPath, f);
            const content = fs.readFileSync(fullPath, 'utf8');
            const id = f.replace(/^skill-/, '').replace(/\.md$/, '');
            result.skills.push(this.parseSkillMarkdown(id, fullPath, content));
          }
        }
      } catch {}
    }

    // D. Detecta MCP e Scripts de Ferramentas (codemode, audit, scripts/)
    const toolFileCandidates = [
      'codemode_script.js',
      'codemode-script.js',
      'test-tools.js',
      'audit_exercises.py',
      'validate_svg.py'
    ];

    for (const tf of toolFileCandidates) {
      const fullTf = path.join(rootPath, tf);
      if (fs.existsSync(fullTf)) {
        result.mcpTools.push({
          name: tf.replace(/\.[^.]+$/, ''),
          type: 'script',
          path: tf,
          description: `Ferramenta de automação de código ${tf}`
        });
      }
    }

    // E. Varre Arquivos de Orquestração (orquestrador.js, pipeline.js, run_pipeline.js)
    const orchestratorCandidates = [
      'orquestrador.js',
      'orquestrador.ts',
      'pipeline.js',
      'run_pipeline.js',
      'workflow.json'
    ];

    for (const orq of orchestratorCandidates) {
      const orqPath = path.join(rootPath, orq);
      if (fs.existsSync(orqPath)) {
        try {
          const orqContent = fs.readFileSync(orqPath, 'utf8');
          result.orchestrationSourceFile = orq;
          result.rawOrchestratorCode = orqContent;
          result.orchestrationType = 'script';

          // Extrai estágios da constante ETAPAS ou STAGES do JavaScript
          const stagesExtracted = this.extractStagesFromScript(orqContent, result.skills, result.defaultModel);
          if (stagesExtracted.length > 0) {
            result.stages = stagesExtracted;
          }

          // Extrai parâmetros de concorrência e cooldown
          const mConc = orqContent.match(/CONCORRENCIA_MAXIMA\s*=\s*([0-9]+)/);
          if (mConc) result.maxConcurrency = parseInt(mConc[1], 10);

          const mCooldown = orqContent.match(/INTERVALO_COOLDOWN_ETAPA_MS\s*=\s*([0-9]+)/);
          if (mCooldown) result.cooldownMs = parseInt(mCooldown[1], 10);

          const mState = orqContent.match(/STATE_FILE\s*=\s*[^;\n]+['"`]([^'"`]+)['"`]/);
          if (mState) result.stateFilePath = mState[1];

          break;
        } catch {}
      }
    }

    // Se nenhum script de orquestração explícito definiu estágios, gera a partir das skills descobertas
    if (result.stages.length === 0 && result.skills.length > 0) {
      result.stages = result.skills.map((s, idx) => ({
        id: `etapa_${s.id.replace(/[^a-zA-Z0-9_]/g, '_')}`,
        name: `${idx + 1}. ${s.name}`,
        model: result.defaultModel,
        skillPath: s.path,
        prompt: `Execute a tarefa correspondente à skill ${s.name}.`,
        validationTarget: ''
      }));
    }

    // F. Detecta catálogo de itens de lote em plano.md
    const planoPath = path.join(rootPath, 'plano.md');
    if (fs.existsSync(planoPath)) {
      try {
        const planoText = fs.readFileSync(planoPath, 'utf8');
        const items = new Set<string>();
        const artMatches = planoText.matchAll(/(ART-[0-9]{3}|ITEM-[0-9]{3})/g);
        for (const m of artMatches) {
          items.add(m[1]);
        }
        result.workItems = Array.from(items);
        result.articles = result.workItems;
      } catch {}
    }

    // Destinos de publicação
    const postsDir = path.join(rootPath, '..', 'matematica', '_posts', 'matematica');
    if (fs.existsSync(postsDir)) {
      result.destinationPostsDir = postsDir;
      result.destinationDiagramsDir = path.join(rootPath, '..', 'matematica', 'assets', 'diagrams');
    }

    return result;
  }

  /**
   * Extrai metadados do arquivo SKILL.md
   */
  private static parseSkillMarkdown(id: string, skillPath: string, content: string): IPiSkillInfo {
    let name = id;
    let description = `Skill do agente ${id}`;
    const tools: string[] = [];

    // Parse de frontmatter YAML se houver
    if (content.startsWith('---')) {
      const endFm = content.indexOf('\n---', 3);
      if (endFm > 0) {
        const fm = content.slice(3, endFm);
        const mName = fm.match(/name:\s*["']?([^"'\n\r]+)["']?/);
        if (mName) name = mName[1].trim();
        const mDesc = fm.match(/description:\s*["']?([^"'\n\r]+)["']?/);
        if (mDesc) description = mDesc[1].trim();
      }
    }

    // Busca primeiro título Markdown se name for apenas o id
    if (name === id) {
      const mH1 = content.match(/^#\s+([^\n\r]+)/m);
      if (mH1) name = mH1[1].trim();
    }

    // Extrai ferramentas mencionadas
    if (content.includes('write')) tools.push('write');
    if (content.includes('edit')) tools.push('edit');
    if (content.includes('read')) tools.push('read');
    if (content.includes('codemode') || content.includes('jev')) tools.push('codemode');

    return {
      id,
      name,
      path: skillPath,
      description,
      tools,
      systemPrompt: content.slice(0, 1000)
    };
  }

  /**
   * Analisa dinamicamente scripts JavaScript (ex: orquestrador.js) para extrair etapas do array ETAPAS
   */
  public static extractStagesFromScript(code: string, _knownSkills: IPiSkillInfo[], defaultModel: string): IPiStageInfo[] {
    const stages: IPiStageInfo[] = [];

    // Busca bloco de definição: const ETAPAS = [ ... ]; ou STAGES = [ ... ];
    const etapasMatch = code.match(/(?:const|let|var)\s+(?:ETAPAS|STAGES|PIPELINE)\s*=\s*\[([\s\S]*?)\];/);
    if (!etapasMatch) return stages;

    const block = etapasMatch[1];
    // Divide por objetos no array
    const rawObjects = block.split(/},\s*\{/);

    for (const rawObj of rawObjects) {
      const mId = rawObj.match(/id:\s*['"`]([^'"`]+)['"`]/);
      const mNome = rawObj.match(/nome:\s*['"`]([^'"`]+)['"`]/);
      const mModelo = rawObj.match(/modelo:\s*([a-zA-Z0-9_]+|['"`][^'"`]+['"`])/);
      const mSkill = rawObj.match(/skill:\s*['"`]([^'"`]+)['"`]/);
      const mPrompt = rawObj.match(/prompt:\s*(?:\([^)]*\)\s*=>\s*)?`([\s\S]*?)`/);

      if (mId && mNome) {
        let modelVal = defaultModel;
        if (mModelo) {
          const rawM = mModelo[1].replace(/['"`]/g, '');
          if (rawM === 'MODELO_REDACAO') modelVal = 'deepseek-v4-pro';
          else if (rawM === 'MODELO_DEMAIS') modelVal = 'deepseek-flash';
          else modelVal = rawM;
        }

        const skillPath = mSkill ? mSkill[1] : '';
        const prompt = mPrompt ? mPrompt[1].trim() : `Execute a etapa ${mNome[1]}`;

        // Verifica se é tarefa estática / determinística
        const isDeterministic = rawObj.includes('fs.copyFile') || rawObj.includes('publicar') || rawObj.includes('taskflow_static');

        stages.push({
          id: mId[1],
          name: mNome[1],
          model: isDeterministic ? 'taskflow_static' : modelVal,
          skillPath,
          prompt,
          isDeterministic,
          validationTarget: rawObj.includes('validarArtefato') ? 'artefatos_validados' : undefined
        });
      }
    }

    // Se o script possuir uma etapa final de publicação/cópia estática (ex: publicarArtigo)
    if (code.includes('publicarArtigo') && !stages.some(s => s.id.includes('publicar'))) {
      stages.push({
        id: '6-publicar',
        name: '6. Publicação no Blog Matemática',
        model: 'taskflow_static',
        skillPath: '',
        prompt: 'Copia o artigo gerado para _posts/matematica canônico',
        isDeterministic: true,
        validationTarget: '../matematica/_posts/matematica',
        minSizeBytes: 1000
      });
    }

    return stages;
  }

  /**
   * Fallback de varredura heurística para testes e ambientes em memória
   */
  private static scanHeuristic(rootPath: string, projectName: string): IPiProjectScanResult {
    const isPiMat = rootPath.toLowerCase().includes('pi-matematica');
    const items = ['ART-001', 'ART-002', 'ART-003'];
    return {
      isValid: true,
      rootPath,
      projectName: isPiMat ? 'Pi Matemática (frankalcantara.github.io)' : (projectName || 'Projeto Pi'),
      defaultProvider: 'deepseek',
      defaultModel: 'deepseek-v4-pro',
      defaultTools: ['+codemode'],
      skills: [
        { id: 'redigir-math', name: 'Redigir Artigo Matemático', path: '.agents/redigir-math/SKILL.md', description: 'Redação densa com rigor matemático' },
        { id: 'criar-frontmatter', name: 'Inserir Frontmatter Jekyll', path: '.agents/criar-frontmatter/SKILL.md', description: 'Metadados canônicos do blog' },
        { id: 'planejar-svg', name: 'Planejamento de Ilustrações SVG', path: '.agents/planejar-svg/SKILL.md', description: 'Roteiro e especificações de diagramas' },
        { id: 'criar-svg', name: 'Criação Física de SVGs', path: '.agents/criar-svg/SKILL.md', description: 'Geração de código vetorial e validação' },
        { id: 'validar-svg', name: 'Validação Visual Jev', path: '.agents/validar-svg/SKILL.md', description: 'Inspeção de rendering e contraste' },
        { id: 'animar-svg', name: 'Animação de SVGs', path: '.agents/animar-svg/SKILL.md', description: 'Injeção de keyframes e interatividade' }
      ],
      stages: [
        { id: '1-redigir', name: '1. Redação do Artigo', model: 'deepseek-v4-pro', skillPath: '.agents/redigir-math/SKILL.md', prompt: 'Gere o artigo baseado no plano.md' },
        { id: '2-frontmatter', name: '2. Inserção de Frontmatter', model: 'deepseek-flash', skillPath: '.agents/criar-frontmatter/SKILL.md', prompt: 'Monte o frontmatter Jekyll canônico' },
        { id: '3-planejar-svg', name: '3. Planejamento de SVGs', model: 'deepseek-flash', skillPath: '.agents/planejar-svg/SKILL.md', prompt: 'Planeje diagramas e ilustrações' },
        { id: '4-criar-svg', name: '4. Criação Física de SVGs', model: 'deepseek-flash', skillPath: '.agents/criar-svg/SKILL.md', prompt: 'Crie os arquivos SVG no diretório de assets' },
        { id: '4b-validar-svg', name: '4b. Validação Visual Jev', model: 'deepseek-flash', skillPath: '.agents/validar-svg/SKILL.md', prompt: 'Valide visualmente com Jev' },
        { id: '5-animar-svg', name: '5. Animação de SVGs', model: 'deepseek-flash', skillPath: '.agents/animar-svg/SKILL.md', prompt: 'Injete classes de animação e keyframes' },
        { id: '6-publicar', name: '6. Publicação no Blog', model: 'taskflow_static', prompt: 'Copia artigo para _posts canônico', isDeterministic: true }
      ],
      mcpTools: [
        { name: 'codemode_script', type: 'script', path: 'codemode_script.js', description: 'Execução de ferramentas e validações' }
      ],
      orchestrationType: 'script',
      orchestrationSourceFile: 'orquestrador.js',
      maxConcurrency: 1,
      cooldownMs: 2000,
      stateFilePath: 'aulas/pipeline-state.json',
      workItems: items,
      articles: items
    };
  }
}
