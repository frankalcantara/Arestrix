import { Graph } from '../../core/Graph.ts';
import { ProjectManager } from '../../core/ProjectManager.ts';
import { WorkspaceManager } from '../../core/workspace/WorkspaceManager.ts';
import { LibraryRegistry, type LibraryScope } from '../../core/libraries/LibraryRegistry.ts';
import { ProjectLibraryStore } from '../../core/libraries/ProjectLibraryStore.ts';
import { bindLibraryBrowser, renderLibraryBrowser } from '../libraries/LibraryTree.ts';
import { DEFAULT_PIPELINE_DEFAULTS } from '../../core/types.ts';
import { sanitizeEnvReferences } from '../../core/env/EnvironmentManager.ts';
import type { CanvasBackground } from '../../view/CanvasPreferences.ts';
import type { IProjectSettings, IResolvedSettings, IStudioCredentialsSnapshot, IStudioModelSettings, IStudioSettings, IWorkspaceSettings } from '../../core/settings/types.ts';
import { t } from '../../i18n/index.ts';
import type {
  IPropertiesScopeConfig,
  IPropertySection,
  PropertiesScope
} from './types.ts';

export interface IPropertiesContext {
  graph: Graph;
  projectManager: ProjectManager;
  workspaceManager: WorkspaceManager;
  libraryRegistry?: LibraryRegistry;
  projectLibraryStore?: ProjectLibraryStore;
  /** Estado efetivo do canvas, que pode ter sido sincronizado com o tema. */
  canvasBackground?: CanvasBackground;
  settings?: {
    studio: IStudioSettings;
    studioModel: IStudioModelSettings;
    credentials: IStudioCredentialsSnapshot;
    workspace: IWorkspaceSettings;
    project: IProjectSettings;
    resolved: IResolvedSettings;
  };
  saveStudio?: (values: Record<string, string>) => Promise<boolean>;
  saveStudioModel?: (values: Record<string, string>) => Promise<boolean>;
  saveStudioCredentials?: (values: Record<string, string>) => Promise<boolean>;
  saveWorkspace?: (values: Record<string, string>) => Promise<boolean>;
  saveProject?: (values: Record<string, string>) => Promise<boolean>;
  saveProjectDefaults?: (values: Record<string, string>) => Promise<boolean>;
  saveProjectCompression?: (values: Record<string, string>) => Promise<boolean>;
  saveProjectEnvironment?: (values: Record<string, string>) => Promise<boolean>;
  saveLibraries?: (scope: LibraryScope, values: Record<string, string>) => Promise<boolean>;
  saveProjectLibraries?: (values: Record<string, string>) => Promise<boolean>;
}

const hasWindow = typeof window !== 'undefined';

function escapeHtml(value: unknown): string {
  return String(value ?? '')
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
}

function label(key: string, fallback: string): string {
  return t(`properties_window.${key}`, fallback);
}

function toneBadge(text: string, tone: 'neutral' | 'success' | 'warning' | 'accent' | 'danger' = 'neutral'): string {
  return `<span class="properties-badge properties-badge--${tone}">${escapeHtml(text)}</span>`;
}

function code(value: string): string {
  return `<code class="properties-code">${escapeHtml(value)}</code>`;
}

function muted(value: string): string {
  return `<span class="properties-muted">${escapeHtml(value)}</span>`;
}

function row(title: string, valueHtml: string): string {
  return `
    <div class="properties-row">
      <span class="properties-row-label">${escapeHtml(title)}</span>
      <span class="properties-row-value">${valueHtml}</span>
    </div>
  `;
}

function grid(rows: string[]): string {
  return `<div class="properties-grid">${rows.join('')}</div>`;
}

function list(items: string[], emptyText: string): string {
  if (items.length === 0) return `<div class="properties-empty">${escapeHtml(emptyText)}</div>`;
  return `<ul class="properties-list">${items.map(item => `<li>${item}</li>`).join('')}</ul>`;
}

function note(text: string, tone: 'neutral' | 'warning' | 'info' = 'neutral'): string {
  return `<p class="properties-note properties-note--${tone}">${escapeHtml(text)}</p>`;
}

function originBadge(origin: string): string {
  const labelText = label(`origin.${origin}`, origin);
  const tone = origin === 'project' ? 'accent' : origin === 'workspace' ? 'success' : 'neutral';
  return toneBadge(labelText, tone);
}

function section(definition: IPropertySection): IPropertySection {
  return definition;
}

function scopeTitle(scope: PropertiesScope): string {
  switch (scope) {
    case 'studio':
      return label('scope.studio_title', 'Propriedades do Studio');
    case 'workspace':
      return label('scope.workspace_title', 'Propriedades do Workspace');
    default:
      return label('scope.project_title', 'Propriedades do Projeto');
  }
}

function scopeLabel(scope: PropertiesScope): string {
  switch (scope) {
    case 'studio':
      return label('scope.studio', 'Studio');
    case 'workspace':
      return label('scope.workspace', 'Workspace');
    default:
      return label('scope.project', 'Projeto');
  }
}

export function buildPropertiesScopeConfig(
  scope: PropertiesScope,
  context: IPropertiesContext
): IPropertiesScopeConfig {
  switch (scope) {
    case 'studio':
      return buildStudio(context);
    case 'workspace':
      return buildWorkspace(context);
    default:
      return buildProject(context);
  }
}

