import { BaseModal, type IModalTabDefinition } from './BaseModal.ts';
import { SkillEditorModal, extractFrontmatter, renderMarkdown } from './SkillEditorModal.ts';
import { ribbonIcon } from '../ribbon/RibbonIcons.ts';
import { t } from '../../i18n/index.ts';
import type {
  ILibraryAsset,
  ILibraryAssetPayload,
  IMcpAssetPayload,
  ISkillAssetPayload
} from '../../core/libraries/types.ts';
import type {
  CodeModeTool,
  INodeAgentSettings,
  INodeSkill,
  LLMProvider,
  ReasoningEffortLevel,
  SandboxIsolationLevel,
  SandboxNetworkPolicy,
  VertexMemoryMode
} from '../../core/types.ts';
import type { ISerializedTemplatePlugin } from '../../core/templates/AgentTemplateTypes.ts';

export type LibraryAssetMetadataPatch = Pick<
  ILibraryAsset,
  'name' | 'description' | 'domain' | 'subdomain' | 'category' | 'version' | 'license' | 'changelog'
>;

export interface ILibraryAssetEditorSavePayload {
  metadata: LibraryAssetMetadataPatch;
  payload: ILibraryAssetPayload;
}

export interface ILibraryAssetEditorOpenOptions {
  asset: ILibraryAsset;
  libraryName?: string;
  /** True quando o ativo selecionado está em biblioteca somente leitura (ex.: Biblioteca Arestrix). */
  isReadOnlySource?: boolean;
  /** Nome sugerido quando uma nova cópia em Minha biblioteca será criada ao salvar. */
  suggestedName?: string;
  /** Callback assíncrono que persiste e audita o ativo; retorna ok/reason. */
  onSave: (data: ILibraryAssetEditorSavePayload) => Promise<{ ok: boolean; reason?: string }>;
}

const NATIVE_TOOLS: Array<{ id: CodeModeTool; label: string }> = [
  { id: 'read', label: 'tools.read' },
  { id: 'write', label: 'tools.write' },
  { id: 'edit', label: 'tools.edit' },
  { id: 'bash', label: 'tools.bash' },
  { id: 'validate_svg', label: 'tools.validate_svg' }
];

const PROVIDERS: Array<{ id: LLMProvider; label: string }> = [
  { id: 'deepseek', label: 'DeepSeek' },
  { id: 'anthropic', label: 'Anthropic (Claude)' },
  { id: 'google', label: 'Google (Gemini)' },
  { id: 'mistral', label: 'Mistral' },
  { id: 'openai', label: 'OpenAI' },
  { id: 'jev', label: 'Jev / Engine Visual' },
  { id: 'local', label: 'Local / OpenAI-compatível' }
];

const REASONING_LEVELS: Array<{ id: '' | ReasoningEffortLevel; labelKey: string; fallback: string }> = [
  { id: '', labelKey: 'library_asset_editor.reasoning_default', fallback: 'Padrão do provedor' },
  { id: 'none', labelKey: 'library_asset_editor.reasoning_off', fallback: 'Desligado (none)' },
  { id: 'low', labelKey: 'library_asset_editor.reasoning_low', fallback: 'Baixo (low)' },
  { id: 'medium', labelKey: 'library_asset_editor.reasoning_medium', fallback: 'Médio (medium)' },
  { id: 'high', labelKey: 'library_asset_editor.reasoning_high', fallback: 'Alto (high)' },
  { id: 'max', labelKey: 'library_asset_editor.reasoning_max', fallback: 'Máximo (max)' }
];

function cloneAsset(asset: ILibraryAsset): ILibraryAsset {
  return JSON.parse(JSON.stringify(asset)) as ILibraryAsset;
}

function serializeEnvLines(env?: Record<string, string>): string {
  if (!env) return '';
  return Object.entries(env)
    .map(([k, v]) => `${k}=${v}`)
    .join('\n');
}

function parseEnvLines(raw: string): Record<string, string> | undefined {
  const entries: Record<string, string> = {};
  for (const line of raw.split(/\r?\n/)) {
    const trimmed = line.trim();
    if (!trimmed || trimmed.startsWith('#')) continue;
    const eq = trimmed.indexOf('=');
    if (eq <= 0) continue;
    const key = trimmed.slice(0, eq).trim();
    const val = trimmed.slice(eq + 1).trim();
    if (key) entries[key] = val;
  }
  return Object.keys(entries).length > 0 ? entries : undefined;
}

function ensureAgentSettings(template: ISerializedTemplatePlugin): INodeAgentSettings {
  if (!template.agentSettings) {
    template.agentSettings = {
      inheritSandbox: true,
      inheritModel: true,
      inheritCodeMode: true,
      inheritMemory: true,
      skills: []
    };
  }
  return template.agentSettings;
}

function iconForKind(kind: ILibraryAsset['kind']): string {
  if (kind === 'skill') return ribbonIcon('book', 17);
  if (kind === 'mcp') return ribbonIcon('plug', 17);
  if (kind === 'pipeline') return ribbonIcon('subflow', 17);
  return ribbonIcon('agent', 17);
}

function kindTitle(kind: ILibraryAsset['kind']): string {
  if (kind === 'skill') return t('properties_window.libraries.kind_skill', 'Skill');
  if (kind === 'mcp') return t('properties_window.libraries.kind_mcp', 'MCP');
  if (kind === 'pipeline') return t('properties_window.libraries.kind_pipeline', 'Pipeline');
  return t('properties_window.libraries.kind_agent_template', 'Template de agente');
}

/**
 * Arestrix Studio - Modal de Edição de Ativo da Biblioteca (Padrão Office 365 / BaseModal)
 *
 * Permite editar metadados, configurações específicas por tipo de ativo
 * (Skill, MCP, Template de Agente e Pipeline) diretamente sobre a tela
 * "Arquivo > Biblioteca de Ativos", sem sair do Backstage nem inserir vértices
 * no grafo. Para editar Markdown de skills (SKILL.md), abre o SkillEditorModal
 * empilhado sobre este modal.
 */
export class LibraryAssetEditorModal extends BaseModal {
  private draft: ILibraryAsset | null = null;
  private libraryName: string = '';
  private isReadOnlySource: boolean = false;
  private dirty: boolean = false;
  private saving: boolean = false;
  private errorMessage: string = '';
  private infoNotice: string = '';
  private invalidMcpHeaders: boolean = false;
  private onSaveHandler?: ILibraryAssetEditorOpenOptions['onSave'];
  private skillEditorModal?: SkillEditorModal;
  private boundShortcutHandler?: (e: KeyboardEvent) => void;

  constructor() {
    super({
      id: 'library-asset-editor-modal',
      dialogClass: 'library-asset-editor-dialog',
      title: t('library_asset_editor.title', 'Editar Ativo da Biblioteca'),
      subtitle: '',
      icon: ribbonIcon('pencil', 17),
      minWidth: 760,
      minHeight: 540,
      resizable: true,
      draggable: true,
      closeOnEscape: true,
      closeOnBackdropClick: false
    });
    this.setupSaveShortcut();
  }

  private setupSaveShortcut(): void {
    this.boundShortcutHandler = (e: KeyboardEvent) => {
      if (this.isOpen && BaseModal.isTopMost(this) && (e.ctrlKey || e.metaKey) && (e.key === 's' || e.key === 'S')) {
        e.preventDefault();
        e.stopPropagation();
        void this.handleSave();
      }
    };
    if (typeof window !== 'undefined') {
      window.addEventListener('keydown', this.boundShortcutHandler);
    }
  }

  private getSkillEditor(): SkillEditorModal {
    if (!this.skillEditorModal) {
      this.skillEditorModal = new SkillEditorModal();
    }
    return this.skillEditorModal;
  }

  public getSkillEditorForTesting(): SkillEditorModal | undefined {
    return this.skillEditorModal;
  }

  public openAsset(options: ILibraryAssetEditorOpenOptions): void {
    const cloned = cloneAsset(options.asset);
    if (options.suggestedName && options.suggestedName.trim()) {
      cloned.name = options.suggestedName.trim();
    }
    this.draft = cloned;
    this.libraryName = options.libraryName || '';
    this.isReadOnlySource = Boolean(options.isReadOnlySource);
    this.onSaveHandler = options.onSave;
    this.dirty = false;
    this.saving = false;
    this.errorMessage = '';
    this.infoNotice = '';
    this.invalidMcpHeaders = false;

    this.config.title = t('library_asset_editor.title_with_kind', 'Editar {{kind}}', {
      kind: kindTitle(cloned.kind)
    });
    this.config.subtitle = [
      cloned.name,
      this.libraryName ? `${t('library_asset_editor.library_prefix', 'Biblioteca')}: ${this.libraryName}` : ''
    ].filter(Boolean).join(' · ');
    this.config.icon = iconForKind(cloned.kind);

    this.tabs = this.buildTabsForAsset(cloned);
    this.activeTabId = this.tabs[0]?.id || 'general';

    super.open();
  }

