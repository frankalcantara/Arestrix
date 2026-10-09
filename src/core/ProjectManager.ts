import { EventBus, globalEventBus } from './EventBus.ts';
import { IProjectManifest, IRecentProjectItem } from './types.ts';
import { Graph } from './Graph.ts';

export class ProjectManager {
  private static instance: ProjectManager;
  private bus: EventBus;
  private currentProject: IProjectManifest;
  private recentProjects: IRecentProjectItem[] = [];

  private constructor(bus: EventBus = globalEventBus) {
    this.bus = bus;
    this.currentProject = this.createDefaultManifest();
  }

  public static getInstance(bus: EventBus = globalEventBus): ProjectManager {
    if (!ProjectManager.instance) {
      ProjectManager.instance = new ProjectManager(bus);
    }
    return ProjectManager.instance;
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
      environment: {}
    };
  }

  public getCurrentProject(): IProjectManifest {
    return { ...this.currentProject };
  }

  public async init(): Promise<void> {
    try {
      if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
        const rawProj = await (window as any).webui.call('get_current_project');
        if (rawProj) {
          this.currentProject = JSON.parse(rawProj);
        }
        const rawRecents = await (window as any).webui.call('list_recent_projects');
        if (rawRecents) {
          this.recentProjects = JSON.parse(rawRecents);
        }
      }
    } catch (e) {
      console.warn('[ProjectManager] Usando modo em memória local:', e);
    }
  }

  public async createProject(manifest: Partial<IProjectManifest>, graph?: Graph): Promise<boolean> {
    const now = new Date().toISOString();
    const fullManifest: IProjectManifest = {
      projectId: manifest.projectId || `proj_${Date.now()}`,
      name: manifest.name || 'Novo Projeto',
      version: manifest.version || '1.0.0',
      description: manifest.description || '',
      createdAt: now,
      updatedAt: now,
      rootPath: manifest.rootPath || `./projects/${manifest.projectId || 'projeto'}`,
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
      this.syncWithGraph(graph);
    }

    this.bus.emit('project:created', this.currentProject);
    this.bus.emit('project:changed', this.currentProject);
    return true;
  }

  public async loadProject(pathOrDir: string, graph?: Graph): Promise<boolean> {
    try {
      if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
        const ok = await (window as any).webui.call('load_project', pathOrDir);
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
        const existing = this.recentProjects.find(r => r.rootPath === pathOrDir || r.manifestPath === pathOrDir);
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

  public async saveProject(_graph?: Graph): Promise<boolean> {
    return this.saveCurrentProject();
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
  }

  // Auxiliar para reset em testes unitários
  public resetForTesting(bus?: EventBus): void {
    if (bus) this.bus = bus;
    this.currentProject = this.createDefaultManifest();
    this.recentProjects = [];
  }
}