function buildLibrariesSection(context: IPropertiesContext, scope: LibraryScope): IPropertySection {
  const sources = typeof (context.libraryRegistry as any)?.listSources === 'function'
    ? context.libraryRegistry!.listSources()
    : [];
  const enabledSources = sources.filter(source => source.enabled);
  const pendingSources = sources.filter(source => !source.lastSyncAt);
  const canBrowse = Boolean(
    context.libraryRegistry &&
    typeof (context.libraryRegistry as any).listAssets === 'function' &&
    typeof (context.libraryRegistry as any).getAsset === 'function'
  );

  const sourceItems = sources.map(source => {
    const repo = source.github?.repo || '—';
    const ref = source.github?.ref || 'main';
    const status = !source.enabled
      ? label('libraries.source_disabled', 'Desativada')
      : source.lastSyncAt
        ? label('libraries.source_synced', 'Sincronizada')
        : label('libraries.source_pending', 'Aguardando sincronização');
    return `
      <strong>${escapeHtml(source.name)}</strong>
      <span class="properties-inline-code">${escapeHtml(repo)}</span>
      ${toneBadge(ref, 'neutral')}
      ${toneBadge(source.licenseAllowlist?.[0] || '—', 'neutral')}
      ${toneBadge(status, source.lastSyncAt ? 'success' : source.enabled ? 'warning' : 'neutral')}
      <div class="properties-hint">${escapeHtml(source.github?.commit ? `commit ${source.github.commit.slice(0, 12)}…` : 'Nenhum manifesto sincronizado.')}</div>
    `;
  });

  return {
    id: 'libraries',
    title: label('section.libraries', 'Bibliotecas'),
    description: scope === 'workspace'
      ? label('libraries.workspace_desc', 'Bibliotecas liberadas e criadas para este Workspace. As fontes de repositório são globais do Studio.')
      : label('libraries.studio_desc', 'Fontes de repositório e bibliotecas de assets desta instalação.'),
    icon: 'catalog',
    badge: scope === 'workspace' ? label('badge.workspace', 'Workspace') : label('badge.global', 'Global'),
    content: `
      <div class="properties-library-source-panel">
        <div class="properties-subsection-title">${escapeHtml(label('libraries.sources_title', 'Fontes de repositório'))}</div>
        ${note(label('libraries.phase_note', 'Cadastre a fonte GitHub, sincronize o manifesto/árvore e baixe somente o asset selecionado. O download vai para quarentena e passa por auditoria antes de ser usado.'), 'info')}
        ${grid([
          row(label('libraries.sources', 'Fontes de repositório'), `${escapeHtml(String(enabledSources.length))} / ${escapeHtml(String(sources.length))} ${label('value.active_plural', 'ativas')}`),
          row(label('libraries.pending_sources', 'Aguardando sincronização'), escapeHtml(String(pendingSources.length)))
        ])}
        ${list(sourceItems, label('value.none', 'Nenhum item configurado.'))}
        <div class="properties-subsection-title">${escapeHtml(label('libraries.add_source_title', 'Adicionar fonte de repositório'))}</div>
        <div class="form-row-grid properties-form-grid">
          <div class="form-group">
            <label class="form-label" for="properties-library-repo-path">${escapeHtml(label('libraries.repo_path', 'Repositório'))}</label>
            <input id="properties-library-repo-path" class="form-input properties-code-input" type="text" data-property-input="repoPath" placeholder="organizacao/projeto" />
          </div>
          <div class="form-group">
            <label class="form-label" for="properties-library-repo-ref">${escapeHtml(label('libraries.repo_ref', 'Referência (branch/tag)'))}</label>
            <input id="properties-library-repo-ref" class="form-input properties-code-input" type="text" data-property-input="repoRef" value="main" placeholder="main" />
          </div>
          <div class="form-group">
            <label class="form-label" for="properties-library-repo-name">${escapeHtml(label('libraries.repo_name', 'Nome da fonte'))}</label>
            <input id="properties-library-repo-name" class="form-input" type="text" data-property-input="repoName" placeholder="${escapeHtml(label('libraries.repo_name_placeholder', 'ex: Skills da Comunidade'))}" />
          </div>
          <div class="form-group">
            <label class="form-label" for="properties-library-repo-license">${escapeHtml(label('libraries.repo_license', 'Licença permitida'))}</label>
            <select id="properties-library-repo-license" class="form-select" data-property-input="repoLicense">
              <option value="permissive" selected>${escapeHtml(label('libraries.repo_license_permissive', 'Permissivas (MIT/Apache/BSD)'))}</option>
              ${['MIT', 'Apache-2.0', 'BSD-3-Clause', 'BSD-2-Clause', 'ISC'].map(license => `<option value="${license}">${license}</option>`).join('')}
            </select>
          </div>
          <div class="form-group">
            <label class="form-label" for="properties-library-repo-description">${escapeHtml(label('libraries.repo_description', 'Descrição da fonte'))}</label>
            <input id="properties-library-repo-description" class="form-input" type="text" data-property-input="repoDescription" placeholder="${escapeHtml(label('libraries.repo_description_placeholder', 'O que esta fonte fornece?'))}" />
          </div>
        </div>
        <p class="properties-hint">${escapeHtml(label('libraries.add_source_hint', 'Depois de salvar, selecione a biblioteca criada abaixo e clique em Sincronizar GitHub para ler o manifesto; os assets são baixados e auditados sob demanda.'))}</p>
      </div>

      ${canBrowse ? renderLibraryBrowser({ registry: context.libraryRegistry!, scope }) : ''}

      <div class="properties-subsection-title">${escapeHtml(label('libraries.create_title', 'Criar nova biblioteca local'))}</div>
      <div class="form-row-grid properties-form-grid">
        <div class="form-group">
          <label class="form-label" for="properties-library-name">${escapeHtml(label('libraries.name', 'Nome da biblioteca'))}</label>
          <input id="properties-library-name" class="form-input" type="text" data-property-input="libraryName" placeholder="${escapeHtml(label('libraries.name_placeholder', 'ex: Skills da Equipe de Dados'))}" />
        </div>
        <div class="form-group">
          <label class="form-label" for="properties-library-description">${escapeHtml(label('libraries.description', 'Descrição'))}</label>
          <input id="properties-library-description" class="form-input" type="text" data-property-input="libraryDescription" placeholder="${escapeHtml(label('libraries.description_placeholder', 'Para que serve esta biblioteca?'))}" />
        </div>
      </div>
      <p class="properties-hint">${escapeHtml(label('libraries.create_hint', 'A biblioteca é criada no escopo atual e fica pronta para receber skills e MCPs. Para associá-la a um repositório, use o formulário de fonte acima.'))}</p>
    `,
    save: context.saveLibraries
      ? (values) => context.saveLibraries!(scope, values)
      : undefined,
    keepOpenOnSave: true,
    bind: canBrowse
      ? (root) => bindLibraryBrowser(root, { registry: context.libraryRegistry!, scope })
      : undefined
  };
}

