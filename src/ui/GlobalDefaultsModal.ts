import { Graph } from '../core/Graph.ts';
import { globalEventBus } from '../core/EventBus.ts';
import { IGlobalPipelineDefaults, LLMProvider, SandboxIsolationLevel, SandboxNetworkPolicy, CodeModeTool, DEFAULT_PIPELINE_DEFAULTS } from '../core/types.ts';
import { ModalWindowManager, IModalWindowController } from './modals/ModalWindowManager.ts';
import { EnvironmentManager } from '../core/env/EnvironmentManager.ts';
import { EnvGuidanceModal } from './modals/EnvGuidanceModal.ts';
import { t } from '../i18n/index.ts';
import { McpToolPicker } from './McpToolPicker.ts';
import { LocalModelsModal } from './modals/LocalModelsModal.ts';
import {
  LOCAL_LLM_DEFAULT_URL,
  LOCAL_LLM_ENV,
  discoverLocalModels,
  loadLocalBaseUrl,
  normalizeLocalBaseUrl,
  renderModelChips,
  saveLocalBaseUrl
} from './LocalModelField.ts';

export type GlobalDefaultsTab = 'model' | 'sandbox' | 'tools' | 'memory';

export class GlobalDefaultsModal {
  private overlay: HTMLElement;
  private graph: Graph;
  private windowController?: IModalWindowController;
  private activeTab: GlobalDefaultsTab = 'model';
  private mcpPicker?: McpToolPicker;
  private localModelsModal?: LocalModelsModal;
  private runtimeManaged = false;

  constructor(graph: Graph) {
    this.graph = graph;

    this.overlay = document.createElement('div');
    this.overlay.className = 'modal-overlay hidden';
    document.body.appendChild(this.overlay);

    this.bindEvents();
  }

  public show(): void {
    this.render();
    this.overlay.classList.remove('hidden');
    this.windowController?.bringToFront();
    this.updateKeyIndicator();
    void this.loadStudioModel();
  }

  public hide(): void {
    this.overlay.classList.add('hidden');
  }

