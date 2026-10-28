import { WorkspaceManager } from '../workspace/WorkspaceManager.ts';
import type { ILibraryState, LibraryScope } from './types.ts';

/**
 * Arestrix Studio - Persistência das Bibliotecas
 *
 * - Workspace: `.workspace/libraries.json`, via `read/write_workspace_asset`.
 * - Studio: fallback local (`localStorage`) até o endpoint nativo dedicado.
 * - Project: apenas pins/habilitação; fallback local por `projectId`.
 *
 * O registro em memória continua sendo a fonte de verdade durante a sessão;
 * este store grava snapshots para reabertura e para o runtime nativo.
 */
export class LibraryStore {
  private static readonly WORKSPACE_KEY = 'arestrix_library_state_workspace_v1';
  private static readonly STUDIO_KEY = 'arestrix_library_state_studio_v1';
  private static readonly PROJECT_PREFIX = 'arestrix_library_state_project_v1:';

  constructor(private readonly manager: WorkspaceManager = WorkspaceManager.getInstance()) {}

  public async load(scope: LibraryScope = 'workspace', projectId = 'default'): Promise<ILibraryState | null> {
    if (scope === 'workspace') {
      const backend = this.backend();
      if (backend) {
        try {
          const raw = await backend.call('read_workspace_asset', JSON.stringify({
            filePath: this.workspacePath()
          }));
          if (raw) {
            const parsed = typeof raw === 'string' ? JSON.parse(raw) : raw;
            const content = parsed?.content
              ? (typeof parsed.content === 'string' ? JSON.parse(parsed.content) : parsed.content)
              : parsed;
            if (content && content.version === 2) return content as ILibraryState;
          }
        } catch {
          // Fallback local abaixo.
        }
      }
      return this.readLocal(LibraryStore.WORKSPACE_KEY);
    }

    const key = scope === 'studio'
      ? LibraryStore.STUDIO_KEY
      : `${LibraryStore.PROJECT_PREFIX}${projectId}`;
    return this.readLocal(key);
  }

  public async save(scope: LibraryScope, state: ILibraryState, projectId = 'default'): Promise<boolean> {
    if (scope === 'workspace') {
      let localOk = this.writeLocal(LibraryStore.WORKSPACE_KEY, state);
      const backend = this.backend();
      if (!backend) return localOk;
      try {
        const response = await backend.call('write_workspace_asset', JSON.stringify({
          filePath: this.workspacePath(),
          content: JSON.stringify(state, null, 2)
        }));
        return response === true || response === 'true' || localOk;
      } catch {
        return localOk;
      }
    }

    const key = scope === 'studio'
      ? LibraryStore.STUDIO_KEY
      : `${LibraryStore.PROJECT_PREFIX}${projectId}`;
    return this.writeLocal(key, state);
  }

  public clear(scope: LibraryScope = 'workspace', projectId = 'default'): void {
    if (typeof localStorage === 'undefined') return;
    if (scope === 'workspace') localStorage.removeItem(LibraryStore.WORKSPACE_KEY);
    else if (scope === 'studio') localStorage.removeItem(LibraryStore.STUDIO_KEY);
    else localStorage.removeItem(`${LibraryStore.PROJECT_PREFIX}${projectId}`);
  }

  private workspacePath(): string {
    return `${this.manager.getWorkspaceDirectories().root}/libraries.json`;
  }

  private readLocal(key: string): ILibraryState | null {
    if (typeof localStorage === 'undefined') return null;
    try {
      const raw = localStorage.getItem(key);
      if (!raw) return null;
      const parsed = JSON.parse(raw);
      return parsed && parsed.version === 2 ? parsed as ILibraryState : null;
    } catch {
      return null;
    }
  }

  private writeLocal(key: string, state: ILibraryState): boolean {
    if (typeof localStorage === 'undefined') return false;
    try {
      localStorage.setItem(key, JSON.stringify(state));
      return true;
    } catch {
      return false;
    }
  }

  private backend(): any {
    if (typeof window === 'undefined') return null;
    const webui = (window as any).webui;
    return webui && typeof webui.call === 'function' ? webui : null;
  }
}
