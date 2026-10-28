import { globalEventBus } from '../../core/EventBus.ts';
import { Graph } from '../../core/Graph.ts';
import { ProjectManager } from '../../core/ProjectManager.ts';
import { WorkspaceManager } from '../../core/workspace/WorkspaceManager.ts';
import { DEFAULT_PIPELINE_DEFAULTS } from '../../core/types.ts';
import { LibraryRegistry, type LibraryScope } from '../../core/libraries/LibraryRegistry.ts';
import { LibraryService } from '../../core/libraries/LibraryService.ts';
import { ProjectLibraryStore } from '../../core/libraries/ProjectLibraryStore.ts';
import { setLocale, t } from '../../i18n/index.ts';
import { Viewport } from '../../view/Viewport.ts';
import { applyAppTheme, type AppTheme } from '../../view/AppTheme.ts';
import { canvasBackgroundForAppTheme } from '../../view/CanvasPreferences.ts';
import { ProjectSettingsStore } from '../../core/settings/ProjectSettingsStore.ts';
import { SettingsResolver } from '../../core/settings/SettingsResolver.ts';
import { StudioSettingsStore } from '../../core/settings/StudioSettingsStore.ts';
import { StudioModelStore } from '../../core/settings/StudioModelStore.ts';
import { StudioCredentialsStore } from '../../core/settings/StudioCredentialsStore.ts';
import { WorkspaceSettingsStore } from '../../core/settings/WorkspaceSettingsStore.ts';
import { sanitizeEnvReferences } from '../../core/env/EnvironmentManager.ts';
import type { IProjectSettings, IStudioCredentialsSnapshot, IStudioModelSettings, IStudioSettings, IWorkspaceSettings } from '../../core/settings/types.ts';
import { PropertiesWindow } from './PropertiesWindow.ts';
import { buildPropertiesScopeConfig, type IPropertiesContext } from './PropertiesData.ts';
import type { PropertiesScope } from './types.ts';

/**
 * Controlador das três telas de propriedades.
 *
 * Mantém uma janela por escopo para preservar posição/tamanho e evitar recriar
 * o DOM a cada abertura. A fonte de dados ainda é somente leitura nesta fase.
 */
export class PropertiesController {
  private readonly context: IPropertiesContext;
  private readonly windows = new Map<PropertiesScope, PropertiesWindow>();
  private readonly studioStore = new StudioSettingsStore();
  private readonly studioModelStore = new StudioModelStore();
  private readonly credentialsStore = new StudioCredentialsStore();
  private readonly workspaceStore: WorkspaceSettingsStore;
  private readonly projectStore: ProjectSettingsStore;
  private readonly resolver = new SettingsResolver();
  private readonly libraryService = LibraryService.getInstance();
  private readonly projectLibraryStore = new ProjectLibraryStore();
  private workspaceSettings?: IWorkspaceSettings;

  constructor(
    graph: Graph,
    projectManager: ProjectManager,
    workspaceManager: WorkspaceManager,
    private readonly viewport?: Viewport
  ) {
    this.context = {
      graph,
      projectManager,
      workspaceManager,
      libraryRegistry: LibraryRegistry.getInstance(),
      projectLibraryStore: this.projectLibraryStore,
      saveStudio: values => this.saveStudio(values),
      saveStudioModel: values => this.saveStudioModel(values),
      saveStudioCredentials: values => this.saveStudioCredentials(values),
      saveWorkspace: values => this.saveWorkspace(values),
      saveProject: values => this.saveProject(values),
      saveProjectDefaults: values => this.saveProjectDefaults(values),
      saveProjectEnvironment: values => this.saveProjectEnvironment(values),
      saveLibraries: (scope, values) => this.saveLibraries(scope, values),
      saveProjectLibraries: values => this.saveProjectLibraries(values)
    };
    this.workspaceStore = new WorkspaceSettingsStore(workspaceManager);
    this.projectStore = new ProjectSettingsStore(projectManager, graph);
  }

