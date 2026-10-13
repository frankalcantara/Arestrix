import { Graph } from '../core/Graph.ts';
import { globalEventBus } from '../core/EventBus.ts';
import { IGlobalPipelineDefaults, LLMProvider, SandboxIsolationLevel, SandboxNetworkPolicy, CodeModeTool, DEFAULT_PIPELINE_DEFAULTS } from '../core/types.ts';
import { ModalWindowManager, IModalWindowController } from './modals/ModalWindowManager.ts';
import { EnvironmentManager } from '../core/env/EnvironmentManager.ts';
import { EnvGuidanceModal } from './modals/EnvGuidanceModal.ts';
import { t } from '../i18n/index.ts';

export type GlobalDefaultsTab = 'model' | 'sandbox' | 'tools' | 'memory';

export class GlobalDefaultsModal {
  private overlay: HTMLElement;
  private graph: Graph;
  private windowController?: IModalWindowController;
  private activeTab: GlobalDefaultsTab = 'model';

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
                    <option value="openai" ${defaults.model.provider === 'openai' ? 'selected' : ''}>OpenAI (GPT-4o / o3)</option>
                    <option value="jev" ${defaults.model.provider === 'jev' ? 'selected' : ''}>Jev / Engine Visual Local</option>
                    <option value="local" ${defaults.model.provider === 'local' ? 'selected' : ''}>Local (Ollama / vLLM)</option>
                  </select>
                </div>

                <div class="form-group">
                  <label class="form-label" for="def-model">${t('global_defaults.model_name_label')}</label>
                  <input type="text" id="def-model" class="form-input" value="${this.escapeHtml(defaults.model.model)}" placeholder="ex: deepseek-flash, claude-opus-5.5" />
                </div>
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
                <label class="form-label" for="def-mcp-tools">${t('global_defaults.mcp_tools_label')} <span style="font-size: 10px; color: #0f6cbd;">${t('global_defaults.mcp_tools_servers_badge')}</span></label>
                <input type="text" id="def-mcp-tools" class="form-input" value="${this.escapeHtml((defaults.codeMode.allowedMcpTools || []).join(', '))}" placeholder="read_file, write_file, fetch_url" />
                <div class="form-hint">${t('global_defaults.mcp_tools_hint')}</div>
              </div>
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
            </div>
          </div>
        </div>

        <div class="modal-footer">
          <button class="btn btn-secondary-pill" id="btn-reset-defaults">${t('global_defaults.btn_reset')}</button>
          <div style="flex: 1;"></div>
          <button class="btn btn-secondary-pill" id="btn-cancel-defaults">${t('global_defaults.btn_cancel')}</button>
          <button class="btn btn-primary" id="btn-save-defaults">${t('global_defaults.btn_save')}</button>
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
      else if (val === 'openai') modelInput.value = 'gpt-4o';
      else if (val === 'jev') modelInput.value = 'jev-svg-validator';

      // Limpa status anterior de teste
      const statusDiv = this.overlay.querySelector('#api-test-status') as HTMLElement;
      if (statusDiv) statusDiv.style.display = 'none';

      this.updateKeyIndicator();
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

    // Salvar/Implantar chave no Windows
    this.overlay.querySelector('#btn-submit-deploy-key')?.addEventListener('click', () => {
      this.deployApiKey();
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
      this.saveValues();
      this.hide();
    });
  }

  private getEnvKeyForProvider(provider: string): string {
    switch (provider) {
      case 'deepseek': return 'DEEPSEEK_API_KEY';
      case 'anthropic': return 'ANTHROPIC_API_KEY';
      case 'google': return 'GEMINI_API_KEY';
      case 'openai': return 'OPENAI_API_KEY';
      case 'groq': return 'GROQ_API_KEY';
      default: return 'DEEPSEEK_API_KEY';
    }
  }

  private async updateKeyIndicator(): Promise<void> {
    const providerSelect = this.overlay.querySelector('#def-provider') as HTMLSelectElement;
    const provider = providerSelect?.value || 'deepseek';
    const indicator = this.overlay.querySelector('#api-key-indicator') as HTMLElement;
    const deployKeyNameInput = this.overlay.querySelector('#deploy-key-name') as HTMLInputElement;

    if (provider === 'jev') {
      if (indicator) {
        indicator.textContent = t('global_defaults.indicator_jev');
        indicator.style.background = '#e1dfdd';
        indicator.style.color = '#242424';
      }
      if (deployKeyNameInput) deployKeyNameInput.value = '';
      return;
    }

    if (provider === 'local') {
      if (indicator) {
        indicator.textContent = t('global_defaults.indicator_local');
        indicator.style.background = '#e1dfdd';
        indicator.style.color = '#242424';
      }
      if (deployKeyNameInput) deployKeyNameInput.value = '';
      return;
    }

    const keyName = this.getEnvKeyForProvider(provider);
    if (deployKeyNameInput) {
      deployKeyNameInput.value = keyName;
    }

    const envManager = EnvironmentManager.getInstance();
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
      try {
        const ctrl = new AbortController();
        const tid = setTimeout(() => ctrl.abort(), 3000);
        const resp = await fetch('http://localhost:11434/api/tags', { signal: ctrl.signal });
        clearTimeout(tid);
        if (resp.ok) {
          statusDiv.style.background = '#dff6dd';
          statusDiv.style.color = '#0e700e';
          statusDiv.style.border = '1px solid #107c41';
          statusDiv.textContent = t('global_defaults.test_local_ok');
          return;
        }
      } catch {}
      statusDiv.style.background = '#fff4ce';
      statusDiv.style.color = '#797673';
      statusDiv.style.border = '1px solid #fce100';
      statusDiv.textContent = t('global_defaults.test_local_hint');
      return;
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

  private saveValues(): void {
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

    const mcpRaw = (this.overlay.querySelector('#def-mcp-tools') as HTMLInputElement)?.value || '';
    const allowedMcpTools = mcpRaw.split(',').map(s => s.trim()).filter(Boolean);

    const memEnabled = (this.overlay.querySelector('#def-memory-enabled') as HTMLInputElement)?.checked || false;
    const memPersistent = (this.overlay.querySelector('input[name="def-memory-persistence"]:checked') as HTMLInputElement)?.value === 'persistent';
    const memReadBb = (this.overlay.querySelector('#def-mem-read-bb') as HTMLInputElement)?.checked || false;
    const memWriteBb = (this.overlay.querySelector('#def-mem-write-bb') as HTMLInputElement)?.checked || false;
    const memReadLessons = (this.overlay.querySelector('#def-mem-read-lessons') as HTMLInputElement)?.checked || false;
    const memWriteLessons = (this.overlay.querySelector('#def-mem-write-lessons') as HTMLInputElement)?.checked || false;

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
        allowedMcpTools
      },
      memory: {
        enabled: memEnabled,
        persistent: memPersistent,
        readBlackboard: memReadBb,
        writeBlackboard: memWriteBb,
        semanticLessons: memReadLessons,
        recordLessons: memWriteLessons,
        allowedKeys: [],
        domains: []
      }
    };

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
