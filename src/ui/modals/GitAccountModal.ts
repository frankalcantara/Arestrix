import { BaseModal } from './BaseModal.ts';
import { GitSyncManager, IGitStatusResult } from '../../core/git/GitSyncManager.ts';
import { ProjectManager } from '../../core/ProjectManager.ts';
import { EventBus, globalEventBus } from '../../core/EventBus.ts';
import { IProjectGitConfig } from '../../core/types.ts';
import { t } from '../../i18n/index.ts';

/**
 * Arestrix Studio - Modal de Conta Git & Sincronização Automática
 * 
 * Permite configurar o repositório Git remoto privado por projeto.
 * Garante que NENHUM dado ou credencial do usuário seja salvo no Arestrix,
 * delegando toda a autenticação para o Sistema Operacional (Git Credential Manager).
 * Exibe aviso de governança obrigatório ao usar provedores comerciais (GitHub, etc.).
 */
export class GitAccountModal extends BaseModal {
  private gitManager: GitSyncManager;
  private projectManager: ProjectManager;
  private currentStatus: IGitStatusResult | null = null;
  private identityName: string = '';
  private identityEmail: string = '';
  private isProcessing: boolean = false;
  private feedbackMessage: string | null = null;
  private feedbackType: 'success' | 'error' | 'info' = 'info';

  constructor(bus: EventBus = globalEventBus) {
    super({
      id: 'git-account-modal',
      title: t('git_sync.modal_title', 'Conta Git & Repositório do Projeto'),
      subtitle: t('git_sync.modal_subtitle', 'Cada projeto é um repositório Git privado sincronizado a cada execução e ao encerrar'),
      icon: '🌿',
      minWidth: 720,
      minHeight: 580,
      dialogClass: 'git-account-modal-dialog'
    });

    this.gitManager = GitSyncManager.getInstance(bus);
    this.projectManager = ProjectManager.getInstance(bus);
  }

  public override async open(): Promise<void> {
    this.feedbackMessage = null;
    this.isProcessing = false;
    super.open();
    await this.loadData();
    this.render();
  }

  private async loadData(): Promise<void> {
    try {
      this.currentStatus = await this.gitManager.getStatus();
      const id = await this.gitManager.getSystemIdentity();
      this.identityName = id.userName || this.currentStatus.userName || '';
      this.identityEmail = id.userEmail || this.currentStatus.userEmail || '';
    } catch (e) {
      console.warn('[GitAccountModal] Falha ao carregar status do Git:', e);
    }
  }

