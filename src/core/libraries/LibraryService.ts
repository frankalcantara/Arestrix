import type { IMcpServerConfig, INodeSkill } from '../types.ts';
import { sanitizeEnvReferences } from '../env/EnvironmentManager.ts';
import { BUILTIN_LIBRARY_SKILLS } from './BuiltinLibraryCatalog.ts';
import { WorkspaceManager } from '../workspace/WorkspaceManager.ts';
import type { IWorkspaceApprovedSkill } from '../workspace/types.ts';
import { LibraryRegistry } from './LibraryRegistry.ts';
import { LibraryAuditGate } from './LibraryAuditGate.ts';
import { LibraryStore } from './LibraryStore.ts';
import { GitHubSourceAdapter } from './sources/GitHubSourceAdapter.ts';
import { sha256 } from '../hash.ts';
import {
  builtinSkillToAsset,
  createSkillAsset,
  workspaceSkillToAsset
} from './adapters/skillAssetAdapter.ts';
import {
  createMcpAsset,
  mcpAssetToServerConfig,
  workspaceMcpToAsset
} from './adapters/mcpAssetAdapter.ts';
import {
  createTemplateAsset,
  serializedTemplateToAssetPayload,
  templateAssetId
} from './adapters/templateAssetAdapter.ts';
import type { ISerializedTemplatePlugin } from '../templates/AgentTemplateTypes.ts';
import {
  assetPayloadToMcpConfig,
  MCP_AUDIT_POLICY_VERSION,
  mcpConfigToAssetPayload
} from './types.ts';
import type {
  ILibraryAsset,
  ILibraryAssetAudit,
  ILibraryAssetPayload,
  ILibraryCloneResult,
  ILibraryDownloadResult,
  ILibraryEntry,
  ILibraryImportResult,
  ILibraryInstallResult,
  ILibrarySnapshot,
  ILibrarySource,
  ILibrarySyncResult,
  ISkillAssetPayload,
  LibraryScope
} from './types.ts';

const BUILTIN_LIBRARY_ID = 'library-arestrix';
const WORKSPACE_LIBRARY_NAME = 'Arestrix Workspace';
const OWN_LIBRARY_NAME = 'Minha biblioteca';

/**
 * Arestrix Studio - Serviço de Bibliotecas
 *
 * Orquestra importação, auditoria, derivação e instalação de assets. Toda
 * mutação passa pelo `LibraryAuditGate`; assets rejeitados nunca são
 * persistidos. A sincronização remota (GitHub) não faz parte desta fase:
 * os blobs serão baixados sob demanda a partir da Fase 5.
 */
export class LibraryService {
  private static instance: LibraryService;

  constructor(
    private readonly registry: LibraryRegistry = LibraryRegistry.getInstance(),
    private readonly workspace: WorkspaceManager = WorkspaceManager.getInstance(),
    private readonly gate: LibraryAuditGate = LibraryAuditGate.getInstance(),
    private readonly store: LibraryStore = new LibraryStore(workspace),
    private readonly githubSource: GitHubSourceAdapter = GitHubSourceAdapter.getInstance()
  ) {}

  public static getInstance(): LibraryService {
    if (!LibraryService.instance) {
      LibraryService.instance = new LibraryService();
    }
    return LibraryService.instance;
  }

  public async hydrate(scope: LibraryScope = 'workspace', projectId?: string): Promise<boolean> {
    const state = await this.store.load(scope, projectId);
    if (!state) return false;
    this.registry.restore(state, false);
    return true;
  }

  public async persist(scope: LibraryScope = 'workspace', projectId?: string): Promise<boolean> {
    return this.store.save(scope, this.registry.snapshot(), projectId);
  }

  public listAssets(libraryId?: string): ILibraryAsset[] {
    return this.registry.listAssets(libraryId);
  }

  public getAsset(assetId: string): ILibraryAsset | undefined {
    return this.registry.getAsset(assetId);
  }

  /** Asset de origem de um snapshot de vértice, quando houver `derivedFrom`. */
  public getAssetForSkill(skill: INodeSkill): ILibraryAsset | undefined {
    if (!skill.derivedFrom?.assetId) return undefined;
    return this.registry.getAsset(skill.derivedFrom.assetId);
  }

  /** True quando o conteúdo da biblioteca mudou desde que o snapshot foi criado. */
  public hasLibraryUpdate(skill: INodeSkill): boolean {
    const asset = this.getAssetForSkill(skill);
    if (!asset || !skill.derivedFrom) return false;
    return asset.audit.hash !== skill.derivedFrom.hash;
  }

