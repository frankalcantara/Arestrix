import { Graph } from '../core/Graph.ts';
import { IGlobalPipelineDefaults, LLMProvider, SandboxIsolationLevel, SandboxNetworkPolicy, CodeModeTool, DEFAULT_PIPELINE_DEFAULTS } from '../core/types.ts';

export class GlobalDefaultsModal {
  private overlay: HTMLElement;
  private graph: Graph;

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
  }

  public hide(): void {
    this.overlay.classList.add('hidden');
  }

  private render(): void {
    const defaults = this.graph.getGlobalDefaults();

    this.overlay.innerHTML = `
      <div class="modal-dialog global-defaults-dialog">
        <div class="modal-header">
          <div class="modal-title-group">
            <div class="modal-header-icon">⚙️</div>
            <div>
              <h2 class="modal-title">Padrões Globais do Pipeline de Agentes</h2>
              <div class="modal-subtitle">Configure o ambiente padrão (VM, Provedor e Ferramentas) herdado pelos nós do grafo</div>
            </div>
          </div>
          <button class="modal-close-btn" id="btn-close-defaults-modal">&times;</button>
        </div>

        <div class="modal-body-scrollable">
          <!-- 1. Provedor & Modelo de IA -->
          <div class="settings-section-card">
            <div class="section-card-header">
              <span class="section-card-icon">🧠</span>
              <div class="section-card-titles">
                <h3 class="section-card-title">Provedor de IA & Modelo Padrão</h3>
                <p class="section-card-desc">Modelo base atribuído a novos nós ou nós com herança ativada</p>
              </div>
            </div>

            <div class="form-row-grid">
              <div class="form-group">
                <label class="form-label" for="def-provider">Provedor Principal</label>
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
                <label class="form-label" for="def-model">Nome do Modelo</label>
                <input type="text" id="def-model" class="form-input" value="${this.escapeHtml(defaults.model.model)}" placeholder="ex: deepseek-flash, claude-opus-5.5" />
              </div>
            </div>

            <div class="form-group">
              <label class="form-label" for="def-skill">Arquivo de Skill / Instruções Padrão</label>
              <input type="text" id="def-skill" class="form-input" value="${this.escapeHtml(defaults.model.skillPath || '')}" placeholder=".agents/sua-skill/SKILL.md" />
            </div>

            <div class="form-group">
              <label class="form-label" for="def-prompt">Template de Prompt Padrão</label>
              <textarea id="def-prompt" class="form-textarea" rows="2">${this.escapeHtml(defaults.model.promptTemplate || '')}</textarea>
              <div class="form-hint">Dica: use <code>\${id}</code> para interpolação dinâmica do identificador do item/artigo.</div>
            </div>
          </div>

          <!-- 2. Máquina Virtual & Sandbox -->
          <div class="settings-section-card">
            <div class="section-card-header">
              <span class="section-card-icon">🛡️</span>
              <div class="section-card-titles">
                <h3 class="section-card-title">Máquina Virtual & Política de Sandbox</h3>
                <p class="section-card-desc">Ambiente de isolamento e fronteira de segurança dos agentes</p>
              </div>
            </div>

            <div class="form-row-grid">
              <div class="form-group">
                <label class="form-label" for="def-isolation">Nível de Isolamento</label>
                <select id="def-isolation" class="form-select">
                  <option value="container" ${defaults.sandbox.isolationLevel === 'container' ? 'selected' : ''}>Container Docker/Podman (Recomendado)</option>
                  <option value="microvm" ${defaults.sandbox.isolationLevel === 'microvm' ? 'selected' : ''}>MicroVM Isolada (Firecracker / WSL2)</option>
                  <option value="host_direct" ${defaults.sandbox.isolationLevel === 'host_direct' ? 'selected' : ''}>Host Direto (Fora da VM / Sem Isolamento)</option>
                </select>
              </div>

              <div class="form-group">
                <label class="form-label" for="def-network">Política de Rede</label>
                <select id="def-network" class="form-select">
                  <option value="egress_llm_only" ${defaults.sandbox.network === 'egress_llm_only' ? 'selected' : ''}>Egress Apenas para API LLM (Seguro)</option>
                  <option value="offline" ${defaults.sandbox.network === 'offline' ? 'selected' : ''}>Totalmente Offline (Bloqueio Total)</option>
                  <option value="full" ${defaults.sandbox.network === 'full' ? 'selected' : ''}>Internet Livre (Acesso Irrestrito)</option>
                </select>
              </div>
            </div>

            <div class="form-row-grid">
              <div class="form-group">
                <label class="form-label" for="def-memory">Limite de RAM (MB)</label>
                <input type="number" id="def-memory" class="form-input" value="${defaults.sandbox.maxMemoryMB}" min="64" step="64" />
              </div>

              <div class="form-group">
                <label class="form-label" for="def-timeout">Timeout de Execução (segundos)</label>
                <input type="number" id="def-timeout" class="form-input" value="${defaults.sandbox.timeoutSeconds}" min="10" step="10" />
              </div>
            </div>

            <div class="form-row-grid">
              <div class="form-group">
                <label class="form-label" for="def-ro-mounts">Montagens Somente-Leitura (RO)</label>
                <input type="text" id="def-ro-mounts" class="form-input" value="${this.escapeHtml(defaults.sandbox.readOnlyMounts.join(', '))}" placeholder="plano.md, .agents/" />
                <div class="form-hint">Separado por vírgulas. Agente não pode modificar.</div>
              </div>

              <div class="form-group">
                <label class="form-label" for="def-rw-mounts">Pastas de Escrita Permitida (RW)</label>
                <input type="text" id="def-rw-mounts" class="form-input" value="${this.escapeHtml(defaults.sandbox.readWriteMounts.join(', '))}" placeholder="aulas/, assets/diagrams/" />
                <div class="form-hint">Separado por vírgulas. Acesso exclusivo de escrita.</div>
              </div>
            </div>
          </div>

          <!-- 3. Ferramentas Code Mode -->
          <div class="settings-section-card">
            <div class="section-card-header">
              <span class="section-card-icon">🔧</span>
              <div class="section-card-titles">
                <h3 class="section-card-title">Ferramentas Code Mode Habilitadas</h3>
                <p class="section-card-desc">Operações procedurais que o agente pode invocar dentro da sua VM</p>
              </div>
            </div>

            <div class="tools-checkbox-grid">
              <label class="tool-checkbox-pill ${defaults.codeMode.allowedTools.includes('write') ? 'checked' : ''}">
                <input type="checkbox" class="def-tool-check" value="write" ${defaults.codeMode.allowedTools.includes('write') ? 'checked' : ''} />
                <span class="tool-pill-name">tools.write</span>
                <span class="tool-pill-desc">Criação de arquivos</span>
              </label>

              <label class="tool-checkbox-pill ${defaults.codeMode.allowedTools.includes('edit') ? 'checked' : ''}">
                <input type="checkbox" class="def-tool-check" value="edit" ${defaults.codeMode.allowedTools.includes('edit') ? 'checked' : ''} />
                <span class="tool-pill-name">tools.edit</span>
                <span class="tool-pill-desc">Edição cirúrgica</span>
              </label>

              <label class="tool-checkbox-pill ${defaults.codeMode.allowedTools.includes('read') ? 'checked' : ''}">
                <input type="checkbox" class="def-tool-check" value="read" ${defaults.codeMode.allowedTools.includes('read') ? 'checked' : ''} />
                <span class="tool-pill-name">tools.read</span>
                <span class="tool-pill-desc">Leitura de arquivos</span>
              </label>

              <label class="tool-checkbox-pill ${defaults.codeMode.allowedTools.includes('bash') ? 'checked' : ''}">
                <input type="checkbox" class="def-tool-check" value="bash" ${defaults.codeMode.allowedTools.includes('bash') ? 'checked' : ''} />
                <span class="tool-pill-name">tools.bash</span>
                <span class="tool-pill-desc">Comandos de terminal</span>
              </label>

              <label class="tool-checkbox-pill ${defaults.codeMode.allowedTools.includes('validate_svg') ? 'checked' : ''}">
                <input type="checkbox" class="def-tool-check" value="validate_svg" ${defaults.codeMode.allowedTools.includes('validate_svg') ? 'checked' : ''} />
                <span class="tool-pill-name">tools.validate_svg</span>
                <span class="tool-pill-desc">Validação Jev / Script</span>
              </label>
            </div>

            <div class="form-group" style="margin-top: 14px;">
              <label class="form-label" for="def-custom-scripts">Scripts Utilitários Permitidos</label>
              <input type="text" id="def-custom-scripts" class="form-input" value="${this.escapeHtml(defaults.codeMode.customScripts.join(', '))}" placeholder="python scripts/audit_exercises.py" />
            </div>
          </div>
        </div>

        <div class="modal-footer">
          <button class="btn btn-secondary-pill" id="btn-reset-defaults">Restaurar Fábrica</button>
          <div style="flex: 1;"></div>
          <button class="btn btn-secondary-pill" id="btn-cancel-defaults">Cancelar</button>
          <button class="btn btn-primary" id="btn-save-defaults">Salvar Padrões Globais</button>
        </div>
      </div>
    `;

    this.bindDialogEvents();
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

    // Provedor altera modelo sugerido se campo estiver padrão
    const providerSelect = this.overlay.querySelector('#def-provider') as HTMLSelectElement;
    const modelInput = this.overlay.querySelector('#def-model') as HTMLInputElement;
    providerSelect?.addEventListener('change', () => {
      const val = providerSelect.value;
      if (val === 'deepseek') modelInput.value = 'deepseek-flash';
      else if (val === 'anthropic') modelInput.value = 'claude-opus-5.5';
      else if (val === 'google') modelInput.value = 'gemini-2.5-flash';
      else if (val === 'openai') modelInput.value = 'gpt-4o';
      else if (val === 'jev') modelInput.value = 'jev-svg-validator';
    });

    // Restaurar Fábrica
    this.overlay.querySelector('#btn-reset-defaults')?.addEventListener('click', () => {
      if (confirm('Deseja restaurar todas as configurações globais de fábrica?')) {
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
        customScripts
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
