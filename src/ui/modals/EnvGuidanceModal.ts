import { EnvironmentManager, IEnvKeyGuidance } from '../../core/env/EnvironmentManager.ts';

/**
 * Arestrix Studio - Modal de Orientação e Gravação de Variáveis de Ambiente
 * Padrão Corporativo Microsoft Office / Segoe UI
 * Proibição Estrita de Sombras em Texto (text-shadow: none !important)
 */
export class EnvGuidanceModal {
  private static instance: EnvGuidanceModal;
  private modal?: HTMLElement;
  private currentGuidance?: IEnvKeyGuidance;
  private envManager: EnvironmentManager;
  private onSavedCallback?: () => void;

  private constructor() {
    this.envManager = EnvironmentManager.getInstance();
    if (typeof document !== 'undefined') {
      this.modal = document.createElement('div');
      this.modal.className = 'modal-backdrop';
      this.modal.id = 'modal-env-guidance';
      this.modal.style.display = 'none';
      this.modal.addEventListener('click', (e: MouseEvent) => {
        if (e.target === this.modal) {
          this.hide();
        }
      });
      document.body.appendChild(this.modal);
    }
  }

  public static getInstance(): EnvGuidanceModal {
    if (!EnvGuidanceModal.instance) {
      EnvGuidanceModal.instance = new EnvGuidanceModal();
    }
    return EnvGuidanceModal.instance;
  }

  public show(keyName: string = 'DEEPSEEK_API_KEY', onSaved?: () => void): void {
    this.currentGuidance = this.envManager.getGuidance(keyName);
    this.onSavedCallback = onSaved;

    if (!this.modal) return;
    this.modal.style.display = 'flex';
    this.render();
  }

  public hide(): void {
    if (this.modal) {
      this.modal.style.display = 'none';
    }
  }