  private buildTabsForAsset(asset: ILibraryAsset): IModalTabDefinition[] {
    const general: IModalTabDefinition = {
      id: 'general',
      label: t('library_asset_editor.tab_general', 'Geral')
    };

    if (asset.payload.kind === 'skill') {
      return [
        general,
        {
          id: 'skill',
          label: t('library_asset_editor.tab_skill', 'Skill & Markdown')
        }
      ];
    }

    if (asset.payload.kind === 'mcp') {
      const toolCount = asset.payload.cachedTools?.length || 0;
      return [
        general,
        {
          id: 'mcp',
          label: t('library_asset_editor.tab_mcp_connection', 'Conexão MCP')
        },
        {
          id: 'mcp-tools',
          label: t('library_asset_editor.tab_mcp_tools', 'Ferramentas'),
          badge: toolCount > 0 ? String(toolCount) : undefined
        }
      ];
    }

    if (asset.payload.kind === 'pipeline') {
      const skillsCount = asset.payload.template.agentSettings?.skills?.length || 0;
      const tabs: IModalTabDefinition[] = [
        general,
        {
          id: 'pipeline',
          label: t('library_asset_editor.tab_pipeline', 'Interface & Grafo')
        }
      ];
      if (skillsCount > 0) {
        tabs.push({
          id: 'skills',
          label: t('library_asset_editor.tab_skills', 'Skills'),
          badge: String(skillsCount)
        });
      }
      return tabs;
    }

    // agent-template
    const skillsCount = asset.payload.template.agentSettings?.skills?.length || 0;
    return [
      general,
      {
        id: 'model',
        label: t('library_asset_editor.tab_model', 'Modelo & Prompt')
      },
      {
        id: 'skills',
        label: t('library_asset_editor.tab_skills', 'Skills'),
        badge: skillsCount > 0 ? String(skillsCount) : undefined
      },
      {
        id: 'runtime',
        label: t('library_asset_editor.tab_runtime', 'Ferramentas & Sandbox')
      }
    ];
  }

  private refreshTabBadges(): void {
    if (!this.draft) return;
    this.tabs = this.buildTabsForAsset(this.draft);
    const strip = this.dialog.querySelector('.office-modal-tab-strip');
    if (strip) {
      const html = this.renderTabsStripHtml();
      const temp = document.createElement('div');
      temp.innerHTML = html.trim();
      const nextStrip = temp.firstElementChild;
      if (nextStrip) {
        strip.replaceWith(nextStrip);
        this.setupTabEvents();
      }
    }
  }

  protected override renderTabContent(tabId: string): string {
    if (!this.draft) {
      return `<div class="properties-empty">${this.escapeHtml(t('library_asset_editor.no_asset', 'Nenhum ativo selecionado.'))}</div>`;
    }

    const banners = this.renderTopBanners();
    let content = '';

    switch (tabId) {
      case 'general':
        content = this.renderGeneralTab(this.draft);
        break;
      case 'skill':
        content = this.renderSkillTab(this.draft);
        break;
      case 'mcp':
        content = this.renderMcpConnectionTab(this.draft);
        break;
      case 'mcp-tools':
        content = this.renderMcpToolsTab(this.draft);
        break;
      case 'model':
        content = this.renderAgentModelTab(this.draft);
        break;
      case 'skills':
        content = this.renderTemplateSkillsTab(this.draft);
        break;
      case 'runtime':
        content = this.renderAgentRuntimeTab(this.draft);
        break;
      case 'pipeline':
        content = this.renderPipelineTab(this.draft);
        break;
      default:
        content = this.renderGeneralTab(this.draft);
        break;
    }

    return `<div class="library-asset-editor-body">${banners}${content}</div>`;
  }

  private renderTopBanners(): string {
    const parts: string[] = [];

    if (this.isReadOnlySource) {
      parts.push(`
        <div class="office-info-callout library-asset-editor-callout" role="note">
          <span class="library-asset-editor-callout-icon" aria-hidden="true">${ribbonIcon('copy', 16)}</span>
          <div>
            <strong>${this.escapeHtml(t('library_asset_editor.readonly_notice_title', 'Ativo de biblioteca somente leitura'))}</strong>
            <div>${this.escapeHtml(t('library_asset_editor.readonly_notice_desc', 'O original permanece intacto. Ao salvar, as alterações serão gravadas em uma cópia editável em Minha biblioteca.'))}</div>
          </div>
        </div>
      `);
    }

    if (this.infoNotice) {
      parts.push(`
        <div class="library-asset-editor-notice" role="status">
          <span>${this.escapeHtml(this.infoNotice)}</span>
        </div>
      `);
    }

    if (this.errorMessage) {
      parts.push(`
        <div class="library-asset-editor-error" id="library-asset-editor-error" role="alert">
          <strong>${this.escapeHtml(t('library_asset_editor.error_title', 'Não foi possível salvar'))}:</strong>
          <span>${this.escapeHtml(this.errorMessage)}</span>
        </div>
      `);
    }

    return parts.join('');
  }

  private renderGeneralTab(asset: ILibraryAsset): string {
    const domain = asset.domain || (
      (asset.payload.kind === 'agent-template' || asset.payload.kind === 'pipeline')
        ? (asset.payload.template.domain || '')
        : ''
    );
    const subdomain = asset.subdomain || (
      (asset.payload.kind === 'agent-template' || asset.payload.kind === 'pipeline')
        ? (asset.payload.template.subdomain || '')
        : ''
    );

    const taskTypeHint = (asset.payload.kind === 'agent-template' || asset.payload.kind === 'pipeline')
      ? `<span class="library-asset-editor-meta-pill">${this.escapeHtml(t('library_asset_editor.meta_task_type', 'Tipo de vértice'))}: <strong>${this.escapeHtml(asset.payload.template.taskType)}</strong></span>`
      : '';

    return `
      <div class="library-asset-editor-meta-strip">
        <span class="library-asset-editor-meta-pill">${this.escapeHtml(t('library_asset_editor.meta_kind', 'Tipo'))}: <strong>${this.escapeHtml(kindTitle(asset.kind))}</strong></span>
        ${taskTypeHint}
        <span class="library-asset-editor-meta-pill">${this.escapeHtml(t('library_asset_editor.meta_audit', 'Auditoria'))}: <strong>${this.escapeHtml(asset.audit.verdict)}</strong> (${asset.audit.riskScore}/100)</span>
        ${asset.derivedFrom ? `<span class="library-asset-editor-meta-pill">${this.escapeHtml(t('properties_window.libraries.meta_derived_from', 'Derivado de'))}: <strong>${this.escapeHtml(asset.derivedFrom.originName)}</strong></span>` : ''}
      </div>

      <div class="settings-section-card library-asset-editor-card">
        <div class="form-group">
          <label class="form-label" for="lae-name">${this.escapeHtml(t('library_asset_editor.field_name', 'Nome do ativo'))}</label>
          <input type="text" id="lae-name" class="form-input" value="${this.escapeHtml(asset.name)}" placeholder="${this.escapeHtml(t('library_asset_editor.placeholder_name', 'Nome exibido na biblioteca'))}" />
        </div>

        <div class="form-group">
          <label class="form-label" for="lae-description">${this.escapeHtml(t('library_asset_editor.field_description', 'Descrição'))}</label>
          <textarea id="lae-description" class="form-textarea" rows="3" placeholder="${this.escapeHtml(t('library_asset_editor.placeholder_description', 'Descreva o propósito e quando usar este ativo...'))}">${this.escapeHtml(asset.description)}</textarea>
        </div>

        <div class="form-row-grid">
          <div class="form-group">
            <label class="form-label" for="lae-domain">${this.escapeHtml(t('properties_window.libraries.meta_domain', 'Domínio'))}</label>
            <input type="text" id="lae-domain" class="form-input" value="${this.escapeHtml(domain)}" placeholder="${this.escapeHtml(t('library_asset_editor.placeholder_domain', 'Ex.: Engenharia de Software'))}" />
          </div>
          <div class="form-group">
            <label class="form-label" for="lae-subdomain">${this.escapeHtml(t('properties_window.libraries.meta_subdomain', 'Subdomínio'))}</label>
            <input type="text" id="lae-subdomain" class="form-input" value="${this.escapeHtml(subdomain)}" placeholder="${this.escapeHtml(t('library_asset_editor.placeholder_subdomain', 'Ex.: Code Review, Debug & Git'))}" />
          </div>
        </div>

        <div class="form-row-grid library-asset-editor-grid-3">
          <div class="form-group">
            <label class="form-label" for="lae-category">${this.escapeHtml(t('properties_window.libraries.meta_category', 'Categoria'))}</label>
            <input type="text" id="lae-category" class="form-input" value="${this.escapeHtml(asset.category)}" />
          </div>
          <div class="form-group">
            <label class="form-label" for="lae-version">${this.escapeHtml(t('properties_window.libraries.meta_version', 'Versão'))}</label>
            <input type="text" id="lae-version" class="form-input" value="${this.escapeHtml(asset.version)}" placeholder="1.0.0" />
          </div>
          <div class="form-group">
            <label class="form-label" for="lae-license">${this.escapeHtml(t('properties_window.libraries.meta_license', 'Licença'))}</label>
            <input type="text" id="lae-license" class="form-input" value="${this.escapeHtml(asset.license)}" placeholder="MIT" />
          </div>
        </div>

        <div class="form-group" style="margin-bottom: 0;">
          <label class="form-label" for="lae-changelog">${this.escapeHtml(t('library_asset_editor.field_changelog', 'Nota da alteração (opcional)'))}</label>
          <input type="text" id="lae-changelog" class="form-input" value="${this.escapeHtml(asset.changelog || '')}" placeholder="${this.escapeHtml(t('library_asset_editor.placeholder_changelog', 'Resumo das modificações feitas nesta revisão'))}" />
        </div>
      </div>
    `;
  }