  public async show(scope: PropertiesScope, sectionId?: string): Promise<void> {
    try {
      await this.libraryService.hydrate('workspace');
    } catch {
      // O arquivo nativo é opcional; o registro local continua disponível.
    }
    const settings = await this.loadSettings();
    const config = buildPropertiesScopeConfig(scope, {
      ...this.context,
      settings,
      canvasBackground: this.viewport?.getCanvasBackground?.()
    });
    let window = this.windows.get(scope);
    if (!window) {
      window = new PropertiesWindow(
        config,
        actionId => this.handleAction(actionId),
        async sectionId => {
          if (sectionId === 'libraries') {
            await this.show(scope, 'libraries');
          }
        }
      );
      this.windows.set(scope, window);
    } else {
      window.setScopeConfig(config);
    }
    window.open(sectionId);
  }

  private async saveStudio(values: Record<string, string>): Promise<boolean> {
    const current = await this.studioStore.load();
    const language = values.language === 'en-US' ? 'en-US' : 'pt-BR';
    const requestedCanvas = values.canvasBackground === 'dark' ? 'dark' : 'light';
    const theme: AppTheme = values.theme === 'dark' || values.theme === 'system'
      ? values.theme
      : 'light';

    const previousLanguage = current.language;
    const previousCanvas = current.appearance.canvasBackground;
    const previousTheme = current.appearance.theme;
    const themeChanged = theme !== previousTheme;
    const canvasBackground = themeChanged
      ? canvasBackgroundForAppTheme(theme)
      : requestedCanvas;
    current.language = language;
    current.appearance.canvasBackground = canvasBackground;
    current.appearance.theme = theme;

    const saved = await this.studioStore.save(current);
    if (!saved) return false;

    if (language !== previousLanguage) {
      await setLocale(language);
    }
    if (canvasBackground !== previousCanvas) {
      this.viewport?.setCanvasBackground(canvasBackground);
    }
    if (theme !== previousTheme) {
      applyAppTheme(theme);
    }
    return true;
  }

  private async saveStudioModel(values: Record<string, string>): Promise<boolean> {
    const settings: IStudioModelSettings = {
      provider: values.provider || 'auto',
      model: values.model || 'auto',
      baseUrl: values.baseUrl || ''
    };
    return this.studioModelStore.save(settings);
  }

  private async saveStudioCredentials(values: Record<string, string>): Promise<boolean> {
    const apiKeys: Record<string, string> = {};
    for (const [key, value] of Object.entries(values)) {
      if (key.startsWith('apiKey_')) {
        apiKeys[key.slice('apiKey_'.length)] = value;
      }
    }
    return this.credentialsStore.save({
      gatewayUrl: values.gatewayUrl || '',
      gatewayKey: values.gatewayKey || '',
      gatewayRequire: values.gatewayRequire === 'true',
      apiKeys
    });
  }

  private async saveWorkspace(values: Record<string, string>): Promise<boolean> {
    // Uma janela pode salvar sandbox + segurança no mesmo clique. Reaproveita o
    // snapshot carregado para que a segunda seção não desfaça a primeira caso o
    // backend nativo ainda devolva o arquivo anterior.
    const settings = this.workspaceSettings || await this.workspaceStore.load();
    settings.defaults.sandbox = {
      ...settings.defaults.sandbox,
      isolationLevel: (values.sandboxIsolation || settings.defaults.sandbox?.isolationLevel || 'container') as any,
      network: (values.sandboxNetwork || settings.defaults.sandbox?.network || 'egress_llm_only') as any,
      maxMemoryMB: Math.max(64, Number(values.sandboxMemory) || settings.defaults.sandbox?.maxMemoryMB || 512),
      timeoutSeconds: Math.max(10, Number(values.sandboxTimeout) || settings.defaults.sandbox?.timeoutSeconds || 300)
    } as any;
    settings.security = {
      autoVerifySkills: values.autoVerifySkills === 'true',
      strictSecurityMode: values.strictSecurityMode === 'true'
    };
    const saved = await this.workspaceStore.save(settings);
    if (saved) this.workspaceSettings = settings;
    return saved;
  }