  private render(): void {
    if (!this.modal || !this.currentGuidance) return;
    const g = this.currentGuidance;

    this.modal.innerHTML = `
      <div class="modal-window" style="max-width: 680px; width: 92%; max-height: 88vh; display: flex; flex-direction: column; background: #ffffff; border-radius: 8px; border: 1px solid #cbd5e1; box-shadow: 0 12px 36px rgba(0,0,0,0.18);">
        <!-- Header Estilo Office -->
        <div class="modal-header" style="background: #f8fafc; border-bottom: 1px solid #e2e8f0; padding: 14px 18px; display: flex; justify-content: space-between; align-items: center;">
          <div style="display: flex; align-items: center; gap: 10px;">
            <div style="width: 32px; height: 32px; background: #0f6cbd; color: white; border-radius: 6px; display: flex; align-items: center; justify-content: center; font-size: 16px;">
              🛡️
            </div>
            <div>
              <h3 style="margin: 0; font-size: 15px; font-weight: 700; color: #0f172a; letter-spacing: -0.2px;">
                Variável de Ambiente Necessária: ${g.keyName}
              </h3>
              <p style="margin: 2px 0 0 0; font-size: 12px; color: #64748b;">
                Política de Segurança e Proteção de Segredos do Arestrix
              </p>
            </div>
          </div>
          <button id="btn-close-env-modal" class="modal-close-btn" style="background: transparent; border: none; font-size: 20px; color: #64748b; cursor: pointer;">&times;</button>
        </div>

        <!-- Corpo do Modal -->
        <div class="modal-body" style="flex: 1; overflow-y: auto; padding: 18px 20px; display: flex; flex-direction: column; gap: 16px; font-family: 'Segoe UI', sans-serif;">
          <!-- Alerta de Segurança e Privacidade -->
          <div style="background: #eff6ff; border: 1px solid #bfdbfe; border-radius: 6px; padding: 12px 14px; display: flex; gap: 10px; align-items: flex-start;">
            <span style="font-size: 18px;">🔒</span>
            <div style="font-size: 12px; color: #1e3a8a; line-height: 1.45;">
              <strong>Zero Segredos em Código ou Disco:</strong> O Arestrix nunca armazena suas chaves de API em arquivos locais, banco de dados ou código-fonte. Todas as chaves devem ser lidas diretamente das variáveis de ambiente do seu sistema operacional.
            </div>
          </div>

          <!-- Opção 1: Gravar Automaticamente pelo Arestrix no Windows -->
          <div style="background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 6px; padding: 14px;">
            <h4 style="margin: 0 0 8px 0; font-size: 13px; font-weight: 700; color: #0f172a;">
              Opção 1: Gravar Automaticamente no seu Usuário do Windows
            </h4>
            <p style="margin: 0 0 10px 0; font-size: 12px; color: #475569;">
              Se desejar, o Arestrix pode executar o comando <code>setx</code> para registrar a variável de ambiente permanentemente no seu usuário do Windows:
            </p>
            <div style="display: flex; gap: 8px;">
              <input type="password" id="input-env-key-val" placeholder="Cole sua chave ${g.keyName} aqui..." style="flex: 1; padding: 7px 10px; border: 1px solid #cbd5e1; border-radius: 4px; font-size: 12.5px;" />
              <button id="btn-save-env-auto" class="tool-btn btn-action" style="background: #0f6cbd; color: #ffffff; font-weight: 700; padding: 7px 16px; border-radius: 4px; border: none; cursor: pointer; white-space: nowrap;">
                💾 Gravar no Windows
              </button>
            </div>
            <div id="env-save-status" style="margin-top: 6px; font-size: 11.5px; display: none;"></div>
          </div>

          <!-- Opção 2: Gravar Manualmente via Terminal ou Sistema -->
          <div>
            <h4 style="margin: 0 0 8px 0; font-size: 13px; font-weight: 700; color: #0f172a;">
              Opção 2: Gravar Manualmente no Windows (Instruções Passo a Passo)
            </h4>

            <div style="display: flex; flex-direction: column; gap: 10px;">
              <!-- Bloco PowerShell -->
              <div style="background: #ffffff; border: 1px solid #cbd5e1; border-radius: 6px; padding: 10px 12px;">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px;">
                  <strong style="font-size: 11.5px; color: #334155;">PowerShell (Recomendado):</strong>
                  <button class="btn-copy-code" data-code="${this.escapeHtml(g.powershellCmd)}" style="background: #f1f5f9; border: 1px solid #cbd5e1; padding: 2px 8px; border-radius: 3px; font-size: 11px; font-weight: 600; cursor: pointer;">
                    📋 Copiar Comando
                  </button>
                </div>
                <code style="display: block; background: #0f172a; color: #f8fafc; padding: 8px 10px; border-radius: 4px; font-size: 11px; font-family: Consolas, monospace; overflow-x: auto;">
                  ${this.escapeHtml(g.powershellCmd)}
                </code>
              </div>

              <!-- Bloco Prompt de Comando (CMD) -->
              <div style="background: #ffffff; border: 1px solid #cbd5e1; border-radius: 6px; padding: 10px 12px;">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 6px;">
                  <strong style="font-size: 11.5px; color: #334155;">Prompt de Comando (CMD):</strong>
                  <button class="btn-copy-code" data-code="${this.escapeHtml(g.cmdCmd)}" style="background: #f1f5f9; border: 1px solid #cbd5e1; padding: 2px 8px; border-radius: 3px; font-size: 11px; font-weight: 600; cursor: pointer;">
                    📋 Copiar Comando
                  </button>
                </div>
                <code style="display: block; background: #0f172a; color: #f8fafc; padding: 8px 10px; border-radius: 4px; font-size: 11px; font-family: Consolas, monospace; overflow-x: auto;">
                  ${this.escapeHtml(g.cmdCmd)}
                </code>
              </div>

              <!-- Bloco Interface Gráfica do Windows -->
              <div style="background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 6px; padding: 10px 12px; font-size: 11.5px; color: #475569;">
                <strong style="display: block; color: #0f172a; margin-bottom: 6px;">Pela Interface Gráfica do Windows (sysdm.cpl):</strong>
                <ol style="margin: 0; padding-left: 18px; line-height: 1.5;">
                  ${g.guiInstructions.map(step => `<li>${step}</li>`).join('')}
                </ol>
              </div>
            </div>
          </div>
        </div>

        <!-- Footer -->
        <div class="modal-footer" style="background: #f8fafc; border-top: 1px solid #e2e8f0; padding: 12px 18px; display: flex; justify-content: space-between; align-items: center;">
          <span style="font-size: 12px; color: #64748b;">
            Após configurar, clique em verificar para continuar a importação.
          </span>
          <div style="display: flex; gap: 8px;">
            <button id="btn-env-cancel" style="background: #ffffff; border: 1px solid #cbd5e1; padding: 6px 14px; border-radius: 4px; font-size: 12px; font-weight: 600; cursor: pointer;">
              Fechar
            </button>
            <button id="btn-env-recheck" style="background: #0f6cbd; color: white; border: none; padding: 6px 18px; border-radius: 4px; font-size: 12px; font-weight: 700; cursor: pointer;">
              🔄 Verificar Variável Novamente
            </button>
          </div>
        </div>
      </div>
    `;

    this.bindEvents();
  }