  private renderSkillTab(asset: ILibraryAsset): string {
    if (asset.payload.kind !== 'skill') return '';
    const payload = asset.payload;
    const fm = extractFrontmatter(payload.markdownContent || '');
    const lineCount = payload.markdownContent ? payload.markdownContent.split(/\r?\n/).length : 0;
    const nativeIds = new Set(NATIVE_TOOLS.map(item => item.id));
    const currentTools = payload.tools || [];
    const customTools = currentTools.filter(tool => !nativeIds.has(tool as CodeModeTool));

    const fmStatus = fm.valid
      ? `<span class="library-asset-editor-status-pill is-ok">${this.escapeHtml(t('library_asset_editor.md_frontmatter_valid', 'Frontmatter válido'))}${fm.name ? `: ${this.escapeHtml(fm.name)}` : ''}</span>`
      : `<span class="library-asset-editor-status-pill is-warn">${this.escapeHtml(t('library_asset_editor.md_frontmatter_missing', 'Sem frontmatter completo'))}</span>`;

    return `
      <div class="settings-section-card library-asset-editor-card">
        <div class="library-asset-editor-card-head">
          <div>
            <h3 class="section-card-title">${this.escapeHtml(t('library_asset_editor.skill_markdown_title', 'Documento Markdown (SKILL.md)'))}</h3>
            <p class="section-card-desc">${this.escapeHtml(t('library_asset_editor.skill_markdown_desc', 'Edite o conteúdo completo da skill no editor Markdown com prévia lado a lado e validação de frontmatter.'))}</p>
          </div>
          <button type="button" class="btn btn-primary-office" id="btn-open-skill-md-editor">
            ${ribbonIcon('pencil', 15)}
            <span>${this.escapeHtml(t('library_asset_editor.btn_open_md_editor', 'Abrir Editor Markdown (SKILL.md)'))}</span>
          </button>
        </div>

        <div class="library-asset-editor-md-meta">
          ${fmStatus}
          <span class="library-asset-editor-md-stats">${lineCount} ${this.escapeHtml(t('library_asset_editor.lines_label', 'linhas'))}</span>
        </div>

        <div class="office-markdown-preview library-asset-editor-md-preview" id="lae-skill-md-preview">
          ${renderMarkdown(payload.markdownContent)}
        </div>
      </div>

      <div class="settings-section-card library-asset-editor-card">
        <div class="form-group">
          <label class="form-label" for="lae-skill-system-prompt">${this.escapeHtml(t('library_asset_editor.field_system_prompt', 'Prompt de sistema'))}</label>
          <textarea id="lae-skill-system-prompt" class="form-textarea" rows="4" placeholder="${this.escapeHtml(t('library_asset_editor.placeholder_system_prompt', 'Instruções diretas injetadas no agente quando a skill é ativada...'))}">${this.escapeHtml(payload.systemPrompt || '')}</textarea>
        </div>

        <div class="form-group" style="margin-bottom: 0;">
          <label class="form-label">${this.escapeHtml(t('library_asset_editor.field_skill_tools', 'Ferramentas declaradas'))}</label>
          <div class="library-asset-editor-tools-grid">
            ${NATIVE_TOOLS.map(tool => {
              const checked = currentTools.includes(tool.id);
              return `
                <label class="tool-checkbox-pill library-asset-editor-tool-pill ${checked ? 'checked' : ''}">
                  <input type="checkbox" class="lae-skill-tool-check" value="${tool.id}" ${checked ? 'checked' : ''} />
                  <span>${tool.label}</span>
                </label>
              `;
            }).join('')}
          </div>
          <div style="margin-top: 10px;">
            <label class="form-label" for="lae-skill-custom-tools">${this.escapeHtml(t('library_asset_editor.field_custom_tools', 'Outras ferramentas (separadas por vírgula)'))}</label>
            <input type="text" id="lae-skill-custom-tools" class="form-input" value="${this.escapeHtml(customTools.join(', '))}" placeholder="ex: search_docs, lint_code" />
          </div>
        </div>
      </div>
    `;
  }

  private renderMcpConnectionTab(asset: ILibraryAsset): string {
    if (asset.payload.kind !== 'mcp') return '';
    const payload = asset.payload;
    const isSse = payload.transport === 'sse';

    return `
      <div class="settings-section-card library-asset-editor-card">
        <div class="form-group">
          <label class="form-label">${this.escapeHtml(t('mcp_servers.transport_label', 'Protocolo de Transporte'))}</label>
          <div class="library-asset-editor-radio-row">
            <label class="library-asset-editor-radio">
              <input type="radio" name="lae-mcp-transport" value="stdio" ${!isSse ? 'checked' : ''} />
              <span>${this.escapeHtml(t('mcp_servers.transport_stdio', 'stdio (Processo Local)'))}</span>
            </label>
            <label class="library-asset-editor-radio">
              <input type="radio" name="lae-mcp-transport" value="sse" ${isSse ? 'checked' : ''} />
              <span>${this.escapeHtml(t('mcp_servers.transport_sse', 'SSE / HTTP (Servidor Remoto)'))}</span>
            </label>
          </div>
        </div>

        <div id="lae-mcp-stdio-fields" style="${isSse ? 'display: none;' : ''}">
          <div class="form-group">
            <label class="form-label" for="lae-mcp-command">${this.escapeHtml(t('mcp_servers.command_label', 'Comando executável'))}</label>
            <input type="text" id="lae-mcp-command" class="form-input code-font" value="${this.escapeHtml(payload.command || '')}" placeholder="npx, uvx, python, node..." />
          </div>

          <div class="form-group">
            <label class="form-label" for="lae-mcp-args">${this.escapeHtml(t('library_asset_editor.field_mcp_args', 'Argumentos (um por linha)'))}</label>
            <textarea id="lae-mcp-args" class="form-textarea code-font" rows="4" spellcheck="false" placeholder="-y&#10;@modelcontextprotocol/server-filesystem&#10;.">${this.escapeHtml((payload.args || []).join('\n'))}</textarea>
          </div>

          <div class="form-group" style="margin-bottom: 0;">
            <label class="form-label" for="lae-mcp-env">${this.escapeHtml(t('library_asset_editor.field_mcp_env', 'Variáveis de ambiente (CHAVE=VALOR, uma por linha)'))}</label>
            <textarea id="lae-mcp-env" class="form-textarea code-font" rows="3" spellcheck="false" placeholder="API_KEY=${'${API_KEY}'}">${this.escapeHtml(serializeEnvLines(payload.env))}</textarea>
            <div class="form-hint">${this.escapeHtml(t('library_asset_editor.hint_env_refs', 'Prefira referências como ${MINHA_CHAVE}; segredos literais são sanitizados automaticamente ao salvar.'))}</div>
          </div>
        </div>

        <div id="lae-mcp-sse-fields" style="${isSse ? '' : 'display: none;'}">
          <div class="form-group">
            <label class="form-label" for="lae-mcp-url">${this.escapeHtml(t('mcp_servers.url_label', 'URL do Endpoint SSE/MCP'))}</label>
            <input type="url" id="lae-mcp-url" class="form-input code-font" value="${this.escapeHtml(payload.url || '')}" placeholder="https://api.exemplo.com/mcp" />
          </div>

          <div class="form-group" style="margin-bottom: 0;">
            <label class="form-label" for="lae-mcp-headers">${this.escapeHtml(t('mcp_servers.headers_label', 'Cabeçalhos HTTP (JSON)'))}</label>
            <textarea id="lae-mcp-headers" class="form-textarea code-font" rows="4" spellcheck="false" placeholder='{"Authorization": "Bearer ${'${API_TOKEN}'}"}'>${this.escapeHtml(payload.headers ? JSON.stringify(payload.headers, null, 2) : '')}</textarea>
            <div class="form-hint">${this.escapeHtml(t('library_asset_editor.hint_env_refs', 'Prefira referências como ${MINHA_CHAVE}; segredos literais são sanitizados automaticamente ao salvar.'))}</div>
          </div>
        </div>
      </div>
    `;
  }

  private renderMcpToolsTab(asset: ILibraryAsset): string {
    if (asset.payload.kind !== 'mcp') return '';
    const tools = asset.payload.cachedTools || [];

    return `
      <div class="settings-section-card library-asset-editor-card">
        <div class="library-asset-editor-card-head">
          <div>
            <h3 class="section-card-title">${this.escapeHtml(t('library_asset_editor.mcp_tools_title', 'Ferramentas declaradas do servidor MCP'))}</h3>
            <p class="section-card-desc">${this.escapeHtml(t('library_asset_editor.mcp_tools_desc', 'Ferramentas em cache exibidas nos seletores de vértices e na auditoria de segurança.'))}</p>
          </div>
          <button type="button" class="btn btn-secondary-office" id="btn-lae-add-mcp-tool">
            ${ribbonIcon('plus', 14)}
            <span>${this.escapeHtml(t('library_asset_editor.btn_add_tool', 'Adicionar ferramenta'))}</span>
          </button>
        </div>

        ${tools.length === 0
          ? `<div class="library-asset-editor-empty">${this.escapeHtml(t('library_asset_editor.mcp_tools_empty', 'Nenhuma ferramenta declarada em cache.'))}</div>`
          : `
            <div class="library-asset-editor-list">
              ${tools.map((tool, index) => `
                <div class="library-asset-editor-list-row" data-mcp-tool-index="${index}">
                  <div class="form-row-grid" style="flex: 1;">
                    <div class="form-group" style="margin-bottom: 0;">
                      <label class="form-label">${this.escapeHtml(t('library_asset_editor.tool_name_label', 'Nome da ferramenta'))}</label>
                      <input type="text" class="form-input code-font lae-mcp-tool-name" data-index="${index}" value="${this.escapeHtml(tool.name)}" placeholder="ex: search_records" />
                    </div>
                    <div class="form-group" style="margin-bottom: 0;">
                      <label class="form-label">${this.escapeHtml(t('library_asset_editor.field_description', 'Descrição'))}</label>
                      <input type="text" class="form-input lae-mcp-tool-desc" data-index="${index}" value="${this.escapeHtml(tool.description || '')}" placeholder="${this.escapeHtml(t('library_asset_editor.tool_desc_placeholder', 'O que esta ferramenta faz'))}" />
                    </div>
                  </div>
                  <button type="button" class="btn btn-danger-office btn-sm lae-remove-mcp-tool" data-index="${index}" title="${this.escapeHtml(t('library_asset_editor.btn_remove', 'Remover'))}">
                    ${ribbonIcon('trash', 14)}
                  </button>
                </div>
              `).join('')}
            </div>
          `}
      </div>
    `;
  }

