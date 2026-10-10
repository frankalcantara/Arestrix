import { EnvironmentManager, IEnvKeyGuidance } from '../../core/env/EnvironmentManager.ts';

/**
 * Arestrix Studio - Diálogo de Orientação e Gravação de Variáveis de Ambiente
 * Padrão Corporativo Microsoft Office (Fluent Dialog)
 * Rigoroso respeito: Zero sombras/glows em fontes
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
      <div class="modal-window" style="max-width: 660px; width: 92%; max-height: 88vh; display: flex; flex-direction: column;">
        <!-- Cabeçalho Estilo Office -->
        <div class="modal-header">
          <div>
            <h3 class="modal-title">Variável de Ambiente: ${g.keyName}</h3>
            <p style="margin: 2px 0 0 0; font-size: 12px; color: #616161; font-family: 'Segoe UI', sans-serif;">
              Política de Segurança e Proteção de Chaves de API do Arestrix
            </p>
          </div>
          <button id="btn-close-env-modal" class="modal-close-btn" title="Fechar">&times;</button>
        </div>

        <!-- Corpo do Diálogo -->
        <div class="modal-body" style="flex: 1; overflow-y: auto; padding: 16px; display: flex; flex-direction: column; gap: 12px;">
          <!-- Aviso de Segurança -->
          <div class="office-group" style="background: #ffffff; border-left: 3px solid #0f6cbd; margin-bottom: 0;">
            <strong style="display: block; font-size: 12.5px; color: #242424; margin-bottom: 4px;">
              Armazenamento Seguro de Segredos
            </strong>
            <p style="margin: 0; font-size: 12px; color: #616161; line-height: 1.45;">
              O Arestrix nunca armazena chaves de API em arquivos locais, bancos de dados ou código-fonte. Todas as credenciais são lidas exclusivamente das variáveis de ambiente do seu sistema operacional.
            </p>
          </div>

          <!-- Opção 1: Gravar Automaticamente pelo Arestrix no Windows -->
          <div class="office-group" style="margin-bottom: 0;">
            <span class="office-group-title">Opção 1: Gravar automaticamente no seu perfil do Windows</span>
            <p style="margin: 0 0 8px 0; font-size: 12px; color: #616161;">
              Registra a variável de ambiente permanentemente no escopo do seu usuário através do comando <code>setx</code>:
            </p>
            <div style="display: flex; gap: 8px;">
              <input type="password" id="input-env-key-val" class="office-input" placeholder="Cole o valor da chave ${g.keyName}..." style="flex: 1;" />
              <button id="btn-save-env-auto" class="tool-btn btn-action" style="white-space: nowrap; font-size: 12px; height: 32px; padding: 4px 14px;">
                Gravar no Windows
              </button>
            </div>
            <div id="env-save-status" style="margin-top: 6px; font-size: 11.5px; display: none;"></div>
          </div>

          <!-- Opção 2: Gravar Manualmente -->
          <div class="office-group" style="margin-bottom: 0;">
            <span class="office-group-title">Opção 2: Gravação manual</span>
            <div style="display: flex; flex-direction: column; gap: 8px;">
              <!-- Bloco PowerShell -->
              <div style="background: #ffffff; border: 1px solid #d1d1d1; border-radius: 2px; padding: 8px 10px;">
                <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 4px;">
                  <strong style="font-size: 11.5px; color: #323130;">PowerShell:</strong>
                  <button class="btn-copy-code tool-btn btn-secondary" data-code="${this.escapeHtml(g.powershellCmd)}" style="font-size: 11px; height: 24px; padding: 2px 8px;">
                    Copiar
                  </button>
                </div>
                <code style="display: block; background: #f3f2f1; color: #242424; padding: 6px 8px; border-radius: 2px; font-size: 11px; font-family: Consolas, monospace; overflow-x: auto;">
                  ${this.escapeHtml(g.powershellCmd)}
                </code>
              </div>

              <!-- Bloco Interface Gráfica -->
              <div style="background: #ffffff; border: 1px solid #d1d1d1; border-radius: 2px; padding: 8px 10px; font-size: 11.5px; color: #616161;">
                <strong style="display: block; color: #323130; margin-bottom: 4px;">Interface do Windows (Painel de Controle):</strong>
                <ol style="margin: 0; padding-left: 18px; line-height: 1.45;">
                  ${g.guiInstructions.map(step => `<li>${step}</li>`).join('')}
                </ol>
              </div>
            </div>
          </div>
        </div>

        <!-- Rodapé Estilo Office -->
        <div class="modal-footer">
          <span class="footer-info">Após gravar, verifique para prosseguir.</span>
          <div class="footer-actions">
            <button id="btn-env-cancel" class="tool-btn btn-secondary">Fechar</button>
            <button id="btn-env-recheck" class="tool-btn btn-action">Verificar Novamente</button>
          </div>
        </div>
      </div>
    `;

    this.bindEvents();
  }

  private bindEvents(): void {
    if (!this.modal || !this.currentGuidance) return;
    const g = this.currentGuidance;

    this.modal.querySelector('#btn-close-env-modal')?.addEventListener('click', () => this.hide());
    this.modal.querySelector('#btn-env-cancel')?.addEventListener('click', () => this.hide());

    // Botão Gravar Automaticamente
    this.modal.querySelector('#btn-save-env-auto')?.addEventListener('click', async () => {
      const input = this.modal?.querySelector('#input-env-key-val') as HTMLInputElement;
      const statusEl = this.modal?.querySelector('#env-save-status') as HTMLElement;
      if (!input || !statusEl) return;

      const val = input.value.trim();
      if (!val) {
        statusEl.style.display = 'block';
        statusEl.style.color = '#a4262c';
        statusEl.innerText = 'Insira o valor da chave antes de gravar.';
        return;
      }

      statusEl.style.display = 'block';
      statusEl.style.color = '#616161';
      statusEl.innerText = 'Gravando no registro do Windows...';

      const ok = await this.envManager.setEnvVar(g.keyName, val);
      if (ok) {
        statusEl.style.color = '#107c41';
        statusEl.innerText = `Variável ${g.keyName} gravada com sucesso.`;
        setTimeout(() => {
          this.hide();
          if (this.onSavedCallback) this.onSavedCallback();
        }, 1000);
      } else {
        statusEl.style.color = '#a4262c';
        statusEl.innerText = 'Não foi possível gravar automaticamente. Utilize o comando manual do PowerShell abaixo.';
      }
    });

    // Botões Copiar
    this.modal.querySelectorAll('.btn-copy-code').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const code = (e.currentTarget as HTMLElement).getAttribute('data-code');
        if (code && typeof navigator !== 'undefined' && navigator.clipboard) {
          navigator.clipboard.writeText(code);
          const orig = (e.currentTarget as HTMLElement).innerText;
          (e.currentTarget as HTMLElement).innerText = 'Copiado!';
          setTimeout(() => {
            (e.currentTarget as HTMLElement).innerText = orig;
          }, 1500);
        }
      });
    });

    // Botão Verificar Novamente
    this.modal.querySelector('#btn-env-recheck')?.addEventListener('click', async () => {
      const exists = await this.envManager.hasEnvVar(g.keyName);
      if (exists) {
        this.hide();
        if (this.onSavedCallback) this.onSavedCallback();
      } else {
        const statusEl = this.modal?.querySelector('#env-save-status') as HTMLElement;
        if (statusEl) {
          statusEl.style.display = 'block';
          statusEl.style.color = '#a4262c';
          statusEl.innerText = `A variável ${g.keyName} ainda não foi detectada no ambiente.`;
        }
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