  private bindEvents(): void {
    if (!this.modal) return;

    this.modal.querySelector('#btn-close-env-modal')?.addEventListener('click', () => this.hide());
    this.modal.querySelector('#btn-env-cancel')?.addEventListener('click', () => this.hide());

    // Botões de copiar comandos
    this.modal.querySelectorAll('.btn-copy-code').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const target = e.currentTarget as HTMLElement;
        const code = target.getAttribute('data-code') || '';
        if (typeof navigator !== 'undefined' && navigator.clipboard) {
          navigator.clipboard.writeText(code);
          const orig = target.textContent;
          target.textContent = '✅ Copiado!';
          setTimeout(() => { target.textContent = orig; }, 2000);
        }
      });
    });

    // Gravação automática via setx
    const saveBtn = this.modal.querySelector('#btn-save-env-auto') as HTMLButtonElement;
    const inputVal = this.modal.querySelector('#input-env-key-val') as HTMLInputElement;
    const statusDiv = this.modal.querySelector('#env-save-status') as HTMLElement;

    saveBtn?.addEventListener('click', async () => {
      const val = inputVal?.value?.trim();
      if (!val || !this.currentGuidance) {
        if (statusDiv) {
          statusDiv.style.display = 'block';
          statusDiv.style.color = '#dc2626';
          statusDiv.textContent = 'Por favor, insira o valor da chave de API.';
        }
        return;
      }

      saveBtn.disabled = true;
      saveBtn.textContent = 'Gravando...';

      const ok = await this.envManager.setEnvVar(this.currentGuidance.keyName, val);
      saveBtn.disabled = false;
      saveBtn.textContent = '💾 Gravar no Windows';

      if (ok) {
        if (statusDiv) {
          statusDiv.style.display = 'block';
          statusDiv.style.color = '#16a34a';
          statusDiv.textContent = '✅ Variável gravada com sucesso no ambiente!';
        }
        setTimeout(() => {
          this.hide();
          if (this.onSavedCallback) this.onSavedCallback();
        }, 1200);
      } else {
        if (statusDiv) {
          statusDiv.style.display = 'block';
          statusDiv.style.color = '#d97706';
          statusDiv.innerHTML = '⚠️ Não foi possível gravar automaticamente via terminal. Por favor, utilize o comando PowerShell ou CMD indicado abaixo.';
        }
      }
    });

    // Botão de re-verificar
    this.modal.querySelector('#btn-env-recheck')?.addEventListener('click', async () => {
      if (!this.currentGuidance) return;
      const has = await this.envManager.hasEnvVar(this.currentGuidance.keyName);
      if (has) {
        this.hide();
        if (this.onSavedCallback) this.onSavedCallback();
      } else {
        alert(`A variável ${this.currentGuidance.keyName} ainda não foi detectada no ambiente. Se você acabou de definir via CMD ou GUI do Windows, reinicie o aplicativo para que o Windows atualize as variáveis herdadas.`);
      }
    });
  }

  private escapeHtml(str: string): string {
    return str
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }
}