  private renderAgentModelTab(asset: ILibraryAsset): string {
    if (asset.payload.kind !== 'agent-template') return '';
    const template = asset.payload.template;
    const settings = ensureAgentSettings(template);
    const inheritModel = settings.inheritModel !== false;
    const model = settings.model || {
      provider: 'anthropic' as LLMProvider,
      model: 'claude-sonnet-4-5',
      temperature: 0.3,
      promptTemplate: ''
    };

    return `
      <div class="inheritance-toggle-card library-asset-editor-inherit-card">
        <label class="inheritance-checkbox-label">
          <input type="checkbox" id="lae-inherit-model" ${inheritModel ? 'checked' : ''} />
          <div class="inheritance-label-text">
            <span class="inheritance-title">${this.escapeHtml(t('library_asset_editor.inherit_model_title', 'Herdar provedor e modelo dos padrões do projeto'))}</span>
            <span class="inheritance-desc">${this.escapeHtml(t('library_asset_editor.inherit_model_desc', 'Desmarque para definir provedor, modelo, temperatura e prompt próprios neste ativo.'))}</span>
          </div>
        </label>
        <span class="inheritance-status-badge ${inheritModel ? 'badge-inherited' : 'badge-overridden'}">
          ${this.escapeHtml(inheritModel ? t('node_modals.badge_inherited', 'Herdado') : t('node_modals.badge_overridden', 'Personalizado'))}
        </span>
      </div>

      <div class="settings-section-card library-asset-editor-card ${inheritModel ? 'form-disabled-opacity' : ''}" id="lae-model-fields-wrapper">
        <div class="form-row-grid">
          <div class="form-group">
            <label class="form-label" for="lae-model-provider">${this.escapeHtml(t('node_modals.provider_label', 'Provedor'))}</label>
            <select id="lae-model-provider" class="form-select" ${inheritModel ? 'disabled' : ''}>
              ${PROVIDERS.map(p => `<option value="${p.id}" ${model.provider === p.id ? 'selected' : ''}>${this.escapeHtml(p.label)}</option>`).join('')}
            </select>
          </div>
          <div class="form-group">
            <label class="form-label" for="lae-model-name">${this.escapeHtml(t('node_modals.model_id_label', 'ID do Modelo'))}</label>
            <input type="text" id="lae-model-name" class="form-input" value="${this.escapeHtml(model.model || '')}" placeholder="ex: claude-sonnet-4-5, deepseek-chat" ${inheritModel ? 'disabled' : ''} />
          </div>
        </div>

        <div class="form-row-grid">
          <div class="form-group">
            <label class="form-label" for="lae-model-temperature">${this.escapeHtml(t('node_modals.temperature_label', 'Temperatura'))}</label>
            <input type="number" id="lae-model-temperature" class="form-input" min="0" max="1" step="0.05" value="${model.temperature ?? 0.3}" ${inheritModel ? 'disabled' : ''} />
          </div>
          <div class="form-group">
            <label class="form-label" for="lae-model-reasoning">${this.escapeHtml(t('node_modals.reasoning_effort_label', 'Esforço de Raciocínio'))}</label>
            <select id="lae-model-reasoning" class="form-select" ${inheritModel ? 'disabled' : ''}>
              ${REASONING_LEVELS.map(level => `<option value="${level.id}" ${(model.reasoningEffort || '') === level.id ? 'selected' : ''}>${this.escapeHtml(t(level.labelKey, level.fallback))}</option>`).join('')}
            </select>
          </div>
        </div>

        <div class="form-group">
          <label class="form-label" for="lae-model-skill-path">${this.escapeHtml(t('node_modals.skill_path_label', 'Caminho da Skill (opcional)'))}</label>
          <input type="text" id="lae-model-skill-path" class="form-input code-font" value="${this.escapeHtml(model.skillPath || '')}" placeholder=".agents/minha-skill/SKILL.md" ${inheritModel ? 'disabled' : ''} />
        </div>

        <div class="form-group" style="margin-bottom: 0;">
          <label class="form-label" for="lae-model-prompt">${this.escapeHtml(t('node_modals.prompt_template_label', 'Prompt de instruções'))}</label>
          <textarea id="lae-model-prompt" class="form-textarea" rows="5" ${inheritModel ? 'disabled' : ''}>${this.escapeHtml(model.promptTemplate || '')}</textarea>
        </div>
      </div>
    `;
  }

  private renderTemplateSkillsTab(asset: ILibraryAsset): string {
    if (asset.payload.kind !== 'agent-template' && asset.payload.kind !== 'pipeline') return '';
    const settings = ensureAgentSettings(asset.payload.template);
    const skills = settings.skills || [];

    return `
      <div class="settings-section-card library-asset-editor-card">
        <div class="library-asset-editor-card-head">
          <div>
            <h3 class="section-card-title">${this.escapeHtml(t('library_asset_editor.template_skills_title', 'Skills vinculadas ao ativo'))}</h3>
            <p class="section-card-desc">${this.escapeHtml(t('library_asset_editor.template_skills_desc', 'Edite o Markdown (SKILL.md) de cada skill no editor dedicado que abre sobre este modal.'))}</p>
          </div>
          <button type="button" class="btn btn-primary-office" id="btn-lae-add-template-skill">
            ${ribbonIcon('plus', 14)}
            <span>${this.escapeHtml(t('library_asset_editor.btn_new_skill', 'Nova Skill'))}</span>
          </button>
        </div>

        ${skills.length === 0
          ? `<div class="library-asset-editor-empty">${this.escapeHtml(t('library_asset_editor.template_skills_empty', 'Nenhuma skill vinculada. Clique em Nova Skill para criar uma com o editor Markdown.'))}</div>`
          : `
            <div class="library-asset-editor-list">
              ${skills.map((skill, index) => {
                const fm = extractFrontmatter(skill.markdownContent || '');
                const lineCount = skill.markdownContent ? skill.markdownContent.split(/\r?\n/).length : 0;
                return `
                  <div class="library-asset-editor-skill-item" data-template-skill-index="${index}">
                    <div class="library-asset-editor-skill-head">
                      <div class="library-asset-editor-skill-meta">
                        <span class="library-asset-editor-status-pill ${fm.valid ? 'is-ok' : 'is-warn'}">
                          ${this.escapeHtml(fm.valid ? t('library_asset_editor.md_frontmatter_valid', 'Frontmatter válido') : t('library_asset_editor.md_frontmatter_missing', 'Sem frontmatter completo'))}
                        </span>
                        <span class="library-asset-editor-md-stats">${lineCount} ${this.escapeHtml(t('library_asset_editor.lines_label', 'linhas'))}</span>
                      </div>
                      <div class="library-asset-editor-skill-actions">
                        <button type="button" class="btn btn-primary-office btn-sm lae-edit-template-skill-md" data-index="${index}">
                          ${ribbonIcon('pencil', 13)}
                          <span>${this.escapeHtml(t('library_asset_editor.btn_edit_markdown', 'Editar Markdown'))}</span>
                        </button>
                        <button type="button" class="btn btn-danger-office btn-sm lae-remove-template-skill" data-index="${index}" title="${this.escapeHtml(t('library_asset_editor.btn_remove', 'Remover'))}">
                          ${ribbonIcon('trash', 13)}
                        </button>
                      </div>
                    </div>

                    <div class="form-row-grid">
                      <div class="form-group">
                        <label class="form-label">${this.escapeHtml(t('library_asset_editor.skill_name_label', 'Nome da skill'))}</label>
                        <input type="text" class="form-input lae-tpl-skill-name" data-index="${index}" value="${this.escapeHtml(skill.name)}" />
                      </div>
                      <div class="form-group">
                        <label class="form-label">${this.escapeHtml(t('library_asset_editor.field_description', 'Descrição'))}</label>
                        <input type="text" class="form-input lae-tpl-skill-desc" data-index="${index}" value="${this.escapeHtml(skill.description || '')}" />
                      </div>
                    </div>

                    <div class="form-group" style="margin-bottom: 0;">
                      <label class="form-label">${this.escapeHtml(t('library_asset_editor.field_system_prompt', 'Prompt de sistema'))}</label>
                      <textarea class="form-textarea lae-tpl-skill-prompt" data-index="${index}" rows="2">${this.escapeHtml(skill.systemPrompt || '')}</textarea>
                    </div>
                  </div>
                `;
              }).join('')}
            </div>
          `}
      </div>
    `;
  }

