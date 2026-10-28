/**
 * Arestrix Studio - Preferências de Bibliotecas por Projeto
 *
 * O Projeto não cria nem edita assets. Ele apenas habilita bibliotecas
 * liberadas no Workspace e fixa versões de assets. A persistência nativa no
 * manifesto entra junto com o runner do seletor (Fase 4); nesta fase o store
 * usa o fallback local por `projectId`.
 */

export interface IProjectLibrarySettings {
  enabledLibraryIds: string[];
  pinnedAssetIds: string[];
  updatedAt?: string;
}

const STORAGE_PREFIX = 'arestrix_project_libraries_v1:';

const EMPTY: IProjectLibrarySettings = {
  enabledLibraryIds: [],
  pinnedAssetIds: []
};

function keyFor(projectId: string): string {
  return `${STORAGE_PREFIX}${projectId || 'default'}`;
}

export class ProjectLibraryStore {
  public load(projectId: string): IProjectLibrarySettings {
    if (typeof localStorage === 'undefined') return { ...EMPTY };
    try {
      const raw = localStorage.getItem(keyFor(projectId));
      if (!raw) return { ...EMPTY };
      const parsed = JSON.parse(raw);
      return {
        enabledLibraryIds: Array.isArray(parsed?.enabledLibraryIds)
          ? parsed.enabledLibraryIds.filter((id: unknown) => typeof id === 'string')
          : [],
        pinnedAssetIds: Array.isArray(parsed?.pinnedAssetIds)
          ? parsed.pinnedAssetIds.filter((id: unknown) => typeof id === 'string')
          : [],
        updatedAt: typeof parsed?.updatedAt === 'string' ? parsed.updatedAt : undefined
      };
    } catch {
      return { ...EMPTY };
    }
  }

  public save(projectId: string, settings: IProjectLibrarySettings): boolean {
    if (typeof localStorage === 'undefined') return false;
    try {
      const next: IProjectLibrarySettings = {
        enabledLibraryIds: [...new Set(settings.enabledLibraryIds || [])],
        pinnedAssetIds: [...new Set(settings.pinnedAssetIds || [])],
        updatedAt: new Date().toISOString()
      };
      localStorage.setItem(keyFor(projectId), JSON.stringify(next));
      return true;
    } catch {
      return false;
    }
  }

  public clear(projectId: string): void {
    if (typeof localStorage === 'undefined') return;
    localStorage.removeItem(keyFor(projectId));
  }
}
