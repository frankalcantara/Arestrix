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
  private provider: 'github' | 'gitlab' | 'gitea' | 'custom' = 'github';
  private serverUrl: string = 'https://github.com';
  private username: string = '';
  private repositoryName: string = '';
  private remoteUrl: string = '';
  private autoCommitOnRun: boolean = true;
  private autoCommitOnShutdown: boolean = true;
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
      minHeight: 560,
      dialogClass: 'git-account-modal-dialog',
      tabs: [
        { id: 'identity', label: t('git_sync.tab_identity', '1. Conta & Identidade SO'), icon: '👤' },
        { id: 'repository', label: t('git_sync.tab_repository', '2. Repositório Remoto'), icon: '📦' },
        { id: 'automation', label: t('git_sync.tab_automation', '3. Automação & Governança'), icon: '🛡️' }
      ],
      defaultTabId: 'identity'
    });

    this.gitManager = GitSyncManager.getInstance(bus);
    this.projectManager = ProjectManager.getInstance(bus);
  }

  public override async open(): Promise<void> {
    this.feedbackMessage = null;
    this.isProcessing = false;
    const proj = this.projectManager.getCurrentProject();
    const gitConfig = proj.git || {
      enabled: true,
      provider: 'github',
      serverUrl: 'https://github.com',
      username: '',
      repositoryName: proj.name || 'projeto-arestrix',
      remoteUrl: '',
      isPrivate: true,
      autoCommitOnRun: true,
      autoCommitOnShutdown: true
    };

    this.provider = (gitConfig.provider as any) || 'github';
    this.serverUrl = gitConfig.serverUrl || 'https://github.com';
    this.username = gitConfig.username || '';
    this.repositoryName = gitConfig.repositoryName || proj.name || 'projeto-arestrix';
    this.remoteUrl = gitConfig.remoteUrl || '';
    this.autoCommitOnRun = gitConfig.autoCommitOnRun ?? true;
    this.autoCommitOnShutdown = gitConfig.autoCommitOnShutdown ?? true;

    super.open();
    await this.loadData();
    if (!this.username && (this.identityName || this.currentStatus?.gcmAccounts?.[0])) {
      this.username = this.identityName || (this.currentStatus?.gcmAccounts?.[0] || '');
    }
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

  protected override saveTabState(): void {
    if (!this.dialog) return;

    const inputName = this.dialog.querySelector('#git-input-user-name') as HTMLInputElement | null;
    if (inputName) this.identityName = inputName.value.trim();

    const inputEmail = this.dialog.querySelector('#git-input-user-email') as HTMLInputElement | null;
    if (inputEmail) this.identityEmail = inputEmail.value.trim();

    const selectProv = this.dialog.querySelector('#git-select-provider') as HTMLSelectElement | null;
    if (selectProv) this.provider = selectProv.value as any;

    const inputServer = this.dialog.querySelector('#git-input-server-url') as HTMLInputElement | null;
    if (inputServer) this.serverUrl = inputServer.value.trim();

    const inputUser = this.dialog.querySelector('#git-input-username') as HTMLInputElement | null;
    if (inputUser) this.username = inputUser.value.trim();

    const inputRepo = this.dialog.querySelector('#git-input-repo-name') as HTMLInputElement | null;
    if (inputRepo) this.repositoryName = inputRepo.value.trim();

    const inputRemote = this.dialog.querySelector('#git-input-remote-url') as HTMLInputElement | null;
    if (inputRemote) this.remoteUrl = inputRemote.value.trim();

    const chkRun = this.dialog.querySelector('#git-chk-on-run') as HTMLInputElement | null;
    if (chkRun) this.autoCommitOnRun = chkRun.checked;

    const chkShutdown = this.dialog.querySelector('#git-chk-on-shutdown') as HTMLInputElement | null;
    if (chkShutdown) this.autoCommitOnShutdown = chkShutdown.checked;
  }

  protected override renderTabContent(tabId: string): string {
    const feedbackHtml = this.feedbackMessage ? `
      <div style="margin-bottom: 14px; padding: 10px 14px; border-radius: 4px; font-size: 12px; font-weight: 600; ${
        this.feedbackType === 'success'
          ? 'background: #ecfdf5; border: 1px solid #a7f3d0; color: #065f46;'
          : this.feedbackType === 'error'
          ? 'background: #fef2f2; border: 1px solid #fecaca; color: #991b1b;'
          : 'background: #f0f9ff; border: 1px solid #bae6fd; color: #0369a1;'
      }">
        <span>${this.escapeHtml(this.feedbackMessage)}</span>
      </div>
    ` : '';

    if (tabId === 'identity') {
      return `
        <div style="padding: 16px 20px; font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, sans-serif; color: #323130;">
          <!-- CALLOUT DE SEGURANÇA E PRIVACIDADE -->
          <div style="margin-bottom: 16px; background: #f0fdf4; border: 1px solid #bbf7d0; border-radius: 4px; padding: 12px 16px; display: flex; align-items: flex-start; gap: 12px;">
            <span style="font-size: 20px; line-height: 1;">🛡️</span>
            <div style="font-size: 12px; color: #166534; line-height: 1.45;">
              <strong style="font-weight: 700;">${t('git_sync.security_rule_title', 'Segurança Absoluta de Credenciais:')}</strong>
              ${t('git_sync.security_rule_desc', 'Nenhum dado de senha ou token é registrado no Arestrix. O processo de autenticação é executado e mantido exclusivamente pelo cofre seguro do seu Sistema Operacional (Git Credential Manager / Windows Credential Locker).')}
            </div>
          </div>

          ${feedbackHtml}

          <!-- IDENTIDADE NO SISTEMA OPERACIONAL -->
          <div style="background: #ffffff; border: 1px solid #edebe9; border-radius: 4px; padding: 16px; margin-bottom: 16px;">
            <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 12px;">
              <h3 style="font-size: 13px; font-weight: 700; color: #201f1e; margin: 0; display: flex; align-items: center; gap: 8px;">
                <span>👤</span> ${t('git_sync.os_identity_title', 'Identidade no Sistema Operacional (Git Config)')}
              </h3>
              <span style="font-size: 11px; padding: 2px 8px; border-radius: 4px; font-weight: 700; background: ${this.currentStatus?.isIdentityConfigured ? '#e0f2fe; color: #0369a1;' : '#fef3c7; color: #92400e;'}">
                ${this.currentStatus?.isIdentityConfigured ? t('git_sync.identity_configured', '✓ Configurada no SO') : t('git_sync.identity_missing', '⚠️ Pendente de Configuração')}
              </span>
            </div>

            <div style="display: grid; grid-template-columns: 1fr 1fr auto; gap: 12px; align-items: flex-end;">
              <div>
                <label style="display: block; font-size: 11.5px; font-weight: 600; color: #605e5c; margin-bottom: 4px;">
                  ${t('git_sync.user_name_label', 'Nome do Usuário (user.name):')}
                </label>
                <input type="text" id="git-input-user-name" class="office-input" value="${this.escapeHtml(this.identityName)}" placeholder="ex: Seu Nome" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #d2d0ce; border-radius: 4px; box-sizing: border-box;">
              </div>
              <div>
                <label style="display: block; font-size: 11.5px; font-weight: 600; color: #605e5c; margin-bottom: 4px;">
                  ${t('git_sync.user_email_label', 'E-mail do Usuário (user.email):')}
                </label>
                <input type="email" id="git-input-user-email" class="office-input" value="${this.escapeHtml(this.identityEmail)}" placeholder="ex: seu.email@exemplo.com" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #d2d0ce; border-radius: 4px; box-sizing: border-box;">
              </div>
              <button id="btn-save-os-identity" class="tool-btn btn-secondary" style="height: 32px; padding: 0 14px; font-size: 11.5px; font-weight: 600; white-space: nowrap;">
                ${t('git_sync.btn_save_identity', 'Gravar no SO')}
              </button>
            </div>
          </div>

          <!-- COFRE DO SO (GCM) -->
          <div style="background: #ffffff; border: 1px solid #edebe9; border-radius: 4px; padding: 16px;">
            <h3 style="font-size: 13px; font-weight: 700; color: #201f1e; margin: 0 0 12px 0; display: flex; align-items: center; gap: 8px;">
              <span>🔑</span> ${t('git_sync.gcm_title', 'Autenticação & Cofre de Senhas do SO')}
            </h3>
            <div style="display: flex; justify-content: space-between; align-items: center; gap: 14px;">
              <div>
                <div style="font-size: 11.5px; color: #605e5c; margin-bottom: 4px;">
                  <strong>${t('git_sync.gcm_accounts_found', 'Contas detectadas no Cofre do SO:')}</strong>
                  ${this.currentStatus?.gcmAccounts?.length
                    ? this.currentStatus.gcmAccounts.map(a => `<span style="display: inline-block; background: #edebe9; color: #201f1e; padding: 1px 6px; border-radius: 3px; font-weight: 700; margin-left: 4px;">${this.escapeHtml(a)}</span>`).join('')
                    : `<span style="color: #a19f9d; font-style: italic;"> ${t('git_sync.no_gcm_accounts', 'Nenhuma conta registrada')}</span>`
                  }
                </div>
                <div style="font-size: 11px; color: #a19f9d;">
                  ${t('git_sync.gcm_desc', 'O Git Credential Manager gerencia os tokens e senhas com segurança nativa de hardware/SO.')}
                </div>
              </div>
              <button id="btn-launch-os-login" class="tool-btn btn-action" style="height: 32px; padding: 0 16px; font-size: 11.5px; font-weight: 600; background: #0f6cbd; color: #fff; border-radius: 4px; border: none; cursor: pointer; display: flex; align-items: center; gap: 6px; white-space: nowrap;">
                <span>🔑</span> ${t('git_sync.btn_launch_login', 'Efetuar Login Seguro no SO')}
              </button>
            </div>
          </div>
        </div>
      `;
    }

    if (tabId === 'repository') {
      return `
        <div style="padding: 16px 20px; font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, sans-serif; color: #323130;">
          ${feedbackHtml}

          <div style="background: #ffffff; border: 1px solid #edebe9; border-radius: 4px; padding: 16px; margin-bottom: 16px;">
            <h3 style="font-size: 13px; font-weight: 700; color: #201f1e; margin: 0 0 14px 0; display: flex; align-items: center; gap: 8px;">
              <span>🌐</span> ${t('git_sync.server_config_title', 'Servidor Git Remoto')}
            </h3>

            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 14px; margin-bottom: 14px;">
              <div>
                <label style="display: block; font-size: 11.5px; font-weight: 600; color: #605e5c; margin-bottom: 4px;">
                  ${t('git_sync.provider_label', 'Provedor Git:')}
                </label>
                <select id="git-select-provider" class="office-select" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #d2d0ce; border-radius: 4px; box-sizing: border-box; background: #fff;">
                  <option value="github" ${this.provider === 'github' ? 'selected' : ''}>GitHub (https://github.com)</option>
                  <option value="gitlab" ${this.provider === 'gitlab' ? 'selected' : ''}>GitLab (https://gitlab.com)</option>
                  <option value="gitea" ${this.provider === 'gitea' ? 'selected' : ''}>Gitea / Forgejo</option>
                  <option value="custom" ${this.provider === 'custom' ? 'selected' : ''}>Servidor Personalizado</option>
                </select>
              </div>
              <div>
                <label style="display: block; font-size: 11.5px; font-weight: 600; color: #605e5c; margin-bottom: 4px;">
                  ${t('git_sync.server_url_label', 'URL do Servidor:')}
                </label>
                <input type="text" id="git-input-server-url" class="office-input" value="${this.escapeHtml(this.serverUrl)}" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #d2d0ce; border-radius: 4px; box-sizing: border-box;">
              </div>
            </div>

            <div style="display: grid; grid-template-columns: 1fr 1fr; gap: 14px;">
              <div>
                <label style="display: block; font-size: 11.5px; font-weight: 600; color: #605e5c; margin-bottom: 4px;">
                  ${t('git_sync.account_username_label', 'Conta / Usuário Remoto:')}
                </label>
                <input type="text" id="git-input-username" class="office-input" value="${this.escapeHtml(this.username)}" placeholder="ex: usuario" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #d2d0ce; border-radius: 4px; box-sizing: border-box;">
                <span style="display: block; font-size: 10.5px; color: #a19f9d; margin-top: 3px;">
                  ${t('git_sync.account_username_tip', 'Usado apenas para compor a URL remota (ex: github.com/usuario/repo). Sem senhas.')}
                </span>
              </div>
              <div>
                <label style="display: block; font-size: 11.5px; font-weight: 600; color: #605e5c; margin-bottom: 4px;">
                  ${t('git_sync.repo_name_label', 'Nome do Repositório (Privado):')}
                </label>
                <input type="text" id="git-input-repo-name" class="office-input" value="${this.escapeHtml(this.repositoryName)}" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #d2d0ce; border-radius: 4px; box-sizing: border-box;">
              </div>
            </div>
          </div>

          <div style="background: #ffffff; border: 1px solid #edebe9; border-radius: 4px; padding: 16px;">
            <label style="display: block; font-size: 11.5px; font-weight: 600; color: #605e5c; margin-bottom: 4px;">
              ${t('git_sync.remote_url_label', 'URL Remota Git (Opcional - sobrescreve a geração automática):')}
            </label>
            <input type="text" id="git-input-remote-url" class="office-input" value="${this.escapeHtml(this.remoteUrl)}" placeholder="ex: https://github.com/usuario/projeto.git" style="width: 100%; height: 32px; padding: 4px 8px; font-size: 12px; border: 1px solid #d2d0ce; border-radius: 4px; box-sizing: border-box;">
            <span style="display: block; font-size: 10.5px; color: #a19f9d; margin-top: 3px;">
              ${t('git_sync.remote_url_hint', 'Deixe em branco para conectar automaticamente ao repositório baseado no usuário e nome informados.')}
            </span>
          </div>
        </div>
      `;
    }

    if (tabId === 'automation') {
      const isCommercial = this.gitManager.isCommercialService(this.serverUrl || this.provider);
      const hasAcknowledged = this.gitManager.hasAcknowledgedCommercialWarning(this.provider || 'github');

      return `
        <div style="padding: 16px 20px; font-family: 'Segoe UI', -apple-system, BlinkMacSystemFont, sans-serif; color: #323130;">
          ${feedbackHtml}

          <!-- POLÍTICAS DE SINCRONIZAÇÃO AUTOMÁTICA -->
          <div style="background: #ffffff; border: 1px solid #edebe9; border-radius: 4px; padding: 16px; margin-bottom: 16px;">
            <h3 style="font-size: 13px; font-weight: 700; color: #201f1e; margin: 0 0 12px 0; display: flex; align-items: center; gap: 8px;">
              <span>⚡</span> ${t('git_sync.automation_settings_title', 'Políticas de Sincronização Automática')}
            </h3>

            <div style="display: flex; flex-direction: column; gap: 10px;">
              <label style="display: flex; align-items: center; gap: 8px; font-size: 12px; cursor: pointer;">
                <input type="checkbox" id="git-chk-private" checked disabled style="cursor: not-allowed;">
                <strong style="font-weight: 700; color: #201f1e;">${t('git_sync.chk_private', 'Repositório Privado (Exigido pelo Arestrix por padrão)')}</strong>
              </label>
              <label style="display: flex; align-items: center; gap: 8px; font-size: 12px; cursor: pointer;">
                <input type="checkbox" id="git-chk-on-run" ${this.autoCommitOnRun ? 'checked' : ''}>
                <span>${t('git_sync.chk_auto_commit_run', 'Executar Commit & Push a cada execução do pipeline (F5)')}</span>
              </label>
              <label style="display: flex; align-items: center; gap: 8px; font-size: 12px; cursor: pointer;">
                <input type="checkbox" id="git-chk-on-shutdown" ${this.autoCommitOnShutdown ? 'checked' : ''}>
                <span>${t('git_sync.chk_auto_commit_shutdown', 'Executar Commit & Push ao encerrar o Arestrix')}</span>
              </label>
            </div>
          </div>

          <!-- AVISO OBRIGATÓRIO DE SERVIÇO COMERCIAL -->
          ${isCommercial ? `
            <div id="git-commercial-warning-box" style="background: #fffbeb; border: 1px solid #f59e0b; border-radius: 4px; padding: 14px 16px;">
              <div style="display: flex; align-items: center; gap: 8px; margin-bottom: 8px;">
                <span style="font-size: 18px; line-height: 1;">⚠️</span>
                <strong style="font-size: 13px; font-weight: 700; color: #b45309;">
                  ${t('git_sync.commercial_warning_title', 'Aviso de Governança & Serviço Comercial de Terceiros')}
                </strong>
              </div>
              <p style="font-size: 11.5px; color: #78350f; line-height: 1.5; margin: 0 0 10px 0;">
                ${t('git_sync.commercial_warning_desc', 'Você configurou o salvamento de projetos em um serviço comercial (GitHub/GitLab). Os dados dos seus projetos serão armazenados em servidores de terceiros sob seus termos de uso. Embora os repositórios sejam criados como PRIVADOS, a custódia física dos dados está sob as políticas de privacidade da empresa provedora.')}
              </p>
              <label style="display: flex; align-items: center; gap: 8px; font-size: 11.5px; font-weight: 700; color: #92400e; cursor: pointer;">
                <input type="checkbox" id="git-chk-commercial-ack" ${hasAcknowledged ? 'checked' : ''}>
                <span>${t('git_sync.commercial_warning_ack', 'Estou ciente de que os dados do projeto residirão em um serviço comercial externo.')}</span>
              </label>
            </div>
          ` : `
            <div style="background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 4px; padding: 14px 16px; font-size: 11.5px; color: #64748b;">
              <span>✓ Servidor sob controle próprio ou sob infraestrutura corporativa privada. Nenhum aviso comercial aplicável.</span>
            </div>
          `}
        </div>
      `;
    }

    return `<div style="padding: 20px;">Aba desconhecida</div>`;
  }

  protected override renderFooter(): string {
    return this.renderStandardFooter({
      primaryLabel: this.isProcessing ? t('common.loading', 'Processando...') : t('git_sync.btn_save_and_sync', 'Salvar & Sincronizar Agora'),
      primaryId: 'btn-git-save-and-sync',
      secondaryLabel: t('common.cancel', 'Cancelar'),
      secondaryId: 'btn-git-cancel',
      extraHtml: `
        <div style="font-size: 11px; color: #605e5c; display: flex; align-items: center; gap: 6px;">
          <span>🔒</span>
          <span>${t('git_sync.zero_secrets_label', 'Sem senhas ou credenciais no Arestrix')}</span>
        </div>
      `
    });
  }

  protected override bindEvents(): void {
    if (!this.dialog) return;

    // 1. Salvar identidade no SO
    const btnSaveId = this.dialog.querySelector('#btn-save-os-identity') as HTMLButtonElement | null;
    btnSaveId?.addEventListener('click', async () => {
      const name = (this.dialog.querySelector('#git-input-user-name') as HTMLInputElement | null)?.value.trim() || '';
      const email = (this.dialog.querySelector('#git-input-user-email') as HTMLInputElement | null)?.value.trim() || '';
      if (btnSaveId) btnSaveId.disabled = true;
      const res = await this.gitManager.setSystemIdentity(name, email);
      if (btnSaveId) btnSaveId.disabled = false;
      if (res.ok) {
        this.identityName = name;
        this.identityEmail = email;
        this.feedbackMessage = t('git_sync.identity_saved_success', 'Identidade do Git gravada no Sistema Operacional com sucesso!');
        this.feedbackType = 'success';
      } else {
        this.feedbackMessage = res.error || t('git_sync.identity_saved_error', 'Falha ao gravar identidade no SO.');
        this.feedbackType = 'error';
      }
      this.render();
    });

    // 2. Disparar Login no SO (Git Credential Manager)
    const btnLogin = this.dialog.querySelector('#btn-launch-os-login') as HTMLButtonElement | null;
    btnLogin?.addEventListener('click', async () => {
      const provider = this.provider || 'github';
      if (btnLogin) btnLogin.disabled = true;
      this.feedbackMessage = t('git_sync.login_launching', 'Abrindo assistente de autenticação seguro no Sistema Operacional...');
      this.feedbackType = 'info';
      this.render();

      const res = await this.gitManager.launchGcmLogin(provider);
      if (btnLogin) btnLogin.disabled = false;
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

    // 3. Atualizar provedor ou serverUrl dinamicamente para sincronizar campos
    const selProvider = this.dialog.querySelector('#git-select-provider') as HTMLSelectElement | null;
    selProvider?.addEventListener('change', () => {
      const prov = selProvider.value as any;
      this.provider = prov;
      if (prov === 'github') this.serverUrl = 'https://github.com';
      else if (prov === 'gitlab') this.serverUrl = 'https://gitlab.com';
      this.render();
    });

    // 4. Rodapé - Cancelar e Salvar & Sincronizar
    const btnCancel = this.dialog.querySelector('#btn-git-cancel') as HTMLButtonElement | null;
    btnCancel?.addEventListener('click', () => this.hide());

    const btnSaveSync = this.dialog.querySelector('#btn-git-save-and-sync') as HTMLButtonElement | null;
    btnSaveSync?.addEventListener('click', async () => {
      await this.handleSaveAndSync();
    });
  }

  private async handleSaveAndSync(): Promise<void> {
    this.saveTabState();

    // Se for comercial, exige reconhecimento do aviso
    const isCommercial = this.gitManager.isCommercialService(this.serverUrl || this.provider);
    if (isCommercial) {
      const chkAck = (this.dialog.querySelector('#git-chk-commercial-ack') as HTMLInputElement | null)?.checked;
      const hasAck = this.gitManager.hasAcknowledgedCommercialWarning(this.provider);
      if (!chkAck && !hasAck) {
        this.feedbackMessage = t('git_sync.must_acknowledge_commercial', 'É necessário marcar o consentimento do aviso de serviço comercial na aba de Governança para prosseguir.');
        this.feedbackType = 'error';
        this.switchTab('automation');
        return;
      }
      if (chkAck) {
        this.gitManager.acknowledgeCommercialWarning(this.provider);
      }
    }

    this.isProcessing = true;
    this.feedbackMessage = t('git_sync.syncing_repo', 'Inicializando repositório local e vinculando remoto privado...');
    this.feedbackType = 'info';
    this.render();

    try {
      // 1. Cria ou valida repositório remoto privado
      let remoteUrl = this.remoteUrl;
      if (!remoteUrl) {
        const createRes = await this.gitManager.createRemoteRepo(this.repositoryName, true, this.username, this.serverUrl, this.provider);
        if (createRes.ok && createRes.remoteUrl) {
          remoteUrl = createRes.remoteUrl;
          this.remoteUrl = remoteUrl;
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
        provider: this.provider,
        serverUrl: this.serverUrl,
        username: this.username,
        repositoryName: this.repositoryName,
        remoteUrl,
        isPrivate: true,
        autoCommitOnRun: this.autoCommitOnRun,
        autoCommitOnShutdown: this.autoCommitOnShutdown,
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