  private async saveProject(values: Record<string, string>): Promise<boolean> {
    const current = this.context.projectManager.getCurrentProject();
    const database = current.database?.type === 'postgres'
      ? current.database
      : {
          type: 'sqlite' as const,
          filePath: values.databasePath?.trim() || current.database?.filePath || 'storage/database.sqlite'
        };
    return this.projectStore.saveMetadata({
      name: values.name?.trim() || current.name,
      version: values.version?.trim() || current.version,
      description: values.description ?? current.description,
      outputDir: values.outputDir?.trim() || current.outputDir || 'Output',
      database
    });
  }

  private async saveProjectDefaults(values: Record<string, string>): Promise<boolean> {
    const tools = ['write', 'edit', 'read', 'bash', 'validate_svg']
      .filter(tool => values[`tool_${tool}`] === 'true');
    const memory = {
      enabled: values.memoryEnabled === 'true',
      persistent: values.memoryPersistent === 'true'
    };
    return this.projectStore.saveDefaults({
      model: {
        provider: (values.provider || 'deepseek') as any,
        model: values.model?.trim() || 'deepseek-flash'
      } as any,
      sandbox: {
        isolationLevel: (values.isolationLevel || 'container') as any,
        network: (values.network || 'egress_llm_only') as any,
        maxMemoryMB: Math.max(64, Number(values.maxMemoryMB) || 512),
        timeoutSeconds: Math.max(10, Number(values.timeoutSeconds) || 300)
      } as any,
      codeMode: {
        allowedTools: tools as any
      } as any,
      memory: memory as any
    });
  }

  private async saveProjectEnvironment(values: Record<string, string>): Promise<boolean> {
    const parsed: Record<string, string> = {};
    for (const line of (values.environment || '').split(/\r?\n/)) {
      const trimmed = line.trim();
      if (!trimmed || trimmed.startsWith('#')) continue;
      const index = trimmed.indexOf('=');
      if (index <= 0) continue;
      const key = trimmed.slice(0, index).trim();
      const value = trimmed.slice(index + 1).trim();
      if (key) parsed[key] = value;
    }
    return this.projectStore.saveMetadata({ environment: sanitizeEnvReferences(parsed) });
  }

  private async saveProjectLibraries(values: Record<string, string>): Promise<boolean> {
    const manifest = this.context.projectManager.getCurrentProject();
    const enabledLibraryIds: string[] = [];
    const pinnedAssetIds: string[] = [];

    for (const [key, value] of Object.entries(values)) {
      if (key.startsWith('libraryEnabled_') && value === 'true') {
        enabledLibraryIds.push(key.slice('libraryEnabled_'.length));
      }
      if (key.startsWith('assetPinned_') && value === 'true') {
        pinnedAssetIds.push(key.slice('assetPinned_'.length));
      }
    }

    // A biblioteca embutida é sempre habilitada.
    if (!enabledLibraryIds.includes('library-arestrix')) {
      enabledLibraryIds.push('library-arestrix');
    }

    const enabled = new Set(enabledLibraryIds);
    const filteredPins = pinnedAssetIds.filter(assetId => {
      const asset = this.context.libraryRegistry?.getAsset(assetId);
      return Boolean(asset && enabled.has(asset.libraryId));
    });

    const ok = this.projectLibraryStore.save(manifest.projectId, {
      enabledLibraryIds,
      pinnedAssetIds: filteredPins
    });
    if (ok) globalEventBus.emit('library:changed');
    return ok;
  }

  /**
   * Cria uma biblioteca local e/ou registra uma fonte de repositório a partir
   * dos campos da seção Bibliotecas. As duas operações são independentes:
   * preencher só o nome cria uma biblioteca vazia; preencher só o repositório
   * cadastra a fonte; preencher os dois cria uma biblioteca ligada à fonte.
   */
  private async saveLibraries(scope: LibraryScope, values: Record<string, string>): Promise<boolean> {
    const libraryName = values.libraryName?.trim() || '';
    const repoPath = values.repoPath?.trim() || '';
    if (!libraryName && !repoPath) return false;

    const libraryRegistry = this.context.libraryRegistry;

    if (repoPath) {
      const created = await this.libraryService.createGitHubSource({
        repo: repoPath,
        ref: values.repoRef?.trim() || 'main',
        name: libraryName || values.repoName,
        scope,
        licenseAllowlist: values.repoLicense && values.repoLicense !== 'permissive'
          ? [values.repoLicense]
          : undefined
      });
      if (!created.ok) return false;
      globalEventBus.emit('library:changed');
      await this.libraryService.persist('workspace');
      return true;
    }

    if (libraryName && libraryRegistry) {
      const duplicate = (libraryRegistry.list(scope) || []).some(library =>
        library.name.trim().toLowerCase() === libraryName.toLowerCase()
      );
      if (duplicate) return false;
      const created = libraryRegistry.create({
        name: libraryName,
        description: values.libraryDescription,
        scope,
        kind: 'local'
      });
      if (!created) return false;
      globalEventBus.emit('library:changed');
      await this.libraryService.persist('workspace');
      return true;
    }

    return false;
  }