  /**
   * Importa as skills da biblioteca embutida para uma biblioteca. A auditoria
   * acontece antes do upsert; REJECT nunca entra no registro.
   */
  public async importBuiltinSkills(options: {
    libraryId?: string;
    skillIds?: string[];
    allowCaution?: boolean;
    strict?: boolean;
  } = {}): Promise<ILibraryImportResult> {
    const libraryId = options.libraryId || BUILTIN_LIBRARY_ID;
    if (!this.registry.get(libraryId)) {
      return {
        imported: [],
        rejected: [{ id: libraryId, name: libraryId, verdict: 'PENDING', reasons: ['Biblioteca não encontrada.'] }]
      };
    }
    const requested = options.skillIds ? new Set(options.skillIds) : null;
    const skills = BUILTIN_LIBRARY_SKILLS.filter(skill => !requested || requested.has(skill.id));
    const result: ILibraryImportResult = { imported: [], rejected: [] };

    for (const item of skills) {
      const audit = this.gate.auditSkill({
        id: item.id,
        name: item.name,
        description: item.description,
        markdownContent: item.markdownContent,
        systemPrompt: item.systemPrompt,
        tools: item.tools,
        license: item.license
      });
      const decision = this.gate.canInstall(audit, {
        allowCaution: options.allowCaution ?? true,
        strict: options.strict ?? false
      });
      if (!decision.ok) {
        result.rejected.push({
          id: item.id,
          name: item.name,
          verdict: audit.verdict,
          reasons: [...audit.findings, ...audit.warnings, decision.reason || ''].filter(Boolean)
        });
        continue;
      }

      const saved = this.registry.upsertAsset(builtinSkillToAsset(item, libraryId, audit));
      if (saved) result.imported.push(saved);
    }

    await this.persist('workspace');
    return result;
  }

  /**
   * Importa o catálogo pré-aprovado do Workspace (skills e MCPs) para uma
   * biblioteca de Workspace. O catálogo é homologado, mas ainda passa pelo
   * gate para registrar hash, política e veredito.
   */
  public async importWorkspaceCatalog(options: {
    libraryId?: string;
    allowCaution?: boolean;
    strict?: boolean;
  } = {}): Promise<ILibraryImportResult> {
    const library = options.libraryId
      ? this.registry.get(options.libraryId)
      : this.ensureWorkspaceLibrary();
    if (!library) return { imported: [], rejected: [] };

    const result: ILibraryImportResult = { imported: [], rejected: [] };

    for (const item of this.workspace.getApprovedSkills()) {
      const audit = this.gate.auditSkill(this.workspaceSkillToNodeSkill(item), true);
      const decision = this.gate.canInstall(audit, {
        allowCaution: options.allowCaution ?? true,
        strict: options.strict ?? false
      });
      if (!decision.ok) {
        result.rejected.push({
          id: item.id,
          name: item.name,
          verdict: audit.verdict,
          reasons: [...audit.findings, ...audit.warnings, decision.reason || ''].filter(Boolean)
        });
        continue;
      }
      const asset = workspaceSkillToAsset(item, library.id, audit);
      asset.status = 'installed';
      const saved = this.registry.upsertAsset(asset);
      if (saved) result.imported.push(saved);
    }

    for (const item of this.workspace.getApprovedMcps()) {
      const config: IMcpServerConfig = {
        id: item.id,
        name: item.name,
        transport: item.transport,
        command: item.command,
        args: [...item.args],
        isEnabled: item.isPreApproved,
        cachedTools: item.tools.map(tool => ({
          name: tool.name,
          description: tool.description,
          serverId: item.id
        }))
      };
      const audit = this.gate.auditMcp(config, true);
      const decision = this.gate.canInstall(audit, {
        allowCaution: options.allowCaution ?? true,
        strict: options.strict ?? false
      });
      if (!decision.ok) {
        result.rejected.push({
          id: item.id,
          name: item.name,
          verdict: audit.verdict,
          reasons: [...audit.findings, ...audit.warnings, decision.reason || ''].filter(Boolean)
        });
        continue;
      }
      const asset = workspaceMcpToAsset(item, library.id, audit);
      asset.status = 'installed';
      const saved = this.registry.upsertAsset(asset);
      if (saved) result.imported.push(saved);
    }

    await this.persist('workspace');
    return result;
  }

  /**
   * Cria um derivado editável em "Minha biblioteca" a partir de um asset.
   * O asset de origem permanece imutável.
   */
  public async deriveAsset(
    sourceAssetId: string,
    options: {
      libraryId?: string;
      name?: string;
      payload?: Partial<ILibraryAssetPayload>;
    } = {}
  ): Promise<ILibraryAsset | null> {
    const source = this.registry.getAsset(sourceAssetId);
    if (!source) return null;

    const sourceLibrary = this.registry.get(source.libraryId);
    const library = options.libraryId
      ? this.registry.get(options.libraryId)
      : this.ensureOwnLibrary(sourceLibrary?.scope === 'studio' ? 'studio' : 'workspace');
    if (!library) return null;

    const name = options.name?.trim() || `${source.name} (cópia)`;
    const id = this.buildDerivedId(library.id, source, name);
    const payload = this.mergePayload(source.payload, options.payload);
    const audit = this.auditPayload(id, name, source, payload);
    const decision = this.gate.canInstall(audit, { allowCaution: true, strict: false });
    if (!decision.ok) return null;

    const base = {
      id,
      libraryId: library.id,
      name,
      description: source.description,
      category: source.category,
      version: source.version,
      license: source.license,
      audit,
      provenance: {
        ...source.provenance,
        authority: 'user',
        importedAt: new Date().toISOString()
      },
      derivedFrom: {
        libraryId: source.libraryId,
        assetId: source.id,
        hash: source.audit.hash,
        policyVersion: source.audit.policyVersion,
        originName: source.name
      }
    };

    const asset = this.createAssetForPayload(base, payload);

    const saved = this.registry.upsertAsset({ ...asset, status: 'available' });
    if (!saved) return null;
    await this.persist('workspace');
    return saved;
  }

