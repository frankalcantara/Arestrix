import { EventBus, globalEventBus } from '../EventBus.ts';
import type {
  ILibraryAsset,
  ILibraryEntry,
  ILibrarySource,
  ILibraryState,
  LibraryScope
} from './types.ts';

export type { ILibraryEntry, LibraryScope } from './types.ts';

/**
 * Arestrix Studio - Registro local de Bibliotecas de Assets
 *
 * Fase 2: além das bibliotecas, o registro guarda os assets auditados e o
 * índice de proveniência. A sincronização remota continua fora do escopo:
 * nesta fase os assets vêm da biblioteca embutida ou do catálogo do Workspace.
 *
 * A persistência local é o fallback de testes/navegador. O `LibraryStore`
 * espelha o snapshot no arquivo nativo do Workspace quando disponível.
 */

export type LibraryKind = 'builtin' | 'local' | 'repository';

export interface ICreateLibraryInput {
  name: string;
  description?: string;
  scope?: LibraryScope;
  kind?: LibraryKind;
  sourceId?: string;
}

const STORAGE_KEY = 'arestrix_libraries_v2';
const LEGACY_STORAGE_KEY = 'arestrix_libraries_v1';

const BUILTIN_LIBRARY: ILibraryEntry = {
  id: 'library-arestrix',
  name: 'Biblioteca Arestrix',
  description: 'Skills e MCPs embutidos e auditados que acompanham o Studio.',
  kind: 'builtin',
  scope: 'studio',
  trust: 'official',
  enabled: true,
  createdAt: '2026-10-27T00:00:00.000Z',
  isBuiltin: true
};

export class LibraryRegistry {
  private static instance: LibraryRegistry;
  private readonly bus: EventBus;
  private libraries: ILibraryEntry[] = [];
  private assets: ILibraryAsset[] = [];
  private sources: ILibrarySource[] = [];

  private constructor(bus: EventBus = globalEventBus) {
    this.bus = bus;
    this.loadPersistedConfig();
  }

  public static getInstance(bus: EventBus = globalEventBus): LibraryRegistry {
    if (!LibraryRegistry.instance) {
      LibraryRegistry.instance = new LibraryRegistry(bus);
    }
    return LibraryRegistry.instance;
  }

  public list(scope?: LibraryScope): ILibraryEntry[] {
    const userLibraries = this.libraries.filter(library => !library.isBuiltin);
    const visible = scope
      ? userLibraries.filter(library => library.scope === scope)
      : userLibraries;
    return [{ ...BUILTIN_LIBRARY }, ...visible];
  }

  public get(id: string): ILibraryEntry | undefined {
    if (id === BUILTIN_LIBRARY.id) return { ...BUILTIN_LIBRARY };
    return this.libraries.find(library => library.id === id);
  }

  // --- Fontes de biblioteca (GitHub, pasta local, etc.) ---

  public listSources(): ILibrarySource[] {
    return this.sources.map(source => ({ ...source }));
  }

  public getSource(id: string): ILibrarySource | undefined {
    const source = this.sources.find(item => item.id === id);
    return source ? { ...source } : undefined;
  }

  public upsertSource(source: ILibrarySource): ILibrarySource | null {
    if (!source || !source.id || !source.name) return null;
    const index = this.sources.findIndex(item => item.id === source.id);
    const next: ILibrarySource = {
      ...source,
      licenseAllowlist: [...(source.licenseAllowlist || [])]
    };
    if (index === -1) this.sources.push(next);
    else this.sources[index] = next;
    this.savePersistedConfig();
    this.bus.emit('library:source-updated', next);
    this.bus.emit('library:changed');
    return next;
  }

  public removeSource(id: string): boolean {
    const index = this.sources.findIndex(source => source.id === id);
    if (index === -1) return false;
    this.sources.splice(index, 1);
    this.savePersistedConfig();
    this.bus.emit('library:source-removed', { id });
    this.bus.emit('library:changed');
    return true;
  }