  private async loadSettings(): Promise<IPropertiesContext['settings']> {
    try {
      const [studio, studioModel, credentials, workspace, project] = await Promise.all([
        this.studioStore.load(),
        this.studioModelStore.load(),
        this.credentialsStore.load(),
        this.workspaceStore.load(),
        this.projectStore.load()
      ]);
      this.workspaceSettings = workspace as IWorkspaceSettings;
      return {
        studio: studio as IStudioSettings,
        studioModel,
        credentials: credentials as IStudioCredentialsSnapshot,
        workspace: workspace as IWorkspaceSettings,
        project: project as IProjectSettings,
        resolved: this.resolver.resolve({ studio, workspace, project })
      };
    } catch {
      return undefined;
    }
  }

  public hide(scope: PropertiesScope): void {
    this.windows.get(scope)?.hide();
  }

  private async rebuildWorkspace(): Promise<void> {
    if (typeof window !== 'undefined' && typeof window.confirm === 'function') {
      const ok = window.confirm(t(
        'properties_window.workspace.rebuild_confirm',
        'Reconstruir .workspace? As skills e MCPs aprovados serão regravados; suas propriedades serão preservadas.'
      ));
      if (!ok) return;
    }
    const snapshot = this.workspaceSettings || await this.workspaceStore.load();
    for (const window of this.windows.values()) {
      window.hide();
    }
    const rebuilt = await this.context.workspaceManager.ensureWorkspaceDirectories();
    const saved = rebuilt ? await this.workspaceStore.save(snapshot) : false;
    if (saved) this.workspaceSettings = snapshot;
    globalEventBus.emit('notification:show', {
      severity: saved ? 'success' : 'error',
      title: t('properties_window.workspace.rebuild_title', 'Workspace'),
      message: saved
        ? t('properties_window.workspace.rebuild_success', 'Estrutura .workspace reconstruída e propriedades preservadas.')
        : t('properties_window.workspace.rebuild_failed', 'Não foi possível reconstruir o .workspace.')
    });
    await this.show('workspace', 'advanced');
  }

  private async resetWorkspace(): Promise<void> {
    if (typeof window !== 'undefined' && typeof window.confirm === 'function') {
      const ok = window.confirm(t(
        'properties_window.workspace.reset_confirm',
        'Restaurar sandbox e auditoria do Workspace para os padrões? Os demais escopos não serão alterados.'
      ));
      if (!ok) return;
    }
    const current = this.workspaceSettings || await this.workspaceStore.load();
    current.defaults = { sandbox: { ...DEFAULT_PIPELINE_DEFAULTS.sandbox } };
    current.security = { autoVerifySkills: true, strictSecurityMode: true };
    for (const window of this.windows.values()) {
      window.hide();
    }
    const saved = await this.workspaceStore.save(current);
    if (saved) this.workspaceSettings = current;
    globalEventBus.emit('notification:show', {
      severity: saved ? 'success' : 'error',
      title: t('properties_window.workspace.reset_title', 'Workspace'),
      message: saved
        ? t('properties_window.workspace.reset_success', 'Padrões do Workspace restaurados.')
        : t('properties_window.workspace.reset_failed', 'Não foi possível restaurar os padrões do Workspace.')
    });
    await this.show('workspace', 'advanced');
  }