  /**
   * Atualiza o conteúdo de um derivado. Assets de biblioteca são imutáveis:
   * para editar um asset original, use `deriveAsset` primeiro.
   */
  public async updateDerivedAsset(
    assetId: string,
    payloadPatch: Partial<ILibraryAssetPayload>
  ): Promise<{ ok: boolean; asset?: ILibraryAsset; reason?: string }> {
    const current = this.registry.getAsset(assetId);
    if (!current) return { ok: false, reason: 'Asset não encontrado.' };
    if (!current.derivedFrom) {
      return { ok: false, reason: 'Assets de biblioteca são imutáveis. Crie um derivado para editar.' };
    }

    const payload = this.mergePayload(current.payload, payloadPatch);
    const audit = this.auditPayload(current.id, current.name, current, payload);
    const decision = this.gate.canInstall(audit, { allowCaution: true, strict: false });
    if (!decision.ok) return { ok: false, reason: decision.reason };

    const updated: ILibraryAsset = {
      ...current,
      payload,
      audit,
      updatedAt: new Date().toISOString()
    };
    const saved = this.registry.upsertAsset(updated);
    if (!saved) return { ok: false, reason: 'Não foi possível salvar o derivado.' };
    await this.persist('workspace');
    return { ok: true, asset: saved };
  }

  /** Reaudita um asset existente; REJECT o marca como desabilitado. */
  public async reauditAsset(assetId: string): Promise<{ ok: boolean; asset?: ILibraryAsset; reason?: string }> {
    const current = this.registry.getAsset(assetId);
    if (!current) return { ok: false, reason: 'Asset não encontrado.' };

    const audit = this.auditPayload(current.id, current.name, current, current.payload);
    const saved = this.registry.upsertAsset({
      ...current,
      audit,
      status: audit.verdict === 'REJECT' ? 'disabled' : current.status,
      updatedAt: new Date().toISOString()
    });
    if (!saved) return { ok: false, reason: 'Não foi possível atualizar a auditoria.' };
    await this.persist('workspace');
    return { ok: true, asset: saved };
  }

  /** Cria uma skill em branco já auditada, pronta para o editor. */
  public async createBlankSkill(options: {
    libraryId?: string;
    scope?: LibraryScope;
    name?: string;
  } = {}): Promise<ILibraryAsset | null> {
    const library = options.libraryId
      ? this.registry.get(options.libraryId)
      : this.ensureOwnLibrary(options.scope || 'workspace');
    if (!library) return null;

    const name = options.name?.trim() || 'Nova habilidade';
    const id = `asset-${library.id}-skill-${Date.now().toString(36)}`;
    const payload: ISkillAssetPayload = {
      kind: 'skill',
      markdownContent: `# ${name}\n\nDescreva as instruções operacionais desta habilidade.\n\n## Diretrizes\n- Seja preciso e objetivo.`,
      systemPrompt: `Você é o agente responsável por "${name}". Execute suas tarefas com precisão técnica.`,
      tools: ['read', 'write']
    };
    const now = new Date().toISOString();
    const audit = this.auditPayload(id, name, {
      id,
      libraryId: library.id,
      kind: 'skill',
      name,
      description: 'Habilidade criada no Arestrix.',
      category: 'Personalizada',
      version: '1.0.0',
      license: 'MIT',
      status: 'available',
      provenance: { sourceKind: 'workspace', sourceId: 'user', authority: 'user', importedAt: now },
      audit: {
        verdict: 'PENDING',
        riskScore: 0,
        policyVersion: '',
        hash: '',
        findings: [],
        warnings: []
      },
      payload,
      createdAt: now,
      updatedAt: now
    }, payload);

    const asset = createSkillAsset({
      id,
      libraryId: library.id,
      name,
      description: 'Habilidade criada no Arestrix.',
      category: 'Personalizada',
      version: '1.0.0',
      license: 'MIT',
      payload,
      audit,
      provenance: { sourceKind: 'workspace', sourceId: 'user', authority: 'user', importedAt: now }
    });
    const saved = this.registry.upsertAsset(asset);
    if (!saved) return null;
    await this.persist('workspace');
    return saved;
  }

  /** Salva um template de vértice do catálogo na biblioteca, com auditoria. */
  public async saveTemplateToLibrary(
    template: ISerializedTemplatePlugin,
    options: { libraryId?: string; scope?: LibraryScope; name?: string } = {}
  ): Promise<ILibraryAsset | null> {
    const library = options.libraryId
      ? this.registry.get(options.libraryId)
      : this.ensureOwnLibrary(options.scope || 'workspace');
    if (!library) return null;

    const audit = this.gate.auditTemplate(template);
    if (audit.verdict === 'REJECT') return null;

    const id = templateAssetId(library.id, template.id || Date.now().toString(36));
    const asset = createTemplateAsset({
      id,
      libraryId: library.id,
      name: options.name?.trim() || template.name || 'Template de vértice',
      description: template.description || 'Template de vértice importado do catálogo.',
      category: template.category || 'Plugins Customizados',
      version: template.version || '1.0.0',
      license: 'MIT',
      payload: serializedTemplateToAssetPayload(template),
      audit,
      provenance: {
        sourceKind: 'workspace',
        sourceId: 'user',
        authority: 'user',
        importedAt: new Date().toISOString()
      }
    });

    const saved = this.registry.upsertAsset(asset);
    if (!saved) return null;
    await this.persist('workspace');
    return saved;
  }