  private render(): void {
    const defaults = this.graph.getGlobalDefaults();

    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: t('global_defaults.title'),
      subtitle: t('global_defaults.subtitle'),
      icon: '⚙️',
      closeBtnId: 'btn-close-defaults-modal'
    });

    this.overlay.innerHTML = `
      <div class="modal-dialog global-defaults-dialog" style="max-width: 780px; width: 92%; max-height: 88vh; display: flex; flex-direction: column;">
        ${titleBarHtml}

        <!-- Abas do Modal / Modal Tabs com Escopos Claros -->
        <div class="modal-tabs">
          <button type="button" class="tab-btn ${this.activeTab === 'model' ? 'active' : ''}" data-tab="model">
            ${t('global_defaults.tab_model')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'sandbox' ? 'active' : ''}" data-tab="sandbox">
            ${t('global_defaults.tab_sandbox')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'tools' ? 'active' : ''}" data-tab="tools">
            ${t('global_defaults.tab_tools')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'memory' ? 'active' : ''}" data-tab="memory">
            ${t('global_defaults.tab_memory')}
          </button>
        </div>

        <div class="modal-body-scrollable" style="flex: 1; overflow-y: auto; padding: 16px;">
          <!-- ABA 1: Provedor & Modelo (Projeto) + Credenciais Globais (Studio) -->
          <div class="tab-pane" id="pane-tab-model" style="${this.activeTab === 'model' ? '' : 'display: none;'}">
            <div class="settings-section-card">
              <div class="section-card-header">
                <span class="section-card-icon">🧠</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('global_defaults.model_section_title')} <span class="badge-inherited" style="font-size: 10px; margin-left: 6px;">${t('global_defaults.badge_project_scope')}</span></h3>
                  <p class="section-card-desc">${t('global_defaults.model_section_desc')}</p>
                </div>
              </div>

              <div class="form-row-grid">
                <div class="form-group">
                  <label class="form-label" for="def-provider">${t('global_defaults.provider_label')}</label>
                  <select id="def-provider" class="form-select">
                    <option value="deepseek" ${defaults.model.provider === 'deepseek' ? 'selected' : ''}>DeepSeek (v4-pro / Flash)</option>
                    <option value="anthropic" ${defaults.model.provider === 'anthropic' ? 'selected' : ''}>Anthropic (Claude Opus 5.5 / Sonnet)</option>
                    <option value="google" ${defaults.model.provider === 'google' ? 'selected' : ''}>Google (Gemini 2.5 Flash / Pro)</option>
                    <option value="mistral" ${defaults.model.provider === 'mistral' ? 'selected' : ''}>Mistral (Large / Codestral)</option>
                    <option value="openai" ${defaults.model.provider === 'openai' ? 'selected' : ''}>OpenAI (GPT-4o / o3)</option>
                    <option value="jev" ${defaults.model.provider === 'jev' ? 'selected' : ''}>Jev / Engine Visual Local</option>
                    <option value="local" ${defaults.model.provider === 'local' ? 'selected' : ''}>Local / OpenAI-compatível (Ollama, LM Studio, llama.cpp, vLLM)</option>
                  </select>
                </div>

                <div class="form-group">
                  <label class="form-label" for="def-model">${t('global_defaults.model_name_label')}</label>
                  <input type="text" id="def-model" class="form-input" value="${this.escapeHtml(defaults.model.model)}" placeholder="ex: deepseek-flash, claude-opus-5.5" />
                </div>
              </div>

              <!-- Servidor local já em execução (Ollama / llama.cpp / vLLM / LM Studio / Jev) -->
              <div class="form-group" id="def-local-endpoint-group" style="display: none; margin-top: 12px;">
                <label class="form-label" for="def-local-base-url" id="def-local-endpoint-label"></label>
                <div style="display: flex; gap: 8px; align-items: center;">
                  <input type="text" id="def-local-base-url" class="form-input" style="font-family: monospace;" placeholder="${LOCAL_LLM_DEFAULT_URL}" />
                  <button type="button" class="btn btn-secondary-pill" id="btn-def-discover-models" style="white-space: nowrap; height: 34px; padding: 0 14px; font-size: 12px;">
                    ${t('global_defaults.local_discover_btn')}
                  </button>
                </div>
                <div class="form-hint" id="def-local-endpoint-hint"></div>
                <div style="margin-top: 8px;">
                  <button type="button" class="btn btn-secondary-pill" id="btn-open-local-models" style="font-size: 12px; height: 30px; padding: 0 12px;">
                    ${t('local_models.manage_btn')}
                  </button>
                </div>
                <div id="def-local-models" style="display: none; flex-wrap: wrap; gap: 6px; margin-top: 8px;"></div>
                <div id="def-local-model-status" style="display: none; margin-top: 6px; font-size: 11.5px;"></div>
              </div>

              <!-- Seção de Conexão com API e Credenciais Globais do Studio -->
              <div style="margin-top: 14px; padding: 12px; background: #faf9f8; border: 1px solid #edebe9; border-radius: 6px;">
                <div style="display: flex; justify-content: space-between; align-items: flex-start; gap: 12px; margin-bottom: 8px;">
                  <div>
                    <div style="font-weight: 700; font-size: 13px; color: #242424; display: flex; align-items: center; gap: 8px;">
                      <span>${t('global_defaults.credentials_title')}</span>
                      <span class="badge-inherited" style="font-size: 9px; padding: 1px 6px;">${t('global_defaults.badge_studio_scope')}</span>
                      <span id="api-key-indicator" style="font-size: 11px; padding: 2px 8px; border-radius: 10px; font-weight: 600; background: #f3f2f1; color: #616161;">${t('global_defaults.key_checking')}</span>
                    </div>
                    <p style="margin: 4px 0 0 0; font-size: 12px; color: #616161; line-height: 1.4;">
                      ${t('global_defaults.credentials_desc')}
                    </p>
                  </div>
                  <div style="display: flex; gap: 8px; flex-shrink: 0;">
                    <button type="button" class="btn btn-secondary-pill" id="btn-test-agent-api" style="font-size: 12px; font-weight: 600; height: 32px; padding: 0 14px; white-space: nowrap;">
                      ${t('global_defaults.btn_test_api')}
                    </button>
                    <button type="button" class="btn btn-primary" id="btn-toggle-deploy-key" style="font-size: 12px; font-weight: 600; height: 32px; padding: 0 14px; white-space: nowrap;">
                      ${t('global_defaults.btn_deploy_key')}
                    </button>
                  </div>
                </div>

                <!-- Resultado do Teste de Conexão -->
                <div id="api-test-status" style="display: none; margin-top: 8px; padding: 8px 12px; border-radius: 4px; font-size: 12px;"></div>

                <!-- Painel de Implantação de Chave no Ambiente -->
                <div id="panel-deploy-key" style="display: none; margin-top: 10px; padding-top: 10px; border-top: 1px solid #edebe9;">
                  <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 8px;">
                    <span style="font-weight: 600; font-size: 12px; color: #242424;">${t('global_defaults.deploy_panel_title')}</span>
                    <button type="button" class="btn btn-secondary-pill" id="btn-open-env-guidance" style="font-size: 11px; padding: 2px 8px; height: 26px;">
                      ${t('global_defaults.btn_guidance')}
                    </button>
                  </div>
                  <div style="display: flex; gap: 8px; margin-bottom: 6px;">
                    <div style="flex: 1; max-width: 220px;">
                      <input type="text" id="deploy-key-name" class="form-input" style="height: 32px; font-size: 12px; font-family: monospace;" value="DEEPSEEK_API_KEY" placeholder="NOME_DA_VARIAVEL" />
                    </div>
                    <div style="flex: 2;">
                      <input type="password" id="deploy-key-value" class="form-input" style="height: 32px; font-size: 12px; font-family: monospace;" placeholder="Cole o token (sk-...)" />
                    </div>
                    <button type="button" class="btn btn-primary" id="btn-submit-deploy-key" style="height: 32px; font-size: 12px; padding: 0 14px; white-space: nowrap;">
                      ${t('global_defaults.btn_save_key')}
                    </button>
                  </div>
                  <div id="deploy-save-feedback" style="display: none; font-size: 11.5px; padding: 6px 10px; border-radius: 4px;"></div>
                </div>
              </div>

              <!-- AI Gateway empresarial (escopo Studio; runners usam as mesmas variáveis) -->
              <div class="settings-section-card" style="margin-top: 16px;" id="card-ai-gateway">
                <div class="section-card-header">
                  <span class="section-card-icon">🛡️</span>
                  <div class="section-card-titles">
                    <h3 class="section-card-title">${t('global_defaults.gateway_title')} <span class="badge-inherited" style="font-size: 10px; margin-left: 6px;">${t('global_defaults.badge_studio_scope')}</span></h3>
                    <p class="section-card-desc">${t('global_defaults.gateway_desc')}</p>
                  </div>
                </div>

                <div class="form-row-grid">
                  <div class="form-group">
                    <label class="form-label" for="def-gateway-url">${t('global_defaults.gateway_url_label')}</label>
                    <input type="text" id="def-gateway-url" class="form-input" style="font-family: monospace;" placeholder="https://gateway.empresa.com" />
                  </div>
                  <div class="form-group">
                    <label class="form-label" for="def-gateway-key">${t('global_defaults.gateway_key_label')}</label>
                    <input type="password" id="def-gateway-key" class="form-input" style="font-family: monospace;" placeholder="av_..." />
                    <div class="form-hint" id="def-gateway-key-hint"></div>
                  </div>
                </div>

                <label style="display: flex; align-items: center; gap: 8px; margin-top: 10px; font-size: 12px; color: #424242;">
                  <input type="checkbox" id="def-gateway-require" />
                  ${t('global_defaults.gateway_require_label')}
                </label>

                <div style="display: flex; flex-wrap: wrap; gap: 8px; margin-top: 12px;">
                  <button type="button" class="btn btn-primary" id="btn-save-gateway" style="font-size: 12px; font-weight: 600; height: 32px; padding: 0 14px;">${t('global_defaults.gateway_save')}</button>
                  <button type="button" class="btn btn-secondary-pill" id="btn-test-gateway" style="font-size: 12px; height: 32px; padding: 0 14px;">${t('global_defaults.gateway_test')}</button>
                  <button type="button" class="btn btn-secondary-pill" id="btn-clear-gateway" style="font-size: 12px; height: 32px; padding: 0 14px;">${t('global_defaults.gateway_clear')}</button>
                </div>

                <div id="gateway-policy-hint" class="form-hint" style="display: none; margin-top: 8px; color: #a80000;"></div>
                <div id="gateway-test-status" style="display: none; margin-top: 8px; padding: 8px 12px; border-radius: 4px; font-size: 12px;"></div>
                <div id="gateway-usage-status" class="form-hint" style="margin-top: 8px;"></div>
                <div class="form-hint" style="margin-top: 6px;">${t('global_defaults.gateway_local_exempt')}</div>
              </div>

              <div class="form-group" style="margin-top: 14px;">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 4px;">
                  <label class="form-label" for="def-skill" style="margin: 0;">${t('global_defaults.skill_label')}</label>
                  <button type="button" class="btn btn-secondary btn-sm" id="btn-defaults-open-github-repos" style="font-size: 11px; padding: 2px 8px; font-weight: 600;">
                    ${t('global_defaults.btn_github_repos')}
                  </button>
                </div>
                <input type="text" id="def-skill" class="form-input" value="${this.escapeHtml(defaults.model.skillPath || '')}" placeholder=".agents/default/SKILL.md" />
              </div>

              <div class="form-group" style="margin-top: 14px;">
                <label class="form-label" for="def-prompt">${t('global_defaults.prompt_label')}</label>
                <textarea id="def-prompt" class="form-textarea" rows="3" placeholder="Digite as diretrizes de prompt para este agente...">${this.escapeHtml(defaults.model.promptTemplate || '')}</textarea>
                <div class="form-hint">${t('global_defaults.prompt_hint')}</div>
              </div>
            </div>

            <!-- Modelo padrão do próprio Studio (escopo Studio): fica só nesta máquina, em variáveis de ambiente -->
            <div class="settings-section-card" style="margin-top: 16px;" id="card-studio-model">
              <div class="section-card-header">
                <span class="section-card-icon">🏢</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('global_defaults.studio_model_title')} <span class="badge-inherited" style="font-size: 10px; margin-left: 6px;">${t('global_defaults.badge_studio_scope')}</span></h3>
                  <p class="section-card-desc">${t('global_defaults.studio_model_desc')}</p>
                </div>
              </div>

              <div class="form-row-grid">
                <div class="form-group">
                  <label class="form-label" for="studio-provider">${t('global_defaults.provider_label')}</label>
                  <select id="studio-provider" class="form-select">
                    <option value="auto">${t('global_defaults.studio_provider_auto')}</option>
                    <option value="deepseek">DeepSeek</option>
                    <option value="openai">OpenAI</option>
                    <option value="anthropic">Anthropic</option>
                    <option value="google">Google (Gemini)</option>
                    <option value="mistral">Mistral</option>
                    <option value="groq">Groq</option>
                    <option value="jev">Jev / Engine Visual Local</option>
                    <option value="local">Local / OpenAI-compatível (Ollama, LM Studio, llama.cpp, vLLM)</option>
                  </select>
                </div>
                <div class="form-group">
                  <label class="form-label" for="studio-model">${t('global_defaults.model_name_label')}</label>
                  <input type="text" id="studio-model" class="form-input" placeholder="${t('global_defaults.studio_model_placeholder')}" />
                </div>
              </div>

              <div class="form-group" id="studio-base-url-group" style="display: none; margin-top: 10px;">
                <label class="form-label" for="studio-base-url" id="studio-base-url-label"></label>
                <input type="text" id="studio-base-url" class="form-input" style="font-family: monospace;" />
              </div>

              <div class="form-hint" style="margin-top: 8px;">${t('global_defaults.studio_model_hint')}</div>

              <div style="display: flex; align-items: center; gap: 12px; margin-top: 12px;">
                <button type="button" class="btn btn-primary" id="btn-save-studio-model" style="font-size: 12px; font-weight: 600; height: 32px; padding: 0 14px;">
                  ${t('global_defaults.studio_btn_save')}
                </button>
                <span id="studio-model-status" style="font-size: 12px; color: #616161;"></span>
              </div>
              <div id="studio-model-feedback" style="display: none; margin-top: 8px; font-size: 11.5px; padding: 6px 10px; border-radius: 4px;"></div>
            </div>
          </div>

          <!-- ABA 2: VM & Sandbox (Workspace) -->
          <div class="tab-pane" id="pane-tab-sandbox" style="${this.activeTab === 'sandbox' ? '' : 'display: none;'}">
            <div class="settings-section-card">
              <div class="section-card-header">
                <span class="section-card-icon">🛡️</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('global_defaults.sandbox_section_title')} <span class="badge-inherited" style="font-size: 10px; margin-left: 6px;">${t('global_defaults.badge_workspace_scope')}</span></h3>
                  <p class="section-card-desc">${t('global_defaults.sandbox_section_desc')}</p>
                </div>
              </div>

              <div class="form-row-grid">
                <div class="form-group">
                  <label class="form-label" for="def-isolation">${t('global_defaults.isolation_label')}</label>
                  <select id="def-isolation" class="form-select">
                    <option value="container" ${defaults.sandbox.isolationLevel === 'container' ? 'selected' : ''}>${t('global_defaults.isolation_container')}</option>
                    <option value="microvm" ${defaults.sandbox.isolationLevel === 'microvm' ? 'selected' : ''}>${t('global_defaults.isolation_microvm')}</option>
                    <option value="host_direct" ${defaults.sandbox.isolationLevel === 'host_direct' ? 'selected' : ''}>${t('global_defaults.isolation_host_direct')}</option>
                  </select>
                </div>

                <div class="form-group">
                  <label class="form-label" for="def-network">${t('global_defaults.network_label')}</label>
                  <select id="def-network" class="form-select">
                    <option value="egress_llm_only" ${defaults.sandbox.network === 'egress_llm_only' ? 'selected' : ''}>${t('global_defaults.network_egress_llm_only')}</option>
                    <option value="offline" ${defaults.sandbox.network === 'offline' ? 'selected' : ''}>${t('global_defaults.network_offline')}</option>
                    <option value="full" ${defaults.sandbox.network === 'full' ? 'selected' : ''}>${t('global_defaults.network_full')}</option>
                  </select>
                </div>
              </div>

              <div class="form-row-grid">
                <div class="form-group">
                  <label class="form-label" for="def-memory">${t('global_defaults.memory_limit_label')}</label>
                  <input type="number" id="def-memory" class="form-input" value="${defaults.sandbox.maxMemoryMB}" min="64" step="64" />
                </div>

                <div class="form-group">
                  <label class="form-label" for="def-timeout">${t('global_defaults.timeout_label')}</label>
                  <input type="number" id="def-timeout" class="form-input" value="${defaults.sandbox.timeoutSeconds}" min="10" step="10" />
                </div>
              </div>

              <div class="form-row-grid">
                <div class="form-group">
                  <label class="form-label" for="def-ro-mounts">${t('global_defaults.ro_mounts_label')}</label>
                  <input type="text" id="def-ro-mounts" class="form-input" value="${this.escapeHtml(defaults.sandbox.readOnlyMounts.join(', '))}" placeholder=".agents/, shared/" />
                  <div class="form-hint">${t('global_defaults.ro_mounts_hint')}</div>
                </div>

                <div class="form-group">
                  <label class="form-label" for="def-rw-mounts">${t('global_defaults.rw_mounts_label')}</label>
                  <input type="text" id="def-rw-mounts" class="form-input" value="${this.escapeHtml(defaults.sandbox.readWriteMounts.join(', '))}" placeholder="output/, dist/" />
                  <div class="form-hint">${t('global_defaults.rw_mounts_hint')}</div>
                </div>
              </div>
            </div>
          </div>

          <!-- ABA 3: Ferramentas Code Mode (Projeto) -->
          <div class="tab-pane" id="pane-tab-tools" style="${this.activeTab === 'tools' ? '' : 'display: none;'}">
            <div class="settings-section-card">
              <div class="section-card-header">
                <span class="section-card-icon">🔧</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('global_defaults.tools_section_title')} <span class="badge-inherited" style="font-size: 10px; margin-left: 6px;">${t('global_defaults.badge_project_scope')}</span></h3>
                  <p class="section-card-desc">${t('global_defaults.tools_section_desc')}</p>
                </div>
              </div>

              <div class="tools-checkbox-grid">
                <label class="tool-checkbox-pill ${defaults.codeMode.allowedTools.includes('write') ? 'checked' : ''}">
                  <input type="checkbox" class="def-tool-check" value="write" ${defaults.codeMode.allowedTools.includes('write') ? 'checked' : ''} />
                  <span class="tool-pill-name">tools.write</span>
                  <span class="tool-pill-desc">${t('global_defaults.tool_desc_write')}</span>
                </label>

                <label class="tool-checkbox-pill ${defaults.codeMode.allowedTools.includes('edit') ? 'checked' : ''}">
                  <input type="checkbox" class="def-tool-check" value="edit" ${defaults.codeMode.allowedTools.includes('edit') ? 'checked' : ''} />
                  <span class="tool-pill-name">tools.edit</span>
                  <span class="tool-pill-desc">${t('global_defaults.tool_desc_edit')}</span>
                </label>

                <label class="tool-checkbox-pill ${defaults.codeMode.allowedTools.includes('read') ? 'checked' : ''}">
                  <input type="checkbox" class="def-tool-check" value="read" ${defaults.codeMode.allowedTools.includes('read') ? 'checked' : ''} />
                  <span class="tool-pill-name">tools.read</span>
                  <span class="tool-pill-desc">${t('global_defaults.tool_desc_read')}</span>
                </label>

                <label class="tool-checkbox-pill ${defaults.codeMode.allowedTools.includes('bash') ? 'checked' : ''}">
                  <input type="checkbox" class="def-tool-check" value="bash" ${defaults.codeMode.allowedTools.includes('bash') ? 'checked' : ''} />
                  <span class="tool-pill-name">tools.bash</span>
                  <span class="tool-pill-desc">${t('global_defaults.tool_desc_bash')}</span>
                </label>

                <label class="tool-checkbox-pill ${defaults.codeMode.allowedTools.includes('validate_svg') ? 'checked' : ''}">
                  <input type="checkbox" class="def-tool-check" value="validate_svg" ${defaults.codeMode.allowedTools.includes('validate_svg') ? 'checked' : ''} />
                  <span class="tool-pill-name">tools.validate_svg</span>
                  <span class="tool-pill-desc">${t('global_defaults.tool_desc_validate_svg')}</span>
                </label>
              </div>

              <div class="form-group" style="margin-top: 14px;">
                <label class="form-label" for="def-custom-scripts">${t('global_defaults.custom_scripts_label')}</label>
                <input type="text" id="def-custom-scripts" class="form-input" value="${this.escapeHtml(defaults.codeMode.customScripts.join(', '))}" placeholder="python scripts/helper.py, npm run check" />
              </div>

              <div class="form-group" style="margin-top: 14px;">
                <label class="form-label">${t('global_defaults.mcp_tools_label')}</label>
                <div class="form-hint" style="margin-bottom: 8px;">${t('global_defaults.mcp_picker_hint')}</div>
                <div id="def-mcp-picker"></div>
              </div>
            </div>

            <!-- Acesso à internet das ferramentas nativas (escopo do projeto) -->
            <div class="settings-section-card" style="margin-top: 16px;" id="card-http-policy">
              <div class="section-card-header">
                <span class="section-card-icon">🌐</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('global_defaults.http_title')} <span class="badge-inherited" style="font-size: 10px; margin-left: 6px;">${t('global_defaults.badge_project_scope')}</span></h3>
                  <p class="section-card-desc">${t('global_defaults.http_desc')}</p>
                </div>
              </div>
              <div class="form-group">
                <label class="form-label" for="http-hosts">${t('global_defaults.http_hosts_label')}</label>
                <textarea id="http-hosts" class="form-textarea" rows="4" style="font-family: monospace;" placeholder="api.exemplo.com&#10;*.exemplo.org"></textarea>
                <div class="form-hint">${t('global_defaults.http_hosts_hint')}</div>
              </div>
              <div style="display: flex; align-items: center; gap: 12px; margin-top: 12px;">
                <button type="button" class="btn btn-primary" id="btn-save-http-policy" style="font-size: 12px; font-weight: 600; height: 32px; padding: 0 14px;">${t('global_defaults.http_btn_save')}</button>
                <span id="http-policy-status" style="font-size: 12px; color: #616161;"></span>
              </div>
              <div id="http-policy-feedback" style="display: none; margin-top: 8px; font-size: 11.5px; padding: 6px 10px; border-radius: 4px;"></div>
            </div>
          </div>

          <!-- ABA 4: Memória Heterogênea do Pipeline (Projeto) -->
          <div class="tab-pane" id="pane-tab-memory" style="${this.activeTab === 'memory' ? '' : 'display: none;'}">
            <div class="settings-section-card">
              <div class="section-card-header">
                <span class="section-card-icon">💾</span>
                <div class="section-card-titles">
                  <h3 class="section-card-title">${t('global_defaults.memory_section_title')} <span class="badge-inherited" style="font-size: 10px; margin-left: 6px;">${t('global_defaults.badge_project_scope')}</span></h3>
                  <p class="section-card-desc">${t('global_defaults.memory_section_desc')}</p>
                </div>
              </div>

              <div class="memory-enable-toggle-row">
                <label class="memory-toggle-label">
                  <input type="checkbox" id="def-memory-enabled" ${defaults.memory?.enabled ? 'checked' : ''} />
                  <span class="memory-toggle-text">${t('global_defaults.memory_toggle_label')}</span>
                </label>
              </div>

              <div class="form-group" style="margin-top: 10px;">
                <label class="form-label">${t('global_defaults.memory_regime_label')}</label>
                <div class="memory-persistence-options">
                  <label class="persistence-radio-card ${!defaults.memory?.persistent ? 'selected' : ''}">
                    <input type="radio" name="def-memory-persistence" value="ephemeral" ${!defaults.memory?.persistent ? 'checked' : ''} />
                    <div class="persistence-card-content">
                      <span class="persistence-card-title">${t('global_defaults.memory_ephemeral_title')}</span>
                      <span class="persistence-card-desc">${t('global_defaults.memory_ephemeral_desc')}</span>
                    </div>
                  </label>

                  <label class="persistence-radio-card ${defaults.memory?.persistent ? 'selected' : ''}">
                    <input type="radio" name="def-memory-persistence" value="persistent" ${defaults.memory?.persistent ? 'checked' : ''} />
                    <div class="persistence-card-content">
                      <span class="persistence-card-title">${t('global_defaults.memory_persistent_title')}</span>
                      <span class="persistence-card-desc">${t('global_defaults.memory_persistent_desc')}</span>
                    </div>
                  </label>
                </div>
              </div>

              <div class="form-group" style="margin-top: 14px; padding: 12px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 6px;">
                <label class="form-label" for="def-memory-measurement">${t('global_defaults.memory_measurement_label')}</label>
                <select id="def-memory-measurement" class="form-select">
                  <option value="normal" ${(defaults.memory?.measurementMode || 'normal') === 'normal' ? 'selected' : ''}>${t('global_defaults.memory_measurement_normal')}</option>
                  <option value="memory_off" ${defaults.memory?.measurementMode === 'memory_off' ? 'selected' : ''}>${t('global_defaults.memory_measurement_off')}</option>
                </select>
                <div class="form-hint">${t('global_defaults.memory_measurement_hint')}</div>
                <label style="display: flex; align-items: center; gap: 8px; margin-top: 10px; font-size: 12px; cursor: pointer;">
                  <input type="checkbox" id="def-memory-record-metrics" ${defaults.memory?.recordMetrics ? 'checked' : ''} />
                  <span>${t('global_defaults.memory_record_metrics_label')}</span>
                </label>
                <div class="form-hint">${t('global_defaults.memory_record_metrics_hint')}</div>
              </div>

              <div class="memory-perms-grid" style="margin-top: 14px;">
                <label class="memory-perm-item">
                  <input type="checkbox" id="def-mem-read-bb" ${defaults.memory?.readBlackboard ? 'checked' : ''} />
                  <div>
                    <span class="perm-item-title">${t('global_defaults.perm_read_bb_title')}</span>
                    <span class="perm-item-desc">${t('global_defaults.perm_read_bb_desc')}</span>
                  </div>
                </label>

                <label class="memory-perm-item">
                  <input type="checkbox" id="def-mem-write-bb" ${defaults.memory?.writeBlackboard ? 'checked' : ''} />
                  <div>
                    <span class="perm-item-title">${t('global_defaults.perm_write_bb_title')}</span>
                    <span class="perm-item-desc">${t('global_defaults.perm_write_bb_desc')}</span>
                  </div>
                </label>

                <label class="memory-perm-item">
                  <input type="checkbox" id="def-mem-read-lessons" ${defaults.memory?.semanticLessons ? 'checked' : ''} />
                  <div>
                    <span class="perm-item-title">${t('global_defaults.perm_read_lessons_title')}</span>
                    <span class="perm-item-desc">${t('global_defaults.perm_read_lessons_desc')}</span>
                  </div>
                </label>

                <label class="memory-perm-item">
                  <input type="checkbox" id="def-mem-write-lessons" ${defaults.memory?.recordLessons ? 'checked' : ''} />
                  <div>
                    <span class="perm-item-title">${t('global_defaults.perm_write_lessons_title')}</span>
                    <span class="perm-item-desc">${t('global_defaults.perm_write_lessons_desc')}</span>
                  </div>
                </label>
              </div>

              <div class="form-group" style="margin-top: 14px; padding: 12px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 6px;">
                <label class="form-label" style="display: flex; align-items: center; gap: 8px; cursor: pointer;">
                  <input type="checkbox" id="def-memory-embedding-enabled" ${defaults.memory?.embeddingEnabled ? 'checked' : ''} />
                  <span>${t('global_defaults.memory_embedding_enable_label')}</span>
                </label>
                <div class="form-hint">${t('global_defaults.memory_embedding_hint')}</div>
                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-top: 10px;">
                  <div>
                    <label class="form-label" for="def-memory-embedding-model">${t('global_defaults.memory_embedding_model_label')}</label>
                    <input type="text" id="def-memory-embedding-model" class="form-input" style="font-family: monospace;"
                           value="${this.escapeHtml(defaults.memory?.embeddingModel || 'embeddinggemma-2')}" />
                  </div>
                  <div>
                    <label class="form-label" for="def-memory-embedding-base-url">${t('global_defaults.memory_embedding_base_url_label')}</label>
                    <input type="text" id="def-memory-embedding-base-url" class="form-input" style="font-family: monospace;"
                           value="${this.escapeHtml(defaults.memory?.embeddingBaseUrl || '')}"
                           placeholder="http://127.0.0.1:11434/v1" />
                  </div>
                </div>
                <div style="display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 12px; margin-top: 10px;">
                  <div>
                    <label class="form-label" for="def-memory-embedding-batch">${t('global_defaults.memory_embedding_batch_label')}</label>
                    <input type="number" id="def-memory-embedding-batch" class="form-input" min="1" max="128" step="1"
                           value="${defaults.memory?.embeddingBatchSize ?? 8}" />
                  </div>
                  <div>
                    <label class="form-label" for="def-memory-embedding-max-chars">${t('global_defaults.memory_embedding_max_chars_label')}</label>
                    <input type="number" id="def-memory-embedding-max-chars" class="form-input" min="64" max="32000" step="100"
                           value="${defaults.memory?.embeddingMaxChars ?? 8000}" />
                  </div>
                  <div>
                    <label class="form-label" for="def-memory-embedding-dim">${t('global_defaults.memory_embedding_dimensions_label')}</label>
                    <input type="number" id="def-memory-embedding-dim" class="form-input" min="0" max="8192" step="1"
                           value="${defaults.memory?.embeddingDimensions ?? 768}" />
                  </div>
                </div>
              </div>

              <div class="form-group" style="margin-top: 14px; padding: 12px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 6px;">
                <label class="form-label" for="def-memory-write-keys">${t('global_defaults.memory_write_keys_label')}</label>
                <input type="text" id="def-memory-write-keys" class="form-input"
                       value="${this.escapeHtml((defaults.memory?.writeKeys || []).join(', '))}"
                       placeholder="${t('global_defaults.memory_write_keys_placeholder')}" />
                <div class="form-hint">${t('global_defaults.memory_write_keys_hint')}</div>

                <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 12px; margin-top: 10px;">
                  <div>
                    <label class="form-label" for="def-memory-max-lessons">${t('global_defaults.memory_max_lessons_label')}</label>
                    <input type="number" id="def-memory-max-lessons" class="form-input" min="1" max="500" step="1"
                           value="${defaults.memory?.maxLessonsPerRun ?? 20}" />
                    <div class="form-hint">${t('global_defaults.memory_max_lessons_hint')}</div>
                  </div>
                  <div>
                    <label class="form-label" for="def-memory-max-lesson-chars">${t('global_defaults.memory_max_lesson_chars_label')}</label>
                    <input type="number" id="def-memory-max-lesson-chars" class="form-input" min="100" max="20000" step="100"
                           value="${defaults.memory?.maxLessonChars ?? 4000}" />
                    <div class="form-hint">${t('global_defaults.memory_max_lesson_chars_hint')}</div>
                  </div>
                </div>

                <div class="form-group" style="margin-top: 10px;">
                  <label class="form-label" for="def-memory-retention-days">${t('global_defaults.memory_retention_label')}</label>
                  <input type="number" id="def-memory-retention-days" class="form-input" min="0" max="3650" step="30"
                         value="${defaults.memory?.retentionDays ?? 180}" />
                  <div class="form-hint">${t('global_defaults.memory_retention_hint')}</div>
                </div>
              </div>

              <div class="form-group" style="margin-top: 14px; padding: 12px; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 6px;">
                <label class="form-label" for="def-memory-consolidation-mode">${t('global_defaults.memory_consolidation_title')}</label>
                <div class="form-hint" style="margin-bottom: 8px;">${t('global_defaults.memory_consolidation_desc')}</div>
                <select id="def-memory-consolidation-mode" class="form-select">
                  <option value="off" ${(defaults.memory?.consolidationMode || 'off') === 'off' ? 'selected' : ''}>${t('global_defaults.memory_consolidation_off')}</option>
                  <option value="idle" ${defaults.memory?.consolidationMode === 'idle' ? 'selected' : ''}>${t('global_defaults.memory_consolidation_idle')}</option>
                  <option value="after_episodes" ${defaults.memory?.consolidationMode === 'after_episodes' ? 'selected' : ''}>${t('global_defaults.memory_consolidation_after_episodes')}</option>
                </select>
                <div style="display: grid; grid-template-columns: 1fr 1fr 1fr; gap: 12px; margin-top: 10px;">
                  <div>
                    <label class="form-label" for="def-memory-consolidation-idle">${t('global_defaults.memory_consolidation_idle_label')}</label>
                    <input type="number" id="def-memory-consolidation-idle" class="form-input" min="5" max="86400" step="30"
                           value="${defaults.memory?.consolidationIdleSeconds ?? 300}" />
                  </div>
                  <div>
                    <label class="form-label" for="def-memory-consolidation-episodes">${t('global_defaults.memory_consolidation_episodes_label')}</label>
                    <input type="number" id="def-memory-consolidation-episodes" class="form-input" min="1" max="100000" step="1"
                           value="${defaults.memory?.consolidationEpisodeThreshold ?? 20}" />
                  </div>
                  <div>
                    <label class="form-label" for="def-memory-consolidation-dormant">${t('global_defaults.memory_consolidation_dormant_label')}</label>
                    <input type="number" id="def-memory-consolidation-dormant" class="form-input" min="0" max="3650" step="30"
                           value="${defaults.memory?.consolidationDormantDays ?? 180}" />
                  </div>
                </div>
                <div class="form-hint">${t('global_defaults.memory_consolidation_hint')}</div>
              </div>
            </div>
          </div>
        </div>

        <div class="modal-footer office-modal-footer">
          <button class="btn btn-secondary-pill btn-secondary-office" id="btn-reset-defaults">${t('global_defaults.btn_reset')}</button>
          <div style="flex: 1;"></div>
          <button class="btn btn-secondary-pill btn-secondary-office" id="btn-cancel-defaults">${t('global_defaults.btn_cancel')}</button>
          <button class="btn btn-primary btn-primary-office" id="btn-save-defaults">${t('global_defaults.btn_save')}</button>
        </div>
      </div>
    `;

    const dialog = this.overlay.querySelector('.modal-dialog') as HTMLElement;
    if (dialog) {
      this.windowController = ModalWindowManager.attach({
        dialog,
        minWidth: 580,
        minHeight: 460,
        onClose: () => this.hide()
      });
    }

    this.bindDialogEvents();
    this.updateKeyIndicator();
    void this.loadRuntimeManaged();
    void this.loadGatewaySettings();
    void this.loadStudioModel();
    this.mountToolsExtras(defaults.codeMode.mcpServers || [], defaults.codeMode.allowedMcpTools || []);
  }

  private mountToolsExtras(servers: string[], tools: string[]): void {
    const root = this.overlay.querySelector('#def-mcp-picker') as HTMLElement | null;
    if (root) {
      this.mcpPicker = new McpToolPicker(root);
      void this.mcpPicker.load({ servers, tools }, false);
    }
    void this.loadHttpPolicy();
  }

  private backend(): any | null {
    const w = typeof window !== 'undefined' ? (window as any) : null;
    return w && w.webui && typeof w.webui.call === 'function' ? w.webui : null;
  }

  private async loadHttpPolicy(): Promise<void> {
    const area = this.overlay.querySelector('#http-hosts') as HTMLTextAreaElement | null;
    if (!area) return;
    const b = this.backend();
    if (!b) { area.disabled = true; return; }
    try {
      const res = JSON.parse((await b.call('get_http_policy')) || '{}');
      area.value = ((res.hosts || []) as string[]).join('\n');
    } catch { /* sem backend: campo vazio */ }
    this.refreshHttpPolicyStatus();
  }

  private refreshHttpPolicyStatus(): void {
    const area = this.overlay.querySelector('#http-hosts') as HTMLTextAreaElement | null;
    const status = this.overlay.querySelector('#http-policy-status') as HTMLElement | null;
    if (!area || !status) return;
    const count = area.value.split('\n').map(s => s.trim()).filter(Boolean).length;
    status.textContent = count === 0 ? t('global_defaults.http_status_none') : t('global_defaults.http_status_count', { count });
    status.style.color = count === 0 ? '#616161' : '#0e700e';
  }

  private async saveHttpPolicy(): Promise<void> {
    const area = this.overlay.querySelector('#http-hosts') as HTMLTextAreaElement | null;
    const feedback = this.overlay.querySelector('#http-policy-feedback') as HTMLElement | null;
    if (!area || !feedback) return;
    const show = (ok: boolean, text: string) => {
      feedback.style.display = 'block';
      feedback.style.background = ok ? '#dff6dd' : '#fde7e9';
      feedback.style.color = ok ? '#0e700e' : '#a80000';
      feedback.textContent = text;
    };
    const b = this.backend();
    if (!b) { show(false, t('mcp_picker.needs_native')); return; }
    const hosts = area.value.split(/[\n,;]+/).map(s => s.trim()).filter(Boolean);
    try {
      const res = JSON.parse((await b.call('set_http_policy', JSON.stringify({ hosts }))) || '{}');
      if (res.ok) {
        show(true, t('global_defaults.http_saved'));
        await this.loadHttpPolicy();
      } else {
        show(false, String(res.error || t('mcp_picker.load_failed')));
      }
    } catch {
      show(false, t('mcp_picker.load_failed'));
    }
  }

  private bindEvents(): void {
    this.overlay.addEventListener('click', (e) => {
      if (e.target === this.overlay) this.hide();
    });
  }

  private bindDialogEvents(): void {
    // Fechar
    this.overlay.querySelector('#btn-close-defaults-modal')?.addEventListener('click', () => this.hide());
    this.overlay.querySelector('#btn-cancel-defaults')?.addEventListener('click', () => this.hide());

    // Abas
    this.overlay.querySelectorAll<HTMLButtonElement>('.modal-tabs .tab-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        const tab = btn.getAttribute('data-tab') as GlobalDefaultsTab;
        if (!tab) return;
        this.activeTab = tab;
        this.overlay.querySelectorAll('.modal-tabs .tab-btn').forEach(b => b.classList.remove('active'));
        btn.classList.add('active');

        this.overlay.querySelectorAll<HTMLElement>('.tab-pane').forEach(pane => {
          pane.style.display = pane.id === `pane-tab-${tab}` ? 'block' : 'none';
        });
      });
    });

    // Abrir repositórios do GitHub
    this.overlay.querySelector('#btn-defaults-open-github-repos')?.addEventListener('click', () => {
      globalEventBus.emit('modal:open-github-repos');
    });

    // Toggle estilo das pílulas de ferramentas
    this.overlay.querySelectorAll('.def-tool-check').forEach(chk => {
      chk.addEventListener('change', (e) => {
        const input = e.target as HTMLInputElement;
        const pill = input.closest('.tool-checkbox-pill');
        if (pill) {
          if (input.checked) pill.classList.add('checked');
          else pill.classList.remove('checked');
        }
      });
    });

    // Provedor altera modelo sugerido e atualiza chave
    const providerSelect = this.overlay.querySelector('#def-provider') as HTMLSelectElement;
    const modelInput = this.overlay.querySelector('#def-model') as HTMLInputElement;
    providerSelect?.addEventListener('change', () => {
      const val = providerSelect.value;
      if (val === 'deepseek') modelInput.value = 'deepseek-flash';
      else if (val === 'anthropic') modelInput.value = 'claude-opus-5.5';
      else if (val === 'google') modelInput.value = 'gemini-2.5-flash';
      else if (val === 'mistral') modelInput.value = 'mistral-large-latest';
      else if (val === 'openai') modelInput.value = 'gpt-4o';
      else if (val === 'jev') modelInput.value = 'jev-svg-validator';

      // Limpa status anterior de teste
      const statusDiv = this.overlay.querySelector('#api-test-status') as HTMLElement;
      if (statusDiv) statusDiv.style.display = 'none';

      void this.loadProjectEndpoint();
      this.updateKeyIndicator();
    });

    // Descobrir modelos de um servidor local já em execução
    this.overlay.querySelector('#btn-def-discover-models')?.addEventListener('click', () => {
      void this.discoverProjectModels();
    });

    // Catálogo e download de modelos locais (runtime gerenciado entra na Fase 2)
    this.overlay.querySelector('#btn-open-local-models')?.addEventListener('click', () => {
      this.openLocalModels();
    });

    // Testar API do Agente
    this.overlay.querySelector('#btn-test-agent-api')?.addEventListener('click', () => {
      this.testAgentApi();
    });

    // Alternar painel de implantação de chave
    this.overlay.querySelector('#btn-toggle-deploy-key')?.addEventListener('click', () => {
      const panel = this.overlay.querySelector('#panel-deploy-key') as HTMLElement;
      if (panel) {
        panel.style.display = panel.style.display === 'none' ? 'block' : 'none';
        if (panel.style.display === 'block') {
          (this.overlay.querySelector('#deploy-key-value') as HTMLInputElement)?.focus();
        }
      }
    });

    // Botão de instruções manuais de ambiente
    this.overlay.querySelector('#btn-open-env-guidance')?.addEventListener('click', () => {
      const provider = (this.overlay.querySelector('#def-provider') as HTMLSelectElement)?.value || 'deepseek';
      const keyName = this.getEnvKeyForProvider(provider);
      EnvGuidanceModal.getInstance().show(keyName, () => {
        this.updateKeyIndicator();
      });
    });

    // Modelo padrão do Studio (escopo Studio)
    this.overlay.querySelector('#studio-provider')?.addEventListener('change', () => {
      this.syncStudioBaseUrlField();
      const sel = this.overlay.querySelector('#studio-provider') as HTMLSelectElement;
      const baseVar = this.studioBaseUrlVar(sel.value);
      if (baseVar) {
        EnvironmentManager.getInstance().getEnvVar(baseVar).then(v => {
          (this.overlay.querySelector('#studio-base-url') as HTMLInputElement).value = v || '';
        });
      }
    });
    this.overlay.querySelector('#btn-save-studio-model')?.addEventListener('click', () => {
      void this.saveStudioModel();
    });
    this.overlay.querySelector('#btn-save-http-policy')?.addEventListener('click', () => {
      void this.saveHttpPolicy();
    });
    this.overlay.querySelector('#http-hosts')?.addEventListener('input', () => this.refreshHttpPolicyStatus());

    // Salvar/Implantar chave no Windows
    this.overlay.querySelector('#btn-submit-deploy-key')?.addEventListener('click', () => {
      this.deployApiKey();
    });

    // AI Gateway
    this.overlay.querySelector('#btn-save-gateway')?.addEventListener('click', () => {
      void this.saveGatewaySettings();
    });
    this.overlay.querySelector('#btn-test-gateway')?.addEventListener('click', () => {
      void this.testGateway();
    });
    this.overlay.querySelector('#btn-clear-gateway')?.addEventListener('click', () => {
      void this.clearGatewaySettings();
    });

    // Restaurar Fábrica
    this.overlay.querySelector('#btn-reset-defaults')?.addEventListener('click', () => {
      if (confirm(t('global_defaults.confirm_reset_factory'))) {
        this.graph.updateGlobalDefaults(DEFAULT_PIPELINE_DEFAULTS);
        this.render();
      }
    });

    // Salvar
    this.overlay.querySelector('#btn-save-defaults')?.addEventListener('click', () => {
      void this.saveValues().finally(() => this.hide());
    });
  }

  // --- Endpoint de servidor local (escopo da máquina, mas usado pelo projeto e pelos vértices) ---

  private openLocalModels(): void {
    if (!this.localModelsModal) {
      this.localModelsModal = new LocalModelsModal({
        onUse: (model) => {
          const providerSelect = this.overlay.querySelector('#def-provider') as HTMLSelectElement | null;
          if (providerSelect) providerSelect.value = 'local';
          const modelInput = this.overlay.querySelector('#def-model') as HTMLInputElement | null;
          if (modelInput) modelInput.value = model.apiModel || model.id;
          this.syncProjectEndpointField();
          void this.loadProjectEndpoint();
        },
        onRuntimeChanged: (running) => {
          this.runtimeManaged = running;
          this.syncProjectEndpointField();
          void this.loadProjectEndpoint();
        },
        onAssistantConfigured: () => {
          void this.loadStudioModel();
        }
      });
    }
    this.localModelsModal.show();
  }

  private projectEndpointVar(provider: string): string | null {
    if (provider === 'local') return LOCAL_LLM_ENV;
    if (provider === 'jev') return 'JEV_BASE_URL';
    return null;
  }

  private syncProjectEndpointField(): void {
    const provider = (this.overlay.querySelector('#def-provider') as HTMLSelectElement)?.value || 'deepseek';
    const group = this.overlay.querySelector('#def-local-endpoint-group') as HTMLElement | null;
    const label = this.overlay.querySelector('#def-local-endpoint-label') as HTMLElement | null;
    const hint = this.overlay.querySelector('#def-local-endpoint-hint') as HTMLElement | null;
    const input = this.overlay.querySelector('#def-local-base-url') as HTMLInputElement | null;
    const discover = this.overlay.querySelector('#btn-def-discover-models') as HTMLElement | null;
    const manage = this.overlay.querySelector('#btn-open-local-models') as HTMLElement | null;
    const models = this.overlay.querySelector('#def-local-models') as HTMLElement | null;
    const status = this.overlay.querySelector('#def-local-model-status') as HTMLElement | null;
    if (!group) return;

    const varName = this.projectEndpointVar(provider);
    group.style.display = varName ? 'block' : 'none';
    if (!varName) return;

    if (label) label.textContent = t('global_defaults.local_endpoint_label', { key: varName });
    if (hint) hint.textContent = provider === 'local'
      ? (this.runtimeManaged ? t('global_defaults.local_endpoint_managed') : t('global_defaults.local_endpoint_hint'))
      : t('global_defaults.jev_endpoint_hint');
    if (input) {
      input.placeholder = provider === 'local' ? LOCAL_LLM_DEFAULT_URL : 'http://localhost:8080/v1';
      input.readOnly = provider === 'local' && this.runtimeManaged;
    }
    if (discover) discover.style.display = provider === 'local' ? '' : 'none';
    if (manage) manage.style.display = provider === 'local' ? '' : 'none';
    if (models && provider !== 'local') {
      models.style.display = 'none';
      models.innerHTML = '';
    }
    if (status && provider !== 'local') status.style.display = 'none';
  }

  private async loadRuntimeManaged(): Promise<void> {
    const webui = this.backend();
    if (webui) {
      try {
        const raw = await webui.call('local_runtime_status', '');
        const status = typeof raw === 'string' ? JSON.parse(raw || '{}') : raw;
        this.runtimeManaged = Boolean(status?.running && status?.ready);
      } catch {
        this.runtimeManaged = false;
      }
    } else {
      this.runtimeManaged = false;
    }
    this.syncProjectEndpointField();
    await this.loadProjectEndpoint();
  }

  private async loadProjectEndpoint(): Promise<void> {
    this.syncProjectEndpointField();
    const provider = (this.overlay.querySelector('#def-provider') as HTMLSelectElement)?.value || 'deepseek';
    const varName = this.projectEndpointVar(provider);
    const input = this.overlay.querySelector('#def-local-base-url') as HTMLInputElement | null;
    if (!varName || !input) return;
    const value = provider === 'local'
      ? await loadLocalBaseUrl()
      : ((await EnvironmentManager.getInstance().getEnvVar(varName)) || '');
    input.value = value || '';
  }

  private async discoverProjectModels(): Promise<void> {
    const provider = (this.overlay.querySelector('#def-provider') as HTMLSelectElement)?.value || 'deepseek';
    if (provider !== 'local') return;

    const input = this.overlay.querySelector('#def-local-base-url') as HTMLInputElement | null;
    const list = this.overlay.querySelector('#def-local-models') as HTMLElement | null;
    const status = this.overlay.querySelector('#def-local-model-status') as HTMLElement | null;
    if (!input || !status) return;

    const base = normalizeLocalBaseUrl(input.value || LOCAL_LLM_DEFAULT_URL);
    input.value = base;
    status.style.display = 'block';
    status.style.color = '#616161';
    status.textContent = t('global_defaults.local_discovering');

    const result = await discoverLocalModels(base);
    if (result.baseUrl) input.value = result.baseUrl;

    if (result.ok) {
      if (result.models.length > 0) {
        status.style.color = '#0e700e';
        status.textContent = t('global_defaults.local_models_found', { count: result.models.length });
        if (list) {
          list.style.display = 'flex';
          const modelInput = this.overlay.querySelector('#def-model') as HTMLInputElement | null;
          renderModelChips(list, result.models, (model) => {
            if (modelInput) modelInput.value = model;
            status.style.color = '#0e700e';
            status.textContent = t('global_defaults.local_model_selected', { model });
          });
        }
      } else {
        status.style.color = '#797673';
        status.textContent = t('global_defaults.local_models_none');
        if (list) { list.style.display = 'none'; list.innerHTML = ''; }
      }
    } else {
      status.style.color = '#a80000';
      status.textContent = result.errorCode === 'invalid_url'
        ? t('global_defaults.local_url_required')
        : result.errorCode === 'http_error'
          ? t('global_defaults.local_http_error', { url: result.baseUrl || base, detail: result.error || '' })
          : t('global_defaults.local_unreachable', { url: result.baseUrl || base });
      if (list) { list.style.display = 'none'; list.innerHTML = ''; }
    }
  }

  // --- AI Gateway (chave virtual + política empresarial; modelos locais ficam isentos) ---

  private async loadGatewaySettings(): Promise<void> {
    const env = EnvironmentManager.getInstance();
    const url = (await env.getEnvVar('ARESTRIX_AI_GATEWAY_URL')) || '';
    const key = (await env.getEnvVar('ARESTRIX_AI_GATEWAY_KEY')) || '';
    const requireValue = (await env.getEnvVar('ARESTRIX_REQUIRE_AI_GATEWAY')) || '';

    const urlInput = this.overlay.querySelector('#def-gateway-url') as HTMLInputElement | null;
    const keyInput = this.overlay.querySelector('#def-gateway-key') as HTMLInputElement | null;
    const keyHint = this.overlay.querySelector('#def-gateway-key-hint') as HTMLElement | null;
    const requireInput = this.overlay.querySelector('#def-gateway-require') as HTMLInputElement | null;
    const usage = this.overlay.querySelector('#gateway-usage-status') as HTMLElement | null;
    const policyHint = this.overlay.querySelector('#gateway-policy-hint') as HTMLElement | null;
    const policy = ((await env.getEnvVar('ARESTRIX_AI_GATEWAY_POLICY')) || '').toLowerCase();
    const locked = policy === 'locked' || policy === 'enforced';
    const saveButton = this.overlay.querySelector('#btn-save-gateway') as HTMLButtonElement | null;
    const clearButton = this.overlay.querySelector('#btn-clear-gateway') as HTMLButtonElement | null;

    if (urlInput) { urlInput.value = url; urlInput.disabled = locked; }
    if (keyInput) { keyInput.value = ''; keyInput.disabled = locked; }
    if (requireInput) {
      requireInput.checked = locked || requireValue === '1' || requireValue.toLowerCase() === 'true';
      requireInput.disabled = locked;
    }
    if (saveButton) saveButton.disabled = locked;
    if (clearButton) clearButton.disabled = locked;
    if (policyHint) {
      policyHint.style.display = locked ? 'block' : 'none';
      policyHint.textContent = locked ? t('global_defaults.gateway_policy_locked') : '';
    }
    if (keyHint) {
      keyHint.textContent = key
        ? t('global_defaults.gateway_key_configured')
        : t('global_defaults.gateway_key_missing');
    }
    if (usage) usage.textContent = '';

    if (url && key) void this.refreshGatewayUsage(url, key);
  }

  private async saveGatewaySettings(): Promise<void> {
    const env = EnvironmentManager.getInstance();
    const urlInput = this.overlay.querySelector('#def-gateway-url') as HTMLInputElement | null;
    const keyInput = this.overlay.querySelector('#def-gateway-key') as HTMLInputElement | null;
    const requireInput = this.overlay.querySelector('#def-gateway-require') as HTMLInputElement | null;
    const status = this.overlay.querySelector('#gateway-test-status') as HTMLElement | null;
    const policy = ((await env.getEnvVar('ARESTRIX_AI_GATEWAY_POLICY')) || '').toLowerCase();
    if (policy === 'locked' || policy === 'enforced') return;
    const url = urlInput?.value.trim() || '';
    const key = keyInput?.value.trim() || '';
    const require = requireInput?.checked || false;

    const show = (ok: boolean, text: string) => {
      if (!status) return;
      status.style.display = 'block';
      status.style.background = ok ? '#dff6dd' : '#fde7e9';
      status.style.color = ok ? '#0e700e' : '#a80000';
      status.textContent = text;
    };

    const results: boolean[] = [];
    results.push(url ? await env.setEnvVar('ARESTRIX_AI_GATEWAY_URL', url) : await env.clearEnvVar('ARESTRIX_AI_GATEWAY_URL'));
    if (key) results.push(await env.setEnvVar('ARESTRIX_AI_GATEWAY_KEY', key));
    results.push(await env.setEnvVar('ARESTRIX_REQUIRE_AI_GATEWAY', require ? '1' : '0'));

    if (keyInput) keyInput.value = '';
    await this.loadGatewaySettings();
    this.updateKeyIndicator();

    if (results.every(Boolean)) show(true, t('global_defaults.gateway_saved'));
    else show(false, t('global_defaults.gateway_saved_session'));
  }

  private async clearGatewaySettings(): Promise<void> {
    const env = EnvironmentManager.getInstance();
    const policy = ((await env.getEnvVar('ARESTRIX_AI_GATEWAY_POLICY')) || '').toLowerCase();
    if (policy === 'locked' || policy === 'enforced') return;
    if (!confirm(t('global_defaults.confirm_clear_gateway'))) return;
    await env.clearEnvVar('ARESTRIX_AI_GATEWAY_URL');
    await env.clearEnvVar('ARESTRIX_AI_GATEWAY_KEY');
    await env.clearEnvVar('ARESTRIX_REQUIRE_AI_GATEWAY');
    await this.loadGatewaySettings();
    this.updateKeyIndicator();
    const status = this.overlay.querySelector('#gateway-test-status') as HTMLElement | null;
    if (status) {
      status.style.display = 'block';
      status.style.background = '#dff6dd';
      status.style.color = '#0e700e';
      status.textContent = t('global_defaults.gateway_cleared');
    }
  }

  private async testGateway(): Promise<void> {
    const webui = this.backend();
    const status = this.overlay.querySelector('#gateway-test-status') as HTMLElement | null;
    const urlInput = this.overlay.querySelector('#def-gateway-url') as HTMLInputElement | null;
    const keyInput = this.overlay.querySelector('#def-gateway-key') as HTMLInputElement | null;
    if (!status) return;

    const show = (ok: boolean, text: string) => {
      status.style.display = 'block';
      status.style.background = ok ? '#dff6dd' : '#fde7e9';
      status.style.color = ok ? '#0e700e' : '#a80000';
      status.textContent = text;
    };

    if (!webui) { show(false, t('global_defaults.gateway_needs_native')); return; }
    const env = EnvironmentManager.getInstance();
    const url = urlInput?.value.trim() || (await env.getEnvVar('ARESTRIX_AI_GATEWAY_URL')) || '';
    const key = keyInput?.value.trim() || (await env.getEnvVar('ARESTRIX_AI_GATEWAY_KEY')) || '';
    if (!url) { show(false, t('global_defaults.gateway_url_required')); return; }

    show(true, t('global_defaults.gateway_testing'));
    try {
      const raw = await webui.call('ai_gateway_health', JSON.stringify({ url, key }));
      const result = typeof raw === 'string' ? JSON.parse(raw || '{}') : raw;
      if (result?.ok) {
        show(true, t('global_defaults.gateway_test_ok', { status: String(result.status || 200) }));
        void this.refreshGatewayUsage(url, key);
      } else {
        const detail = result?.error || result?.status || result?.body || '?';
        show(false, t('global_defaults.gateway_test_failed', { detail: String(detail) }));
      }
    } catch {
      show(false, t('global_defaults.gateway_test_failed', { detail: 'bridge_error' }));
    }
  }

  private async refreshGatewayUsage(url: string, key: string): Promise<void> {
    const webui = this.backend();
    const host = this.overlay.querySelector('#gateway-usage-status') as HTMLElement | null;
    if (!webui || !host) return;
    try {
      const raw = await webui.call('ai_gateway_usage', JSON.stringify({ url, key }));
      const result = typeof raw === 'string' ? JSON.parse(raw || '{}') : raw;
      if (!result?.ok || !result.usage) {
        host.textContent = t('global_defaults.gateway_usage_unavailable');
        return;
      }
      const usage = result.usage;
      const totals = usage.usage || {};
      const lines: string[] = [];
      lines.push(t('global_defaults.gateway_usage', {
        requests: String(totals.requests ?? 0),
        cost: Number(totals.costUsd || 0).toFixed(6),
        tokens: String((totals.inputTokens || 0) + (totals.outputTokens || 0))
      }));
      if (Number(totals.memoryContextChars || 0) > 0) {
        lines.push(t('global_defaults.gateway_memory_usage', {
          requests: String(totals.memoryRequests ?? 0),
          chars: String(totals.memoryContextChars ?? 0),
          tokens: String(totals.memoryTokens ?? 0),
          ratio: (Number(totals.memoryInputRatio || 0) * 100).toFixed(1)
        }));
        const modes = Array.isArray(usage.byMemory) ? usage.byMemory : [];
        const on = modes.find((m: any) => m.memoryEnabled);
        const off = modes.find((m: any) => !m.memoryEnabled);
        if (on || off) {
          lines.push(t('global_defaults.gateway_memory_compare', {
            onRequests: String(on?.requests ?? 0),
            onTokens: String(on?.tokens ?? 0),
            offRequests: String(off?.requests ?? 0),
            offTokens: String(off?.tokens ?? 0)
          }));
        }
      }
      for (const budget of usage.budgets || []) {
        const limit = budget.limitUsd;
        lines.push(t('global_defaults.gateway_budget_line', {
          scope: String(budget.scopeType || '?'),
          id: String(budget.scopeId || '?'),
          period: String(budget.period || '?'),
          spent: Number(budget.spentUsd || 0).toFixed(6),
          limit: limit === null || limit === undefined ? t('global_defaults.gateway_budget_no_limit') : `US$ ${Number(limit).toFixed(6)}`
        }));
      }
      host.textContent = lines.join(' · ');
    } catch {
      host.textContent = t('global_defaults.gateway_usage_unavailable');
    }
  }

  // --- Modelo padrão do Studio (variáveis de ambiente do usuário; nunca gravado no projeto) ---

  private static readonly STUDIO_PROVIDER_VAR = 'ARESTRIX_DEFAULT_PROVIDER';
  private static readonly STUDIO_MODEL_VAR = 'ARESTRIX_DEFAULT_MODEL';

  private studioBaseUrlVar(provider: string): string | null {
    if (provider === 'local') return 'LOCAL_LLM_BASE_URL';
    if (provider === 'jev') return 'JEV_BASE_URL';
    return null;
  }

  private syncStudioBaseUrlField(): void {
    const provider = (this.overlay.querySelector('#studio-provider') as HTMLSelectElement)?.value || 'auto';
    const group = this.overlay.querySelector('#studio-base-url-group') as HTMLElement;
    const label = this.overlay.querySelector('#studio-base-url-label') as HTMLElement;
    const varName = this.studioBaseUrlVar(provider);
    if (group) group.style.display = varName ? 'block' : 'none';
    if (label && varName) label.textContent = t('global_defaults.studio_base_url_label', { key: varName });
    const input = this.overlay.querySelector('#studio-base-url') as HTMLInputElement | null;
    if (input) {
      input.placeholder = provider === 'local' ? LOCAL_LLM_DEFAULT_URL : 'http://localhost:8080/v1';
      input.readOnly = provider === 'local' && this.runtimeManaged;
    }
  }

  private async loadStudioModel(): Promise<void> {
    const providerSel = this.overlay.querySelector('#studio-provider') as HTMLSelectElement | null;
    const modelInput = this.overlay.querySelector('#studio-model') as HTMLInputElement | null;
    if (!providerSel || !modelInput) return;
    const env = EnvironmentManager.getInstance();
    const provider = (await env.getEnvVar(GlobalDefaultsModal.STUDIO_PROVIDER_VAR)) || 'auto';
    const model = (await env.getEnvVar(GlobalDefaultsModal.STUDIO_MODEL_VAR)) || '';
    providerSel.value = Array.from(providerSel.options).some(o => o.value === provider) ? provider : 'auto';
    modelInput.value = model === 'auto' ? '' : model;
    const baseVar = this.studioBaseUrlVar(providerSel.value);
    if (baseVar) {
      const url = (await env.getEnvVar(baseVar)) || '';
      (this.overlay.querySelector('#studio-base-url') as HTMLInputElement).value = url;
    }
    this.syncStudioBaseUrlField();
    await this.refreshStudioModelStatus();
    await this.markAssistantSupport();
  }

  /** Destaca os provedores que não servem ao Assistente (sem chamada de ferramentas), segundo o backend. */
  private async markAssistantSupport(): Promise<void> {
    const providerSel = this.overlay.querySelector('#studio-provider') as HTMLSelectElement | null;
    const webui = typeof window !== 'undefined' ? (window as any).webui : undefined;
    if (!providerSel || !webui || typeof webui.call !== 'function') return;
    let status: any = null;
    try {
      const raw = await webui.call('assistant_status');
      status = typeof raw === 'string' ? JSON.parse(raw) : raw;
    } catch {
      return;
    }
    const unsupported = new Set<string>(
      (status?.providers || []).filter((p: any) => p && p.supportsTools === false).map((p: any) => String(p.id))
    );
    for (const option of Array.from(providerSel.options)) {
      if (!unsupported.has(option.value)) continue;
      const base = option.dataset.baseLabel || option.textContent || option.value;
      option.dataset.baseLabel = base;
      option.textContent = `${base} — ${t('assistant.provider_unsupported')}`;
      option.classList.add('assistant-unsupported-option');
    }
  }

  /** Mostra qual modelo o Studio usaria agora, com as mesmas regras do backend. */
  private async refreshStudioModelStatus(): Promise<void> {
    const status = this.overlay.querySelector('#studio-model-status') as HTMLElement | null;
    if (!status) return;
    const env = EnvironmentManager.getInstance();
    let provider = (await env.getEnvVar(GlobalDefaultsModal.STUDIO_PROVIDER_VAR)) || 'auto';
    let model = (await env.getEnvVar(GlobalDefaultsModal.STUDIO_MODEL_VAR)) || 'auto';
    const defaults: Record<string, string> = {
      deepseek: 'deepseek-flash', openai: 'gpt-4o-mini', anthropic: 'claude-haiku-5-5',
      google: 'gemini-2.5-flash', mistral: 'mistral-small-latest', groq: 'llama-3.3-70b-versatile'
    };
    if (provider === 'auto') {
      provider = '';
      for (const p of ['deepseek', 'openai', 'anthropic', 'google', 'mistral', 'groq']) {
        if (await env.hasEnvVar(this.getEnvKeyForProvider(p))) { provider = p; break; }
      }
      // Modelo local: a escolha só é automática quando há endpoint configurado e um nome de modelo
      // explícito; o backend exige o nome porque cada servidor local expõe modelos diferentes.
      if (!provider && model !== 'auto' && (await env.hasEnvVar(LOCAL_LLM_ENV))) {
        provider = 'local';
      }
    }
    if (model === 'auto') model = provider ? (defaults[provider] || '') : '';
    if (provider && model) {
      status.textContent = t('global_defaults.studio_status_in_use', { model: `${provider}/${model}` });
      status.style.color = '#0e700e';
    } else {
      status.textContent = t('global_defaults.studio_status_none');
      status.style.color = '#a80000';
    }
  }

  private async saveStudioModel(): Promise<void> {
    const provider = (this.overlay.querySelector('#studio-provider') as HTMLSelectElement)?.value || 'auto';
    const model = (this.overlay.querySelector('#studio-model') as HTMLInputElement)?.value.trim() || 'auto';
    const baseVar = this.studioBaseUrlVar(provider);
    const baseInput = this.overlay.querySelector('#studio-base-url') as HTMLInputElement | null;
    let baseUrl = baseInput?.value.trim() || '';
    if (provider === 'local') {
      // Sem endereço explícito, vale o padrão do runtime: Ollama em localhost:11434.
      baseUrl = normalizeLocalBaseUrl(baseUrl || LOCAL_LLM_DEFAULT_URL);
      if (baseInput) baseInput.value = baseUrl;
    }
    const feedback = this.overlay.querySelector('#studio-model-feedback') as HTMLElement;

    const show = (ok: boolean, text: string) => {
      if (!feedback) return;
      feedback.style.display = 'block';
      feedback.style.background = ok ? '#dff6dd' : '#fde7e9';
      feedback.style.color = ok ? '#0e700e' : '#a80000';
      feedback.textContent = text;
    };

    if ((provider === 'local' || provider === 'jev') && model === 'auto') {
      show(false, t('global_defaults.studio_err_model_required'));
      return;
    }
    if (baseVar && !baseUrl && provider === 'jev') {
      show(false, t('global_defaults.studio_err_url_required', { key: baseVar }));
      return;
    }

    const env = EnvironmentManager.getInstance();
    const results = [
      await env.setEnvVar(GlobalDefaultsModal.STUDIO_PROVIDER_VAR, provider),
      await env.setEnvVar(GlobalDefaultsModal.STUDIO_MODEL_VAR, model)
    ];
    if (baseVar && baseUrl && !(provider === 'local' && this.runtimeManaged)) {
      results.push(await env.setEnvVar(baseVar, baseUrl));
    }

    if (results.every(Boolean)) show(true, t('global_defaults.studio_saved'));
    else show(false, t('global_defaults.studio_saved_session'));
    await this.refreshStudioModelStatus();
  }

  private getEnvKeyForProvider(provider: string): string {
    switch (provider) {
      case 'deepseek': return 'DEEPSEEK_API_KEY';
      case 'anthropic': return 'ANTHROPIC_API_KEY';
      case 'google': return 'GEMINI_API_KEY';
      case 'mistral': return 'MISTRAL_API_KEY';
      case 'openai': return 'OPENAI_API_KEY';
      case 'groq': return 'GROQ_API_KEY';
      case 'local': return 'LOCAL_LLM_API_KEY';
      case 'jev': return 'JEV_API_KEY';
      default: return 'DEEPSEEK_API_KEY';
    }
  }

  private async updateKeyIndicator(): Promise<void> {
    const providerSelect = this.overlay.querySelector('#def-provider') as HTMLSelectElement;
    const provider = providerSelect?.value || 'deepseek';
    const indicator = this.overlay.querySelector('#api-key-indicator') as HTMLElement;
    const deployKeyNameInput = this.overlay.querySelector('#deploy-key-name') as HTMLInputElement;
    const envManager = EnvironmentManager.getInstance();

    if (provider === 'jev') {
      if (indicator) {
        indicator.textContent = t('global_defaults.indicator_jev');
        indicator.style.background = '#e1dfdd';
        indicator.style.color = '#242424';
      }
      if (deployKeyNameInput) deployKeyNameInput.value = 'JEV_API_KEY';
      return;
    }

    if (provider === 'local') {
      // Chave opcional: só existe em servidores locais protegidos por token.
      const keyName = 'LOCAL_LLM_API_KEY';
      const keyVal = await envManager.getEnvVar(keyName);
      if (deployKeyNameInput) deployKeyNameInput.value = keyName;
      if (indicator) {
        if (keyVal && keyVal.length > 0) {
          const masked = keyVal.length > 8 ? `${keyVal.slice(0, 3)}...${keyVal.slice(-3)}` : '••••';
          indicator.textContent = t('global_defaults.indicator_configured', { key: keyName, masked });
          indicator.style.background = '#dff6dd';
          indicator.style.color = '#0e700e';
        } else {
          indicator.textContent = t('global_defaults.indicator_local');
          indicator.style.background = '#e1dfdd';
          indicator.style.color = '#242424';
        }
      }
      return;
    }

    // Provedor hospedado com gateway: a credencial relevante é a chave virtual.
    const gatewayUrl = await envManager.getEnvVar('ARESTRIX_AI_GATEWAY_URL');
    if (gatewayUrl) {
      if (indicator) {
        indicator.textContent = t('global_defaults.indicator_gateway');
        indicator.style.background = '#dff6dd';
        indicator.style.color = '#0e700e';
      }
      if (deployKeyNameInput) deployKeyNameInput.value = 'ARESTRIX_AI_GATEWAY_KEY';
      return;
    }

    const keyName = this.getEnvKeyForProvider(provider);
    if (deployKeyNameInput) {
      deployKeyNameInput.value = keyName;
    }

    const keyVal = await envManager.getEnvVar(keyName);

    if (indicator) {
      if (keyVal && keyVal.length > 0) {
        const masked = keyVal.length > 8 ? `${keyVal.slice(0, 3)}...${keyVal.slice(-3)}` : '••••';
        indicator.textContent = t('global_defaults.indicator_configured', { key: keyName, masked });
        indicator.style.background = '#dff6dd';
        indicator.style.color = '#0e700e';
      } else {
        indicator.textContent = t('global_defaults.indicator_missing', { key: keyName });
        indicator.style.background = '#fde7e9';
        indicator.style.color = '#a80000';
      }
    }
  }

  private async testAgentApi(): Promise<void> {
    const providerSelect = this.overlay.querySelector('#def-provider') as HTMLSelectElement;
    const provider = providerSelect?.value || 'deepseek';
    const statusDiv = this.overlay.querySelector('#api-test-status') as HTMLElement;
    if (!statusDiv) return;

    statusDiv.style.display = 'block';
    statusDiv.style.background = '#f3f2f1';
    statusDiv.style.color = '#242424';
    statusDiv.style.border = '1px solid #d2d0ce';
    statusDiv.textContent = t('global_defaults.test_loading');

    if (provider === 'jev') {
      statusDiv.style.background = '#dff6dd';
      statusDiv.style.color = '#0e700e';
      statusDiv.style.border = '1px solid #107c41';
      statusDiv.textContent = t('global_defaults.test_jev_ok');
      return;
    }

    if (provider === 'local') {
      const input = this.overlay.querySelector('#def-local-base-url') as HTMLInputElement | null;
      const base = normalizeLocalBaseUrl(input?.value || LOCAL_LLM_DEFAULT_URL);
      if (input) input.value = base;
      const result = await discoverLocalModels(base);
      if (result.ok) {
        statusDiv.style.background = '#dff6dd';
        statusDiv.style.color = '#0e700e';
        statusDiv.style.border = '1px solid #107c41';
        statusDiv.textContent = result.models.length > 0
          ? t('global_defaults.test_local_ok', { url: result.baseUrl || base, count: result.models.length })
          : t('global_defaults.test_local_ok_empty', { url: result.baseUrl || base });
      } else {
        statusDiv.style.background = '#fff4ce';
        statusDiv.style.color = '#797673';
        statusDiv.style.border = '1px solid #fce100';
        statusDiv.textContent = t('global_defaults.test_local_fail', { url: result.baseUrl || base });
      }
      return;
    }

    // Com AI Gateway configurado, a credencial relevante e a chave virtual.
    if (provider !== 'local' && provider !== 'jev') {
      const envManager = EnvironmentManager.getInstance();
      const gatewayUrl = await envManager.getEnvVar('ARESTRIX_AI_GATEWAY_URL');
      if (gatewayUrl) {
        const webui = this.backend();
        if (webui) {
          try {
            const raw = await webui.call('ai_gateway_health', JSON.stringify({ url: gatewayUrl, key: '' }));
            const result = typeof raw === 'string' ? JSON.parse(raw || '{}') : raw;
            if (result?.ok) {
              statusDiv.style.background = '#dff6dd';
              statusDiv.style.color = '#0e700e';
              statusDiv.style.border = '1px solid #107c41';
              statusDiv.textContent = t('global_defaults.gateway_test_ok', { status: String(result.status || 200) });
            } else {
              statusDiv.style.background = '#fde7e9';
              statusDiv.style.color = '#a80000';
              statusDiv.style.border = '1px solid #d13438';
              statusDiv.textContent = t('global_defaults.gateway_test_failed', {
                detail: String(result?.error || result?.status || result?.body || '?')
              });
            }
          } catch {
            statusDiv.style.background = '#fde7e9';
            statusDiv.style.color = '#a80000';
            statusDiv.textContent = t('global_defaults.gateway_test_failed', { detail: 'bridge_error' });
          }
          return;
        }
      }
    }

    const keyName = this.getEnvKeyForProvider(provider);
    const envManager = EnvironmentManager.getInstance();
    const keyVal = await envManager.getEnvVar(keyName);

    if (!keyVal || keyVal.trim().length === 0) {
      statusDiv.style.background = '#fde7e9';
      statusDiv.style.color = '#a80000';
      statusDiv.style.border = '1px solid #d13438';
      statusDiv.innerHTML = t('global_defaults.test_missing_key', { key: keyName });
      const panel = this.overlay.querySelector('#panel-deploy-key') as HTMLElement;
      if (panel) panel.style.display = 'block';
      return;
    }

    const masked = keyVal.length > 8 ? `${keyVal.slice(0, 4)}...${keyVal.slice(-4)}` : '••••';

    if (provider === 'deepseek') {
      try {
        const ctrl = new AbortController();
        const tid = setTimeout(() => ctrl.abort(), 6000);
        const resp = await fetch('https://api.deepseek.com/models', {
          headers: { 'Authorization': `Bearer ${keyVal}` },
          signal: ctrl.signal
        });
        clearTimeout(tid);

        if (resp.ok) {
          statusDiv.style.background = '#dff6dd';
          statusDiv.style.color = '#0e700e';
          statusDiv.style.border = '1px solid #107c41';
          statusDiv.textContent = t('global_defaults.test_deepseek_ok', { key: keyName, masked });
          return;
        } else if (resp.status === 401 || resp.status === 403) {
          statusDiv.style.background = '#fde7e9';
          statusDiv.style.color = '#a80000';
          statusDiv.style.border = '1px solid #d13438';
          statusDiv.textContent = t('global_defaults.test_deepseek_fail', { status: String(resp.status), key: keyName });
          return;
        }
      } catch {}
    } else if (provider === 'openai') {
      try {
        const ctrl = new AbortController();
        const tid = setTimeout(() => ctrl.abort(), 6000);
        const resp = await fetch('https://api.openai.com/v1/models', {
          headers: { 'Authorization': `Bearer ${keyVal}` },
          signal: ctrl.signal
        });
        clearTimeout(tid);

        if (resp.ok) {
          statusDiv.style.background = '#dff6dd';
          statusDiv.style.color = '#0e700e';
          statusDiv.style.border = '1px solid #107c41';
          statusDiv.textContent = t('global_defaults.test_openai_ok', { key: keyName, masked });
          return;
        } else if (resp.status === 401 || resp.status === 403) {
          statusDiv.style.background = '#fde7e9';
          statusDiv.style.color = '#a80000';
          statusDiv.style.border = '1px solid #d13438';
          statusDiv.textContent = t('global_defaults.test_openai_fail', { status: String(resp.status), key: keyName });
          return;
        }
      } catch {}
    }

    statusDiv.style.background = '#dff6dd';
    statusDiv.style.color = '#0e700e';
    statusDiv.style.border = '1px solid #107c41';
    statusDiv.textContent = t('global_defaults.test_os_ok', { key: keyName, masked });
  }

  private async deployApiKey(): Promise<void> {
    const keyNameInput = this.overlay.querySelector('#deploy-key-name') as HTMLInputElement;
    const keyValInput = this.overlay.querySelector('#deploy-key-value') as HTMLInputElement;
    const feedback = this.overlay.querySelector('#deploy-save-feedback') as HTMLElement;

    const keyName = keyNameInput?.value.trim();
    const keyVal = keyValInput?.value.trim();

    if (!keyName || !keyVal) {
      if (feedback) {
        feedback.style.display = 'block';
        feedback.style.background = '#fde7e9';
        feedback.style.color = '#a80000';
        feedback.textContent = t('global_defaults.deploy_missing_inputs');
      }
      return;
    }

    if (feedback) {
      feedback.style.display = 'block';
      feedback.style.background = '#f3f2f1';
      feedback.style.color = '#242424';
      feedback.textContent = t('global_defaults.deploy_saving');
    }

    const envManager = EnvironmentManager.getInstance();
    const success = await envManager.setEnvVar(keyName, keyVal);

    if (feedback) {
      if (success) {
        feedback.style.background = '#dff6dd';
        feedback.style.color = '#0e700e';
        feedback.textContent = t('global_defaults.deploy_saved_windows', { key: keyName });
        keyValInput.value = '';
        this.updateKeyIndicator();
      } else {
        feedback.style.background = '#fff4ce';
        feedback.style.color = '#797673';
        feedback.textContent = t('global_defaults.deploy_saved_session');
        this.updateKeyIndicator();
      }
    }
  }

  private async saveValues(): Promise<void> {
    const provider = (this.overlay.querySelector('#def-provider') as HTMLSelectElement)?.value as LLMProvider || 'deepseek';
    const model = (this.overlay.querySelector('#def-model') as HTMLInputElement)?.value.trim() || 'deepseek-flash';
    const skillPath = (this.overlay.querySelector('#def-skill') as HTMLInputElement)?.value.trim();
    const promptTemplate = (this.overlay.querySelector('#def-prompt') as HTMLTextAreaElement)?.value.trim();

    const isolationLevel = (this.overlay.querySelector('#def-isolation') as HTMLSelectElement)?.value as SandboxIsolationLevel || 'container';
    const network = (this.overlay.querySelector('#def-network') as HTMLSelectElement)?.value as SandboxNetworkPolicy || 'egress_llm_only';
    const maxMemoryMB = parseInt((this.overlay.querySelector('#def-memory') as HTMLInputElement)?.value, 10) || 512;
    const timeoutSeconds = parseInt((this.overlay.querySelector('#def-timeout') as HTMLInputElement)?.value, 10) || 300;

    const roRaw = (this.overlay.querySelector('#def-ro-mounts') as HTMLInputElement)?.value || '';
    const readOnlyMounts = roRaw.split(',').map(s => s.trim()).filter(Boolean);

    const rwRaw = (this.overlay.querySelector('#def-rw-mounts') as HTMLInputElement)?.value || '';
    const readWriteMounts = rwRaw.split(',').map(s => s.trim()).filter(Boolean);

    const allowedTools: CodeModeTool[] = [];
    this.overlay.querySelectorAll<HTMLInputElement>('.def-tool-check:checked').forEach(chk => {
      allowedTools.push(chk.value as CodeModeTool);
    });

    const scriptsRaw = (this.overlay.querySelector('#def-custom-scripts') as HTMLInputElement)?.value || '';
    const customScripts = scriptsRaw.split(',').map(s => s.trim()).filter(Boolean);

    const mcp = this.mcpPicker?.getValue() ?? { servers: [], tools: [] };

    const memEnabled = (this.overlay.querySelector('#def-memory-enabled') as HTMLInputElement)?.checked || false;
    const memPersistent = (this.overlay.querySelector('input[name="def-memory-persistence"]:checked') as HTMLInputElement)?.value === 'persistent';
    const memReadBb = (this.overlay.querySelector('#def-mem-read-bb') as HTMLInputElement)?.checked || false;
    const memWriteBb = (this.overlay.querySelector('#def-mem-write-bb') as HTMLInputElement)?.checked || false;
    const memReadLessons = (this.overlay.querySelector('#def-mem-read-lessons') as HTMLInputElement)?.checked || false;
    const memWriteLessons = (this.overlay.querySelector('#def-mem-write-lessons') as HTMLInputElement)?.checked || false;
    const memMeasurement = ((this.overlay.querySelector('#def-memory-measurement') as HTMLSelectElement)?.value || 'normal') as 'normal' | 'memory_off';
    const memRecordMetrics = (this.overlay.querySelector('#def-memory-record-metrics') as HTMLInputElement)?.checked || false;
    const memWriteKeysRaw = (this.overlay.querySelector('#def-memory-write-keys') as HTMLInputElement)?.value || '';
    const memWriteKeys = memWriteKeysRaw.split(',').map(s => s.trim()).filter(Boolean);
    const memMaxLessons = Math.max(1, Math.min(500,
      Math.floor(Number((this.overlay.querySelector('#def-memory-max-lessons') as HTMLInputElement)?.value) || 20)));
    const memMaxLessonChars = Math.max(100, Math.min(20000,
      Math.floor(Number((this.overlay.querySelector('#def-memory-max-lesson-chars') as HTMLInputElement)?.value) || 4000)));
    const retentionRaw = Number((this.overlay.querySelector('#def-memory-retention-days') as HTMLInputElement)?.value);
    const memRetentionDays = Number.isFinite(retentionRaw)
      ? Math.max(0, Math.min(3650, Math.floor(retentionRaw)))
      : 180;
    const memConsolidationMode = ((this.overlay.querySelector('#def-memory-consolidation-mode') as HTMLSelectElement)?.value
      || 'off') as 'off' | 'idle' | 'after_episodes';
    const memConsolidationIdle = Math.max(5, Math.min(86400,
      Math.floor(Number((this.overlay.querySelector('#def-memory-consolidation-idle') as HTMLInputElement)?.value) || 300)));
    const memConsolidationEpisodes = Math.max(1, Math.min(100000,
      Math.floor(Number((this.overlay.querySelector('#def-memory-consolidation-episodes') as HTMLInputElement)?.value) || 20)));
    const memConsolidationDormantRaw = Number((this.overlay.querySelector('#def-memory-consolidation-dormant') as HTMLInputElement)?.value);
    const memConsolidationDormant = Number.isFinite(memConsolidationDormantRaw)
      ? Math.max(0, Math.min(3650, Math.floor(memConsolidationDormantRaw)))
      : 180;
    const memEmbeddingEnabled = (this.overlay.querySelector('#def-memory-embedding-enabled') as HTMLInputElement)?.checked || false;
    const memEmbeddingModel = (this.overlay.querySelector('#def-memory-embedding-model') as HTMLInputElement)?.value.trim()
      || 'embeddinggemma-2';
    const memEmbeddingBaseUrl = (this.overlay.querySelector('#def-memory-embedding-base-url') as HTMLInputElement)?.value.trim() || '';
    const memEmbeddingBatch = Math.max(1, Math.min(128,
      Math.floor(Number((this.overlay.querySelector('#def-memory-embedding-batch') as HTMLInputElement)?.value) || 8)));
    const memEmbeddingMaxChars = Math.max(64, Math.min(32000,
      Math.floor(Number((this.overlay.querySelector('#def-memory-embedding-max-chars') as HTMLInputElement)?.value) || 8000)));
    const memEmbeddingDim = Math.max(0, Math.min(8192,
      Math.floor(Number((this.overlay.querySelector('#def-memory-embedding-dim') as HTMLInputElement)?.value) || 768)));

    const newDefaults: IGlobalPipelineDefaults = {
      sandbox: {
        isolationLevel,
        network,
        maxMemoryMB,
        timeoutSeconds,
        readOnlyMounts,
        readWriteMounts
      },
      model: {
        provider,
        model,
        skillPath,
        promptTemplate
      },
      codeMode: {
        allowedTools,
        customScripts,
        mcpServers: mcp.servers,
        allowedMcpTools: mcp.tools
      },
      memory: {
        enabled: memEnabled,
        persistent: memPersistent,
        readBlackboard: memReadBb,
        writeBlackboard: memWriteBb,
        semanticLessons: memReadLessons,
        recordLessons: memWriteLessons,
        allowedKeys: [],
        writeKeys: memWriteKeys,
        domains: [],
        maxLessonsPerRun: memMaxLessons,
        maxLessonChars: memMaxLessonChars,
        retentionDays: memRetentionDays,
        consolidationMode: memConsolidationMode,
        consolidationIdleSeconds: memConsolidationIdle,
        consolidationEpisodeThreshold: memConsolidationEpisodes,
        consolidationDormantDays: memConsolidationDormant,
        measurementMode: memMeasurement,
        recordMetrics: memRecordMetrics,
        embeddingEnabled: memEmbeddingEnabled,
        embeddingProvider: 'local',
        embeddingModel: memEmbeddingModel,
        embeddingBaseUrl: memEmbeddingBaseUrl,
        embeddingTimeoutSeconds: 60,
        embeddingBatchSize: memEmbeddingBatch,
        embeddingMaxChars: memEmbeddingMaxChars,
        embeddingDimensions: memEmbeddingDim
      }
    };

    // O endpoint do servidor local/Jev é uma variável de ambiente da máquina (não vai para o projeto).
    // Grava antes de atualizar o grafo para que a execução seguinte já encontre o valor.
    const endpointVar = this.projectEndpointVar(provider);
    if (endpointVar && !(provider === 'local' && this.runtimeManaged)) {
      const input = this.overlay.querySelector('#def-local-base-url') as HTMLInputElement | null;
      const raw = input?.value.trim() || (provider === 'local' ? LOCAL_LLM_DEFAULT_URL : '');
      if (raw) {
        if (provider === 'local') {
          const normalized = normalizeLocalBaseUrl(raw);
          if (input) input.value = normalized;
          await saveLocalBaseUrl(normalized);
        } else {
          await EnvironmentManager.getInstance().setEnvVar(endpointVar, raw);
        }
      }
    }

    this.graph.updateGlobalDefaults(newDefaults);
  }

  private escapeHtml(text: string): string {
    return text
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
}