  protected escapeHtml(str: string): string {
    return (str || '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }

  protected renderBody(): string {
    const proj = this.projectManager.getCurrentProject();
    const gitConfig = proj.git || {
      enabled: true,
      provider: 'github',
      serverUrl: 'https://github.com',
      username: this.identityName || (this.currentStatus?.gcmAccounts?.[0] || ''),
      repositoryName: proj.name || 'projeto-arestrix',
      remoteUrl: '',
      isPrivate: true,
      autoCommitOnRun: true,
      autoCommitOnShutdown: true
    };

    const isCommercial = this.gitManager.isCommercialService(gitConfig.serverUrl || gitConfig.provider);
    const hasAcknowledged = this.gitManager.hasAcknowledgedCommercialWarning(gitConfig.provider || 'github');

    return `
      <div style="padding: 16px 20px; font-family: Segoe UI, -apple-system, BlinkMacSystemFont, sans-serif; color: #1e293b;">
        <!-- CALLOUT DE SEGURANÇA E PRIVACIDADE -->
        <div style="margin-bottom: 16px; background: #f0fdf4; border: 1px solid #bbf7d0; border-radius: 6px; padding: 12px 16px; display: flex; align-items: flex-start; gap: 12px;">
          <span style="font-size: 20px; line-height: 1;">🛡️</span>
          <div style="font-size: 12px; color: #166534; line-height: 1.45;">
            <strong style="font-weight: 700;">${t('git_sync.security_rule_title', 'Segurança Absoluta de Credenciais:')}</strong>
            ${t('git_sync.security_rule_desc', 'Nenhum dado de senha ou token é registrado no Arestrix. O processo de autenticação é executado e mantido exclusivamente pelo cofre seguro do seu Sistema Operacional (Git Credential Manager / Windows Credential Locker).')}
          </div>
        </div>

        ${this.feedbackMessage ? `
          <div style="margin-bottom: 14px; padding: 10px 14px; border-radius: 6px; font-size: 12px; font-weight: 600; ${
            this.feedbackType === 'success'
              ? 'background: #ecfdf5; border: 1px solid #a7f3d0; color: #065f46;'
              : this.feedbackType === 'error'
              ? 'background: #fef2f2; border: 1px solid #fecaca; color: #991b1b;'
              : 'background: #f0f9ff; border: 1px solid #bae6fd; color: #0369a1;'
          }">
            <span>${this.escapeHtml(this.feedbackMessage)}</span>
          </div>
        ` : ''}

        <!-- 1. IDENTIDADE NO SISTEMA OPERACIONAL -->
        <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 6px; padding: 16px; margin-bottom: 16px;">
          <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px;">
            <h3 style="font-size: 13.5px; font-weight: 700; color: #0f172a; margin: 0; display: flex; align-items: center; gap: 8px;">
              <span>👤</span> ${t('git_sync.os_identity_title', 'Identidade no Sistema Operacional (Git Config)')}
            </h3>
            <span style="font-size: 11px; padding: 2px 8px; border-radius: 4px; font-weight: 700; background: ${this.currentStatus?.isIdentityConfigured ? '#e0f2fe; color: #0369a1;' : '#fef3c7; color: #92400e;'}">
              ${this.currentStatus?.isIdentityConfigured ? t('git_sync.identity_configured', '✓ Configurada no SO') : t('git_sync.identity_missing', '⚠️ Pendente de Configuração')}
            </span>
          </div>

          <div style="display: grid; grid-template-columns: 1fr 1fr auto; gap: 12px; align-items: flex-end;">
            <div>
              <label style="display: block; font-size: 11.5px; font-weight: 600; color: #475569; margin-bottom: 4px;">
                ${t('git_sync.user_name_label', 'Nome do Usuário (user.name):')}
              </label>
              <input type="text" id="git-input-user-name" class="office-input" value="${this.escapeHtml(this.identityName)}" placeholder="ex: Seu Nome" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #cbd5e1; border-radius: 4px; box-sizing: border-box;">
            </div>
            <div>
              <label style="display: block; font-size: 11.5px; font-weight: 600; color: #475569; margin-bottom: 4px;">
                ${t('git_sync.user_email_label', 'E-mail do Usuário (user.email):')}
              </label>
              <input type="email" id="git-input-user-email" class="office-input" value="${this.escapeHtml(this.identityEmail)}" placeholder="ex: seu.email@exemplo.com" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #cbd5e1; border-radius: 4px; box-sizing: border-box;">
            </div>
            <button id="btn-save-os-identity" class="tool-btn btn-secondary" style="height: 32px; padding: 0 14px; font-size: 11.5px; font-weight: 700; white-space: nowrap;">
              ${t('git_sync.btn_save_identity', 'Gravar no SO')}
            </button>
          </div>
        </div>

        <!-- 2. SERVIDOR GIT & AUTENTICAÇÃO NO SO -->
        <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 6px; padding: 16px; margin-bottom: 16px;">
          <h3 style="font-size: 13.5px; font-weight: 700; color: #0f172a; margin: 0 0 12px 0; display: flex; align-items: center; gap: 8px;">
            <span>🌐</span> ${t('git_sync.server_config_title', 'Servidor Git & Autenticação Segura')}
          </h3>

          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 14px; margin-bottom: 12px;">
            <div>
              <label style="display: block; font-size: 11.5px; font-weight: 600; color: #475569; margin-bottom: 4px;">
                ${t('git_sync.provider_label', 'Provedor Git:')}
              </label>
              <select id="git-select-provider" class="office-select" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #cbd5e1; border-radius: 4px; box-sizing: border-box; background: #fff;">
                <option value="github" ${gitConfig.provider === 'github' ? 'selected' : ''}>GitHub (Padrão - https://github.com)</option>
                <option value="gitlab" ${gitConfig.provider === 'gitlab' ? 'selected' : ''}>GitLab (https://gitlab.com)</option>
                <option value="gitea" ${gitConfig.provider === 'gitea' ? 'selected' : ''}>Gitea / Forgejo</option>
                <option value="custom" ${gitConfig.provider === 'custom' ? 'selected' : ''}>Servidor Personalizado</option>
              </select>
            </div>
            <div>
              <label style="display: block; font-size: 11.5px; font-weight: 600; color: #475569; margin-bottom: 4px;">
                ${t('git_sync.server_url_label', 'URL do Servidor:')}
              </label>
              <input type="text" id="git-input-server-url" class="office-input" value="${this.escapeHtml(gitConfig.serverUrl || 'https://github.com')}" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #cbd5e1; border-radius: 4px; box-sizing: border-box;">
            </div>
          </div>

          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 14px; align-items: flex-end; margin-bottom: 12px;">
            <div>
              <label style="display: block; font-size: 11.5px; font-weight: 600; color: #475569; margin-bottom: 4px;">
                ${t('git_sync.account_username_label', 'Conta / Usuário Remoto:')}
              </label>
              <input type="text" id="git-input-username" class="office-input" value="${this.escapeHtml(gitConfig.username || '')}" placeholder="ex: usuario" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #cbd5e1; border-radius: 4px; box-sizing: border-box;">
              <span style="display: block; font-size: 10.5px; color: #64748b; margin-top: 3px;">
                ${t('git_sync.account_username_tip', 'Usado apenas para compor a URL remota (ex: github.com/usuario/repo). Sem senhas.')}
              </span>
            </div>

            <div>
              <div style="margin-bottom: 6px; font-size: 11px; color: #475569;">
                <strong>${t('git_sync.gcm_accounts_found', 'Contas no Cofre do SO:')}</strong>
                ${this.currentStatus?.gcmAccounts?.length
                  ? this.currentStatus.gcmAccounts.map(a => `<span style="display: inline-block; background: #e2e8f0; padding: 1px 6px; border-radius: 3px; font-weight: 700; margin-left: 4px;">${this.escapeHtml(a)}</span>`).join('')
                  : `<span style="color: #64748b; font-style: italic;"> ${t('git_sync.no_gcm_accounts', 'Nenhuma')}</span>`
                }
              </div>
              <button id="btn-launch-os-login" class="tool-btn btn-action" style="width: 100%; height: 32px; font-size: 11.5px; font-weight: 700; background: #0f6cbd; color: #fff; border-radius: 4px; border: none; cursor: pointer; display: flex; align-items: center; justify-content: center; gap: 6px;">
                <span>🔑</span> ${t('git_sync.btn_launch_login', 'Efetuar Login Seguro no Sistema Operacional')}
              </button>
            </div>
          </div>
        </div>

        <!-- 3. CONFIGURAÇÕES DO REPOSITÓRIO PRIVADO DO PROJETO -->
        <div style="background: #ffffff; border: 1px solid #e2e8f0; border-radius: 6px; padding: 16px; margin-bottom: 16px;">
          <h3 style="font-size: 13.5px; font-weight: 700; color: #0f172a; margin: 0 0 12px 0; display: flex; align-items: center; gap: 8px;">
            <span>📦</span> ${t('git_sync.repo_settings_title', 'Repositório Privado deste Projeto')}
          </h3>

          <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 14px; margin-bottom: 12px;">
            <div>
              <label style="display: block; font-size: 11.5px; font-weight: 600; color: #475569; margin-bottom: 4px;">
                ${t('git_sync.repo_name_label', 'Nome do Repositório (Criado Automaticamente):')}
              </label>
              <input type="text" id="git-input-repo-name" class="office-input" value="${this.escapeHtml(gitConfig.repositoryName || proj.name)}" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #cbd5e1; border-radius: 4px; box-sizing: border-box;">
            </div>
            <div>
              <label style="display: block; font-size: 11.5px; font-weight: 600; color: #475569; margin-bottom: 4px;">
                ${t('git_sync.remote_url_label', 'URL Remota Git:')}
              </label>
              <input type="text" id="git-input-remote-url" class="office-input" value="${this.escapeHtml(gitConfig.remoteUrl || '')}" placeholder="ex: https://github.com/usuario/projeto.git" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #cbd5e1; border-radius: 4px; box-sizing: border-box;">
            </div>
          </div>

          <div style="display: flex; flex-direction: column; gap: 8px; margin-top: 10px;">
            <label style="display: flex; align-items: center; gap: 8px; font-size: 12px; cursor: pointer;">
              <input type="checkbox" id="git-chk-private" checked disabled style="cursor: not-allowed;">
              <strong style="font-weight: 700; color: #0f172a;">${t('git_sync.chk_private', 'Repositório Privado (Exigido pelo Arestrix por padrão)')}</strong>
            </label>
            <label style="display: flex; align-items: center; gap: 8px; font-size: 12px; cursor: pointer;">
              <input type="checkbox" id="git-chk-on-run" ${gitConfig.autoCommitOnRun ? 'checked' : ''}>
              <span>${t('git_sync.chk_auto_commit_run', 'Executar Commit & Push a cada execução do pipeline (F5)')}</span>
            </label>
            <label style="display: flex; align-items: center; gap: 8px; font-size: 12px; cursor: pointer;">
              <input type="checkbox" id="git-chk-on-shutdown" ${gitConfig.autoCommitOnShutdown ? 'checked' : ''}>
              <span>${t('git_sync.chk_auto_commit_shutdown', 'Executar Commit & Push ao encerrar o Arestrix')}</span>
            </label>
          </div>
        </div>

        <!-- 4. AVISO OBRIGATÓRIO DE SERVIÇO COMERCIAL / PRIVACIDADE -->
        ${isCommercial ? `
          <div id="git-commercial-warning-box" style="background: #fffbeb; border: 1.5px solid #f59e0b; border-radius: 6px; padding: 14px 16px; margin-bottom: 16px;">
            <div style="display: flex; align-items: center; gap: 8px; margin-bottom: 8px;">
              <span style="font-size: 18px; line-height: 1;">⚠️</span>
              <strong style="font-size: 13px; font-weight: 700; color: #b45309;">
                ${t('git_sync.commercial_warning_title', 'Aviso de Governança & Serviço Comercial de Terceiros')}
              </strong>
            </div>
            <p style="font-size: 11.5px; color: #78350f; line-height: 1.5; margin: 0 0 10px 0;">
              ${t('git_sync.commercial_warning_desc', 'Você configurou o salvamento de projetos em um serviço comercial (GitHub/GitLab). Os dados dos seus projetos (grafos, instruções de agentes, metadados e configurações) serão transmitidos e armazenados em servidores de terceiros, fora do controle e soberania do Arestrix. Embora os repositórios sejam criados como PRIVADOS, a custódia física dos dados está sob os termos de uso e políticas de privacidade da empresa provedora.')}
            </p>
            <label style="display: flex; align-items: center; gap: 8px; font-size: 11.5px; font-weight: 700; color: #92400e; cursor: pointer;">
              <input type="checkbox" id="git-chk-commercial-ack" ${hasAcknowledged ? 'checked' : ''}>
              <span>${t('git_sync.commercial_warning_ack', 'Estou ciente de que os dados do projeto residirão em um serviço comercial externo.')}</span>
            </label>
          </div>
        ` : ''}
      </div>
    `;
  }

  protected renderFooter(): string {
    return `
      <div style="display: flex; justify-content: flex-end; gap: 10px; width: 100%; padding: 10px 20px;">
        <button id="btn-git-cancel" class="tool-btn btn-secondary" style="height: 32px; padding: 0 16px; font-size: 12px; font-weight: 600;">
          ${t('common.cancel', 'Cancelar')}
        </button>
        <button id="btn-git-save-and-sync" class="tool-btn btn-action" style="height: 32px; padding: 0 18px; font-size: 12px; font-weight: 700; background: #0f6cbd; color: #fff; border: none; border-radius: 4px; cursor: pointer;">
          ${this.isProcessing ? t('common.loading', 'Processando...') : t('git_sync.btn_save_and_sync', 'Salvar & Sincronizar Agora')}
        </button>
      </div>
    `;
  }

  protected override bindEvents(): void {
    // 1. Salvar identidade no SO
    const btnSaveId = this.dialog.querySelector('#btn-save-os-identity') as HTMLButtonElement;
    btnSaveId?.addEventListener('click', async () => {
      const name = (this.dialog.querySelector('#git-input-user-name') as HTMLInputElement)?.value.trim();
      const email = (this.dialog.querySelector('#git-input-user-email') as HTMLInputElement)?.value.trim();
      btnSaveId.disabled = true;
      const res = await this.gitManager.setSystemIdentity(name, email);
      btnSaveId.disabled = false;
      if (res.ok) {
        this.feedbackMessage = t('git_sync.identity_saved_success', 'Identidade do Git gravada no Sistema Operacional com sucesso!');
        this.feedbackType = 'success';
      } else {
        this.feedbackMessage = res.error || t('git_sync.identity_saved_error', 'Falha ao gravar identidade no SO.');
        this.feedbackType = 'error';
      }
      this.render();
    });

    // 2. Disparar Login no SO (Git Credential Manager)
    const btnLogin = this.dialog.querySelector('#btn-launch-os-login') as HTMLButtonElement;
    btnLogin?.addEventListener('click', async () => {
      const provider = (this.dialog.querySelector('#git-select-provider') as HTMLSelectElement)?.value || 'github';
      btnLogin.disabled = true;
      this.feedbackMessage = t('git_sync.login_launching', 'Abrindo assistente de autenticação seguro no Sistema Operacional...');
      this.feedbackType = 'info';
      this.render();

      const res = await this.gitManager.launchGcmLogin(provider);
      btnLogin.disabled = false;
      if (res.ok) {
        this.feedbackMessage = t('git_sync.login_success', 'Autenticação no SO concluída com sucesso!');
        this.feedbackType = 'success';
      } else {
        this.feedbackMessage = res.error || t('git_sync.login_error', 'Processo de login encerrado ou pendente no SO.');
        this.feedbackType = 'error';
      }
      await this.loadData();
      this.render();
    });

    // 3. Atualizar provedor ou serverUrl dinamicamente para mostrar/esconder aviso comercial
    const selProvider = this.dialog.querySelector('#git-select-provider') as HTMLSelectElement;
    selProvider?.addEventListener('change', () => {
      const prov = selProvider.value;
      const inputServer = this.dialog.querySelector('#git-input-server-url') as HTMLInputElement;
      if (prov === 'github') inputServer.value = 'https://github.com';
      else if (prov === 'gitlab') inputServer.value = 'https://gitlab.com';
      this.render();
    });

    // 4. Rodapé - Cancelar e Salvar & Sincronizar
    const btnCancel = this.dialog.querySelector('#btn-git-cancel') as HTMLButtonElement;
    btnCancel?.addEventListener('click', () => this.hide());

    const btnSaveSync = this.dialog.querySelector('#btn-git-save-and-sync') as HTMLButtonElement;
    btnSaveSync?.addEventListener('click', async () => {
      await this.handleSaveAndSync();
    });
  }

  private async handleSaveAndSync(): Promise<void> {
    const provider = (this.dialog.querySelector('#git-select-provider') as HTMLSelectElement)?.value as any || 'github';
    const serverUrl = (this.dialog.querySelector('#git-input-server-url') as HTMLInputElement)?.value.trim() || 'https://github.com';
    const username = (this.dialog.querySelector('#git-input-username') as HTMLInputElement)?.value.trim() || '';
    const repoName = (this.dialog.querySelector('#git-input-repo-name') as HTMLInputElement)?.value.trim() || '';
    const remoteUrlInput = (this.dialog.querySelector('#git-input-remote-url') as HTMLInputElement)?.value.trim() || '';
    const autoRun = (this.dialog.querySelector('#git-chk-on-run') as HTMLInputElement)?.checked ?? true;
    const autoShutdown = (this.dialog.querySelector('#git-chk-on-shutdown') as HTMLInputElement)?.checked ?? true;

    // Se for comercial, exige reconhecimento do aviso
    const isCommercial = this.gitManager.isCommercialService(serverUrl || provider);
    if (isCommercial) {
      const chkAck = (this.dialog.querySelector('#git-chk-commercial-ack') as HTMLInputElement)?.checked;
      if (!chkAck) {
        this.feedbackMessage = t('git_sync.must_acknowledge_commercial', 'É necessário marcar o consentimento do aviso de serviço comercial para prosseguir.');
        this.feedbackType = 'error';
        this.render();
        return;
      }
      this.gitManager.acknowledgeCommercialWarning(provider);
    }

    this.isProcessing = true;
    this.feedbackMessage = t('git_sync.syncing_repo', 'Inicializando repositório local e vinculando remoto privado...');
    this.feedbackType = 'info';
    this.render();

    try {
      // 1. Cria ou valida repositório remoto privado
      let remoteUrl = remoteUrlInput;
      if (!remoteUrl) {
        const createRes = await this.gitManager.createRemoteRepo(repoName, true, username, serverUrl, provider);
        if (createRes.ok && createRes.remoteUrl) {
          remoteUrl = createRes.remoteUrl;
        }
      }

      // 2. Inicializa o repositório Git local e configura o remote
      const initRes = await this.gitManager.initProjectRepo(remoteUrl);
      if (!initRes.ok) {
        throw new Error(initRes.error || 'Falha ao inicializar repositório local.');
      }

      // 3. Atualiza manifesto do projeto ativo
      const gitConfig: IProjectGitConfig = {
        enabled: true,
        provider,
        serverUrl,
        username,
        repositoryName: repoName,
        remoteUrl,
        isPrivate: true,
        autoCommitOnRun: autoRun,
        autoCommitOnShutdown: autoShutdown,
        lastSyncAt: new Date().toISOString()
      };
      this.projectManager.updateProjectMetadata({ git: gitConfig });
      await this.projectManager.saveCurrentProject();

      // 4. Executa primeiro commit & push
      const pushRes = await this.gitManager.commitAndPush(t('git_sync.initial_commit_msg', 'Configuração inicial do projeto Arestrix'));

      this.isProcessing = false;
      if (pushRes.ok) {
        this.feedbackMessage = t('git_sync.sync_success', 'Repositório privado criado e sincronizado com sucesso no Git!');
        this.feedbackType = 'success';
        this.render();
        setTimeout(() => this.hide(), 1200);
      } else {
        this.feedbackMessage = pushRes.error || t('git_sync.push_warning', 'Repositório configurado, mas o push inicial retornou um aviso. Verifique as credenciais no SO.');
        this.feedbackType = 'error';
        this.render();
      }
    } catch (e: any) {
      this.isProcessing = false;
      this.feedbackMessage = e?.message || String(e);
      this.feedbackType = 'error';
      this.render();
    }
  }
}
