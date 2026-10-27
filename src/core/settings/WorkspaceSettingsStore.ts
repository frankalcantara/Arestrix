import { DEFAULT_PIPELINE_DEFAULTS } from '../types.ts';
import { WorkspaceManager } from '../workspace/WorkspaceManager.ts';
import type { IWorkspaceSettings } from './types.ts';

const STORAGE_PREFIX = 'arestrix_workspace_settings_v1:';

function storageKey(root: string): string {
  return `${STORAGE_PREFIX}${root.replace(/\\/g, '/').replace(/\/$/, '') || '.'}`;
}

function defaultSettings(manager: WorkspaceManager): IWorkspaceSettings {
  const directories = manager.getWorkspaceDirectories();
  return {
    version: 1,
    rootPath: manager.getWorkspaceRoot(),
    directories: {
      root: directories.root,
      skills: directories.skills,
      mcps: directories.mcps,
      agents: directories.agents,
      graphs: `${directories.root}/graphs`
    },
    defaults: {
      sandbox: { ...DEFAULT_PIPELINE_DEFAULTS.sandbox }
    },
    security: {
      autoVerifySkills: true,
      strictSecurityMode: true
    }
  };
}

function normalize(raw: unknown, manager: WorkspaceManager): IWorkspaceSettings {
  const base = defaultSettings(manager);
  if (!raw || typeof raw !== 'object') return base;
  const input = raw as Partial<IWorkspaceSettings> & Record<string, unknown>;
  const properties = input.properties && typeof input.properties === 'object'
    ? input.properties as Partial<IWorkspaceSettings>
    : input;
  const directories = (properties.directories || {}) as Partial<IWorkspaceSettings['directories']>;
  const defaults = (properties.defaults || {}) as Partial<IWorkspaceSettings['defaults']>;
  const security = (properties.security || {}) as Partial<IWorkspaceSettings['security']>;

  return {
    version: 1,
    rootPath: typeof properties.rootPath === 'string' && properties.rootPath ? properties.rootPath : base.rootPath,
    directories: {
      root: directories.root || String(input.workspaceRoot || base.directories.root),
      skills: directories.skills || String(input.skillsDir || base.directories.skills),
      mcps: directories.mcps || String(input.mcpsDir || base.directories.mcps),
      agents: directories.agents || String(input.agentsDir || base.directories.agents),
      graphs: directories.graphs || String(input.graphsDir || base.directories.graphs)
    },
    defaults: {
      ...base.defaults,
      ...defaults,
      sandbox: { ...base.defaults.sandbox, ...(defaults.sandbox || {}) }
    },
    security: {
      autoVerifySkills: security.autoVerifySkills ?? (typeof input.autoVerifySkills === 'boolean' ? input.autoVerifySkills : base.security.autoVerifySkills),
      strictSecurityMode: security.strictSecurityMode ?? (typeof input.strictSecurityMode === 'boolean' ? input.strictSecurityMode : base.security.strictSecurityMode)
    },
    lastSyncTimestamp: typeof properties.lastSyncTimestamp === 'string'
      ? properties.lastSyncTimestamp
      : typeof input.lastSyncTimestamp === 'string'
        ? input.lastSyncTimestamp
        : undefined
  };
}

/**
 * Persistência das propriedades do Workspace.
 *
 * O manifesto é gravado em `workspace.config.json` via `write_workspace_asset`;
 * o fallback local garante que a tela funcione no navegador e em testes.
 */
export class WorkspaceSettingsStore {
  constructor(private readonly manager: WorkspaceManager = WorkspaceManager.getInstance()) {}

  public async load(): Promise<IWorkspaceSettings> {
    const root = this.manager.getWorkspaceRoot();
    const backend = this.backend();
    if (backend) {
      try {
        const raw = await backend.call('read_workspace_asset', JSON.stringify({
          filePath: this.configPath()
        }));
        if (raw) {
          const parsed = typeof raw === 'string' ? JSON.parse(raw) : raw;
          const content = parsed?.content
            ? (typeof parsed.content === 'string' ? JSON.parse(parsed.content) : parsed.content)
            : parsed;
          return normalize(content, this.manager);
        }
      } catch {
        // Endpoint opcional; usa o fallback local abaixo.
      }
    }

    try {
      if (typeof localStorage === 'undefined') return defaultSettings(this.manager);
      const raw = localStorage.getItem(storageKey(root));
      return raw ? normalize(JSON.parse(raw), this.manager) : defaultSettings(this.manager);
    } catch {
      return defaultSettings(this.manager);
    }
  }

  public async save(settings: IWorkspaceSettings): Promise<boolean> {
    const normalized = normalize(settings, this.manager);
    let localOk = false;
    try {
      if (typeof localStorage !== 'undefined') {
        localStorage.setItem(storageKey(normalized.rootPath), JSON.stringify(normalized));
        localOk = true;
      }
    } catch {
      localOk = false;
    }

    const backend = this.backend();
    if (!backend) return localOk;

    const serialized = {
      workspaceRoot: normalized.directories.root,
      skillsDir: normalized.directories.skills,
      mcpsDir: normalized.directories.mcps,
      agentsDir: normalized.directories.agents,
      graphsDir: normalized.directories.graphs,
      autoVerifySkills: normalized.security.autoVerifySkills,
      strictSecurityMode: normalized.security.strictSecurityMode,
      installedAssetsVersion: '3.0.0',
      lastSyncTimestamp: normalized.lastSyncTimestamp,
      properties: normalized
    };

    try {
      const response = await backend.call('write_workspace_asset', JSON.stringify({
        filePath: this.configPath(),
        content: JSON.stringify(serialized, null, 2)
      }));
      return response === true || response === 'true' || localOk;
    } catch {
      return localOk;
    }
  }

  private configPath(): string {
    return `${this.manager.getWorkspaceDirectories().root}/workspace.config.json`;
  }

  private backend(): any {
    if (typeof window === 'undefined') return null;
    const webui = (window as any).webui;
    return webui && typeof webui.call === 'function' ? webui : null;
  }
}