  private renderAgentRuntimeTab(asset: ILibraryAsset): string {
    if (asset.payload.kind !== 'agent-template') return '';
    const template = asset.payload.template;
    const settings = ensureAgentSettings(template);

    const inheritTools = settings.inheritCodeMode !== false;
    const codeMode = settings.codeMode || { allowedTools: ['read', 'write'] };
    const allowedTools = codeMode.allowedTools || [];

    const inheritSandbox = settings.inheritSandbox !== false;
    const sandbox = settings.sandbox || {
      isolationLevel: 'host_direct' as SandboxIsolationLevel,
      network: 'egress_llm_only' as SandboxNetworkPolicy,
      maxMemoryMB: 512,
      timeoutSeconds: 300
    };

    const inheritMemory = settings.inheritMemory !== false;
    const memory = settings.memory || {
      enabled: true,
      mode: 'consult' as VertexMemoryMode,
      persistent: false,
      domains: [],
      contextBudgetTokens: 800
    };

    const lambdaSection = template.taskType !== 'agent'
      ? `
        <div class="settings-section-card library-asset-editor-card">
          <h3 class="section-card-title" style="margin-bottom: 8px;">${this.escapeHtml(t('library_asset_editor.section_lambda', 'Código / Template C++'))}</h3>
          <textarea id="lae-lambda-body" class="form-textarea code-font" rows="5" spellcheck="false">${this.escapeHtml(template.lambdaBodyTemplate || '')}</textarea>
        </div>
      `
      : '';

    return `
      <div class="settings-section-card library-asset-editor-card">
        <div class="library-asset-editor-section-toggle">
          <label class="library-asset-editor-check-inline">
            <input type="checkbox" id="lae-inherit-tools" ${inheritTools ? 'checked' : ''} />
            <strong>${this.escapeHtml(t('library_asset_editor.section_tools', 'Ferramentas nativas'))}</strong>
            <span class="form-hint">(${this.escapeHtml(t('library_asset_editor.inherit_project_short', 'herdar do projeto'))})</span>
          </label>
        </div>

        <div id="lae-tools-fields" class="${inheritTools ? 'form-disabled-opacity' : ''}">
          <div class="library-asset-editor-tools-grid">
            ${NATIVE_TOOLS.map(tool => {
              const checked = allowedTools.includes(tool.id);
              return `
                <label class="tool-checkbox-pill library-asset-editor-tool-pill ${checked ? 'checked' : ''}">
                  <input type="checkbox" class="lae-agent-tool-check" value="${tool.id}" ${checked ? 'checked' : ''} ${inheritTools ? 'disabled' : ''} />
                  <span>${tool.label}</span>
                </label>
              `;
            }).join('')}
          </div>
          <div class="form-row-grid" style="margin-top: 10px;">
            <div class="form-group" style="margin-bottom: 0;">
              <label class="form-label" for="lae-custom-scripts">${this.escapeHtml(t('node_modals.custom_scripts_label', 'Scripts customizados'))}</label>
              <input type="text" id="lae-custom-scripts" class="form-input" value="${this.escapeHtml((codeMode.customScripts || []).join(', '))}" ${inheritTools ? 'disabled' : ''} />
            </div>
            <div class="form-group" style="margin-bottom: 0;">
              <label class="form-label" for="lae-mcp-servers">${this.escapeHtml(t('library_asset_editor.field_mcp_servers', 'Servidores MCP permitidos (IDs)'))}</label>
              <input type="text" id="lae-mcp-servers" class="form-input code-font" value="${this.escapeHtml((codeMode.mcpServers || []).join(', '))}" ${inheritTools ? 'disabled' : ''} />
            </div>
          </div>
        </div>
      </div>

      <div class="settings-section-card library-asset-editor-card">
        <div class="library-asset-editor-section-toggle">
          <label class="library-asset-editor-check-inline">
            <input type="checkbox" id="lae-inherit-sandbox" ${inheritSandbox ? 'checked' : ''} />
            <strong>${this.escapeHtml(t('library_asset_editor.section_sandbox', 'Sandbox & Isolamento'))}</strong>
            <span class="form-hint">(${this.escapeHtml(t('library_asset_editor.inherit_project_short', 'herdar do projeto'))})</span>
          </label>
        </div>

        <div id="lae-sandbox-fields" class="${inheritSandbox ? 'form-disabled-opacity' : ''}">
          <div class="form-row-grid">
            <div class="form-group">
              <label class="form-label" for="lae-sandbox-isolation">${this.escapeHtml(t('library_asset_editor.field_isolation', 'Isolamento'))}</label>
              <select id="lae-sandbox-isolation" class="form-select" ${inheritSandbox ? 'disabled' : ''}>
                <option value="host_direct" ${sandbox.isolationLevel === 'host_direct' ? 'selected' : ''}>Host Direct</option>
                <option value="container" ${sandbox.isolationLevel === 'container' ? 'selected' : ''}>Container</option>
                <option value="microvm" ${sandbox.isolationLevel === 'microvm' ? 'selected' : ''}>MicroVM</option>
              </select>
            </div>
            <div class="form-group">
              <label class="form-label" for="lae-sandbox-network">${this.escapeHtml(t('library_asset_editor.field_network', 'Política de rede'))}</label>
              <select id="lae-sandbox-network" class="form-select" ${inheritSandbox ? 'disabled' : ''}>
                <option value="offline" ${sandbox.network === 'offline' ? 'selected' : ''}>Offline (offline)</option>
                <option value="egress_llm_only" ${sandbox.network === 'egress_llm_only' ? 'selected' : ''}>Somente LLM (egress_llm_only)</option>
                <option value="full" ${sandbox.network === 'full' ? 'selected' : ''}>Completa (full)</option>
              </select>
            </div>
          </div>
          <div class="form-row-grid">
            <div class="form-group" style="margin-bottom: 0;">
              <label class="form-label" for="lae-sandbox-memory">${this.escapeHtml(t('library_asset_editor.field_max_memory', 'Memória máxima (MB)'))}</label>
              <input type="number" id="lae-sandbox-memory" class="form-input" min="64" max="65536" step="64" value="${sandbox.maxMemoryMB ?? 512}" ${inheritSandbox ? 'disabled' : ''} />
            </div>
            <div class="form-group" style="margin-bottom: 0;">
              <label class="form-label" for="lae-sandbox-timeout">${this.escapeHtml(t('library_asset_editor.field_timeout', 'Timeout (segundos)'))}</label>
              <input type="number" id="lae-sandbox-timeout" class="form-input" min="5" max="3600" step="5" value="${sandbox.timeoutSeconds ?? 300}" ${inheritSandbox ? 'disabled' : ''} />
            </div>
          </div>
        </div>
      </div>

      <div class="settings-section-card library-asset-editor-card">
        <div class="library-asset-editor-section-toggle">
          <label class="library-asset-editor-check-inline">
            <input type="checkbox" id="lae-inherit-memory" ${inheritMemory ? 'checked' : ''} />
            <strong>${this.escapeHtml(t('library_asset_editor.section_memory', 'Memória do agente'))}</strong>
            <span class="form-hint">(${this.escapeHtml(t('library_asset_editor.inherit_project_short', 'herdar do projeto'))})</span>
          </label>
        </div>

        <div id="lae-memory-fields" class="${inheritMemory ? 'form-disabled-opacity' : ''}">
          <div class="form-row-grid library-asset-editor-grid-3">
            <div class="form-group" style="margin-bottom: 0;">
              <label class="form-label" for="lae-memory-mode">${this.escapeHtml(t('library_asset_editor.field_memory_mode', 'Modo'))}</label>
              <select id="lae-memory-mode" class="form-select" ${inheritMemory ? 'disabled' : ''}>
                <option value="off" ${memory.mode === 'off' ? 'selected' : ''}>Off</option>
                <option value="consult" ${memory.mode === 'consult' ? 'selected' : ''}>Consult</option>
                <option value="learn" ${memory.mode === 'learn' ? 'selected' : ''}>Learn</option>
              </select>
            </div>
            <div class="form-group" style="margin-bottom: 0;">
              <label class="form-label" for="lae-memory-domains">${this.escapeHtml(t('library_asset_editor.field_memory_domains', 'Domínios'))}</label>
              <input type="text" id="lae-memory-domains" class="form-input" value="${this.escapeHtml((memory.domains || []).join(', '))}" ${inheritMemory ? 'disabled' : ''} />
            </div>
            <div class="form-group" style="margin-bottom: 0;">
              <label class="form-label" for="lae-memory-budget">${this.escapeHtml(t('library_asset_editor.field_memory_budget', 'Orçamento (tokens)'))}</label>
              <input type="number" id="lae-memory-budget" class="form-input" min="100" max="32000" step="100" value="${memory.contextBudgetTokens ?? 800}" ${inheritMemory ? 'disabled' : ''} />
            </div>
          </div>
        </div>
      </div>

      ${lambdaSection}
    `;
  }

  private renderPipelineTab(asset: ILibraryAsset): string {
    if (asset.payload.kind !== 'pipeline') return '';
    const template = asset.payload.template;
    const iface = template.pipelineInterface || {
      input: { name: 'input', description: '' },
      outputs: []
    };
    const nodes = template.childGraph?.nodes || [];
    const edges = template.childGraph?.edges || [];

    return `
      <div class="settings-section-card library-asset-editor-card">
        <h3 class="section-card-title" style="margin-bottom: 10px;">${this.escapeHtml(t('library_asset_editor.pipeline_interface_title', 'Contrato de Entrada'))}</h3>
        <div class="form-row-grid">
          <div class="form-group" style="margin-bottom: 0;">
            <label class="form-label" for="lae-pipe-input-name">${this.escapeHtml(t('library_asset_editor.pipeline_input_name', 'Nome da entrada'))}</label>
            <input type="text" id="lae-pipe-input-name" class="form-input" value="${this.escapeHtml(iface.input?.name || 'input')}" />
          </div>
          <div class="form-group" style="margin-bottom: 0;">
            <label class="form-label" for="lae-pipe-input-desc">${this.escapeHtml(t('library_asset_editor.pipeline_input_desc', 'Descrição da entrada'))}</label>
            <input type="text" id="lae-pipe-input-desc" class="form-input" value="${this.escapeHtml(iface.input?.description || '')}" />
          </div>
        </div>
      </div>

      <div class="settings-section-card library-asset-editor-card">
        <div class="library-asset-editor-card-head">
          <div>
            <h3 class="section-card-title">${this.escapeHtml(t('library_asset_editor.pipeline_graph_title', 'Estrutura do grafo interno'))}</h3>
            <p class="section-card-desc">${this.escapeHtml(t('library_asset_editor.pipeline_graph_summary', '{{nodes}} vértices · {{edges}} conexões', { nodes: nodes.length, edges: edges.length }))}</p>
          </div>
        </div>
        ${nodes.length === 0
          ? `<div class="library-asset-editor-empty">${this.escapeHtml(t('library_asset_editor.pipeline_graph_empty', 'Este pipeline não possui vértices internos registrados.'))}</div>`
          : `
            <table>
              <thead>
                <tr>
                  <th>${this.escapeHtml(t('library_asset_editor.col_vertex', 'Vértice'))}</th>
                  <th>${this.escapeHtml(t('library_asset_editor.col_type', 'Tipo'))}</th>
                  <th>${this.escapeHtml(t('library_asset_editor.col_skills', 'Skills'))}</th>
                </tr>
              </thead>
              <tbody>
                ${nodes.map((node, nodeIdx) => {
                  const nodeSkills = node.agentSettings?.skills || [];
                  return `
                    <tr>
                      <td><strong>${this.escapeHtml(node.label || node.id)}</strong></td>
                      <td><code class="code-font">${this.escapeHtml(node.type)}</code></td>
                      <td>
                        ${nodeSkills.length > 0
                          ? nodeSkills.map((sk, skIdx) => `
                              <button type="button" class="btn btn-secondary-office btn-sm lae-edit-pipeline-node-skill" data-node-index="${nodeIdx}" data-skill-index="${skIdx}">
                                ${ribbonIcon('pencil', 12)} ${this.escapeHtml(sk.name || 'SKILL.md')}
                              </button>
                            `).join(' ')
                          : '<span class="form-hint">—</span>'}
                      </td>
                    </tr>
                  `;
                }).join('')}
              </tbody>
            </table>
          `}
      </div>
    `;
  }

