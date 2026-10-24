import { EventBus, globalEventBus } from './EventBus.ts';
import {
  IProjectManifest,
  IRecentProjectItem,
  IArestrixBundle,
  IGraphSchema,
  IOpenableListing,
  IFilePickFilter,
  IOpenOptions,
  IOpenResult
} from './types.ts';
import { Graph } from './Graph.ts';
import { hashProjectName } from './hash.ts';
import { baseName, dirName, joinPath, stripExtension } from './paths.ts';

const PROJECT_MANIFEST_NAMES = ['arestrix.project.json', 'taskforge.project.json'];

export type OpenedFileContent =
  | { type: 'package'; bundle: IArestrixBundle }
  | { type: 'manifest' }
  | { type: 'graph'; schema: IGraphSchema }
  | { type: 'unknown' };

// Identifica pelo conteudo o que um arquivo escolhido na tela Abrir contem (pacote gzip ou JSON).
export async function classifyOpenedFile(bytes: Uint8Array): Promise<OpenedFileContent> {
  try {
    const isGzip = bytes.length > 2 && bytes[0] === 0x1f && bytes[1] === 0x8b;
    const parsed = JSON.parse(isGzip ? await decompressGzip(bytes) : new TextDecoder().decode(bytes));
    if (parsed && parsed.format === 'arestrix-package' && parsed.manifest) return { type: 'package', bundle: parsed };
    if (parsed && typeof parsed.projectId === 'string' && Array.isArray(parsed.pipelines)) return { type: 'manifest' };
    if (parsed && Array.isArray(parsed.nodes)) return { type: 'graph', schema: parsed };
  } catch (_) {}
  return { type: 'unknown' };
}

// Utilitários de Compressão e Base64 para Pacotes .arestrix
export async function compressGzip(data: string | Uint8Array): Promise<Uint8Array> {
  const bytes = typeof data === 'string' ? new TextEncoder().encode(data) : data;
  if (typeof CompressionStream !== 'undefined') {
    const cs = new CompressionStream('gzip');
    const writer = cs.writable.getWriter();
    writer.write(bytes as any);
    writer.close();
    const reader = cs.readable.getReader();
    const chunks: Uint8Array[] = [];
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      if (value) chunks.push(value);
    }
    const totalLength = chunks.reduce((acc, c) => acc + c.length, 0);
    const result = new Uint8Array(totalLength);
    let offset = 0;
    for (const chunk of chunks) {
      result.set(chunk, offset);
      offset += chunk.length;
    }
    return result;
  }
  return bytes;
}

export async function decompressGzip(data: Uint8Array): Promise<string> {
  if (typeof DecompressionStream !== 'undefined') {
    const ds = new DecompressionStream('gzip');
    const writer = ds.writable.getWriter();
    writer.write(data as any);
    writer.close();
    const reader = ds.readable.getReader();
    const chunks: Uint8Array[] = [];
    while (true) {
      const { done, value } = await reader.read();
      if (done) break;
      if (value) chunks.push(value);
    }
    const totalLength = chunks.reduce((acc, c) => acc + c.length, 0);
    const result = new Uint8Array(totalLength);
    let offset = 0;
    for (const chunk of chunks) {
      result.set(chunk, offset);
      offset += chunk.length;
    }
    return new TextDecoder().decode(result);
  }
  return new TextDecoder().decode(data);
}

export function uint8ArrayToBase64(bytes: Uint8Array): string {
  const nodeBuf = (globalThis as any).Buffer;
  if (nodeBuf && typeof nodeBuf.from === 'function') {
    return nodeBuf.from(bytes).toString('base64');
  }
  let binary = '';
  const len = bytes.byteLength;
  for (let i = 0; i < len; i++) {
    binary += String.fromCharCode(bytes[i]);
  }
  return btoa(binary);
}

export function base64ToUint8Array(base64: string): Uint8Array {
  const nodeBuf = (globalThis as any).Buffer;
  if (nodeBuf && typeof nodeBuf.from === 'function') {
    return new Uint8Array(nodeBuf.from(base64, 'base64'));
  }
  const binary = atob(base64);
  const len = binary.length;
  const bytes = new Uint8Array(len);
  for (let i = 0; i < len; i++) {
    bytes[i] = binary.charCodeAt(i);
  }
  return bytes;
}

export class ProjectManager {
  private static instance: ProjectManager;
  private bus: EventBus;
  private currentProject: IProjectManifest;
  private recentProjects: IRecentProjectItem[] = [];
  private pipelineStorage: Map<string, string> = new Map();
  private isInitialized = false;
  private initPromise: Promise<void> | null = null;

  private constructor(bus: EventBus = globalEventBus) {
    this.bus = bus;
    this.currentProject = this.createDefaultManifest();
    this.loadFromLocalStorageSync();
  }

  private loadFromLocalStorageSync(): void {
    if (typeof localStorage !== 'undefined') {
      try {
        const stored = localStorage.getItem('arestrix_recent_projects');
        if (stored) {
          const parsed = JSON.parse(stored);
          if (Array.isArray(parsed) && parsed.length > 0) {
            this.recentProjects = parsed;
          }
        }
      } catch (_) {}
    }
  }