  private async resetStudioPreferences(): Promise<void> {
    if (typeof window !== 'undefined' && typeof window.confirm === 'function') {
      const ok = window.confirm(t(
        'properties_window.studio.reset_confirm',
        'Restaurar aparência e preferências do Studio? Idioma, modelo e credenciais serão preservados.'
      ));
      if (!ok) return;
    }
    const current = await this.studioStore.load();
    current.appearance = { ...current.appearance, theme: 'light', canvasBackground: 'light' };
    current.defaults = {};
    for (const window of this.windows.values()) {
      window.hide();
    }
    const saved = await this.studioStore.save(current);
    if (saved) {
      this.viewport?.setCanvasBackground('light');
      applyAppTheme('light');
    }
    globalEventBus.emit('notification:show', {
      severity: saved ? 'success' : 'error',
      title: t('properties_window.studio.reset_title', 'Studio'),
      message: saved
        ? t('properties_window.studio.reset_success', 'Preferências do Studio restauradas.')
        : t('properties_window.studio.reset_failed', 'Não foi possível restaurar as preferências do Studio.')
    });
    await this.show('studio', 'advanced');
  }

  private async importBuiltinLibrary(): Promise<void> {
    const result = await this.libraryService.importBuiltinSkills();
    this.notifyLibraryImport(
      t('properties_window.libraries.scope_studio', 'Studio'),
      result.imported.length,
      result.rejected.length
    );
    await this.show('studio', 'libraries');
  }

  private async importWorkspaceLibrary(): Promise<void> {
    const result = await this.libraryService.importWorkspaceCatalog();
    this.notifyLibraryImport(
      t('properties_window.libraries.scope_workspace', 'Workspace'),
      result.imported.length,
      result.rejected.length
    );
    await this.show('workspace', 'libraries');
  }

  private notifyLibraryImport(scope: string, imported: number, rejected: number): void {
    globalEventBus.emit('notification:show', {
      severity: rejected > 0 ? 'warning' : 'success',
      title: t('properties_window.libraries.import_title', 'Bibliotecas'),
      message: rejected > 0
        ? t('properties_window.libraries.import_result_warning', '{{scope}}: {{imported}} assets importados e auditados; {{rejected}} rejeitados pelo Skill Guard.', { scope, imported, rejected })
        : t('properties_window.libraries.import_result', '{{scope}}: {{imported}} assets importados e auditados.', { scope, imported })
    });
  }

  private handleAction(actionId: string): void {
    if (actionId === 'studio.reset-preferences') {
      void this.resetStudioPreferences();
      return;
    }
    if (actionId === 'workspace.rebuild') {
      void this.rebuildWorkspace();
      return;
    }
    if (actionId === 'workspace.reset') {
      void this.resetWorkspace();
      return;
    }
    if (actionId === 'libraries.import-builtin') {
      void this.importBuiltinLibrary();
      return;
    }
    if (actionId === 'libraries.import-workspace') {
      void this.importWorkspaceLibrary();
      return;
    }

    // As ações abrem um editor especializado. Escondemos as propriedades para
    // que o editor não fique atrás da janela de propriedades (mesmo z-index).
    for (const window of this.windows.values()) {
      window.hide();
    }

    switch (actionId) {
      case 'studio.open-defaults':
      case 'project.open-defaults':
        void this.show('project', 'defaults');
        return;
      case 'project.open-local-models':
        globalEventBus.emit('modal:open-local-models');
        return;
      case 'project.open-http-policy':
        globalEventBus.emit('modal:open-global-tools');
        return;
      case 'project.open-memory-advanced':
        globalEventBus.emit('modal:open-global-memory');
        return;
      case 'studio.open-libraries':
      case 'studio.open-github':
        void this.show('studio', 'libraries');
        return;
      case 'workspace.open-libraries':
      case 'workspace.open-github':
        void this.show('workspace', 'libraries');
        return;
      case 'studio.open-git':
      case 'project.open-git':
        globalEventBus.emit('modal:open-git-account');
        return;
      case 'workspace.open-mcp':
        globalEventBus.emit('modal:open-mcp-servers');
        return;
      case 'project.open-manager':
        globalEventBus.emit('modal:open-project-manager', 'settings');
        return;
      case 'project.open-runner':
        globalEventBus.emit('runner:open-modal');
        return;
      default:
        return;
    }
  }
}