  protected override renderFooter(): string {
    const saveLabel = this.isReadOnlySource
      ? t('library_asset_editor.btn_save_copy', 'Salvar cópia em Minha biblioteca')
      : t('library_asset_editor.btn_save', 'Salvar alterações');
    const footerNote = this.dirty
      ? t('library_asset_editor.footer_dirty', 'Existem alterações pendentes · Ctrl+S para salvar')
      : t('library_asset_editor.footer_hint', 'Ao salvar, o ativo é reauditado automaticamente.');

    return this.renderStandardFooter({
      primaryId: 'btn-lae-save',
      secondaryId: 'btn-lae-cancel',
      primaryLabel: saveLabel,
      secondaryLabel: t('common.cancel', 'Cancelar'),
      extraHtml: `<span id="lae-footer-note">${this.escapeHtml(footerNote)}</span>`
    });
  }

  private markDirty(): void {
    if (!this.dirty) {
      this.dirty = true;
      const note = this.dialog.querySelector('#lae-footer-note');
      if (note) {
        note.textContent = t('library_asset_editor.footer_dirty', 'Existem alterações pendentes · Ctrl+S para salvar');
      }
    }
    if (this.errorMessage) {
      this.errorMessage = '';
      this.dialog.querySelector('#library-asset-editor-error')?.remove();
    }
  }

  protected override saveTabState(): void {
    if (!this.draft) return;

    if (this.activeTabId === 'general') {
      const name = (this.dialog.querySelector('#lae-name') as HTMLInputElement | null)?.value;
      const desc = (this.dialog.querySelector('#lae-description') as HTMLTextAreaElement | null)?.value;
      const domain = (this.dialog.querySelector('#lae-domain') as HTMLInputElement | null)?.value;
      const subdomain = (this.dialog.querySelector('#lae-subdomain') as HTMLInputElement | null)?.value;
      const category = (this.dialog.querySelector('#lae-category') as HTMLInputElement | null)?.value;
      const version = (this.dialog.querySelector('#lae-version') as HTMLInputElement | null)?.value;
      const license = (this.dialog.querySelector('#lae-license') as HTMLInputElement | null)?.value;
      const changelog = (this.dialog.querySelector('#lae-changelog') as HTMLInputElement | null)?.value;

      if (name !== undefined) this.draft.name = name;
      if (desc !== undefined) this.draft.description = desc;
      if (domain !== undefined) this.draft.domain = domain.trim() || undefined;
      if (subdomain !== undefined) this.draft.subdomain = subdomain.trim() || undefined;
      if (category !== undefined) this.draft.category = category;
      if (version !== undefined) this.draft.version = version;
      if (license !== undefined) this.draft.license = license;
      if (changelog !== undefined) this.draft.changelog = changelog.trim() || undefined;
      return;
    }

    if (this.activeTabId === 'skill' && this.draft.payload.kind === 'skill') {
      const promptEl = this.dialog.querySelector('#lae-skill-system-prompt') as HTMLTextAreaElement | null;
      if (promptEl) this.draft.payload.systemPrompt = promptEl.value;

      const checkedTools = Array.from(this.dialog.querySelectorAll<HTMLInputElement>('.lae-skill-tool-check'))
        .filter(input => input.checked)
        .map(input => input.value);
      const customRaw = (this.dialog.querySelector('#lae-skill-custom-tools') as HTMLInputElement | null)?.value || '';
      const customTools = customRaw.split(',').map(s => s.trim()).filter(Boolean);
      this.draft.payload.tools = Array.from(new Set([...checkedTools, ...customTools]));
      return;
    }

    if (this.activeTabId === 'mcp' && this.draft.payload.kind === 'mcp') {
      const transportRadio = this.dialog.querySelector<HTMLInputElement>('input[name="lae-mcp-transport"]:checked');
      if (transportRadio) {
        this.draft.payload.transport = transportRadio.value === 'sse' ? 'sse' : 'stdio';
      }
      const cmd = (this.dialog.querySelector('#lae-mcp-command') as HTMLInputElement | null)?.value;
      const argsRaw = (this.dialog.querySelector('#lae-mcp-args') as HTMLTextAreaElement | null)?.value;
      const envRaw = (this.dialog.querySelector('#lae-mcp-env') as HTMLTextAreaElement | null)?.value;
      const url = (this.dialog.querySelector('#lae-mcp-url') as HTMLInputElement | null)?.value;
      const headersRaw = (this.dialog.querySelector('#lae-mcp-headers') as HTMLTextAreaElement | null)?.value;

      if (cmd !== undefined) this.draft.payload.command = cmd.trim();
      if (argsRaw !== undefined) {
        this.draft.payload.args = argsRaw.split(/\r?\n/).map(line => line.trim()).filter(Boolean);
      }
      if (envRaw !== undefined) {
        this.draft.payload.env = parseEnvLines(envRaw);
      }
      if (url !== undefined) this.draft.payload.url = url.trim();
      if (headersRaw !== undefined) {
        const trimmed = headersRaw.trim();
        if (!trimmed) {
          this.draft.payload.headers = undefined;
          this.invalidMcpHeaders = false;
        } else {
          try {
            const parsed = JSON.parse(trimmed);
            if (parsed && typeof parsed === 'object' && !Array.isArray(parsed)) {
              this.draft.payload.headers = parsed;
              this.invalidMcpHeaders = false;
            } else {
              this.invalidMcpHeaders = true;
            }
          } catch {
            this.invalidMcpHeaders = true;
          }
        }
      }
      return;
    }

    if (this.activeTabId === 'mcp-tools' && this.draft.payload.kind === 'mcp') {
      const tools = this.draft.payload.cachedTools || [];
      this.dialog.querySelectorAll<HTMLInputElement>('.lae-mcp-tool-name').forEach(input => {
        const idx = Number(input.dataset.index);
        if (!Number.isNaN(idx) && tools[idx]) tools[idx].name = input.value.trim();
      });
      this.dialog.querySelectorAll<HTMLInputElement>('.lae-mcp-tool-desc').forEach(input => {
        const idx = Number(input.dataset.index);
        if (!Number.isNaN(idx) && tools[idx]) tools[idx].description = input.value.trim();
      });
      return;
    }

    if (this.activeTabId === 'model' && this.draft.payload.kind === 'agent-template') {
      const settings = ensureAgentSettings(this.draft.payload.template);
      const inheritCheck = this.dialog.querySelector('#lae-inherit-model') as HTMLInputElement | null;
      if (inheritCheck) settings.inheritModel = inheritCheck.checked;

      const provider = (this.dialog.querySelector('#lae-model-provider') as HTMLSelectElement | null)?.value as LLMProvider | undefined;
      const modelName = (this.dialog.querySelector('#lae-model-name') as HTMLInputElement | null)?.value;
      const tempRaw = (this.dialog.querySelector('#lae-model-temperature') as HTMLInputElement | null)?.value;
      const reasoning = (this.dialog.querySelector('#lae-model-reasoning') as HTMLSelectElement | null)?.value as '' | ReasoningEffortLevel | undefined;
      const skillPath = (this.dialog.querySelector('#lae-model-skill-path') as HTMLInputElement | null)?.value;
      const prompt = (this.dialog.querySelector('#lae-model-prompt') as HTMLTextAreaElement | null)?.value;

      const prev = settings.model || { provider: 'anthropic' as LLMProvider, model: 'claude-sonnet-4-5' };
      settings.model = {
        ...prev,
        provider: provider || prev.provider,
        model: modelName !== undefined ? modelName.trim() : prev.model,
        temperature: tempRaw !== undefined && !Number.isNaN(Number(tempRaw)) ? Number(tempRaw) : prev.temperature,
        reasoningEffort: reasoning ? reasoning : undefined,
        skillPath: skillPath !== undefined ? (skillPath.trim() || undefined) : prev.skillPath,
        promptTemplate: prompt !== undefined ? prompt : prev.promptTemplate
      };
      return;
    }

    if (this.activeTabId === 'skills' && (this.draft.payload.kind === 'agent-template' || this.draft.payload.kind === 'pipeline')) {
      const skills = ensureAgentSettings(this.draft.payload.template).skills || [];
      this.dialog.querySelectorAll<HTMLInputElement>('.lae-tpl-skill-name').forEach(input => {
        const idx = Number(input.dataset.index);
        if (!Number.isNaN(idx) && skills[idx]) skills[idx].name = input.value;
      });
      this.dialog.querySelectorAll<HTMLInputElement>('.lae-tpl-skill-desc').forEach(input => {
        const idx = Number(input.dataset.index);
        if (!Number.isNaN(idx) && skills[idx]) skills[idx].description = input.value;
      });
      this.dialog.querySelectorAll<HTMLTextAreaElement>('.lae-tpl-skill-prompt').forEach(input => {
        const idx = Number(input.dataset.index);
        if (!Number.isNaN(idx) && skills[idx]) skills[idx].systemPrompt = input.value;
      });
      return;
    }

    if (this.activeTabId === 'runtime' && this.draft.payload.kind === 'agent-template') {
      const template = this.draft.payload.template;
      const settings = ensureAgentSettings(template);

      const inheritTools = (this.dialog.querySelector('#lae-inherit-tools') as HTMLInputElement | null)?.checked;
      if (inheritTools !== undefined) settings.inheritCodeMode = inheritTools;
      const allowedTools = Array.from(this.dialog.querySelectorAll<HTMLInputElement>('.lae-agent-tool-check'))
        .filter(i => i.checked)
        .map(i => i.value as CodeModeTool);
      const customScripts = ((this.dialog.querySelector('#lae-custom-scripts') as HTMLInputElement | null)?.value || '')
        .split(',').map(s => s.trim()).filter(Boolean);
      const mcpServers = ((this.dialog.querySelector('#lae-mcp-servers') as HTMLInputElement | null)?.value || '')
        .split(',').map(s => s.trim()).filter(Boolean);
      settings.codeMode = {
        ...(settings.codeMode || {}),
        allowedTools,
        customScripts,
        mcpServers
      };

      const inheritSandbox = (this.dialog.querySelector('#lae-inherit-sandbox') as HTMLInputElement | null)?.checked;
      if (inheritSandbox !== undefined) settings.inheritSandbox = inheritSandbox;
      const isolation = (this.dialog.querySelector('#lae-sandbox-isolation') as HTMLSelectElement | null)?.value as SandboxIsolationLevel | undefined;
      const network = (this.dialog.querySelector('#lae-sandbox-network') as HTMLSelectElement | null)?.value as SandboxNetworkPolicy | undefined;
      const maxMem = Number((this.dialog.querySelector('#lae-sandbox-memory') as HTMLInputElement | null)?.value || 512);
      const timeout = Number((this.dialog.querySelector('#lae-sandbox-timeout') as HTMLInputElement | null)?.value || 300);
      settings.sandbox = {
        ...(settings.sandbox || { isolationLevel: 'host_direct', network: 'egress_llm_only', maxMemoryMB: 512, timeoutSeconds: 300 }),
        isolationLevel: isolation || 'host_direct',
        network: network || 'egress_llm_only',
        maxMemoryMB: Number.isFinite(maxMem) && maxMem > 0 ? maxMem : 512,
        timeoutSeconds: Number.isFinite(timeout) && timeout > 0 ? timeout : 300
      };

      const inheritMemory = (this.dialog.querySelector('#lae-inherit-memory') as HTMLInputElement | null)?.checked;
      if (inheritMemory !== undefined) settings.inheritMemory = inheritMemory;
      const memMode = (this.dialog.querySelector('#lae-memory-mode') as HTMLSelectElement | null)?.value as VertexMemoryMode | undefined;
      const memDomains = ((this.dialog.querySelector('#lae-memory-domains') as HTMLInputElement | null)?.value || '')
        .split(',').map(s => s.trim()).filter(Boolean);
      const memBudget = Number((this.dialog.querySelector('#lae-memory-budget') as HTMLInputElement | null)?.value || 800);
      settings.memory = {
        ...(settings.memory || { enabled: true, mode: 'consult', persistent: false }),
        enabled: memMode !== 'off',
        mode: memMode || 'consult',
        domains: memDomains,
        contextBudgetTokens: Number.isFinite(memBudget) && memBudget > 0 ? memBudget : 800
      };

      const lambdaEl = this.dialog.querySelector('#lae-lambda-body') as HTMLTextAreaElement | null;
      if (lambdaEl) {
        template.lambdaBodyTemplate = lambdaEl.value;
      }
      return;
    }

    if (this.activeTabId === 'pipeline' && this.draft.payload.kind === 'pipeline') {
      const template = this.draft.payload.template;
      const inputName = (this.dialog.querySelector('#lae-pipe-input-name') as HTMLInputElement | null)?.value;
      const inputDesc = (this.dialog.querySelector('#lae-pipe-input-desc') as HTMLInputElement | null)?.value;
      if (!template.pipelineInterface) {
        template.pipelineInterface = { input: { name: 'input' }, outputs: [] };
      }
      if (inputName !== undefined) template.pipelineInterface.input.name = inputName.trim() || 'input';
      if (inputDesc !== undefined) template.pipelineInterface.input.description = inputDesc.trim() || undefined;
    }
  }