  public static getInstance(bus: EventBus = globalEventBus): ProjectManager {
    if (!ProjectManager.instance) {
      ProjectManager.instance = new ProjectManager(bus);
    }
    return ProjectManager.instance;
  }

  public static resetInstance(): void {
    ProjectManager.instance = undefined as any;
  }

  public createDefaultManifest(): IProjectManifest {
    const now = new Date().toISOString();
    return {
      projectId: 'default-workspace',
      name: 'Workspace Padrão',
      version: '1.0.0',
      description: 'Workspace nativo inicial do Arestrix Studio',
      createdAt: now,
      updatedAt: now,
      rootPath: '.',
      outputDir: 'Output',
      pipelines: [
        {
          id: 'main',
          name: 'Pipeline Principal',
          file: 'pipelines/main.json',
          isPrimary: true,
          description: 'Pipeline central do projeto'
        }
      ],
      database: {
        type: 'sqlite',
        filePath: 'storage/database.sqlite'
      },
      environment: {},
      git: {
        enabled: true,
        provider: 'github',
        serverUrl: 'https://github.com',
        username: '',
        repositoryName: '',
        remoteUrl: '',
        isPrivate: true,
        autoCommitOnRun: true,
        autoCommitOnShutdown: true,
        timedPushEnabled: true,
        timedPushIntervalMinutes: 10
      },
      runner: {
        enabled: true,
        title: 'Workspace Padrão - Runner',
        description: 'Executável do pipeline',
        version: '1.0.0',
        outputDir: 'dist/runner',
        outputName: 'pipeline_runner.exe',
        autoStart: false,
        defaultTab: 'progress',
        allowRestart: true,
        parameters: [],
        signature: {
          enabled: false,
          algorithm: 'hmac-sha256',
          keyId: 'corp-signing-key-1',
          requiredForExecution: false,
          issuer: 'Arestrix Enterprise Security'
        },
        vault: {
          provider: 'windows-credential-vault',
          azureKeyVaultUrl: '',
          azureAuthMethod: 'managed-identity',
          secretPrefix: 'Arestrix_',
          fallbackToEnvironment: true,
          autoPurgeMemory: true
        },
        packaging: {
          format: 'standalone-folder',
          appId: 'Arestrix.Company.PipelineRunner',
          publisher: 'CN=Corporate, O=Enterprise, C=BR',
          silentInstallArgs: '/qn ALLUSERS=1',
          createDesktopShortcut: true,
          createStartMenuShortcut: true
        }
      }
    };
  }

  public getCurrentProject(): IProjectManifest {
    return { ...this.currentProject };
  }

  public getProjectOutputDir(): string {
    return this.currentProject?.outputDir?.trim() || 'Output';
  }

  private getGlobalScope(): any {
    if (typeof window !== 'undefined' && (window as any).webui) return window;
    if (typeof globalThis !== 'undefined' && (globalThis as any).webui) return globalThis;
    if (typeof window !== 'undefined') return window;
    if (typeof globalThis !== 'undefined') return globalThis;
    return null;
  }

  public isWebUIEnvironment(): boolean {
    const glob = this.getGlobalScope();
    if (!glob) return false;
    const hasWebuiGlobal = Boolean(glob.webui);
    const hasScriptTag = typeof document !== 'undefined' && Boolean(document.querySelector?.('script[src*="webui"]'));
    return hasWebuiGlobal || hasScriptTag;
  }

  public async init(forceRefresh = false): Promise<void> {
    if (this.initPromise) {
      return this.initPromise;
    }
    if (this.isInitialized && !forceRefresh) {
      return;
    }

    this.initPromise = this.performInit();
    try {
      await this.initPromise;
    } finally {
      this.initPromise = null;
    }
  }

  private async performInit(): Promise<void> {
    if (!this.isWebUIEnvironment()) {
      if (this.recentProjects.length === 0 && typeof localStorage !== 'undefined') {
        this.loadFromLocalStorageSync();
      }
      this.isInitialized = true;
      return;
    }

    let loadedFromWebUI = false;
    const maxAttempts = 30; // 30 * 50ms = até 1.5s aguardando handshake inicial do WebUI
    const intervalMs = 50;
    const glob = this.getGlobalScope();

    for (let attempt = 0; attempt < maxAttempts; attempt++) {
      const webui = glob ? glob.webui : null;
      if (webui && typeof webui.call === 'function') {
        try {
          const rawRecents = await webui.call('list_recent_projects');
          if (rawRecents !== undefined) {
            try {
              const parsed = typeof rawRecents === 'string' ? JSON.parse(rawRecents) : rawRecents;
              if (Array.isArray(parsed) && parsed.length > 0) {
                this.recentProjects = parsed;
              }
            } catch (err) {
              console.warn('[ProjectManager] Erro ao parsear lista de projetos recentes:', err);
            }

            try {
              const rawProj = await webui.call('get_current_project');
              if (rawProj) {
                const parsedProj = typeof rawProj === 'string' ? JSON.parse(rawProj) : rawProj;
                if (parsedProj && parsedProj.projectId) {
                  this.currentProject = parsedProj;
                }
              }
            } catch (err) {
              console.warn('[ProjectManager] Erro ao obter projeto atual via WebUI:', err);
            }

            loadedFromWebUI = true;
            break;
          }
        } catch (_) {
          // Erro esperado enquanto handshake WebSocket ainda está conectando ("WebSocket is not connected")
        }
      }
      await new Promise(resolve => setTimeout(resolve, intervalMs));
    }

    if (!loadedFromWebUI && this.recentProjects.length === 0 && typeof localStorage !== 'undefined') {
      this.loadFromLocalStorageSync();
    }

    this.isInitialized = true;
    this.bus.emit('project:recents-updated', this.recentProjects);
    this.bus.emit('project:changed', this.currentProject);
  }