function buildStudio(context: IPropertiesContext): IPropertiesScopeConfig {
  const savedSettings = context.settings?.studio;
  const studioModel = context.settings?.studioModel;
  const credentials = context.settings?.credentials;
  const gateway = credentials?.gateway;
  const canvas = context.canvasBackground
    || savedSettings?.appearance.canvasBackground
    || (hasWindow ? (localStorage.getItem('arestrix_canvas_background') || 'light') : 'light');
  const sources = context.libraryRegistry?.listSources() || [];
  const enabledSources = sources.filter(source => source.enabled);

  return {
    scope: 'studio',
    title: scopeTitle('studio'),
    subtitle: label('scope.studio_subtitle', 'Configurações globais desta instalação'),
    scopeLabel: scopeLabel('studio'),
    readOnlyNote: label('note.read_only', 'Visão geral somente leitura nesta fase. A edição será habilitada na próxima etapa.'),
    sections: [
      section({
        id: 'general',
        title: label('section.general', 'Geral'),
        description: label('studio.general_desc', 'Identidade da instalação e preferências que valem para todos os projetos.'),
        icon: 'settings',
        badge: label('badge.global', 'Global'),
        content: `
          ${grid([
            row(label('label.application', 'Aplicação'), escapeHtml('Arestrix Studio'))
          ])}
          <div class="form-row-grid properties-form-grid">
            <div class="form-group">
              <label class="form-label" for="properties-studio-language">${escapeHtml(label('label.language', 'Idioma'))}</label>
              <select id="properties-studio-language" class="form-select" data-property-input="language">
                <option value="pt-BR" ${savedSettings?.language !== 'en-US' ? 'selected' : ''}>Português (Brasil)</option>
                <option value="en-US" ${savedSettings?.language === 'en-US' ? 'selected' : ''}>English (United States)</option>
              </select>
            </div>
            <div class="form-group">
              <label class="form-label" for="properties-studio-theme">${escapeHtml(label('label.theme', 'Tema'))}</label>
              <select id="properties-studio-theme" class="form-select" data-property-input="theme">
                <option value="light" ${(savedSettings?.appearance.theme || 'light') === 'light' ? 'selected' : ''}>${escapeHtml(label('value.theme_light', 'Claro'))}</option>
                <option value="dark" ${savedSettings?.appearance.theme === 'dark' ? 'selected' : ''}>${escapeHtml(label('value.theme_dark', 'Escuro'))}</option>
                <option value="system" ${savedSettings?.appearance.theme === 'system' ? 'selected' : ''}>${escapeHtml(label('value.theme_system', 'Seguir o sistema'))}</option>
              </select>
            </div>
            <div class="form-group">
              <label class="form-label" for="properties-studio-canvas">${escapeHtml(label('label.canvas', 'Canvas'))}</label>
              <select id="properties-studio-canvas" class="form-select" data-property-input="canvasBackground">
                <option value="light" ${canvas !== 'dark' ? 'selected' : ''}>${escapeHtml(label('value.light', 'Claro'))}</option>
                <option value="dark" ${canvas === 'dark' ? 'selected' : ''}>${escapeHtml(label('value.dark', 'Preto'))}</option>
              </select>
            </div>
          </div>
          <p class="properties-hint">${escapeHtml(label('studio.preferences_hint', 'Idioma, tema e canvas são aplicados imediatamente ao salvar. O tema escuro ainda está em validação visual.'))}</p>
        `,
        save: context.saveStudio
          ? (values) => context.saveStudio!(values)
          : undefined
      }),
      section({
        id: 'studio-model',
        title: label('section.studio_model', 'Modelo do Studio'),
        description: label('studio.model_desc', 'Modelo padrão usado pelo Assistente e pelas ferramentas do Studio.'),
        icon: 'brain',
        badge: label('badge.global', 'Global'),
        content: `
          <div class="properties-form-grid">
            <div class="form-row-grid">
              <div class="form-group">
                <label class="form-label" for="properties-studio-model-provider">${escapeHtml(label('label.provider', 'Provedor'))}</label>
                <select id="properties-studio-model-provider" class="form-select" data-property-input="provider">
                  ${['auto', 'deepseek', 'openai', 'anthropic', 'google', 'mistral', 'groq', 'local', 'jev'].map(provider => `
                    <option value="${provider}" ${(studioModel?.provider || 'auto') === provider ? 'selected' : ''}>${provider === 'auto' ? label('value.auto', 'Automático') : provider}</option>
                  `).join('')}
                </select>
              </div>
              <div class="form-group">
                <label class="form-label" for="properties-studio-model-name">${escapeHtml(label('label.model', 'Modelo'))}</label>
                <input id="properties-studio-model-name" class="form-input" type="text" data-property-input="model" value="${escapeHtml(studioModel?.model || '')}" placeholder="deepseek-flash, claude-opus-5.5, gpt-4o-mini…" />
              </div>
            </div>
            <div class="form-group">
              <label class="form-label" for="properties-studio-model-base-url">${escapeHtml(label('label.base_url', 'Endpoint local / Jev'))}</label>
              <input id="properties-studio-model-base-url" class="form-input properties-code-input" type="text" data-property-input="baseUrl" value="${escapeHtml(studioModel?.baseUrl || '')}" placeholder="http://localhost:11434/v1" />
              <div class="properties-hint">${escapeHtml(label('studio.model_base_hint', 'Usado apenas pelos provedores local e Jev. As chaves de API continuam no ambiente do sistema.'))}</div>
            </div>
            <div class="properties-hint">${escapeHtml(label('studio.model_assistant_hint', 'O Assistente do Studio exige um provedor com chamada de ferramentas. O Jev não oferece esse recurso; num servidor local, o suporte depende do modelo carregado.'))}</div>
          </div>
        `,
        save: context.saveStudioModel
          ? (values) => context.saveStudioModel!(values)
          : undefined
      }),
      section({
        id: 'credentials',
        title: label('section.credentials', 'Credenciais e Gateway'),
        description: label('studio.credentials_desc', 'Chaves de provedores e AI Gateway permanecem no ambiente protegido do sistema.'),
        icon: 'key',
        badge: label('badge.global', 'Global'),
        content: `
          ${note(label('note.secrets', 'Nenhum valor secreto é exibido ou armazenado nesta tela. O Studio lê as chaves do ambiente do Windows e do cofre configurado.'), 'info')}
          <div class="properties-subsection-title">${escapeHtml(label('studio.gateway_title', 'AI Gateway'))}</div>
          <div class="form-row-grid properties-form-grid">
            <div class="form-group">
              <label class="form-label" for="properties-gateway-url">${escapeHtml(label('label.gateway_url', 'URL do gateway'))}</label>
              <input id="properties-gateway-url" class="form-input properties-code-input" type="text" data-property-input="gatewayUrl" value="${escapeHtml(gateway?.url || '')}" placeholder="https://gateway.empresa.com" ${gateway?.locked ? 'disabled' : ''} />
            </div>
            <div class="form-group">
              <label class="form-label" for="properties-gateway-key">${escapeHtml(label('label.gateway_key', 'Chave virtual'))} ${gateway?.keyConfigured ? toneBadge(label('value.configured', 'Configurada'), 'success') : toneBadge(label('value.not_configured', 'Não configurada'), 'warning')}</label>
              <input id="properties-gateway-key" class="form-input" type="password" data-property-input="gatewayKey" placeholder="${escapeHtml(label('studio.gateway_key_placeholder', 'Nova chave (deixe vazio para manter)'))}" autocomplete="new-password" ${gateway?.locked ? 'disabled' : ''} />
            </div>
          </div>
          <label class="properties-check-row">
            <input type="checkbox" data-property-input="gatewayRequire" ${gateway?.requireGateway ? 'checked' : ''} ${gateway?.locked ? 'disabled' : ''} />
            <span>${escapeHtml(label('studio.gateway_require', 'Exigir gateway para provedores hospedados (fail-closed)'))}</span>
          </label>
          ${gateway?.locked ? note(label('studio.gateway_locked', 'O gateway foi bloqueado por política corporativa e não pode ser alterado aqui.'), 'warning') : ''}
          <div class="properties-subsection-title">${escapeHtml(label('studio.provider_keys_title', 'Chaves dos provedores'))}</div>
          <div class="properties-key-list">
            ${(credentials?.providers || []).map(provider => `
              <div class="properties-key-row">
                <div class="properties-key-meta">
                  <strong>${escapeHtml(provider.label)}</strong>
                  ${provider.configured ? toneBadge(label('value.configured', 'Configurada'), 'success') : toneBadge(label('value.not_configured', 'Não configurada'), 'warning')}
                </div>
                <input class="form-input" type="password" data-property-input="apiKey_${provider.id}" placeholder="${escapeHtml(label('studio.key_placeholder', 'Nova chave (deixe vazio para manter)'))}" autocomplete="new-password" />
              </div>
            `).join('')}
          </div>
        `,
        save: context.saveStudioCredentials
          ? (values) => context.saveStudioCredentials!(values)
          : undefined,
        actions: [
          { id: 'studio.open-defaults', label: label('action.open_defaults', 'Abrir configurações globais'), kind: 'primary' }
        ]
      }),
      section({
        id: 'integrations',
        title: label('section.integrations', 'Integrações'),
        description: label('studio.integrations_desc', 'Conta Git, fontes de repositório e bibliotecas de assets.'),
        icon: 'hub',
        badge: label('badge.global', 'Global'),
        content: `
          ${grid([
            row(label('label.github_repositories', 'Fontes de repositório'), `${escapeHtml(String(enabledSources.length))} / ${escapeHtml(String(sources.length))} ${label('value.active_plural', 'ativas')}`),
            row(label('label.libraries', 'Bibliotecas'), escapeHtml(String((context.libraryRegistry?.list('studio') || []).length))),
            row(label('label.git_identity', 'Identidade Git'), toneBadge(label('value.managed_elsewhere', 'Gerenciada em conta Git'), 'neutral'))
          ])}
          ${list(enabledSources.map(source => `${escapeHtml(source.name)} <span class="properties-inline-code">${escapeHtml(source.github?.repo || '—')}</span>`), label('value.none', 'Nenhum item configurado.'))}
        `,
        actions: [
          { id: 'studio.open-libraries', label: label('action.manage_sources', 'Gerenciar fontes GitHub'), kind: 'secondary' },
          { id: 'studio.open-git', label: label('action.manage_git', 'Gerenciar conta Git'), kind: 'secondary' }
        ]
      }),
      buildLibrariesSection(context, 'studio'),
      section({
        id: 'advanced',
        title: label('section.advanced', 'Avançado'),
        description: label('studio.advanced_desc', 'Arquivo de configuração e restauração das preferências do Studio.'),
        icon: 'shield',
        content: `
          ${grid([
            row(label('label.config_file_windows', 'Windows'), code('%APPDATA%\\Arestrix\\studio.config.json')),
            row(label('label.config_file_linux', 'Linux'), code('~/.config/arestrix/studio.config.json')),
            row(label('label.editing', 'Edição'), toneBadge(label('value.editing_enabled', 'Habilitada'), 'success'))
          ])}
          ${note(label('studio.advanced_hint', 'Idioma, canvas e preferências visuais ficam no studio.config.json. Modelo, gateway e chaves continuam no ambiente protegido do sistema.'), 'info')}
        `,
        actions: [
          { id: 'studio.reset-preferences', label: label('action.reset_studio', 'Restaurar preferências'), kind: 'secondary' }
        ]
      })
    ]
  };
}