  public create(input: ICreateLibraryInput): ILibraryEntry | null {
    const name = input.name?.trim();
    if (!name) return null;

    const existing = this.list().some(library =>
      library.name.trim().toLowerCase() === name.toLowerCase()
    );
    if (existing) return null;

    const entry: ILibraryEntry = {
      id: this.buildId(name),
      name,
      description: input.description?.trim() || 'Biblioteca local criada no Arestrix.',
      kind: input.kind || (input.sourceId ? 'repository' : 'local'),
      scope: input.scope === 'studio' ? 'studio' : input.scope === 'project' ? 'project' : 'workspace',
      sourceId: input.sourceId || undefined,
      trust: input.kind === 'repository' ? 'community' : 'local',
      enabled: true,
      createdAt: new Date().toISOString(),
      isBuiltin: false
    };

    this.libraries.push(entry);
    this.savePersistedConfig();
    this.bus.emit('library:created', entry);
    this.bus.emit('library:changed');
    return entry;
  }

  public remove(id: string): boolean {
    if (id === BUILTIN_LIBRARY.id) return false;
    const index = this.libraries.findIndex(library => library.id === id && !library.isBuiltin);
    if (index === -1) return false;

    this.libraries.splice(index, 1);
    this.assets = this.assets.filter(asset => asset.libraryId !== id);
    this.savePersistedConfig();
    this.bus.emit('library:removed', { id });
    this.bus.emit('library:changed');
    return true;
  }

  // --- Assets ---

  public listAssets(libraryId?: string): ILibraryAsset[] {
    return libraryId
      ? this.assets.filter(asset => asset.libraryId === libraryId)
      : [...this.assets];
  }

  public getAsset(id: string): ILibraryAsset | undefined {
    return this.assets.find(asset => asset.id === id);
  }

  public upsertAsset(asset: ILibraryAsset): ILibraryAsset | null {
    if (!asset || !asset.id || !asset.libraryId || !asset.payload) return null;
    if (!this.get(asset.libraryId)) return null;
    if (asset.payload.kind !== 'skill' && asset.payload.kind !== 'mcp' && asset.payload.kind !== 'agent-template') return null;

    // IDs viram nomes de pasta/arquivo em .workspace: nunca aceitar ':' (Windows).
    const safeId = String(asset.id).replace(/[:]+/g, '-');
    const index = this.assets.findIndex(existing => existing.id === safeId);
    const next: ILibraryAsset = {
      ...asset,
      id: safeId,
      derivedFrom: asset.derivedFrom
        ? { ...asset.derivedFrom, assetId: String(asset.derivedFrom.assetId).replace(/[:]+/g, '-') }
        : undefined,
      updatedAt: asset.updatedAt || new Date().toISOString(),
      createdAt: asset.createdAt || new Date().toISOString()
    };
    if (index === -1) {
      this.assets.push(next);
    } else {
      this.assets[index] = next;
    }
    this.savePersistedConfig();
    this.bus.emit('library:asset-updated', next);
    this.bus.emit('library:changed');
    return next;
  }

  public removeAsset(id: string): boolean {
    const index = this.assets.findIndex(asset => asset.id === id);
    if (index === -1) return false;
    const [removed] = this.assets.splice(index, 1);
    this.savePersistedConfig();
    this.bus.emit('library:asset-removed', removed);
    this.bus.emit('library:changed');
    return true;
  }

  public countAssets(libraryId?: string): number {
    return this.listAssets(libraryId).length;
  }

  // --- Snapshot / persistência ---

  public snapshot(): ILibraryState {
    return {
      version: 2,
      libraries: this.list().map(library => ({ ...library })),
      assets: this.listAssets().map(asset => ({ ...asset })),
      sources: this.listSources(),
      updatedAt: new Date().toISOString()
    };
  }