  /** Cria uma fonte GitHub e a biblioteca que receberá seus candidatos. */
  public async createGitHubSource(options: {
    repo: string;
    ref?: string;
    name?: string;
    scope?: LibraryScope;
    licenseAllowlist?: string[];
  }): Promise<{ ok: boolean; source?: ILibrarySource; library?: ILibraryEntry; error?: string; errorCode?: string }> {
    const repo = options.repo?.trim().replace(/^https?:\/\/github\.com\//i, '').replace(/\.git$/i, '');
    const ref = options.ref?.trim() || 'main';
    if (!/^[A-Za-z0-9_.-]+\/[A-Za-z0-9_.-]+$/.test(repo) || repo.includes('..')) {
      return { ok: false, errorCode: 'github_invalid_repo', error: 'Use o formato organizacao/projeto.' };
    }
    if (!ref || ref.includes('..') || ref.includes('\\')) {
      return { ok: false, errorCode: 'github_invalid_ref', error: 'Referência (branch/tag) inválida.' };
    }

    const sourceId = `source-github-${repo.replace(/[^a-zA-Z0-9_-]/g, '-')}-${ref.replace(/[^a-zA-Z0-9_-]/g, '-')}`;
    if (this.registry.getSource(sourceId)) {
      return { ok: false, errorCode: 'github_source_exists', error: 'Esta fonte já está cadastrada.' };
    }

    const scope = options.scope || 'workspace';
    const library = this.registry.create({
      name: options.name?.trim() || repo,
      description: `Biblioteca da fonte GitHub ${repo} (${ref}).`,
      scope,
      kind: 'repository',
      sourceId
    });
    if (!library) {
      return { ok: false, errorCode: 'library_exists', error: 'Já existe uma biblioteca com este nome.' };
    }

    const source: ILibrarySource = {
      id: sourceId,
      kind: 'github',
      scope,
      name: options.name?.trim() || repo,
      enabled: true,
      trust: 'community',
      github: {
        repo,
        ref
      },
      licenseAllowlist: options.licenseAllowlist?.length
        ? [...options.licenseAllowlist]
        : ['MIT', 'Apache-2.0', 'BSD-3-Clause', 'BSD-2-Clause', 'ISC']
    };

    this.registry.upsertSource(source);
    await this.persist('workspace');
    return { ok: true, source, library };
  }

  /**
   * Sincroniza uma fonte GitHub lendo apenas metadados/árvore. Nenhum blob é
   * baixado aqui; os assets entram como PENDING até o download sob demanda.
   */
  public async syncGitHubSource(options: {
    sourceId: string;
    libraryId?: string;
  }): Promise<ILibrarySyncResult> {
    const empty = (errorCode: string, error: string): ILibrarySyncResult => ({
      ok: false,
      error,
      errorCode,
      commit: '',
      license: '',
      added: 0,
      updated: 0,
      removed: 0,
      truncated: false,
      imported: [],
      rejected: []
    });

    const source = this.registry.getSource(options.sourceId);
    if (!source || source.kind !== 'github' || !source.github) {
      return empty('github_source_missing', 'Fonte GitHub não encontrada.');
    }

    const manifest = await this.githubSource.fetchManifest(source.github.repo, source.github.ref);
    if (!manifest.ok || !manifest.commit) {
      return empty(manifest.errorCode || 'github_sync_failed', manifest.error || 'Falha ao sincronizar a fonte GitHub.');
    }

    const license = manifest.license || 'NOASSERTION';
    const allowlist = source.licenseAllowlist?.length
      ? source.licenseAllowlist
      : ['MIT', 'Apache-2.0', 'BSD-3-Clause', 'BSD-2-Clause', 'ISC'];
    if (!allowlist.includes(license)) {
      return empty('github_license_not_allowed', `Licença "${license}" não está na allowlist da fonte.`);
    }

    let library = options.libraryId
      ? this.registry.get(options.libraryId)
      : this.registry.list().find(item => item.sourceId === source.id);
    if (!library) {
      library = this.registry.create({
        name: source.name,
        description: `Biblioteca da fonte GitHub ${source.github.repo}.`,
        scope: source.scope,
        kind: 'repository',
        sourceId: source.id
      }) || undefined;
    }
    if (!library) {
      return empty('library_missing', 'Não foi possível criar a biblioteca da fonte.');
    }

    const now = new Date().toISOString();
    this.registry.upsertSource({
      ...source,
      github: {
        ...source.github,
        commit: manifest.commit,
        licenseSpdx: license
      },
      lastSyncAt: now,
      lastSyncError: undefined
    });

    const existing = this.registry.listAssets(library.id).filter(asset => asset.remote);
    const byPath = new Map(existing.map(asset => [asset.remote!.path, asset]));
    const seen = new Set<string>();
    const imported: ILibraryAsset[] = [];
    let added = 0;
    let updated = 0;

    for (const candidate of manifest.assets || []) {
      if (candidate.kind !== 'skill' && candidate.kind !== 'mcp') continue;
      seen.add(candidate.path);
      const current = byPath.get(candidate.path);

      if (current && current.remote?.blobSha === candidate.sha && current.provenance.commit === manifest.commit) {
        continue;
      }

      const asset: ILibraryAsset = {
        id: current?.id || this.buildGitHubAssetId(library.id, candidate.kind, candidate.path),
        libraryId: library.id,
        kind: candidate.kind,
        name: candidate.name,
        description: `Asset remoto de ${source.github.repo} (${candidate.path}). Baixe para auditar antes de usar.`,
        category: candidate.kind === 'skill' ? 'Remoto' : 'MCP remoto',
        version: '0.0.0',
        license,
        status: 'available',
        provenance: {
          sourceKind: 'github',
          sourceId: source.id,
          repo: source.github.repo,
          path: candidate.path,
          commit: manifest.commit,
          url: `https://github.com/${source.github.repo}/blob/${manifest.commit}/${candidate.path}`,
          authority: 'GitHub',
          importedAt: now
        },
        audit: this.pendingAudit(),
        payload: this.emptyPayloadForKind(candidate.kind),
        remote: {
          path: candidate.path,
          blobSha: candidate.sha,
          size: candidate.size
        },
        createdAt: current?.createdAt || now,
        updatedAt: now
      };

      const saved = this.registry.upsertAsset(asset);
      if (saved) {
        imported.push(saved);
        if (current) updated++;
        else added++;
      }
    }

    let removed = 0;
    for (const asset of existing) {
      if (asset.remote && !seen.has(asset.remote.path)) {
        this.registry.upsertAsset({ ...asset, status: 'disabled', updatedAt: now });
        removed++;
      }
    }

    await this.persist('workspace');
    return {
      ok: true,
      commit: manifest.commit,
      license,
      added,
      updated,
      removed,
      truncated: Boolean(manifest.truncated),
      imported,
      rejected: []
    };
  }

  /**
   * Baixa um blob remoto sob demanda, audita e só então o promove para uso.
   * REJECT descarta o conteúdo baixado.
   */
  public async downloadGitHubAsset(
    assetId: string,
    options: { allowCaution?: boolean; strict?: boolean } = {}
  ): Promise<ILibraryDownloadResult> {
    const asset = this.registry.getAsset(assetId);
    if (!asset?.remote) return { ok: false, reason: 'Asset não é remoto.' };
    const source = this.registry.getSource(asset.provenance.sourceId);
    if (!source?.github) return { ok: false, reason: 'Fonte GitHub não encontrada.' };

    const response = await this.githubSource.fetchAsset(
      source.github.repo,
      source.github.ref,
      asset.remote.path,
      asset.provenance.commit || source.github.commit || ''
    );
    if (!response.ok || typeof response.content !== 'string') {
      return { ok: false, reason: response.error || 'Falha ao baixar o asset.', errorCode: response.errorCode };
    }

    let payload: ILibraryAssetPayload;
    let audit: ILibraryAssetAudit;
    const content = response.content;

    if (asset.kind === 'skill') {
      const tools = this.toolsFromMarkdown(content);
      audit = this.gate.auditSkill({
        id: asset.id,
        name: asset.name,
        description: asset.description,
        markdownContent: content,
        systemPrompt: content,
        tools,
        license: asset.license
      });
      payload = {
        kind: 'skill',
        markdownContent: content,
        systemPrompt: content,
        tools
      };
    } else if (asset.kind === 'mcp') {
      let parsed: any;
      try {
        parsed = JSON.parse(content);
      } catch {
        return { ok: false, reason: 'O arquivo MCP remoto não contém JSON válido.' };
      }
      if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
        return { ok: false, reason: 'O arquivo MCP remoto não contém um objeto de configuração.' };
      }
      const config: IMcpServerConfig = {
        ...parsed,
        id: asset.id,
        name: parsed.name || asset.name,
        isEnabled: true
      };
      audit = this.gate.auditMcp(config);
      payload = mcpConfigToAssetPayload(config);
      if (payload.env) payload.env = sanitizeEnvReferences(payload.env);
      if (payload.headers) payload.headers = sanitizeEnvReferences(payload.headers);
    } else {
      return { ok: false, reason: 'Tipo de asset remoto não suportado.' };
    }

    const decision = this.gate.canInstall(audit, {
      allowCaution: options.allowCaution ?? false,
      strict: options.strict ?? false
    });
    if (!decision.ok) {
      if (audit.verdict === 'CAUTION' && !options.allowCaution) {
        return {
          ok: false,
          needsConfirmation: true,
          audit,
          reason: decision.reason || 'O asset remoto exige confirmação por apresentar alertas.'
        };
      }
      const rejected = this.registry.upsertAsset({
        ...asset,
        audit,
        status: 'disabled',
        payload: this.emptyPayloadForKind(asset.kind),
        updatedAt: new Date().toISOString()
      });
      await this.persist('workspace');
      return {
        ok: false,
        audit,
        asset: rejected || undefined,
        reason: decision.reason || 'Asset rejeitado pelo auditor.'
      };
    }

    const saved = this.registry.upsertAsset({
      ...asset,
      payload,
      audit,
      status: 'available',
      updatedAt: new Date().toISOString()
    });
    if (!saved) return { ok: false, reason: 'Não foi possível salvar o asset baixado.' };
    await this.persist('workspace');
    return { ok: true, asset: saved, audit };
  }