function buildWorkspace(context: IPropertiesContext): IPropertiesScopeConfig {
  const manager = context.workspaceManager;
  const directories = manager.getWorkspaceDirectories();
  const skills = manager.getApprovedSkills();
  const mcps = manager.getApprovedMcps();
  const provisioned = manager.isWorkspaceProvisioned();
  const workspaceRoot = manager.getWorkspaceRoot();
  const sandbox = context.settings?.workspace.defaults.sandbox || DEFAULT_PIPELINE_DEFAULTS.sandbox;
  const security = context.settings?.workspace.security || {
    autoVerifySkills: true,
    strictSecurityMode: true
  };

  return {
    scope: 'workspace',
    title: scopeTitle('workspace'),
    subtitle: workspaceRoot === '.' ? label('scope.workspace_default', 'Workspace padrão') : workspaceRoot,
    scopeLabel: scopeLabel('workspace'),
    readOnlyNote: label('note.read_only', 'Visão geral somente leitura nesta fase. A edição será habilitada na próxima etapa.'),
    sections: [
      section({
        id: 'general',
        title: label('section.general', 'Geral'),
        description: label('workspace.general_desc', 'Pasta física e infraestrutura compartilhada pelo projeto ativo.'),
        icon: 'folder',
        badge: label('badge.workspace', 'Workspace'),
        content: `
          ${grid([
            row(label('label.status', 'Status'), toneBadge(
              provisioned ? label('value.provisioned', 'Provisionado') : label('value.not_provisioned', 'Não provisionado'),
              provisioned ? 'success' : 'warning'
            )),
            row(label('label.root', 'Raiz'), code(workspaceRoot)),
            row(label('label.skills_dir', 'Skills'), code(directories.skills)),
            row(label('label.mcps_dir', 'MCPs'), code(directories.mcps)),
            row(label('label.agents_dir', 'Agentes'), code(directories.agents)),
            row(label('label.graphs_dir', 'Grafos'), code(`${directories.root}/graphs`))
          ])}
        `
      }),
      section({
        id: 'catalogs',
        title: label('section.catalogs', 'Catálogos'),
        description: label('workspace.catalogs_desc', 'Skills, servidores MCP e bibliotecas de assets aprovados para uso no workspace.'),
        icon: 'catalog',
        badge: label('badge.workspace', 'Workspace'),
        content: `
          ${grid([
            row(label('label.skills', 'Skills aprovadas'), escapeHtml(String(skills.length))),
            row(label('label.mcp_servers', 'Servidores MCP'), escapeHtml(String(mcps.length))),
            row(label('label.libraries', 'Bibliotecas'), escapeHtml(String((context.libraryRegistry?.list('workspace') || []).length)))
          ])}
          <div class="properties-subsection-title">${escapeHtml(label('label.skills', 'Skills aprovadas'))}</div>
          ${list(skills.slice(0, 8).map(skill => `${escapeHtml(skill.name)} ${toneBadge(skill.category, 'neutral')}`), label('value.none', 'Nenhum item configurado.'))}
          <div class="properties-subsection-title">${escapeHtml(label('label.mcp_servers', 'Servidores MCP'))}</div>
          ${list(mcps.slice(0, 8).map(mcp => `${escapeHtml(mcp.name)} ${toneBadge(mcp.transport.toUpperCase(), 'accent')}`), label('value.none', 'Nenhum item configurado.'))}
        `,
        actions: [
          { id: 'workspace.open-libraries', label: label('action.manage_libraries', 'Gerenciar bibliotecas'), kind: 'secondary' },
          { id: 'workspace.open-mcp', label: label('action.manage_mcp', 'Gerenciar servidores MCP'), kind: 'secondary' }
        ]
      }),
      buildLibrariesSection(context, 'workspace'),
      section({
        id: 'sandbox',
        title: label('section.sandbox', 'Sandbox padrão'),
        description: label('workspace.sandbox_desc', 'Política padrão aplicada aos vértices que herdam o sandbox do Workspace.'),
        icon: 'shield',
        badge: label('badge.workspace', 'Workspace'),
        content: `
          <div class="form-row-grid properties-form-grid">
            <div class="form-group">
              <label class="form-label" for="properties-workspace-isolation">${escapeHtml(label('label.isolation', 'Isolamento'))}</label>
              <select id="properties-workspace-isolation" class="form-select" data-property-input="sandboxIsolation">
                ${['container', 'microvm', 'host_direct'].map(level => `<option value="${level}" ${sandbox.isolationLevel === level ? 'selected' : ''}>${level}</option>`).join('')}
              </select>
            </div>
            <div class="form-group">
              <label class="form-label" for="properties-workspace-network">${escapeHtml(label('label.network', 'Rede'))}</label>
              <select id="properties-workspace-network" class="form-select" data-property-input="sandboxNetwork">
                ${['egress_llm_only', 'offline', 'full'].map(network => `<option value="${network}" ${sandbox.network === network ? 'selected' : ''}>${network}</option>`).join('')}
              </select>
            </div>
            <div class="form-group">
              <label class="form-label" for="properties-workspace-memory">${escapeHtml(label('label.memory_limit', 'Memória (MB)'))}</label>
              <input id="properties-workspace-memory" class="form-input" type="number" min="64" step="64" data-property-input="sandboxMemory" value="${sandbox.maxMemoryMB ?? 512}" />
            </div>
            <div class="form-group">
              <label class="form-label" for="properties-workspace-timeout">${escapeHtml(label('label.timeout', 'Timeout (s)'))}</label>
              <input id="properties-workspace-timeout" class="form-input" type="number" min="10" step="10" data-property-input="sandboxTimeout" value="${sandbox.timeoutSeconds ?? 300}" />
            </div>
          </div>
          <p class="properties-hint">${escapeHtml(label('label.origin', 'Origem'))}: ${originBadge('workspace')}</p>
        `,
        save: context.saveWorkspace
          ? (values) => context.saveWorkspace!(values)
          : undefined
      }),
      section({
        id: 'security',
        title: label('section.security', 'Segurança e auditoria'),
        description: label('workspace.security_desc', 'Políticas aplicadas às skills, MCPs e execuções do workspace.'),
        icon: 'shield',
        badge: label('badge.workspace', 'Workspace'),
        content: `
          <div class="properties-check-grid properties-form-grid">
            <label class="properties-check-row">
              <input type="checkbox" data-property-input="autoVerifySkills" ${security.autoVerifySkills ? 'checked' : ''} />
              <span>${escapeHtml(label('label.auto_verify', 'Auto-verificação de skills'))}</span>
            </label>
            <label class="properties-check-row">
              <input type="checkbox" data-property-input="strictSecurityMode" ${security.strictSecurityMode ? 'checked' : ''} />
              <span>${escapeHtml(label('label.strict_mode', 'Modo estrito'))}</span>
            </label>
          </div>
          <div class="properties-grid properties-form-grid">
            ${row(label('label.audit_policy', 'Política'), code('SkillSpector'))}
          </div>
          <p class="properties-hint">${escapeHtml(label('workspace.security_hint', 'Essas políticas valem para o workspace atual e são aplicadas na próxima sincronização.'))}</p>
        `,
        save: context.saveWorkspace
          ? (values) => context.saveWorkspace!(values)
          : undefined
      }),
      section({
        id: 'memory',
        title: label('section.memory', 'Memória do Workspace'),
        description: label('workspace.memory_desc', 'Banco compartilhado usado apenas por promoções explícitas do projeto.'),
        icon: 'database',
        badge: label('badge.workspace', 'Workspace'),
        content: grid([
          row(label('label.database', 'Banco'), code(`${workspaceRoot}/.workspace/memory/memory.db`)),
          row(label('label.status', 'Status'), toneBadge(label('value.on_demand', 'Sob demanda'), 'neutral'))
        ])
      }),
      section({
        id: 'advanced',
        title: label('section.advanced', 'Avançado'),
        description: label('workspace.advanced_desc', 'Manifesto de configuração do workspace.'),
        icon: 'settings',
        content: `
          ${grid([
            row(label('label.config_file', 'Arquivo de configuração'), code(`${directories.root}/workspace.config.json`)),
            row(label('label.sync', 'Sincronização'), toneBadge(label('value.manual', 'Manual'), 'neutral'))
          ])}
          ${note(label('workspace.advanced_hint', 'Reconstruir regrava as skills e MCPs aprovados sem perder as propriedades editadas. Restaurar padrões afeta apenas sandbox e auditoria.'), 'info')}
        `,
        actions: [
          { id: 'workspace.rebuild', label: label('action.rebuild_workspace', 'Reconstruir .workspace'), kind: 'secondary' },
          { id: 'workspace.reset', label: label('action.reset_workspace', 'Restaurar padrões'), kind: 'secondary' }
        ]
      })
    ]
  };
}