  public restore(state: ILibraryState | null | undefined, replace = true): void {
    if (!state || !Array.isArray(state.libraries)) return;
    const libraries = state.libraries
      .filter(entry => entry && !entry.isBuiltin && entry.id !== BUILTIN_LIBRARY.id)
      .map(entry => this.normalizeLibrary(entry));
    const libraryIds = new Set(['library-arestrix', ...libraries.map(library => library.id)]);
    const assets = Array.isArray(state.assets)
      ? state.assets.filter(asset => this.isValidAsset(asset) && libraryIds.has(asset.libraryId))
      : [];
    const sources = Array.isArray(state.sources)
      ? state.sources.filter(source => source && typeof source.id === 'string' && typeof source.name === 'string')
      : [];

    if (replace) {
      this.libraries = libraries;
      this.assets = assets;
      this.sources = sources;
    } else {
      for (const library of libraries) {
        const index = this.libraries.findIndex(existing => existing.id === library.id);
        if (index === -1) this.libraries.push(library);
        else this.libraries[index] = library;
      }
      for (const asset of assets) {
        const index = this.assets.findIndex(existing => existing.id === asset.id);
        if (index === -1) this.assets.push(asset);
        else this.assets[index] = asset;
      }
      for (const source of sources) {
        const index = this.sources.findIndex(existing => existing.id === source.id);
        if (index === -1) this.sources.push(source);
        else this.sources[index] = source;
      }
    }
    this.savePersistedConfig();
    this.bus.emit('library:changed');
  }

  public resetForTesting(bus: EventBus = globalEventBus): void {
    this.libraries = [];
    this.assets = [];
    this.sources = [];
    (this as any).bus = bus;
    if (typeof localStorage !== 'undefined') {
      localStorage.removeItem(STORAGE_KEY);
      localStorage.removeItem(LEGACY_STORAGE_KEY);
    }
  }

  private buildId(name: string): string {
    const base = name
      .normalize('NFD')
      .replace(/[\u0300-\u036f]/g, '')
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, '-')
      .replace(/^-+|-+$/g, '')
      .slice(0, 40) || 'biblioteca';

