import { EnvironmentManager, IEnvKeyGuidance } from '../../core/env/EnvironmentManager.ts';
import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';
import { t } from '../../i18n/index.ts';

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
  private windowController?: IModalWindowController;
  private activeTab: 'auto' | 'manual' = 'auto';

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
    this.windowController?.bringToFront();
  }

  public hide(): void {
    if (this.modal) {
      this.modal.style.display = 'none';
    }
  }

  private render(): void {
    if (!this.modal || !this.currentGuidance) return;
    const g = this.currentGuidance;

    const titleBarHtml = ModalWindowManager.createTitleBarHTML({
      title: t('env_guidance.title', { key: g.keyName }),
      subtitle: t('env_guidance.subtitle'),
      icon: '🛡️',
      closeBtnId: 'btn-close-env-modal'
    });

    this.modal.innerHTML = `
      <div class="modal-window" style="max-width: 660px; width: 92%; max-height: 88vh; display: flex; flex-direction: column;">
        ${titleBarHtml}

        <!-- Abas Estilo Office 365 -->
        <div class="modal-tabs" role="tablist">
          <button type="button" class="tab-btn ${this.activeTab === 'auto' ? 'active' : ''}" data-tab="auto" role="tab" aria-selected="${this.activeTab === 'auto'}">
            ${t('env_guidance.tab_auto', '⚡ Gravar Automaticamente')}
          </button>
          <button type="button" class="tab-btn ${this.activeTab === 'manual' ? 'active' : ''}" data-tab="manual" role="tab" aria-selected="${this.activeTab === 'manual'}">
            ${t('env_guidance.tab_manual', '📖 Instruções Manuais')}
          </button>
        </div>

        <!-- Corpo do Diálogo -->
        <div class="modal-body" style="flex: 1; overflow-y: auto; padding: 16px; display: flex; flex-direction: column; gap: 12px;">
          <!-- Aviso de Segurança Comum -->
          <div class="office-group" style="background: #ffffff; border-left: 3px solid #0f6cbd; margin-bottom: 0;">
            <strong style="display: block; font-size: 12.5px; color: #242424; margin-bottom: 4px;">
              ${t('env_guidance.security_title')}
            </strong>
            <p style="margin: 0; font-size: 12px; color: #616161; line-height: 1.45;">
              ${t('env_guidance.security_desc')}
            </p>
          </div>

          <!-- Aba 1: Gravar Automaticamente pelo Arestrix no Windows -->
          <div id="pane-tab-auto" class="tab-pane" style="${this.activeTab === 'auto' ? '' : 'display: none;'}">
            <div class="office-group" style="margin-bottom: 0;">
              <span class="office-group-title">${t('env_guidance.option1_title')}</span>
              <p style="margin: 0 0 8px 0; font-size: 12px; color: #616161;">
                ${t('env_guidance.option1_desc')}
              </p>
              <div style="display: flex; gap: 8px;">
                <input type="password" id="input-env-key-val" class="office-input" placeholder="${t('env_guidance.placeholder_key', { key: g.keyName })}" style="flex: 1;" />
                <button id="btn-save-env-auto" class="tool-btn btn-action btn-primary-office" style="white-space: nowrap; font-size: 12px; height: 32px; padding: 4px 14px;">
                  ${t('env_guidance.btn_save_windows')}
                </button>
              </div>
              <div id="env-save-status" style="margin-top: 6px; font-size: 11.5px; display: none;"></div>
            </div>
          </div>

          <!-- Aba 2: Gravar Manualmente -->
          <div id="pane-tab-manual" class="tab-pane" style="${this.activeTab === 'manual' ? '' : 'display: none;'}">
            <div class="office-group" style="margin-bottom: 0;">
              <span class="office-group-title">${t('env_guidance.option2_title')}</span>
              <div style="display: flex; flex-direction: column; gap: 8px;">
                <!-- Bloco PowerShell -->
                <div style="background: #ffffff; border: 1px solid #d1d1d1; border-radius: 2px; padding: 8px 10px;">
                  <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 4px;">
                    <strong style="font-size: 11.5px; color: #323130;">${t('env_guidance.label_powershell')}</strong>
                    <button class="btn-copy-code tool-btn btn-secondary btn-secondary-office" data-code="${this.escapeHtml(g.powershellCmd)}" style="font-size: 11px; height: 24px; padding: 2px 8px;">
                      ${t('env_guidance.btn_copy')}
                    </button>
                  </div>
                  <code style="display: block; background: #f3f2f1; color: #242424; padding: 6px 8px; border-radius: 2px; font-size: 11px; font-family: Consolas, monospace; overflow-x: auto;">
                    ${this.escapeHtml(g.powershellCmd)}
                  </code>
                </div>

                <!-- Bloco Interface Gráfica -->
                <div style="background: #ffffff; border: 1px solid #d1d1d1; border-radius: 2px; padding: 8px 10px; font-size: 11.5px; color: #616161;">
                  <strong style="display: block; color: #323130; margin-bottom: 4px;">${t('env_guidance.label_gui')}</strong>
                  <ol style="margin: 0; padding-left: 18px; line-height: 1.45;">
                    ${g.guiInstructions.map(step => `<li>${step}</li>`).join('')}
                  </ol>
                </div>
              </div>
            </div>
          </div>
        </div>

        <!-- Rodapé Estilo Office -->
        <div class="modal-footer office-modal-footer">
          <span class="footer-info">${t('env_guidance.footer_info')}</span>
          <div class="footer-actions">
            <button id="btn-env-cancel" class="tool-btn btn-secondary btn-secondary-office">${t('env_guidance.btn_close')}</button>
            <button id="btn-env-recheck" class="tool-btn btn-action btn-primary-office">${t('env_guidance.btn_recheck')}</button>
          </div>
        </div>
      </div>
    `;

    const dialog = this.modal.querySelector?.('.modal-window') as HTMLElement;
    if (dialog) {
      this.windowController = ModalWindowManager.attach({
        dialog,
        minWidth: 540,
        minHeight: 400,
        onClose: () => this.hide()
      });
    }

    this.bindEvents();
  }

  private bindEvents(): void {
    if (!this.modal || !this.currentGuidance) return;
    const g = this.currentGuidance;

    // Alternância de abas
    this.modal.querySelectorAll('.modal-tabs .tab-btn').forEach(btn => {
      btn.addEventListener('click', () => {
        const tab = (btn as HTMLElement).getAttribute('data-tab') as 'auto' | 'manual';
        if (!tab) return;
        this.activeTab = tab;
        this.modal?.querySelectorAll('.modal-tabs .tab-btn').forEach(b => {
          const isCurrent = b.getAttribute('data-tab') === tab;
          b.classList.toggle('active', isCurrent);
          b.setAttribute('aria-selected', isCurrent ? 'true' : 'false');
        });
        const pAuto = this.modal?.querySelector('#pane-tab-auto') as HTMLElement;
        const pManual = this.modal?.querySelector('#pane-tab-manual') as HTMLElement;
        if (pAuto) pAuto.style.display = tab === 'auto' ? '' : 'none';
        if (pManual) pManual.style.display = tab === 'manual' ? '' : 'none';
      });
    });

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
        statusEl.innerText = t('env_guidance.msg_fill_val');
        return;
      }

      statusEl.style.display = 'block';
      statusEl.style.color = '#616161';
      statusEl.innerText = t('env_guidance.msg_saving');

      const ok = await this.envManager.setEnvVar(g.keyName, val);
      if (ok) {
        statusEl.style.color = '#107c41';
        statusEl.innerText = t('env_guidance.msg_saved_success', { key: g.keyName });
        setTimeout(() => {
          this.hide();
          if (this.onSavedCallback) this.onSavedCallback();
        }, 1000);
      } else {
        statusEl.style.color = '#a4262c';
        statusEl.innerText = t('env_guidance.msg_save_fail');
      }
    });

    // Botões Copiar
    this.modal.querySelectorAll('.btn-copy-code').forEach(btn => {
      btn.addEventListener('click', (e) => {
        const code = (e.currentTarget as HTMLElement).getAttribute('data-code');
        if (code && typeof navigator !== 'undefined' && navigator.clipboard) {
          navigator.clipboard.writeText(code);
          const orig = (e.currentTarget as HTMLElement).innerText;
          (e.currentTarget as HTMLElement).innerText = t('env_guidance.btn_copied');
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
          statusEl.innerText = t('env_guidance.msg_not_detected', { key: g.keyName });
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