function buildProject(context: IPropertiesContext): IPropertiesScopeConfig {
  const manifest = context.projectManager.getCurrentProject();
  const defaults = context.graph.getGlobalDefaults();
  const resolved = context.settings?.resolved;
  const resolvedModel = resolved?.model;
  const resolvedSandbox = resolved?.sandbox;
  const resolvedCodeMode = resolved?.codeMode;
  const resolvedMemory = resolved?.memory;
  const safeEnvironment = sanitizeEnvReferences(manifest.environment || {});
  const envKeys = Object.keys(safeEnvironment);
  const pipelines = manifest.pipelines || [];
  const projectLibraries = context.projectLibraryStore?.load(manifest.projectId) || {
    enabledLibraryIds: [],
    pinnedAssetIds: []
  };
  const availableLibraries = context.libraryRegistry?.list('workspace') || [];
  const compression = {
    enabled: false,
    minBlockTokens: 2000,
    minRunTokens: 8000,
    maxStringTokens: 1000,
    maxArrayItems: 20,
    maxLogLines: 200,
    code: true,
    ...(defaults.compression || {})
  };

  return {
    scope: 'project',
    title: scopeTitle('project'),
    subtitle: `${manifest.name} · ${manifest.projectId}`,
    scopeLabel: scopeLabel('project'),
    readOnlyNote: label('note.read_only', 'Visão geral somente leitura nesta fase. A edição será habilitada na próxima etapa.'),
    sections: [
      section({
        id: 'general',
        title: label('section.general', 'Geral'),
        description: label('project.general_desc', 'Identidade, caminhos e metadados do projeto ativo.'),
        icon: 'file',
        badge: label('badge.project', 'Projeto'),
        content: `
          ${grid([
            row(label('label.id', 'ID'), code(manifest.projectId)),
            row(label('label.root', 'Raiz'), code(manifest.rootPath)),
            row(label('label.environment_variables', 'Variáveis de ambiente'), escapeHtml(String(envKeys.length))),
            row(label('label.database', 'Banco de dados'), manifest.database ? code(`${manifest.database.type} · ${manifest.database.filePath || manifest.database.url || ''}`) : muted(label('value.not_configured', 'Não configurado')))
          ])}
          <div class="form-row-grid properties-form-grid">
            <div class="form-group">
              <label class="form-label" for="properties-project-name">${escapeHtml(label('label.name', 'Nome'))}</label>
              <input id="properties-project-name" class="form-input" type="text" data-property-input="name" value="${escapeHtml(manifest.name)}" />
            </div>
            <div class="form-group">
              <label class="form-label" for="properties-project-version">${escapeHtml(label('label.version', 'Versão'))}</label>
              <input id="properties-project-version" class="form-input" type="text" data-property-input="version" value="${escapeHtml(manifest.version)}" />
            </div>
          </div>
          <div class="form-group">
            <label class="form-label" for="properties-project-description">${escapeHtml(label('label.description', 'Descrição'))}</label>
            <textarea id="properties-project-description" class="form-textarea" rows="3" data-property-input="description">${escapeHtml(manifest.description || '')}</textarea>
          </div>
          <div class="form-group">
            <label class="form-label" for="properties-project-output">${escapeHtml(label('label.output', 'Saída'))}</label>
            <input id="properties-project-output" class="form-input" type="text" data-property-input="outputDir" value="${escapeHtml(manifest.outputDir || 'Output')}" />
          </div>
          ${manifest.database?.type === 'postgres'
            ? note(label('project.database_postgres_hint', 'Este projeto usa PostgreSQL. A URL e as credenciais continuam sendo gerenciadas fora desta tela.'), 'warning')
            : `<div class="form-group">
                <label class="form-label" for="properties-project-database">${escapeHtml(label('label.database_path', 'Banco SQLite'))}</label>
                <input id="properties-project-database" class="form-input properties-code-input" type="text" data-property-input="databasePath" value="${escapeHtml(manifest.database?.filePath || 'storage/database.sqlite')}" placeholder="storage/database.sqlite" />
                <div class="properties-hint">${escapeHtml(label('project.database_path_hint', 'Caminho relativo à raiz do projeto. Nenhuma credencial é armazenada no manifesto.'))}</div>
              </div>`}
        `,
        save: context.saveProject
          ? (values) => context.saveProject!(values)
          : undefined
      }),
      section({
        id: 'pipelines',
        title: label('section.pipelines', 'Pipelines'),
        description: label('project.pipelines_desc', 'Grafos registrados no manifesto e pipeline principal.'),
        icon: 'subflow',
        badge: label('badge.project', 'Projeto'),
        content: list(
          pipelines.map(pipeline => `${escapeHtml(pipeline.name)} ${pipeline.isPrimary ? toneBadge(label('value.primary', 'Principal'), 'accent') : ''} <span class="properties-inline-code">${escapeHtml(pipeline.file)}</span>`),
          label('value.none', 'Nenhum item configurado.')
        )
      }),
      section({
        id: 'defaults',
        title: label('section.project_defaults', 'Modelo, ferramentas e memória'),
        description: label('project.defaults_desc', 'Valores padrão herdados pelos vértices que não têm override próprio.'),
        icon: 'brain',
        badge: label('badge.project', 'Projeto'),
        content: `
          ${note(label('project.defaults_hint', 'Os valores abaixo partem do valor efetivo atual. Salvar cria um override no Projeto.'), 'info')}
          <div class="form-row-grid properties-form-grid">
            <div class="form-group">
              <label class="form-label" for="properties-project-provider">${escapeHtml(label('label.provider', 'Provedor'))}</label>
              <select id="properties-project-provider" class="form-select" data-property-input="provider">
                ${['deepseek', 'openai', 'anthropic', 'google', 'mistral', 'groq', 'local', 'jev'].map(provider => `
                  <option value="${provider}" ${(resolvedModel?.value || defaults.model).provider === provider ? 'selected' : ''}>${provider}</option>
                `).join('')}
              </select>
            </div>
            <div class="form-group">
              <label class="form-label" for="properties-project-model">${escapeHtml(label('label.model', 'Modelo'))}</label>
              <input id="properties-project-model" class="form-input" type="text" data-property-input="model" value="${escapeHtml((resolvedModel?.value || defaults.model).model)}" />
            </div>
          </div>
          <div class="form-row-grid">
            <div class="form-group">
              <label class="form-label" for="properties-project-isolation">${escapeHtml(label('label.isolation', 'Isolamento'))}</label>
              <select id="properties-project-isolation" class="form-select" data-property-input="isolationLevel">
                ${['container', 'microvm', 'host_direct'].map(level => `<option value="${level}" ${(resolvedSandbox?.value || defaults.sandbox).isolationLevel === level ? 'selected' : ''}>${level}</option>`).join('')}
              </select>
            </div>
            <div class="form-group">
              <label class="form-label" for="properties-project-network">${escapeHtml(label('label.network', 'Rede'))}</label>
              <select id="properties-project-network" class="form-select" data-property-input="network">
                ${['egress_llm_only', 'offline', 'full'].map(network => `<option value="${network}" ${(resolvedSandbox?.value || defaults.sandbox).network === network ? 'selected' : ''}>${network}</option>`).join('')}
              </select>
            </div>
            <div class="form-group">
              <label class="form-label" for="properties-project-memory-limit">${escapeHtml(label('label.memory_limit', 'Memória (MB)'))}</label>
              <input id="properties-project-memory-limit" class="form-input" type="number" min="64" step="64" data-property-input="maxMemoryMB" value="${(resolvedSandbox?.value || defaults.sandbox).maxMemoryMB ?? 512}" />
            </div>
            <div class="form-group">
              <label class="form-label" for="properties-project-timeout">${escapeHtml(label('label.timeout', 'Timeout (s)'))}</label>
              <input id="properties-project-timeout" class="form-input" type="number" min="10" step="10" data-property-input="timeoutSeconds" value="${(resolvedSandbox?.value || defaults.sandbox).timeoutSeconds ?? 300}" />
            </div>
          </div>
          <div class="form-group">
            <label class="form-label">${escapeHtml(label('label.allowed_tools', 'Ferramentas permitidas'))}</label>
            <div class="properties-check-grid">
              ${['write', 'edit', 'read', 'bash', 'validate_svg'].map(tool => {
                const checked = ((resolvedCodeMode?.value || defaults.codeMode).allowedTools || []).includes(tool as any);
                return `<label class="properties-check-row"><input type="checkbox" data-property-input="tool_${tool}" ${checked ? 'checked' : ''} /><span>${tool}</span></label>`;
              }).join('')}
            </div>
          </div>
          <div class="properties-check-grid">
            <label class="properties-check-row">
              <input type="checkbox" data-property-input="memoryEnabled" ${(resolvedMemory?.value || defaults.memory)?.enabled ? 'checked' : ''} />
              <span>${escapeHtml(label('label.memory_enabled', 'Memória ativada'))}</span>
            </label>
            <label class="properties-check-row">
              <input type="checkbox" data-property-input="memoryPersistent" ${(resolvedMemory?.value || defaults.memory)?.persistent ? 'checked' : ''} />
              <span>${escapeHtml(label('label.memory_persistent', 'Memória persistente'))}</span>
            </label>
          </div>
          <p class="properties-hint">${escapeHtml(label('project.defaults_origin_hint', 'Origem efetiva atual: modelo') )} ${originBadge(resolvedModel?.origin || 'project')} · ${escapeHtml(label('label.sandbox', 'Sandbox'))} ${originBadge(resolvedSandbox?.origin || 'project')}</p>
        `,
        save: context.saveProjectDefaults
          ? (values) => context.saveProjectDefaults!(values)
          : undefined,
        actions: [
          { id: 'project.open-local-models', label: label('action.manage_local_models', 'Gerenciar modelos locais'), kind: 'secondary' },
          { id: 'project.open-http-policy', label: label('action.manage_http_policy', 'Gerenciar allowlist HTTP'), kind: 'secondary' },
          { id: 'project.open-memory-advanced', label: label('action.manage_memory_advanced', 'Gerenciar memória avançada'), kind: 'secondary' }
        ]
      }),
      section({
        id: 'compression',
        title: label('section.compression', 'Compactação de contexto'),
        description: label('project.compression_desc', 'Reduz as saídas de ferramenta antes de elas entrarem na janela do modelo. Desligada por padrão.'),
        icon: 'database',
        badge: label('badge.project', 'Projeto'),
        content: `
          ${note(label('project.compression_hint', 'Só observações de ferramenta (leitura, bash e MCP) são compactadas. O texto original fica recuperável pela ferramenta `retrieve`, com o token mostrado no aviso [CCR:...]. O system prompt, a skill e o brief de memória nunca são tocados.'), 'info')}
          <label class="properties-check-row">
            <input type="checkbox" data-property-input="compressionEnabled" ${compression.enabled ? 'checked' : ''} />
            <span>${escapeHtml(label('label.compression_enabled', 'Compactar saídas de ferramenta (JSON e logs)'))}</span>
          </label>
          <label class="properties-check-row">
            <input type="checkbox" data-property-input="compressionCode" ${compression.code !== false ? 'checked' : ''} />
            <span>${escapeHtml(label('label.compression_code', 'Incluir código (mantém assinaturas e estrutura)'))}</span>
          </label>
          <div class="form-row-grid properties-form-grid">
            <div class="form-group">
              <label class="form-label" for="properties-compression-block">${escapeHtml(label('label.compression_block', 'Bloco mínimo (tokens)'))}</label>              <input id="properties-compression-block" class="form-input" type="number" min="100" step="100" data-property-input="compressionMinBlockTokens" value="${compression.minBlockTokens}" />
            </div>
            <div class="form-group">
              <label class="form-label" for="properties-compression-run">${escapeHtml(label('label.compression_run', 'Run mínimo (tokens)'))}</label>
              <input id="properties-compression-run" class="form-input" type="number" min="100" step="500" data-property-input="compressionMinRunTokens" value="${compression.minRunTokens}" />
            </div>
            <div class="form-group">
              <label class="form-label" for="properties-compression-string">${escapeHtml(label('label.compression_string', 'String máxima (tokens)'))}</label>
              <input id="properties-compression-string" class="form-input" type="number" min="64" step="100" data-property-input="compressionMaxStringTokens" value="${compression.maxStringTokens}" />
            </div>
            <div class="form-group">
              <label class="form-label" for="properties-compression-array">${escapeHtml(label('label.compression_array', 'Itens por array'))}</label>
              <input id="properties-compression-array" class="form-input" type="number" min="1" step="1" data-property-input="compressionMaxArrayItems" value="${compression.maxArrayItems}" />
            </div>
            <div class="form-group">
              <label class="form-label" for="properties-compression-log">${escapeHtml(label('label.compression_log', 'Linhas de log'))}</label>
              <input id="properties-compression-log" class="form-input" type="number" min="20" step="10" data-property-input="compressionMaxLogLines" value="${compression.maxLogLines}" />
            </div>
          </div>
          ${grid([
            row(label('label.compression_model', 'Modelo de prosa (Kompress)'), `<span id="properties-compression-model">${escapeHtml(label('value.checking', 'Verificando…'))}</span>`),
            row(label('label.compression_hardware', 'Hardware'), `<span id="properties-compression-hardware">—</span>`),
            row(label('label.compression_recommendation', 'Sugestão para esta máquina'), `<span id="properties-compression-suggestion">—</span>`)
          ])}
          <div id="properties-compression-download-actions">
            <div class="form-row-grid properties-form-grid">
              <div class="form-group">
                <label class="form-label" for="properties-compression-variant">${escapeHtml(label('label.compression_variant', 'Variante do modelo'))}</label>
                <select id="properties-compression-variant" class="form-select">
                  <option value="fp32">fp32 · 601 MB</option>
                  <option value="int8">int8 · 274 MB</option>
                </select>
              </div>
              <div class="form-group">
                <label class="form-label">&nbsp;</label>
                <div>
                  <button id="properties-compression-download" class="btn-primary" type="button">${escapeHtml(label('action.compression_download', 'Baixar modelo'))}</button>
                  <button id="properties-compression-cancel" class="btn-secondary" type="button" style="display:none;margin-left:8px">${escapeHtml(label('action.compression_cancel', 'Cancelar'))}</button>
                </div>
              </div>
            </div>
            <div id="properties-compression-download-status" class="properties-hint" role="status"></div>
          </div>
          <p class="properties-hint">${escapeHtml(label('project.compression_ccr_hint', 'O cache reversível fica em %LOCALAPPDATA%\\Arestrix\\ccr (fora do repositório), com retenção de 7 dias e teto de 256 MB por projeto. A economia de cada execução aparece no Console: "contexto compactado: N bloco(s), X -> Y tokens".'))}</p>
        `,
        save: context.saveProjectCompression
          ? (values) => context.saveProjectCompression!(values)
          : undefined,
        bind: (root) => {
          const target = root.querySelector('#properties-compression-model') as HTMLElement | null;
          if (!target) return;
          const webui = typeof window !== 'undefined' ? (window as any).webui : null;
          const actions = root.querySelector('#properties-compression-download-actions') as HTMLElement | null;
          if (!webui || typeof webui.call !== 'function') {
            target.textContent = label('value.desktop_only', 'Disponível no app desktop');
            if (actions) actions.style.display = 'none';
            return;
          }

          const suggestion = root.querySelector('#properties-compression-suggestion') as HTMLElement | null;
          const hardware = root.querySelector('#properties-compression-hardware') as HTMLElement | null;
          const downloadButton = root.querySelector('#properties-compression-download') as HTMLButtonElement | null;
          const cancelButton = root.querySelector('#properties-compression-cancel') as HTMLButtonElement | null;
          const variantSelect = root.querySelector('#properties-compression-variant') as HTMLSelectElement | null;
          const downloadStatus = root.querySelector('#properties-compression-download-status') as HTMLElement | null;

          let busy = false;
          let jobId = '';
          let installed: Record<string, boolean> = {};
          let buildOnnx = true;

          const parse = (raw: any) => (typeof raw === 'string' ? JSON.parse(raw) : raw);
          const formatMB = (bytes: number) => {
            const value = Number.isFinite(bytes) ? Math.round(bytes / (1024 * 1024)) : 0;
            return `${value} MB`;
          };

          const syncButtons = () => {
            if (!downloadButton) return;
            const selected = variantSelect?.value || 'fp32';
            const already = installed[selected] === true;
            downloadButton.disabled = busy || already || !buildOnnx;
            downloadButton.textContent = already
              ? label('value.compression_model_installed', 'Instalado')
              : label('action.compression_download', 'Baixar modelo');
            if (cancelButton) cancelButton.style.display = busy ? '' : 'none';
            if (variantSelect) variantSelect.disabled = busy;
          };

          const render = (data: any) => {
            const state = data?.available
              ? label('value.compression_model_installed', 'Instalado')
              : label('value.compression_model_missing', 'Não instalado');
            target.textContent = `${state} · ${data?.dir || ''}`;

            const ramGB = data?.hardware?.ramTotalMB > 0 ? Math.round(data.hardware.ramTotalMB / 1024) : '?';
            const diskGB = data?.hardware?.diskFreeMB >= 0 ? Math.round(data.hardware.diskFreeMB / 1024) : '?';

            if (hardware) {
              const gpus = Array.isArray(data?.hardware?.gpus) ? data.hardware.gpus : [];
              const gpuText = gpus.length
                ? gpus.map((gpu: any) => {
                    const vram = gpu.vramMB >= 1024 ? `${Math.round(gpu.vramMB / 1024)} GB` : `${gpu.vramMB} MB`;
                    return `${gpu.name} (${vram})`;
                  }).join(' + ')
                : t('properties_window.project.compression_no_gpu', 'sem GPU');
              const cpu = data?.hardware?.cpuThreads ?? '?';
              const dml = data?.hardware?.dmlAvailable === true;
              const suffix = gpus.length && !dml
                ? ` · ${t('properties_window.project.compression_dml_off', 'DirectML desligado')}`
                : '';
              hardware.textContent = t('properties_window.project.compression_hardware_summary', {
                cpu, ram: ramGB, gpus: gpuText,
                defaultValue: `${cpu} núcleos · ${ramGB} GB RAM · ${gpuText}`
              }) + suffix;
            }

            const rec = data?.recommendation || {};
            if (suggestion) {
              if (rec.variant === 'gpu') {
                suggestion.textContent = t('properties_window.project.compression_recommend_gpu', {
                  gpu: rec.gpuName || 'GPU', size: rec.sizeMB || 601,
                  defaultValue: `Sugestão: GPU (DirectML) — ${rec.gpuName || 'GPU'}; usa o modelo fp32 (~601 MB).`
                });
              } else if (rec.variant === 'fp32') {
                suggestion.textContent = t('properties_window.project.compression_recommend_fp32', {
                  ram: ramGB, disk: diskGB,
                  defaultValue: `Sugestão: fp32 (rápido) — ${ramGB} GB de RAM e ${diskGB} GB livres; o modelo ocupa ~601 MB.`
                });
              } else if (rec.variant === 'int8') {
                suggestion.textContent = t('properties_window.project.compression_recommend_int8', {
                  ram: ramGB, disk: diskGB,
                  defaultValue: `Sugestão: int8 (compacto) — RAM ou disco limitados (${ramGB} GB de RAM, ${diskGB} GB livres); o modelo ocupa ~274 MB.`
                });
              } else if (rec.reason === 'cpu') {
                suggestion.textContent = t('properties_window.project.compression_recommend_cpu',
                  'Poucos núcleos de CPU para compressão de prosa.');
              } else {
                suggestion.textContent = t('properties_window.project.compression_recommend_none',
                  'Máquina abaixo do mínimo para prosa: ~1 GB de RAM livre e ~700 MB em disco.');
              }
            }

            installed = data?.installedVariants || {};
            buildOnnx = data?.build?.onnx !== false;
            if (!buildOnnx && downloadStatus) {
              downloadStatus.textContent = t('properties_window.project.compression_build_no_onnx',
                'Este build não inclui compressão de prosa (ONNX); baixar o modelo não habilita a prosa.');
            }
            if (variantSelect && !busy) {
              variantSelect.value = rec.variant === 'int8' ? 'int8' : 'fp32';
            }
            syncButtons();
          };

          const refresh = () => {
            Promise.resolve(webui.call('prose_model_status'))
              .then((raw: any) => render(parse(raw)))
              .catch(() => {
                target.textContent = label('value.compression_model_unknown', 'Não foi possível verificar');
              });
          };

          const poll = () => {
            if (!jobId || !root.isConnected) return;
            Promise.resolve(webui.call('prose_model_download_status', JSON.stringify({ jobId })))
              .then((raw: any) => {
                const job = parse(raw);
                if (!job?.ok) {
                  if (downloadStatus) {
                    downloadStatus.textContent = t('properties_window.project.compression_download_error', {
                      error: job?.error || '?',
                      defaultValue: `Falha no download: ${job?.error || '?'}`
                    });
                  }
                  busy = false;
                  jobId = '';
                  syncButtons();
                  return;
                }
                const total = Number(job.total) || 0;
                const received = Number(job.received) || 0;
                const pct = total > 0 ? Math.min(100, Math.floor((received / total) * 100)) : 0;
                if (downloadStatus) {
                  if (job.status === 'downloading') {
                    downloadStatus.textContent = t('properties_window.project.compression_download_progress', {
                      pct, file: job.file || '',
                      received: formatMB(received), total: formatMB(total),
                      defaultValue: `${pct}% · ${job.file || ''} (${formatMB(received)} de ${formatMB(total)})`
                    });
                  } else if (job.status === 'verifying') {
                    downloadStatus.textContent = t('properties_window.project.compression_download_verifying', {
                      file: job.file || 'arquivo',
                      defaultValue: `Verificando SHA-256 de ${job.file || 'arquivo'}…`
                    });
                  } else if (job.status === 'cancelling') {
                    downloadStatus.textContent = t('properties_window.project.compression_download_cancelling', 'Cancelando…');
                  } else if (job.status === 'done') {
                    downloadStatus.textContent = t('properties_window.project.compression_download_done', 'Modelo instalado.');
                  } else if (job.status === 'cancelled') {
                    downloadStatus.textContent = t('properties_window.project.compression_download_cancelled', 'Download cancelado.');
                  } else if (job.status === 'failed') {
                    downloadStatus.textContent = t('properties_window.project.compression_download_error', {
                      error: job.error || '?',
                      defaultValue: `Falha no download: ${job.error || '?'}`
                    });
                  }
                }
                if (job.status === 'done' || job.status === 'cancelled' || job.status === 'failed') {
                  busy = false;
                  jobId = '';
                  syncButtons();
                  if (job.status === 'done') refresh();
                  return;
                }
                setTimeout(poll, 600);
              })
              .catch(() => setTimeout(poll, 1200));
          };

          if (downloadButton) {
            downloadButton.addEventListener('click', () => {
              if (busy || !variantSelect) return;
              busy = true;
              syncButtons();
              if (downloadStatus) {
                downloadStatus.textContent = t('properties_window.project.compression_download_starting', 'Iniciando download…');
              }
              Promise.resolve(webui.call('prose_model_download', JSON.stringify({ variant: variantSelect.value })))
                .then((raw: any) => {
                  const res = parse(raw);
                  if (!res?.ok) {
                    if (downloadStatus) {
                      downloadStatus.textContent = t('properties_window.project.compression_download_error', {
                        error: res?.error || '?',
                        defaultValue: `Falha no download: ${res?.error || '?'}`
                      });
                    }
                    busy = false;
                    jobId = '';
                    syncButtons();
                    if (res?.error === 'already_installed') refresh();
                    return;
                  }
                  jobId = String(res.jobId || '');
                  poll();
                })
                .catch((err: any) => {
                  if (downloadStatus) downloadStatus.textContent = String(err?.message || err);
                  busy = false;
                  syncButtons();
                });
            });
          }

          if (cancelButton) {
            cancelButton.addEventListener('click', () => {
              if (!jobId) return;
              Promise.resolve(webui.call('prose_model_download_cancel', JSON.stringify({ jobId }))).catch(() => {});
            });
          }

          if (variantSelect) variantSelect.addEventListener('change', syncButtons);
          refresh();
        }
      }),
      section({
        id: 'libraries',
        title: label('section.project_libraries', 'Bibliotecas do Projeto'),
        description: label('project.libraries_desc', 'Habilite bibliotecas liberadas no Workspace e fixe versões de assets.'),
        icon: 'catalog',
        badge: label('badge.project', 'Projeto'),
        content: `
          ${note(label('project.libraries_hint', 'O Projeto não cria nem edita assets: edições viram derivados em Minha biblioteca do Workspace. Os pins abaixo serão consumidos pelo seletor de agentes/grafos na Fase 4.'), 'info')}
          ${availableLibraries.length === 0
            ? `<div class="properties-empty">${escapeHtml(label('project.libraries_empty', 'Nenhuma biblioteca liberada no Workspace.'))}</div>`
            : availableLibraries.map(library => {
              const assets = context.libraryRegistry?.listAssets(library.id) || [];
              const enabled = library.isBuiltin || projectLibraries.enabledLibraryIds.includes(library.id);
              return `
                <div class="properties-project-library-group">
                  <label class="properties-check-row">
                    <input type="checkbox" data-property-input="libraryEnabled_${escapeHtml(library.id)}" ${enabled ? 'checked' : ''} ${library.isBuiltin ? 'disabled' : ''} />
                    <span>
                      <strong>${escapeHtml(library.name)}</strong>
                      ${toneBadge(library.isBuiltin ? label('libraries.kind_builtin', 'Embutida') : label('libraries.kind_local', 'Local'), library.isBuiltin ? 'success' : 'neutral')}
                      <span class="properties-muted">${escapeHtml(String(assets.length))} ${escapeHtml(label('libraries.assets', 'assets'))}</span>
                    </span>
                  </label>
                  ${assets.length === 0
                    ? `<div class="properties-hint properties-project-library-empty">${escapeHtml(label('project.libraries_no_assets', 'Importe um catálogo nas Propriedades do Studio ou do Workspace.'))}</div>`
                    : `<div class="properties-project-assets">
                        ${assets.map(asset => {
                          const tone = asset.audit.verdict === 'APPROVE' ? 'success' : asset.audit.verdict === 'CAUTION' ? 'warning' : asset.audit.verdict === 'REJECT' ? 'danger' : 'neutral';
                          const pinned = projectLibraries.pinnedAssetIds.includes(asset.id);
                          return `
                            <label class="properties-check-row">
                              <input type="checkbox" data-property-input="assetPinned_${escapeHtml(asset.id)}" ${pinned ? 'checked' : ''} ${enabled ? '' : 'disabled'} />
                              <span>
                                ${escapeHtml(asset.name)}
                                ${toneBadge(asset.kind === 'skill' ? 'Skill' : 'MCP', 'neutral')}
                                ${toneBadge(asset.audit.verdict, tone)}
                              </span>
                            </label>
                          `;
                        }).join('')}
                      </div>`}
                </div>
              `;
            }).join('')}
        `,
        save: context.saveProjectLibraries
          ? (values) => context.saveProjectLibraries!(values)
          : undefined
      }),
      section({
        id: 'environment',
        title: label('section.environment', 'Ambiente do Projeto'),
        description: label('project.environment_desc', 'Variáveis injetadas na execução. Somente referências ${VAR} são persistidas.'),
        icon: 'key',
        badge: label('badge.project', 'Projeto'),
        content: `
          ${note(label('note.environment_safe', 'Valores literais são descartados. Use \${NOME_DA_VARIAVEL} para referenciar uma variável do ambiente do sistema.'), 'warning')}
          <div class="form-group properties-form-grid">
            <label class="form-label" for="properties-project-environment">${escapeHtml(label('label.environment_lines', 'Variáveis (KEY=valor)'))}</label>
            <textarea id="properties-project-environment" class="form-textarea properties-code-input" rows="8" data-property-input="environment" placeholder="MINHA_VARIAVEL=\${MINHA_VARIAVEL}">${escapeHtml(envKeys.map(key => `${key}=${safeEnvironment[key] ?? ''}`).join('\n'))}</textarea>
          </div>
        `,
        save: context.saveProjectEnvironment
          ? (values) => context.saveProjectEnvironment!(values)
          : undefined
      }),
      section({
        id: 'git',
        title: label('section.git', 'Git e sincronização'),
        description: label('project.git_desc', 'Repositório remoto e automação de commits do projeto.'),
        icon: 'git',
        badge: label('badge.project', 'Projeto'),
        content: manifest.git ? grid([
          row(label('label.provider', 'Provedor'), escapeHtml(manifest.git.provider)),
          row(label('label.repository', 'Repositório'), code(manifest.git.repositoryName || manifest.git.remoteUrl || '—')),
          row(label('label.auto_commit', 'Commit ao executar'), toneBadge(manifest.git.autoCommitOnRun ? label('value.active', 'Ativo') : label('value.disabled', 'Desativado'), manifest.git.autoCommitOnRun ? 'success' : 'neutral')),
          row(label('label.timed_push', 'Push temporizado'), manifest.git.timedPushEnabled ? `${label('value.active', 'Ativo')} · ${escapeHtml(String(manifest.git.timedPushIntervalMinutes || 10))} min` : label('value.disabled', 'Desativado'))
        ]) : muted(label('value.not_configured', 'Não configurado.')),
        actions: [
          { id: 'project.open-git', label: label('action.manage_git', 'Gerenciar conta Git'), kind: 'secondary' }
        ]
      }),
      section({
        id: 'runner',
        title: label('section.runner', 'Runner e distribuição'),
        description: label('project.runner_desc', 'Configuração do executável autônomo e seus parâmetros.'),
        icon: 'rocket',
        badge: label('badge.project', 'Projeto'),
        content: manifest.runner ? grid([
          row(label('label.status', 'Status'), toneBadge(manifest.runner.enabled ? label('value.runner_enabled', 'Ativado') : label('value.runner_disabled', 'Desativado'), manifest.runner.enabled ? 'success' : 'neutral')),
          row(label('label.output', 'Saída'), code(`${manifest.runner.outputDir || 'dist/runner'}/${manifest.runner.outputName || '—'}`)),
          row(label('label.version', 'Versão'), escapeHtml(manifest.runner.version || '—')),
          row(label('label.parameters', 'Parâmetros'), escapeHtml(String((manifest.runner.parameters || []).length)))
        ]) : muted(label('value.not_configured', 'Não configurado.')),
        actions: [
          { id: 'project.open-runner', label: label('action.manage_runner', 'Abrir Runner'), kind: 'secondary' }
        ]
      }),
      section({
        id: 'advanced',
        title: label('section.advanced', 'Avançado'),
        description: label('project.advanced_desc', 'Manifesto, ambiente e informações de diagnóstico.'),
        icon: 'settings',
        content: grid([
          row(label('label.manifest', 'Manifesto'), code(`${manifest.rootPath}/arestrix.project.json`)),
          row(label('label.environment_variables', 'Variáveis de ambiente'), escapeHtml(String(envKeys.length))),
          row(label('label.created', 'Criado em'), manifest.createdAt ? escapeHtml(new Date(manifest.createdAt).toLocaleString()) : muted('—')),
          row(label('label.updated', 'Atualizado em'), manifest.updatedAt ? escapeHtml(new Date(manifest.updatedAt).toLocaleString()) : muted('—'))
        ])
      })
    ]
  };
}