  protected override bindEvents(): void {
    super.bindEvents();
    if (!this.draft) return;

    const cancelBtn = this.dialog.querySelector('#btn-lae-cancel') as HTMLButtonElement | null;
    if (cancelBtn && cancelBtn.dataset.laeBound !== 'true') {
      cancelBtn.dataset.laeBound = 'true';
      cancelBtn.addEventListener('click', () => this.hide());
    }
    const saveBtn = this.dialog.querySelector('#btn-lae-save') as HTMLButtonElement | null;
    if (saveBtn && saveBtn.dataset.laeBound !== 'true') {
      saveBtn.dataset.laeBound = 'true';
      saveBtn.addEventListener('click', () => {
        void this.handleSave();
      });
    }

    this.bindBodyEvents();
  }

  private bindBodyEvents(): void {
    if (!this.draft) return;

    // Marca rascunho como sujo em qualquer alteração de campo
    this.dialog.querySelectorAll<HTMLElement>('.modal-body-scrollable input, .modal-body-scrollable textarea, .modal-body-scrollable select').forEach(el => {
      el.addEventListener('input', () => this.markDirty());
      el.addEventListener('change', () => this.markDirty());
    });

    // Visual das pills de ferramentas
    this.dialog.querySelectorAll<HTMLInputElement>('.lae-skill-tool-check, .lae-agent-tool-check').forEach(input => {
      input.addEventListener('change', () => {
        input.closest('.tool-checkbox-pill')?.classList.toggle('checked', input.checked);
      });
    });

    // Skill: abrir SkillEditorModal sobre este modal
    this.dialog.querySelector('#btn-open-skill-md-editor')?.addEventListener('click', () => {
      this.openMarkdownEditorForSkillAsset();
    });

    // MCP: alternar campos stdio vs sse
    this.dialog.querySelectorAll<HTMLInputElement>('input[name="lae-mcp-transport"]').forEach(radio => {
      radio.addEventListener('change', () => {
        const isSse = radio.value === 'sse' && radio.checked;
        const stdioEl = this.dialog.querySelector('#lae-mcp-stdio-fields') as HTMLElement | null;
        const sseEl = this.dialog.querySelector('#lae-mcp-sse-fields') as HTMLElement | null;
        if (stdioEl) stdioEl.style.display = isSse ? 'none' : '';
        if (sseEl) sseEl.style.display = isSse ? '' : 'none';
      });
    });

    // MCP: adicionar/remover ferramenta em cache
    this.dialog.querySelector('#btn-lae-add-mcp-tool')?.addEventListener('click', () => {
      if (!this.draft || this.draft.payload.kind !== 'mcp') return;
      this.saveTabState();
      const payload = this.draft.payload as IMcpAssetPayload;
      payload.cachedTools = payload.cachedTools ? [...payload.cachedTools] : [];
      payload.cachedTools.push({
        name: `tool_${payload.cachedTools.length + 1}`,
        description: '',
        serverId: this.draft.id
      });
      this.markDirty();
      this.refreshTabBadges();
      this.rerenderActiveTabBody();
    });

    this.dialog.querySelectorAll<HTMLButtonElement>('.lae-remove-mcp-tool').forEach(btn => {
      btn.addEventListener('click', () => {
        if (!this.draft || this.draft.payload.kind !== 'mcp') return;
        this.saveTabState();
        const idx = Number(btn.dataset.index);
        const tools = this.draft.payload.cachedTools || [];
        if (!Number.isNaN(idx) && idx >= 0 && idx < tools.length) {
          tools.splice(idx, 1);
          this.markDirty();
          this.refreshTabBadges();
          this.rerenderActiveTabBody();
        }
      });
    });

    // Agent Template: toggles de herança
    this.bindInheritanceToggle('#lae-inherit-model', '#lae-model-fields-wrapper');
    this.bindInheritanceToggle('#lae-inherit-tools', '#lae-tools-fields');
    this.bindInheritanceToggle('#lae-inherit-sandbox', '#lae-sandbox-fields');
    this.bindInheritanceToggle('#lae-inherit-memory', '#lae-memory-fields');

    // Template Skills: adicionar, editar Markdown (abre SkillEditorModal sobre este modal) e remover
    this.dialog.querySelector('#btn-lae-add-template-skill')?.addEventListener('click', () => {
      if (!this.draft || (this.draft.payload.kind !== 'agent-template' && this.draft.payload.kind !== 'pipeline')) return;
      this.saveTabState();
      const settings = ensureAgentSettings(this.draft.payload.template);
      settings.skills = settings.skills ? [...settings.skills] : [];
      const newSkill: INodeSkill = {
        id: `skill-${Date.now().toString(36)}`,
        name: t('library_asset_editor.new_skill_default_name', 'Nova Skill'),
        description: '',
        markdownContent: `---\nname: Nova Skill\ndescription: Instruções especializadas para este agente.\n---\n\n# Nova Skill\n\nDescreva as diretrizes operacionais.\n`,
        systemPrompt: '',
        tools: ['read', 'write']
      };
      settings.skills.push(newSkill);
      const newIdx = settings.skills.length - 1;
      this.markDirty();
      this.refreshTabBadges();
      this.rerenderActiveTabBody();
      this.openMarkdownEditorForTemplateSkill(newIdx);
    });

    this.dialog.querySelectorAll<HTMLButtonElement>('.lae-edit-template-skill-md').forEach(btn => {
      btn.addEventListener('click', () => {
        const idx = Number(btn.dataset.index);
        if (!Number.isNaN(idx)) {
          this.openMarkdownEditorForTemplateSkill(idx);
        }
      });
    });

    this.dialog.querySelectorAll<HTMLButtonElement>('.lae-remove-template-skill').forEach(btn => {
      btn.addEventListener('click', () => {
        if (!this.draft || (this.draft.payload.kind !== 'agent-template' && this.draft.payload.kind !== 'pipeline')) return;
        this.saveTabState();
        const idx = Number(btn.dataset.index);
        const skills = ensureAgentSettings(this.draft.payload.template).skills || [];
        if (!Number.isNaN(idx) && idx >= 0 && idx < skills.length) {
          skills.splice(idx, 1);
          this.markDirty();
          this.refreshTabBadges();
          this.rerenderActiveTabBody();
        }
      });
    });

    // Pipeline: editar Markdown de skill de vértice interno
    this.dialog.querySelectorAll<HTMLButtonElement>('.lae-edit-pipeline-node-skill').forEach(btn => {
      btn.addEventListener('click', () => {
        const nodeIdx = Number(btn.dataset.nodeIndex);
        const skillIdx = Number(btn.dataset.skillIndex);
        if (!Number.isNaN(nodeIdx) && !Number.isNaN(skillIdx)) {
          this.openMarkdownEditorForPipelineNodeSkill(nodeIdx, skillIdx);
        }
      });
    });
  }

