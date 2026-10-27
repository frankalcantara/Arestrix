import { globalEventBus } from '../EventBus.ts';
import { Graph } from '../Graph.ts';
import { ProjectManager } from '../ProjectManager.ts';
import type { IGlobalPipelineDefaults, IProjectManifest } from '../types.ts';
import type { IProjectSettings } from './types.ts';

export interface IProjectSettingsPatch {
  manifest?: Partial<IProjectManifest>;
  defaults?: Partial<IGlobalPipelineDefaults>;
}

/**
 * Persistência das propriedades do Projeto.
 *
 * Reaproveita o ProjectManager e o Graph existentes: metadados e defaults são
 * atualizados em memória, sincronizados com o grafo e gravados no manifesto.
 */
export class ProjectSettingsStore {
  constructor(
    private readonly projectManager: ProjectManager = ProjectManager.getInstance(globalEventBus),
    private readonly graph: Graph
  ) {}

  public async load(): Promise<IProjectSettings> {
    const manifest = this.projectManager.getCurrentProject();
    const explicitDefaults = manifest.globalDefaults;
    return {
      version: 1,
      manifest,
      defaults: explicitDefaults || {},
      hasExplicitDefaults: Boolean(explicitDefaults)
    };
  }

  public async save(patch: IProjectSettingsPatch): Promise<boolean> {
    if (patch.defaults) {
      this.graph.updateGlobalDefaults(patch.defaults);
    }

    const defaults = this.graph.getGlobalDefaults();
    this.projectManager.updateProjectMetadata({
      ...(patch.manifest || {}),
      globalDefaults: defaults
    }, this.graph);

    const saved = await this.projectManager.saveCurrentProject();
    if (saved) {
      const manifest = this.projectManager.getCurrentProject();
      globalEventBus.emit('project:changed', manifest);
      globalEventBus.emit('properties:project-saved', { manifest, defaults });
    }
    return saved;
  }

  public async saveDefaults(defaults: Partial<IGlobalPipelineDefaults>): Promise<boolean> {
    return this.save({ defaults });
  }

  public async saveMetadata(manifest: Partial<IProjectManifest>): Promise<boolean> {
    return this.save({ manifest });
  }
}