    let id = `lib-${base}`;
    let counter = 2;
    while (this.libraries.some(library => library.id === id)) {
      id = `lib-${base}-${counter}`;
      counter += 1;
    }
    return id;
  }

  private normalizeLibrary(entry: Partial<ILibraryEntry>): ILibraryEntry {
    const name = String(entry.name || '').trim() || 'Biblioteca';
    return {
      id: String(entry.id || this.buildId(name)),
      name,
      description: String(entry.description || 'Biblioteca local criada no Arestrix.'),
      kind: entry.kind === 'repository' ? 'repository' : entry.kind === 'builtin' ? 'builtin' : 'local',
      scope: entry.scope === 'studio' ? 'studio' : entry.scope === 'project' ? 'project' : 'workspace',
      sourceId: entry.sourceId ? String(entry.sourceId) : undefined,
      trust: entry.trust || (entry.kind === 'repository' ? 'community' : 'local'),
      enabled: entry.enabled !== false,
      createdAt: String(entry.createdAt || new Date().toISOString()),
      isBuiltin: false
    };
  }

  private isValidAsset(asset: Partial<ILibraryAsset>): asset is ILibraryAsset {
    return Boolean(
      asset &&
      typeof asset.id === 'string' &&
      typeof asset.libraryId === 'string' &&
      (asset.payload?.kind === 'skill' || asset.payload?.kind === 'mcp' || asset.payload?.kind === 'agent-template') &&
      asset.audit &&
      typeof asset.audit.hash === 'string'
    );
  }

  private migrateLegacyGithubSources(): void {
    if (typeof localStorage === 'undefined') return;
    try {
      const raw = localStorage.getItem('arestrix_github_skills_repos');
      if (!raw) return;
      const parsed = JSON.parse(raw);
      if (!Array.isArray(parsed)) return;

      let changed = false;
      for (const repo of parsed) {
        if (!repo || repo.isDefault === true || repo.id === 'library-arestrix') continue;
        const repoPath = String(repo.repo || '').trim();
        if (!/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(repoPath) || repoPath.includes('..')) continue;

        const sourceId = `source-github-${repoPath.replace(/[^a-zA-Z0-9_-]/g, '-')}-main`;
        if (this.getSource(sourceId)) continue;

        const name = String(repo.name || repoPath);
        const license = String(repo.license || 'MIT');
        const allowlist = ['MIT', 'Apache-2.0', 'BSD-3-Clause', 'BSD-2-Clause', 'ISC'].includes(license)
          ? [license]
          : ['MIT', 'Apache-2.0', 'BSD-3-Clause', 'BSD-2-Clause', 'ISC'];

        this.sources.push({
          id: sourceId,
          kind: 'github',
          scope: 'workspace',
          name,
          enabled: true,
          trust: 'community',
          github: { repo: repoPath, ref: 'main' },
          licenseAllowlist: allowlist
        });

        if (!this.libraries.some(library => library.sourceId === sourceId)
          && !this.libraries.some(library => library.name.toLowerCase() === name.toLowerCase())) {
          this.libraries.push({
            id: this.buildId(name),
            name,
            description: `Fonte GitHub migrada do catálogo legado: ${repoPath}.`,
            kind: 'repository',
            scope: 'workspace',
            sourceId,
            trust: 'community',
            enabled: true,
            createdAt: new Date().toISOString(),
            isBuiltin: false
          });
        }
        changed = true;
      }

      if (changed) {
        this.savePersistedConfig();
        this.bus.emit('library:changed');
      }
      localStorage.removeItem('arestrix_github_skills_repos');
    } catch {
      // Migração é best-effort; dados legados inválidos não impedem o Studio.
    }
  }

  private loadPersistedConfig(): void {
    if (typeof localStorage === 'undefined') return;
    try {
      const saved = localStorage.getItem(STORAGE_KEY) || localStorage.getItem(LEGACY_STORAGE_KEY);
      if (!saved) {
        this.migrateLegacyGithubSources();
        return;
      }
      const parsed = JSON.parse(saved);

      // v1 era um array apenas de bibliotecas; v2 é um objeto com assets.
      const rawLibraries = Array.isArray(parsed) ? parsed : parsed?.libraries;
      const rawAssets = Array.isArray(parsed) ? [] : parsed?.assets;
      const rawSources = Array.isArray(parsed) ? [] : parsed?.sources;
      if (!Array.isArray(rawLibraries)) {
        this.migrateLegacyGithubSources();
        return;
      }

      this.libraries = rawLibraries
        .filter(entry => entry && typeof entry.name === 'string' && entry.name.trim().length > 0)
        .filter(entry => entry.isBuiltin !== true && entry.id !== BUILTIN_LIBRARY.id)
        .map(entry => this.normalizeLibrary(entry));

      const libraryIds = new Set(['library-arestrix', ...this.libraries.map(library => library.id)]);
      this.assets = Array.isArray(rawAssets)
        ? rawAssets
          .map(asset => ({
            ...asset,
            id: typeof asset?.id === 'string' ? asset.id.replace(/[:]+/g, '-') : asset?.id,
            derivedFrom: asset?.derivedFrom
              ? {
                  ...asset.derivedFrom,
                  assetId: String(asset.derivedFrom.assetId || '').replace(/[:]+/g, '-')
                }
              : undefined
          }))
          .filter(asset => this.isValidAsset(asset) && libraryIds.has(asset.libraryId))
        : [];

      this.sources = Array.isArray(rawSources)
        ? rawSources.filter(source => source && typeof source.id === 'string' && typeof source.name === 'string')
        : [];

      if (!localStorage.getItem(STORAGE_KEY)) {
        this.savePersistedConfig();
      }
      this.migrateLegacyGithubSources();
    } catch {
      this.libraries = [];
      this.assets = [];
      this.sources = [];
      this.migrateLegacyGithubSources();
    }
  }

  private savePersistedConfig(): void {
    if (typeof localStorage === 'undefined') return;
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(this.snapshot()));
    } catch {
      // Persistência local é opcional; o LibraryStore nativo complementa.
    }
  }
}