  private bindInheritanceToggle(checkboxSelector: string, wrapperSelector: string): void {
    const chk = this.dialog.querySelector(checkboxSelector) as HTMLInputElement | null;
    const wrapper = this.dialog.querySelector(wrapperSelector) as HTMLElement | null;
    if (!chk || !wrapper) return;
    chk.addEventListener('change', () => {
      const inherited = chk.checked;
      wrapper.classList.toggle('form-disabled-opacity', inherited);
      wrapper.querySelectorAll<HTMLInputElement | HTMLSelectElement | HTMLTextAreaElement>('input, select, textarea').forEach(field => {
        field.disabled = inherited;
      });
      const badgeEl = chk.closest('.inheritance-toggle-card')?.querySelector('.inheritance-status-badge');
      if (badgeEl) {
        badgeEl.className = `inheritance-status-badge ${inherited ? 'badge-inherited' : 'badge-overridden'}`;
        badgeEl.textContent = inherited
          ? t('node_modals.badge_inherited', 'Herdado')
          : t('node_modals.badge_overridden', 'Personalizado');
      }
    });
  }

  private rerenderActiveTabBody(): void {
    const bodyContainer = this.dialog.querySelector('.modal-body-scrollable');
    if (bodyContainer) {
      bodyContainer.innerHTML = this.renderTabContent(this.activeTabId);
      this.bindBodyEvents();
    }
  }

  private openMarkdownEditorForSkillAsset(): void {
    if (!this.draft || this.draft.payload.kind !== 'skill') return;
    this.saveTabState();
    const skillPayload = this.draft.payload as ISkillAssetPayload;
    const editor = this.getSkillEditor();
    editor.openSkill({
      path: `.workspace/library-edits/${this.draft.id}/SKILL.md`,
      initialContent: skillPayload.markdownContent,
      subtitle: `${t('library_asset_editor.library_prefix', 'Biblioteca')}: ${this.draft.name} · SKILL.md`,
      skipDiskWrite: true,
      onSave: (_savedPath, content) => {
        if (!this.draft || this.draft.payload.kind !== 'skill') return;
        this.draft.payload.markdownContent = content;
        this.infoNotice = t('library_asset_editor.md_updated_notice', 'Markdown atualizado no rascunho. Clique em Salvar para aplicar e auditar.');
        this.markDirty();
        this.rerenderActiveTabBody();
      }
    });
  }

  private openMarkdownEditorForTemplateSkill(index: number): void {
    if (!this.draft || (this.draft.payload.kind !== 'agent-template' && this.draft.payload.kind !== 'pipeline')) return;
    this.saveTabState();
    const skills = ensureAgentSettings(this.draft.payload.template).skills || [];
    const skill = skills[index];
    if (!skill) return;

    const editor = this.getSkillEditor();
    editor.openSkill({
      path: `.workspace/library-edits/${this.draft.id}/${skill.id || `skill-${index}`}/SKILL.md`,
      initialContent: skill.markdownContent || '',
      subtitle: `${this.draft.name} · ${skill.name || 'SKILL.md'}`,
      skipDiskWrite: true,
      onSave: (_savedPath, content) => {
        if (!this.draft || (this.draft.payload.kind !== 'agent-template' && this.draft.payload.kind !== 'pipeline')) return;
        const currentSkills = ensureAgentSettings(this.draft.payload.template).skills || [];
        const target = currentSkills[index];
        if (!target) return;
        target.markdownContent = content;
        const fm = extractFrontmatter(content);
        if (fm.name && (!target.name || target.name === t('library_asset_editor.new_skill_default_name', 'Nova Skill'))) {
          target.name = fm.name;
        }
        if (fm.description && !target.description) {
          target.description = fm.description;
        }
        target.updatedAt = new Date().toISOString();
        this.infoNotice = t('library_asset_editor.md_updated_notice', 'Markdown atualizado no rascunho. Clique em Salvar para aplicar e auditar.');
        this.markDirty();
        this.rerenderActiveTabBody();
      }
    });
  }

  private openMarkdownEditorForPipelineNodeSkill(nodeIdx: number, skillIdx: number): void {
    if (!this.draft || this.draft.payload.kind !== 'pipeline') return;
    this.saveTabState();
    const node = this.draft.payload.template.childGraph?.nodes?.[nodeIdx];
    const skill = node?.agentSettings?.skills?.[skillIdx];
    if (!node || !skill) return;

    const editor = this.getSkillEditor();
    editor.openSkill({
      path: `.workspace/library-edits/${this.draft.id}/${node.id}/${skill.id || `skill-${skillIdx}`}/SKILL.md`,
      initialContent: skill.markdownContent || '',
      subtitle: `${this.draft.name} › ${node.label || node.id} · ${skill.name || 'SKILL.md'}`,
      skipDiskWrite: true,
      onSave: (_savedPath, content) => {
        const targetNode = this.draft?.payload.kind === 'pipeline'
          ? this.draft.payload.template.childGraph?.nodes?.[nodeIdx]
          : undefined;
        const targetSkill = targetNode?.agentSettings?.skills?.[skillIdx];
        if (!targetSkill) return;
        targetSkill.markdownContent = content;
        targetSkill.updatedAt = new Date().toISOString();
        this.infoNotice = t('library_asset_editor.md_updated_notice', 'Markdown atualizado no rascunho. Clique em Salvar para aplicar e auditar.');
        this.markDirty();
        this.rerenderActiveTabBody();
      }
    });
  }

  private validateBeforeSave(): string | null {
    if (!this.draft) return t('library_asset_editor.no_asset', 'Nenhum ativo selecionado.');
    if (!this.draft.name.trim()) {
      return t('library_asset_editor.error_empty_name', 'Informe um nome para o ativo.');
    }
    if (this.draft.payload.kind === 'mcp' && this.invalidMcpHeaders) {
      return t('library_asset_editor.error_invalid_headers', 'Cabeçalhos HTTP devem ser um objeto JSON válido.');
    }
    return null;
  }

  private async handleSave(): Promise<void> {
    if (!this.draft || this.saving || !this.onSaveHandler) return;
    this.saveTabState();

    const validationError = this.validateBeforeSave();
    if (validationError) {
      this.errorMessage = validationError;
      this.rerenderActiveTabBody();
      return;
    }

    const saveBtn = this.dialog.querySelector('#btn-lae-save') as HTMLButtonElement | null;
    this.saving = true;
    if (saveBtn) saveBtn.disabled = true;

    try {
      const result = await this.onSaveHandler({
        metadata: {
          name: this.draft.name.trim(),
          description: this.draft.description,
          domain: this.draft.domain,
          subdomain: this.draft.subdomain,
          category: this.draft.category,
          version: this.draft.version,
          license: this.draft.license,
          changelog: this.draft.changelog
        },
        payload: this.draft.payload
      });

      if (!result.ok) {
        this.errorMessage = result.reason || t('properties_window.libraries.msg_edit_failed', 'Não foi possível atualizar o ativo.');
        this.saving = false;
        if (saveBtn) saveBtn.disabled = false;
        this.rerenderActiveTabBody();
        return;
      }

      this.dirty = false;
      this.saving = false;
      super.hide();
    } catch (err: any) {
      this.errorMessage = err?.message || t('properties_window.libraries.msg_edit_failed', 'Não foi possível atualizar o ativo.');
      this.saving = false;
      if (saveBtn) saveBtn.disabled = false;
      this.rerenderActiveTabBody();
    }
  }

  public override hide(): void {
    if (this.dirty && !this.saving && typeof window !== 'undefined' && typeof window.confirm === 'function') {
      if (!window.confirm(t('properties_window.note.discard', 'Descartar as alterações não salvas?'))) {
        return;
      }
    }
    this.dirty = false;
    this.saving = false;
    this.errorMessage = '';
    this.infoNotice = '';
    this.invalidMcpHeaders = false;
    super.hide();
  }

  public override destroy(): void {
    if (this.boundShortcutHandler && typeof window !== 'undefined') {
      window.removeEventListener('keydown', this.boundShortcutHandler);
      this.boundShortcutHandler = undefined;
    }
    this.skillEditorModal?.destroy();
    this.skillEditorModal = undefined;
    super.destroy();
  }
}