  public async getDefaultWorkspaceDirectory(): Promise<string> {
    // 1. Tenta recuperar pasta corrente de execução onde o Arestrix foi aberto via C++ WebUI
    try {
      if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
        const cwd = await (window as any).webui.call('get_current_working_dir');
        if (cwd && cwd.trim()) {
          return cwd.trim();
        }
      }
    } catch (_) {}

    // 2. Se houver projeto salvo no histórico recente, utiliza o diretório dele
    if (this.recentProjects && this.recentProjects.length > 0 && this.recentProjects[0].rootPath) {
      return this.recentProjects[0].rootPath;
    }

    // 3. Fallback para rootPath do projeto ativo se diferente de default '.'
    if (this.currentProject && this.currentProject.rootPath && this.currentProject.rootPath !== '.') {
      return this.currentProject.rootPath;
    }

    return '.';
  }

  public async createProject(manifest: Partial<IProjectManifest>, graph?: Graph): Promise<boolean> {
    const now = new Date().toISOString();
    const projectName = manifest.name || 'Novo Projeto';
    const projectId = manifest.projectId || hashProjectName(projectName);
    const defaultRootPath = `./projects/${projectName}`;
    const fullManifest: IProjectManifest = {
      projectId,
      name: projectName,
      version: manifest.version || '1.0.0',
      description: manifest.description || '',
      createdAt: now,
      updatedAt: now,
      rootPath: manifest.rootPath || defaultRootPath,
      outputDir: manifest.outputDir || 'Output',
      pipelines: manifest.pipelines && manifest.pipelines.length > 0 ? manifest.pipelines : [
        {
          id: 'main',
          name: 'Pipeline Principal',
          file: 'pipelines/main.json',
          isPrimary: true,
          description: 'Pipeline central do projeto'
        }
      ],
      globalDefaults: manifest.globalDefaults,
      database: manifest.database || { type: 'sqlite', filePath: 'storage/database.sqlite' },
      environment: manifest.environment || {}
    };

    try {
      if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
        const ok = await (window as any).webui.call('create_project', JSON.stringify(fullManifest));
        if (!ok) return false;
      }
    } catch (e) {
      console.warn('[ProjectManager] Fallback local para criação de projeto:', e);
    }

    this.currentProject = fullManifest;
    this.addRecentItem({
      projectId: fullManifest.projectId,
      name: fullManifest.name,
      rootPath: fullManifest.rootPath,
      manifestPath: `${fullManifest.rootPath}/arestrix.project.json`,
      lastOpened: now
    });

    if (graph) {
      graph.clear();
      this.syncWithGraph(graph);
      await this.savePipeline(graph);
    }

    this.bus.emit('project:created', this.currentProject);
    this.bus.emit('project:changed', this.currentProject);
    return true;
  }

  public async loadProject(pathOrDir: string, graph?: Graph): Promise<boolean> {
    try {
      const existing = this.recentProjects.find(r => r.projectId === pathOrDir || r.rootPath === pathOrDir || r.manifestPath === pathOrDir);
      const effectivePath = existing ? (existing.rootPath || existing.manifestPath) : pathOrDir;

      if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
        const ok = await (window as any).webui.call('load_project', effectivePath);
        if (ok) {
          const raw = await (window as any).webui.call('get_current_project');
          if (raw) {
            this.currentProject = JSON.parse(raw);
          }
        } else {
          return false;
        }
      } else {
        // Fallback local se rodando em testes
        if (existing) {
          this.currentProject = {
            ...this.createDefaultManifest(),
            projectId: existing.projectId,
            name: existing.name,
            rootPath: existing.rootPath
          };
        } else {
          this.currentProject = {
            ...this.createDefaultManifest(),
            projectId: pathOrDir.replace(/[^a-zA-Z0-9_-]/g, '_'),
            name: pathOrDir,
            rootPath: pathOrDir
          };
        }
      }

      this.addRecentItem({
        projectId: this.currentProject.projectId,
        name: this.currentProject.name,
        rootPath: this.currentProject.rootPath,
        manifestPath: `${this.currentProject.rootPath}/arestrix.project.json`,
        lastOpened: new Date().toISOString()
      });

      if (graph) {
        this.syncWithGraph(graph);
        await this.loadPipeline(graph);
      }

      this.bus.emit('project:loaded', this.currentProject);
      this.bus.emit('project:changed', this.currentProject);
      return true;
    } catch (e) {
      console.error('[ProjectManager] Erro ao carregar projeto:', e);
      return false;
    }
  }

  public async saveCurrentProject(): Promise<boolean> {
    this.currentProject.updatedAt = new Date().toISOString();
    try {
      if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
        const ok = await (window as any).webui.call('save_project', JSON.stringify(this.currentProject));
        if (!ok) return false;
      }
    } catch (e) {
      console.warn('[ProjectManager] Salvo localmente em memória:', e);
    }

    this.bus.emit('project:saved', this.currentProject);
    return true;
  }

  public async saveProject(graph?: Graph): Promise<boolean> {
    const ok = await this.saveCurrentProject();
    if (graph) {
      await this.savePipeline(graph);
    }
    return ok;
  }

  public async deleteProject(projectIdOrPath: string, deleteFiles: boolean = true, graph?: Graph): Promise<boolean> {
    const target = this.recentProjects.find(
      r => r.projectId === projectIdOrPath || r.rootPath === projectIdOrPath || r.manifestPath === projectIdOrPath
    );
    const pId = target ? target.projectId : projectIdOrPath;
    const pRoot = target ? target.rootPath : (projectIdOrPath !== pId ? projectIdOrPath : '');

    // 1. Remove da lista de projetos recentes
    this.recentProjects = this.recentProjects.filter(
      r => r.projectId !== pId && r.rootPath !== pRoot && r.manifestPath !== projectIdOrPath
    );
    if (typeof localStorage !== 'undefined') {
      try {
        localStorage.setItem('arestrix_recent_projects', JSON.stringify(this.recentProjects));
        localStorage.removeItem(`arestrix_pipeline_${pId}`);
      } catch (_) {}
    }
    this.pipelineStorage.delete(pId);

    // 2. Notifica backend C++ via WebUI se disponível
    try {
      if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
        await (window as any).webui.call('delete_project', JSON.stringify({
          projectId: pId,
          rootPath: pRoot,
          deleteFiles
        }));
      }
    } catch (e) {
      console.warn('[ProjectManager] Erro ao comunicar delete_project:', e);
    }

    // 3. Se era o projeto ativo, reseta para o manifest padrão e limpa grafo
    const isActive = this.currentProject.projectId === pId || (pRoot && this.currentProject.rootPath === pRoot);
    if (isActive) {
      this.currentProject = this.createDefaultManifest();
      if (graph) {
        graph.clear();
        this.syncWithGraph(graph);
      }
      this.bus.emit('project:changed', this.currentProject);
    }

    this.bus.emit('project:recents-updated', this.recentProjects);
    this.bus.emit('project:deleted', { projectId: pId, rootPath: pRoot });
    return true;
  }

  public async savePipeline(graph: Graph): Promise<boolean> {
    try {
      // Sempre grava o grafo raiz com os subgrafos integrados: dentro de um subfluxo, toJSON() devolveria
      // so o grafo filho e o autosave sobrescreveria o pipeline principal.
      const schema = graph.getRootSchema();
      const jsonStr = JSON.stringify(schema, null, 2);
      this.pipelineStorage.set(this.currentProject.projectId, jsonStr);
      if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
        await (window as any).webui.call('save_pipeline', jsonStr);
      }
      if (typeof localStorage !== 'undefined') {
        try {
          localStorage.setItem(`arestrix_pipeline_${this.currentProject.projectId}`, jsonStr);
        } catch (_) {}
      }
      return true;
    } catch (e) {
      console.warn('[ProjectManager] Falha ao salvar pipeline:', e);
      return false;
    }
  }

  public async loadPipeline(graph: Graph, pipelineFile?: string): Promise<boolean> {
    try {
      if (pipelineFile && pipelineFile.replace(/\\/g, '/') !== 'pipelines/main.json') {
        const cleanRel = pipelineFile.replace(/\\/g, '/').replace(/^\/+/, '');
        const root = (this.currentProject.rootPath || '.').replace(/\\/g, '/').replace(/\/$/, '');
        const fullPath = pipelineFile.includes(':') || pipelineFile.startsWith('/')
          ? pipelineFile.replace(/\\/g, '/')
          : `${root}/${cleanRel}`;
        const bytes = await this.readDiskFile(fullPath);
        if (bytes) {
          const decoded = new TextDecoder().decode(bytes);
          const schema = JSON.parse(decoded);
          if (schema && Array.isArray(schema.nodes)) {
            graph.fromJSON(schema);
            return true;
          }
        }
        const storageKey = `${this.currentProject.projectId}:${cleanRel}`;
        const cached = this.pipelineStorage.get(storageKey)
          || (typeof localStorage !== 'undefined' ? localStorage.getItem(`arestrix_pipeline_${storageKey}`) || '' : '');
        if (cached && cached.trim()) {
          const schema = JSON.parse(cached);
          if (schema && Array.isArray(schema.nodes)) {
            graph.fromJSON(schema);
            return true;
          }
        }
      }
      let rawPipeline = '';
      if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
        rawPipeline = await (window as any).webui.call('load_pipeline');
      }
      if (!rawPipeline && typeof localStorage !== 'undefined') {
        try {
          rawPipeline = localStorage.getItem(`arestrix_pipeline_${this.currentProject.projectId}`) || '';
        } catch (_) {}
      }
      if (!rawPipeline) {
        rawPipeline = this.pipelineStorage.get(this.currentProject.projectId) || '';
      }

      if (rawPipeline && rawPipeline.trim()) {
        const schema = JSON.parse(rawPipeline);
        if (schema && Array.isArray(schema.nodes)) {
          graph.fromJSON(schema);
          return true;
        }
      }
      // Se não há pipeline salvo ou estiver vazio, limpa o grafo
      graph.clear();
      return true;
    } catch (e) {
      console.warn('[ProjectManager] Falha ao carregar pipeline:', e);
      graph.clear();
      return false;
    }
  }

  public hasActiveProject(): boolean {
    return !!this.currentProject && !!this.currentProject.projectId && this.currentProject.projectId !== 'default-workspace';
  }

  public updateProjectMetadata(partial: Partial<IProjectManifest>, graph?: Graph): void {
    this.currentProject = {
      ...this.currentProject,
      ...partial,
      updatedAt: new Date().toISOString()
    };
    if (graph) {
      this.syncWithGraph(graph);
    }
    this.bus.emit('project:updated', this.currentProject);
  }

  public syncWithGraph(graph: Graph): void {
    graph.setProjectId(this.currentProject.projectId);
    graph.updateMetadata({
      graphName: this.currentProject.name,
      description: this.currentProject.description,
      projectId: this.currentProject.projectId
    });
    if (this.currentProject.globalDefaults) {
      graph.updateGlobalDefaults(this.currentProject.globalDefaults);
    }
  }

  public listRecentProjects(): IRecentProjectItem[] {
    return [...this.recentProjects];
  }

  private addRecentItem(item: IRecentProjectItem): void {
    this.recentProjects = this.recentProjects.filter(r => r.projectId !== item.projectId && r.rootPath !== item.rootPath);
    this.recentProjects.unshift(item);
    if (this.recentProjects.length > 15) {
      this.recentProjects = this.recentProjects.slice(0, 15);
    }
    if (typeof localStorage !== 'undefined') {
      try {
        localStorage.setItem('arestrix_recent_projects', JSON.stringify(this.recentProjects));
      } catch (_) {}
    }
    this.bus.emit('project:recents-updated', this.recentProjects);
  }

  // --- Pacote de Projeto Comprimido (.arestrix) ---

  public exportArestrixBundle(graph: Graph): IArestrixBundle {
    const manifest = this.getCurrentProject();
    const pipelines: Record<string, any> = {
      main: graph.getRootSchema()
    };

    // Inclui pipelines em cache se houver outros
    this.pipelineStorage.forEach((val, key) => {
      try {
        pipelines[key] = JSON.parse(val);
      } catch (_) {
        pipelines[key] = val;
      }
    });

    return {
      format: 'arestrix-package',
      version: '1.0.0',
      generator: 'Arestrix Studio 2.0',
      exportedAt: new Date().toISOString(),
      manifest,
      pipelines,
      environment: manifest.environment || {},
      globalDefaults: manifest.globalDefaults || graph.getGlobalDefaults()
    };
  }

  public async compressArestrixBundle(bundle: IArestrixBundle): Promise<Uint8Array> {
    const jsonStr = JSON.stringify(bundle, null, 2);
    return await compressGzip(jsonStr);
  }

  public async decompressArestrixBundle(data: Uint8Array): Promise<IArestrixBundle> {
    const jsonStr = await decompressGzip(data);
    const parsed = JSON.parse(jsonStr);
    if (!parsed || parsed.format !== 'arestrix-package') {
      throw new Error('Arquivo não possui o formato de pacote válido do Arestrix (.arestrix)');
    }
    return parsed as IArestrixBundle;
  }

  public async saveProjectAsArestrix(
    targetDir: string,
    fileName: string,
    graph: Graph
  ): Promise<{ success: boolean; filePath: string; bundle: IArestrixBundle }> {
    let cleanName = fileName.trim();
    if (!cleanName) cleanName = this.currentProject.name || 'projeto';
    if (!cleanName.endsWith('.arestrix')) {
      cleanName += '.arestrix';
    }

    const normalizedDir = (targetDir || '.').replace(/\\/g, '/').replace(/\/$/, '');
    const fullPath = `${normalizedDir}/${cleanName}`;

    const bundle = this.exportArestrixBundle(graph);
    const baseProjectName = cleanName.replace(/\.arestrix$/, '');
    bundle.manifest.name = baseProjectName;
    bundle.manifest.rootPath = normalizedDir;
    bundle.manifest.updatedAt = new Date().toISOString();

    const compressed = await this.compressArestrixBundle(bundle);
    const b64 = uint8ArrayToBase64(compressed);

    let savedOnDisk = false;
    try {
      if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
        const ok = await (window as any).webui.call('save_arestrix_file', JSON.stringify({
          targetPath: fullPath,
          content: b64,
          isBase64: true
        }));
        savedOnDisk = ok;
      }
    } catch (e) {
      console.warn('[ProjectManager] Erro ao salvar via WebUI:', e);
    }

    // Se estiver em navegador sem backend C++, dispara download direto do arquivo
    if (!savedOnDisk && typeof document !== 'undefined' && typeof Blob !== 'undefined') {
      try {
        const blob = new Blob([compressed as any], { type: 'application/octet-stream' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = cleanName;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
        savedOnDisk = true;
      } catch (_) {}
    }

    this.currentProject = {
      ...this.currentProject,
      name: baseProjectName,
      rootPath: normalizedDir,
      updatedAt: bundle.manifest.updatedAt
    };

    this.addRecentItem({
      projectId: bundle.manifest.projectId || hashProjectName(baseProjectName),
      name: baseProjectName,
      rootPath: normalizedDir,
      manifestPath: fullPath,
      lastOpened: new Date().toISOString()
    });

    this.bus.emit('project:saved', this.currentProject);
    this.bus.emit('project:exported', { filePath: fullPath, bundle });

    return { success: true, filePath: fullPath, bundle };
  }

  public async saveProjectToDirectory(
    targetDir: string,
    folderName: string,
    graph: Graph
  ): Promise<{ success: boolean; rootPath: string; manifestPath: string }> {
    const now = new Date().toISOString();
    let cleanFolder = folderName.trim().replace(/\.(arestrix|json)$/i, '').trim();
    if (!cleanFolder) cleanFolder = this.currentProject.name || 'projeto';

    const normalizedDir = (targetDir || '.').replace(/\\/g, '/').replace(/\/$/, '');
    const dirBase = baseName(normalizedDir);
    const rootPath = dirBase.toLowerCase() === cleanFolder.toLowerCase()
      ? normalizedDir
      : `${normalizedDir}/${cleanFolder}`;
    const manifestPath = `${rootPath}/arestrix.project.json`;

    const fullManifest: IProjectManifest = {
      ...this.currentProject,
      projectId: hashProjectName(cleanFolder),
      name: cleanFolder,
      updatedAt: now,
      rootPath,
      pipelines: this.currentProject.pipelines && this.currentProject.pipelines.length > 0
        ? this.currentProject.pipelines
        : [
            {
              id: 'main',
              name: 'Pipeline Principal',
              file: 'pipelines/main.json',
              isPrimary: true,
              description: 'Pipeline central do projeto'
            }
          ]
    };

    try {
      const webui = this.webuiBridge();
      if (webui) {
        const ok = await webui.call('create_project', JSON.stringify(fullManifest));
        if (!ok) {
          await webui.call('save_project', JSON.stringify(fullManifest));
        }
      }
    } catch (e) {
      console.warn('[ProjectManager] Fallback local ao salvar pasta de projeto:', e);
    }

    this.currentProject = fullManifest;
    this.syncWithGraph(graph);
    await this.savePipeline(graph);

    this.addRecentItem({
      projectId: fullManifest.projectId,
      name: fullManifest.name,
      rootPath: fullManifest.rootPath,
      manifestPath,
      lastOpened: now
    });

    this.bus.emit('project:saved', this.currentProject);
    this.bus.emit('project:changed', this.currentProject);

    return { success: true, rootPath, manifestPath };
  }

  public async saveGraphToFile(
    targetDir: string,
    fileName: string,
    graph: Graph
  ): Promise<{ success: boolean; filePath: string; registeredInProject: boolean; schema: IGraphSchema }> {
    let cleanName = fileName.trim().replace(/\.(arestrix|json)$/i, '').trim();
    if (!cleanName) {
      cleanName = graph.getMetadata()?.graphName || 'pipeline';
    }
    const baseGraphName = cleanName;
    const jsonFileName = `${cleanName}.json`;

    const normalizedDir = (targetDir || '.').replace(/\\/g, '/').replace(/\/$/, '');
    const fullPath = `${normalizedDir}/${jsonFileName}`;

    const schema = graph.getRootSchema();
    schema.metadata = {
      ...(schema.metadata || { graphName: baseGraphName }),
      graphName: baseGraphName,
      projectId: this.currentProject.projectId
    };
    const jsonStr = JSON.stringify(schema, null, 2);

    let savedOnDisk = false;
    try {
      const webui = this.webuiBridge();
      if (webui) {
        const ok = await webui.call('save_arestrix_file', JSON.stringify({
          targetPath: fullPath,
          content: jsonStr,
          isBase64: false
        }));
        savedOnDisk = Boolean(ok);
      }
    } catch (e) {
      console.warn('[ProjectManager] Erro ao salvar grafo via WebUI:', e);
    }

    if (!savedOnDisk && typeof document !== 'undefined' && typeof Blob !== 'undefined') {
      try {
        const blob = new Blob([jsonStr], { type: 'application/json' });
        const url = URL.createObjectURL(blob);
        const a = document.createElement('a');
        a.href = url;
        a.download = jsonFileName;
        document.body.appendChild(a);
        a.click();
        document.body.removeChild(a);
        URL.revokeObjectURL(url);
        savedOnDisk = true;
      } catch (_) {}
    }

    this.pipelineStorage.set(fullPath, jsonStr);

    let registeredInProject = false;
    const projRoot = (this.currentProject.rootPath || '').replace(/\\/g, '/').replace(/\/$/, '');
    if (projRoot && projRoot !== '.') {
      const normLower = normalizedDir.toLowerCase();
      const rootLower = projRoot.toLowerCase();
      if (normLower === rootLower || normLower.startsWith(`${rootLower}/`)) {
        const relDir = normLower === rootLower ? '' : normalizedDir.slice(projRoot.length + 1);
        const relFile = relDir ? `${relDir}/${jsonFileName}` : jsonFileName;
        const pipeId = baseGraphName.toLowerCase().replace(/[^a-z0-9_-]/g, '_') || 'pipeline';

        const pipelines = Array.isArray(this.currentProject.pipelines) ? [...this.currentProject.pipelines] : [];
        const existingIdx = pipelines.findIndex(
          p => p.file.replace(/\\/g, '/').toLowerCase() === relFile.toLowerCase() || p.id === pipeId
        );
        if (existingIdx >= 0) {
          pipelines[existingIdx] = {
            ...pipelines[existingIdx],
            name: baseGraphName,
            file: relFile
          };
        } else {
          pipelines.push({
            id: pipeId,
            name: baseGraphName,
            file: relFile,
            isPrimary: pipelines.length === 0,
            description: schema.metadata?.description || ''
          });
        }
        this.currentProject = {
          ...this.currentProject,
          pipelines,
          updatedAt: new Date().toISOString()
        };

        const storageKey = `${this.currentProject.projectId}:${relFile}`;
        this.pipelineStorage.set(storageKey, jsonStr);
        if (relFile.toLowerCase() === 'pipelines/main.json') {
          this.pipelineStorage.set(this.currentProject.projectId, jsonStr);
        }
        if (typeof localStorage !== 'undefined') {
          try {
            localStorage.setItem(`arestrix_pipeline_${storageKey}`, jsonStr);
          } catch (_) {}
        }

        await this.saveCurrentProject();
        registeredInProject = true;
      }
    }

    this.bus.emit('graph:saved', { filePath: fullPath, registeredInProject, schema });
    this.bus.emit('project:saved', this.currentProject);

    return { success: true, filePath: fullPath, registeredInProject, schema };
  }

  public async loadArestrixBundle(bundle: IArestrixBundle, graph?: Graph): Promise<boolean> {
    if (!bundle || bundle.format !== 'arestrix-package' || !bundle.manifest) {
      return false;
    }

    this.currentProject = {
      ...this.createDefaultManifest(),
      ...bundle.manifest
    };

    if (bundle.pipelines) {
      for (const [key, pipe] of Object.entries(bundle.pipelines)) {
        this.pipelineStorage.set(key, JSON.stringify(pipe));
      }
      if (bundle.pipelines['main'] && graph) {
        graph.fromJSON(bundle.pipelines['main']);
      }
    }

    this.addRecentItem({
      projectId: this.currentProject.projectId,
      name: this.currentProject.name,
      rootPath: this.currentProject.rootPath,
      manifestPath: `${this.currentProject.rootPath}/${this.currentProject.name}.arestrix`,
      lastOpened: new Date().toISOString()
    });

    if (graph) {
      this.syncWithGraph(graph);
    }

    this.bus.emit('project:loaded', this.currentProject);
    this.bus.emit('project:changed', this.currentProject);
    return true;
  }

  public async listDrives(): Promise<string[]> {
    try {
      if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
        const raw = await (window as any).webui.call('list_drives');
        if (raw) {
          const list = JSON.parse(raw);
          if (Array.isArray(list) && list.length > 0) return list;
        }
      }
    } catch (_) {}
    return ['C:\\'];
  }

  public async listSubdirectories(dirPath: string): Promise<Array<{ name: string; path: string }>> {
    try {
      if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
        const raw = await (window as any).webui.call('list_subdirectories', dirPath);
        if (raw) {
          const list = JSON.parse(raw);
          if (Array.isArray(list)) return list;
        }
      }
    } catch (_) {}
    return [];
  }

  // --- Tela Abrir: navegar no disco e abrir projetos, pacotes e grafos ---

  private webuiBridge(): any | null {
    const webui = typeof window !== 'undefined' ? (window as any).webui : undefined;
    return webui && typeof webui.call === 'function' ? webui : null;
  }

  public hasNativeBridge(): boolean {
    return this.webuiBridge() !== null;
  }

  public async listOpenableEntries(dirPath: string): Promise<IOpenableListing> {
    const listing: IOpenableListing = { path: dirPath, parent: '', exists: false, truncated: false, entries: [], available: false };
    const webui = this.webuiBridge();
    if (!webui) return listing;
    listing.available = true;
    try {
      const raw = await webui.call('list_openable_entries', dirPath);
      const parsed = typeof raw === 'string' ? JSON.parse(raw) : raw;
      if (parsed && Array.isArray(parsed.entries)) {
        return {
          path: String(parsed.path || dirPath),
          parent: String(parsed.parent || ''),
          exists: Boolean(parsed.exists),
          truncated: Boolean(parsed.truncated),
          entries: parsed.entries,
          available: true
        };
      }
    } catch (_) {}
    return listing;
  }

  // Seletor de arquivo nativo; vazio se cancelado ou fora do aplicativo desktop.
  public async pickFile(request: { title: string; filters: IFilePickFilter[]; initialDir?: string }): Promise<string> {
    const webui = this.webuiBridge();
    if (!webui) return '';
    try {
      const picked = await webui.call('pick_file', JSON.stringify(request));
      return typeof picked === 'string' ? picked.trim() : '';
    } catch (_) {
      return '';
    }
  }

  private async readDiskFile(filePath: string): Promise<Uint8Array | null> {
    const webui = this.webuiBridge();
    if (!webui) return null;
    try {
      const raw = await webui.call('load_arestrix_file', JSON.stringify({ filePath }));
      const resp = typeof raw === 'string' ? JSON.parse(raw) : raw;
      if (resp && resp.success && typeof resp.base64 === 'string') {
        return base64ToUint8Array(resp.base64);
      }
    } catch (_) {}
    return null;
  }

  /**
   * Abre o que foi escolhido no disco: pasta de projeto, manifesto, pacote .arestrix ou grafo .json.
   * Um grafo dentro de <projeto>/pipelines abre o projeto inteiro; um grafo avulso so substitui o
   * canvas do projeto atual se `options.confirmLooseGraph` aceitar.
   */
  public async openFromDisk(path: string, graph: Graph, options: IOpenOptions = {}): Promise<IOpenResult> {
    const name = baseName(path);
    const lower = name.toLowerCase();
    const isFile = lower.endsWith('.arestrix') || lower.endsWith('.json');
    if (!isFile || PROJECT_MANIFEST_NAMES.includes(lower)) {
      return this.openProjectAt(path, graph);
    }

    const bytes = await this.readDiskFile(path);
    if (!bytes) return { ok: false, reason: 'invalid' };
    const content = await classifyOpenedFile(bytes);
    switch (content.type) {
      case 'package':
        return this.extractPackage(content.bundle, path, graph);
      case 'manifest':
        return this.openProjectAt(path, graph);
      case 'graph': {
        const folder = dirName(path);
        if (baseName(folder).toLowerCase() === 'pipelines') {
          const opened = await this.openProjectAt(dirName(folder), graph);
          if (opened.ok) return opened;
        }
        return this.importLooseGraph(content.schema, name, graph, options);
      }
      default:
        return { ok: false, reason: 'invalid' };
    }
  }

  // Navegador sem backend C++ (input type=file): ha so os bytes, sem caminho no disco.
  public async openFromBytes(bytes: Uint8Array, fileName: string, graph: Graph, options: IOpenOptions = {}): Promise<IOpenResult> {
    const content = await classifyOpenedFile(bytes);
    switch (content.type) {
      case 'package':
        return (await this.loadArestrixBundle(content.bundle, graph))
          ? { ok: true, kind: 'project', path: fileName }
          : { ok: false, reason: 'invalid' };
      case 'manifest':
        return { ok: false, reason: 'unsupported' };
      case 'graph':
        return this.importLooseGraph(content.schema, fileName, graph, options);
      default:
        return { ok: false, reason: 'invalid' };
    }
  }

  private async openProjectAt(path: string, graph: Graph): Promise<IOpenResult> {
    const ok = await this.loadProject(path, graph);
    return ok ? { ok: true, kind: 'project', path: this.currentProject.rootPath } : { ok: false, reason: 'invalid' };
  }

  private importLooseGraph(schema: IGraphSchema, fileName: string, graph: Graph, options: IOpenOptions): IOpenResult {
    if (!options.confirmLooseGraph || !options.confirmLooseGraph(fileName)) {
      return { ok: false, reason: 'cancelled' };
    }
    graph.fromJSON(schema);
    this.syncWithGraph(graph);
    return { ok: true, kind: 'graph', path: fileName };
  }

  // O pacote vira um projeto numa pasta nova ao lado dele ("nome", "nome (2)", ...): os salvamentos
  // seguintes precisam de uma pasta de projeto e nao podem sobrescrever outro projeto.
  private async extractPackage(bundle: IArestrixBundle, packagePath: string, graph: Graph): Promise<IOpenResult> {
    const parent = dirName(packagePath);
    const base = stripExtension(baseName(packagePath)) || bundle.manifest.name || 'projeto';
    const siblings = await this.listOpenableEntries(parent);
    const taken = new Set(siblings.entries.map(e => e.name.toLowerCase()));
    let folder = base;
    for (let i = 2; taken.has(folder.toLowerCase()); i++) {
      folder = `${base} (${i})`;
    }
    const rootPath = joinPath(parent, folder);

    if (!(await this.createProject({ ...bundle.manifest, rootPath }))) {
      return { ok: false, reason: 'invalid' };
    }
    const main = bundle.pipelines?.main;
    if (main && Array.isArray(main.nodes)) {
      graph.fromJSON(main);
    } else {
      graph.clear();
    }
    this.syncWithGraph(graph);
    await this.savePipeline(graph);
    this.bus.emit('project:loaded', this.currentProject);
    return { ok: true, kind: 'package', path: rootPath };
  }

  // Auxiliar para reset em testes unitários
  public resetForTesting(bus?: EventBus): void {
    if (bus) this.bus = bus;
    this.currentProject = this.createDefaultManifest();
    this.recentProjects = [];
    this.pipelineStorage.clear();
    this.isInitialized = false;
    this.initPromise = null;
  }
}