  private emptyPayloadForKind(kind: 'skill' | 'mcp'): ILibraryAssetPayload {
    if (kind === 'skill') {
      return { kind: 'skill', markdownContent: '', systemPrompt: '', tools: [] };
    }
    return { kind: 'mcp', transport: 'stdio', command: '', args: [] };
  }

  private pendingAudit(): ILibraryAssetAudit {
    return {
      verdict: 'PENDING',
      riskScore: 0,
      policyVersion: '',
      hash: '',
      findings: [],
      warnings: []
    };
  }

  private buildGitHubAssetId(libraryId: string, kind: 'skill' | 'mcp', path: string): string {
    const slug = path
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, '-')
      .replace(/^-+|-+$/g, '')
      .slice(0, 60) || kind;
    return `asset-${libraryId}-${kind}-${slug}-${sha256(path).slice(0, 8)}`;
  }

  private toolsFromMarkdown(markdown: string): string[] {
    const frontmatter = markdown.match(/^---\s*\n([\s\S]*?)\n---/);
    if (!frontmatter) return ['read'];
    const lines = frontmatter[1].split(/\r?\n/);
    const tools: string[] = [];
    let inTools = false;
    for (const line of lines) {
      if (/^tools\s*:/i.test(line)) {
        inTools = true;
        continue;
      }
      if (inTools) {
        const item = line.match(/^\s*-\s*([A-Za-z0-9_]+)/);
        if (item) {
          tools.push(item[1]);
        } else if (line.trim() && !/^\s/.test(line)) {
          inTools = false;
        }
      }
    }
    return tools.length > 0 ? Array.from(new Set(tools)) : ['read'];
  }

  /** Clona uma biblioteca inteira, reauditando cada asset. */
  public async cloneLibrary(
    sourceId: string,
    name: string,
    scope?: LibraryScope
  ): Promise<ILibraryCloneResult | null> {
    const source = this.registry.get(sourceId);
    if (!source) return null;

    const library = this.registry.create({
      name,
      description: `Clone de ${source.name}.`,
      scope: scope || source.scope,
      kind: 'local'
    });
    if (!library) return null;

    const result: ILibraryCloneResult = { library, imported: [], rejected: [] };
    const assets = this.registry.listAssets(sourceId);
    const now = new Date().toISOString();

    for (const [index, asset] of assets.entries()) {
      const payload = this.mergePayload(asset.payload);
      const audit = this.auditPayload(asset.id, asset.name, asset, payload);
      const decision = this.gate.canInstall(audit, { allowCaution: true });
      if (!decision.ok) {
        result.rejected.push({
          id: asset.id,
          name: asset.name,
          verdict: audit.verdict,
          reasons: [...audit.findings, ...audit.warnings, decision.reason || ''].filter(Boolean)
        });
        continue;
      }

      const id = `asset-${library.id}-${payload.kind}-${index}-${Date.now().toString(36)}`;
      const base = {
        id,
        libraryId: library.id,
        name: asset.name,
        description: asset.description,
        category: asset.category,
        version: asset.version,
        license: asset.license,
        audit,
        provenance: {
          sourceKind: 'local-folder' as const,
          sourceId: `clone:${sourceId}`,
          authority: 'clone',
          importedAt: now
        },
        derivedFrom: {
          libraryId: sourceId,
          assetId: asset.id,
          hash: asset.audit.hash,
          policyVersion: asset.audit.policyVersion,
          originName: asset.name
        },
        status: 'available' as const
      };

      const created = this.createAssetForPayload(base, payload);
      const saved = this.registry.upsertAsset(created);
      if (saved) result.imported.push(saved);
    }

    await this.persist('workspace');
    return result;
  }

  /** Exporta uma biblioteca em JSON, sem segredos literais. */
  public exportLibrary(libraryId: string): string | null {
    const library = this.registry.get(libraryId);
    if (!library) return null;

    const assets = this.registry.listAssets(libraryId).map(asset => {
      if (asset.payload.kind !== 'mcp') return asset;
      return {
        ...asset,
        payload: {
          ...asset.payload,
          env: asset.payload.env ? sanitizeEnvReferences(asset.payload.env) : undefined,
          headers: asset.payload.headers ? sanitizeEnvReferences(asset.payload.headers) : undefined
        }
      };
    });

    const snapshot: ILibrarySnapshot = {
      format: 'arestrix-library',
      version: 1,
      exportedAt: new Date().toISOString(),
      library,
      assets
    };
    return JSON.stringify(snapshot, null, 2);
  }

  /** Importa uma biblioteca exportada, reauditando todos os assets. */
  public async importLibrary(
    snapshotOrJson: string | ILibrarySnapshot,
    options: { scope?: LibraryScope; name?: string } = {}
  ): Promise<ILibraryCloneResult | null> {
    let snapshot: ILibrarySnapshot;
    try {
      snapshot = typeof snapshotOrJson === 'string'
        ? JSON.parse(snapshotOrJson)
        : snapshotOrJson;
    } catch {
      return null;
    }

    if (!snapshot || snapshot.format !== 'arestrix-library' || !Array.isArray(snapshot.assets)) {
      return null;
    }

    const baseName = options.name?.trim() || `${snapshot.library?.name || 'Biblioteca'} (importada)`;
    const library = this.registry.create({
      name: baseName,
      description: snapshot.library?.description || 'Biblioteca importada de um arquivo.',
      scope: options.scope || 'workspace',
      kind: 'local'
    });
    if (!library) return null;

    const result: ILibraryCloneResult = { library, imported: [], rejected: [] };
    const now = new Date().toISOString();

    for (const [index, asset] of snapshot.assets.entries()) {
      if (!asset || (asset.payload?.kind !== 'skill' && asset.payload?.kind !== 'mcp' && asset.payload?.kind !== 'agent-template')) continue;
      const payload = this.mergePayload(asset.payload);
      const audit = this.auditPayload(asset.id || `import-${index}`, asset.name || 'Asset', asset, payload);
      const decision = this.gate.canInstall(audit, { allowCaution: true });
      if (!decision.ok) {
        result.rejected.push({
          id: asset.id || `import-${index}`,
          name: asset.name || 'Asset',
          verdict: audit.verdict,
          reasons: [...audit.findings, ...audit.warnings, decision.reason || ''].filter(Boolean)
        });
        continue;
      }

      const id = `asset-${library.id}-${payload.kind}-${index}-${Date.now().toString(36)}`;
      const base = {
        id,
        libraryId: library.id,
        name: asset.name || 'Asset importado',
        description: asset.description || '',
        category: asset.category || 'Importado',
        version: asset.version || '1.0.0',
        license: asset.license || 'MIT',
        audit,
        provenance: {
          sourceKind: 'local-folder' as const,
          sourceId: 'import',
          authority: 'import',
          importedAt: now
        },
        derivedFrom: asset.derivedFrom,
        status: 'available' as const
      };

      const created = this.createAssetForPayload(base, payload);
      const saved = this.registry.upsertAsset(created);
      if (saved) result.imported.push(saved);
    }

    await this.persist('workspace');
    return result;
  }

  /**
   * Instala um asset aprovado no Workspace físico (`.workspace/skills` ou
   * `.workspace/mcps`). `REJECT` nunca instala; `CAUTION` exige confirmação.
   */
  public async installAsset(
    assetId: string,
    options: { allowCaution?: boolean; strict?: boolean } = {}
  ): Promise<ILibraryInstallResult> {
    const asset = this.registry.getAsset(assetId);
    if (!asset) return { ok: false, reason: 'Asset não encontrado.' };

    const decision = this.gate.canInstall(asset.audit, {
      allowCaution: options.allowCaution ?? true,
      strict: options.strict ?? false
    });
    if (!decision.ok) return { ok: false, reason: decision.reason };

    const directories = this.workspace.getWorkspaceDirectories();
    let path: string;

    if (asset.payload.kind !== 'skill' && asset.payload.kind !== 'mcp') {
      return { ok: false, reason: 'Templates não são instalados no Workspace; insira o template no grafo.' };
    }

    if (asset.payload.kind === 'skill') {
      const base = `${directories.skills}/${asset.id}`;
      const markdownOk = await this.writeWorkspaceFile(`${base}/SKILL.md`, asset.payload.markdownContent);
      const metadataOk = await this.writeWorkspaceFile(`${base}/skill.json`, JSON.stringify({
        id: asset.id,
        name: asset.name,
        description: asset.description,
        license: asset.license,
        tools: asset.payload.tools,
        systemPrompt: asset.payload.systemPrompt,
        audit: asset.audit,
        provenance: asset.provenance,
        derivedFrom: asset.derivedFrom
      }, null, 2));
      if (!markdownOk || !metadataOk) return { ok: false, reason: 'Não foi possível gravar a skill no Workspace.' };

      this.workspace.registerInstalledSkill(this.toWorkspaceSkill(asset));
      path = base;
    } else {
      const config = mcpAssetToServerConfig(asset);
      if (config.env) config.env = sanitizeEnvReferences(config.env);
      if (config.headers) config.headers = sanitizeEnvReferences(config.headers);
      path = `${directories.mcps}/${asset.id}.json`;
      const ok = await this.writeWorkspaceFile(path, JSON.stringify(config, null, 2));
      if (!ok) return { ok: false, reason: 'Não foi possível gravar o servidor MCP no Workspace.' };
    }

    this.registry.upsertAsset({ ...asset, status: 'installed', updatedAt: new Date().toISOString() });
    await this.persist('workspace');
    return { ok: true, path };
  }

  public ensureWorkspaceLibrary() {
    const existing = this.registry.list('workspace').find(library => library.name === WORKSPACE_LIBRARY_NAME);
    if (existing) return existing;
    return this.registry.create({
      name: WORKSPACE_LIBRARY_NAME,
      description: 'Catálogo do Workspace importado para a biblioteca.',
      scope: 'workspace',
      kind: 'local'
    });
  }

  public ensureOwnLibrary(scope: LibraryScope = 'workspace') {
    const name = scope === 'studio' ? 'Minha biblioteca do Studio' : OWN_LIBRARY_NAME;
    const existing = this.registry.list(scope).find(library => library.name === name);
    if (existing) return existing;
    return this.registry.create({
      name,
      description: 'Derivados e assets criados pelo usuário.',
      scope,
      kind: 'local'
    });
  }

  private workspaceSkillToNodeSkill(item: IWorkspaceApprovedSkill): INodeSkill {
    return {
      id: item.id,
      name: item.name,
      description: item.description,
      markdownContent: item.markdownContent,
      systemPrompt: item.systemPrompt,
      tools: [...item.tools],
      license: item.license
    };
  }

  private toWorkspaceSkill(asset: ILibraryAsset): IWorkspaceApprovedSkill {
    const payload = asset.payload.kind === 'skill' ? asset.payload : { markdownContent: '', systemPrompt: '', tools: [] };
    return {
      id: asset.id,
      name: asset.name,
      category: asset.category,
      description: asset.description,
      license: asset.license,
      tools: [...payload.tools],
      systemPrompt: payload.systemPrompt,
      markdownContent: payload.markdownContent,
      version: asset.version,
      provenance: asset.derivedFrom ? 'custom' : 'community',
      isPreApproved: asset.audit.verdict === 'APPROVE'
    };
  }

  private createAssetForPayload(base: Record<string, any>, payload: ILibraryAssetPayload): ILibraryAsset {
    if (payload.kind === 'skill') return createSkillAsset({ ...(base as any), payload });
    if (payload.kind === 'agent-template') return createTemplateAsset({ ...(base as any), payload });
    return createMcpAsset({ ...(base as any), payload });
  }

  private mergePayload(
    current: ILibraryAssetPayload,
    patch?: Partial<ILibraryAssetPayload>
  ): ILibraryAssetPayload {
    if (current.kind === 'skill') {
      const skillPatch = (patch && patch.kind === 'skill' ? patch : {}) as Partial<typeof current>;
      return {
        kind: 'skill',
        markdownContent: skillPatch.markdownContent ?? current.markdownContent,
        systemPrompt: skillPatch.systemPrompt ?? current.systemPrompt,
        tools: skillPatch.tools ? [...skillPatch.tools] : [...current.tools]
      };
    }

    if (current.kind === 'agent-template') {
      const templatePatch = (patch && patch.kind === 'agent-template' ? patch : {}) as Partial<typeof current>;
      return {
        kind: 'agent-template',
        template: JSON.parse(JSON.stringify(templatePatch.template || current.template))
      };
    }

    const mcpPatch = (patch && patch.kind === 'mcp' ? patch : {}) as Partial<typeof current>;
    return {
      kind: 'mcp',
      transport: mcpPatch.transport ?? current.transport,
      command: mcpPatch.command ?? current.command,
      args: mcpPatch.args ? [...mcpPatch.args] : [...(current.args || [])],
      env: mcpPatch.env ? { ...mcpPatch.env } : (current.env ? { ...current.env } : undefined),
      url: mcpPatch.url ?? current.url,
      headers: mcpPatch.headers ? { ...mcpPatch.headers } : (current.headers ? { ...current.headers } : undefined),
      cachedTools: mcpPatch.cachedTools ? [...mcpPatch.cachedTools] : (current.cachedTools ? [...current.cachedTools] : undefined)
    };
  }

  private auditPayload(
    id: string,
    name: string,
    source: ILibraryAsset,
    payload: ILibraryAssetPayload
  ): ILibraryAssetAudit {
    if (payload.kind === 'skill') {
      return this.gate.auditSkill({
        id,
        name,
        description: source.description,
        markdownContent: payload.markdownContent,
        systemPrompt: payload.systemPrompt,
        tools: payload.tools,
        license: source.license
      });
    }

    if (payload.kind === 'agent-template') {
      return this.gate.auditTemplate(payload.template);
    }

    const config: IMcpServerConfig = assetPayloadToMcpConfig({
      id,
      name,
      description: source.description,
      payload
    });
    return {
      ...this.gate.auditMcp(config),
      policyVersion: MCP_AUDIT_POLICY_VERSION
    };
  }

  private buildDerivedId(libraryId: string, source: ILibraryAsset, name: string): string {
    const slug = name
      .normalize('NFD')
      .replace(/[\u0300-\u036f]/g, '')
      .toLowerCase()
      .replace(/[^a-z0-9]+/g, '-')
      .replace(/^-+|-+$/g, '')
      .slice(0, 32) || source.kind;
    return `asset-${libraryId}-${source.kind}-${slug}-${Date.now().toString(36)}`;
  }

  private async writeWorkspaceFile(filePath: string, content: string): Promise<boolean> {
    const backend = this.backend();
    if (backend) {
      try {
        const response = await backend.call('write_workspace_asset', JSON.stringify({ filePath, content }));
        if (response === true || response === 'true') return true;
      } catch {
        // Fallback local abaixo.
      }
    }

    if (typeof localStorage === 'undefined') return false;
    try {
      const key = 'arestrix_workspace_files_v1';
      const raw = localStorage.getItem(key);
      const files = raw ? JSON.parse(raw) : {};
      files[filePath] = content;
      localStorage.setItem(key, JSON.stringify(files));
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
