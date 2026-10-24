import { ProjectManager } from '../ProjectManager.ts';
import { globalEventBus, EventBus } from '../EventBus.ts';
import { IProjectGitConfig } from '../types.ts';

declare const window: any;

export interface IGitStatusResult {
  gitInstalled: boolean;
  gitVersion?: string;
  userName?: string;
  userEmail?: string;
  isIdentityConfigured: boolean;
  gcmAccounts: string[];
  isRepo: boolean;
  currentBranch?: string;
  remoteUrl?: string;
  hasUncommittedChanges: boolean;
  hasUnpushedCommits?: boolean;
  unpushedCommitsCount?: number;
  lastCommitHash?: string;
  lastCommitMessage?: string;
  error?: string;
}

export class GitSyncManager {
  private static instance: GitSyncManager;
  private projectManager: ProjectManager;
  private bus: EventBus;
  private isSyncing = false;
  private lastStatus: IGitStatusResult | null = null;
  private timedPushTimer: any = null;

  private constructor(bus: EventBus = globalEventBus) {
    this.bus = bus;
    this.projectManager = ProjectManager.getInstance(bus);

    // Escuta eventos de encerramento da janela no navegador/WebUI
    if (typeof window !== 'undefined') {
      window.addEventListener('beforeunload', () => {
        void this.syncOnShutdown();
      });
    }

    // Atualiza status e inicia push temporizado quando projeto é carregado
    this.bus.on('project:loaded', () => {
      void this.refreshStatus();
      this.startTimedPush();
    });

    this.startTimedPush();
  }

  public static getInstance(bus: EventBus = globalEventBus): GitSyncManager {
    if (!GitSyncManager.instance) {
      GitSyncManager.instance = new GitSyncManager(bus);
    }
    return GitSyncManager.instance;
  }

  /**
   * Verifica se o backend WebUI está disponível
   */
  private hasWebUI(): boolean {
    return typeof window !== 'undefined' && window.webui && typeof window.webui.call === 'function';
  }

  /**
   * Identifica se um servidor ou provedor é um serviço comercial de terceiros
   */
  public isCommercialService(serverOrProvider: string): boolean {
    const s = (serverOrProvider || '').toLowerCase();
    return s === 'github' || s.includes('github.com') ||
           s === 'gitlab.com' || s.includes('gitlab.com') ||
           s === 'bitbucket' || s.includes('bitbucket.org');
  }

  /**
   * Verifica se o usuário já consentiu com o aviso de serviço comercial
   */
  public hasAcknowledgedCommercialWarning(serverOrProvider: string): boolean {
    if (typeof localStorage === 'undefined') return false;
    try {
      const key = `arestrix_git_commercial_ack_${serverOrProvider.toLowerCase()}`;
      return localStorage.getItem(key) === 'true';
    } catch (_) {
      return false;
    }
  }

  /**
   * Grava o consentimento do aviso de serviço comercial
   */
  public acknowledgeCommercialWarning(serverOrProvider: string): void {
    if (typeof localStorage === 'undefined') return;
    try {
      const key = `arestrix_git_commercial_ack_${serverOrProvider.toLowerCase()}`;
      localStorage.setItem(key, 'true');
    } catch (_) {}
  }

  /**
   * Obtém o status do Git para o projeto ativo
   */
  public async getStatus(rootPath?: string): Promise<IGitStatusResult> {
    const proj = this.projectManager.getCurrentProject();
    const targetRoot = rootPath || proj.rootPath;

    if (!this.hasWebUI()) {
      return {
        gitInstalled: false,
        isIdentityConfigured: false,
        gcmAccounts: [],
        isRepo: false,
        hasUncommittedChanges: false,
        hasUnpushedCommits: false,
        unpushedCommitsCount: 0,
        error: 'Backend nativo WebUI indisponível no navegador puro.'
      };
    }

    try {
      const raw = await window.webui.call('git_get_status', JSON.stringify({ rootPath: targetRoot }));
      if (raw) {
        const parsed = JSON.parse(raw);
        this.lastStatus = parsed;
        this.bus.emit('git:status-changed', parsed);
        return parsed;
      }
    } catch (e: any) {
      console.warn('[GitSyncManager] Falha ao consultar git_get_status:', e);
    }

    return {
      gitInstalled: false,
      isIdentityConfigured: false,
      gcmAccounts: [],
      isRepo: false,
      hasUncommittedChanges: false,
      hasUnpushedCommits: false,
      unpushedCommitsCount: 0
    };
  }

  public async refreshStatus(): Promise<IGitStatusResult> {
    return await this.getStatus();
  }

  public getLastStatus(): IGitStatusResult | null {
    return this.lastStatus;
  }

  /**
   * Consulta a identidade do Git configurada no Sistema Operacional
   */
  public async getSystemIdentity(): Promise<{ configured: boolean; userName: string; userEmail: string }> {
    if (!this.hasWebUI()) {
      return { configured: false, userName: '', userEmail: '' };
    }
    try {
      const raw = await window.webui.call('git_get_system_identity', '');
      if (raw) return JSON.parse(raw);
    } catch (e) {
      console.warn('[GitSyncManager] Falha ao obter identidade do SO:', e);
    }
    return { configured: false, userName: '', userEmail: '' };
  }

  /**
   * Configura user.name e user.email globalmente no SO
   */
  public async setSystemIdentity(name: string, email: string): Promise<{ ok: boolean; error?: string }> {
    if (!this.hasWebUI()) {
      return { ok: false, error: 'Sem backend WebUI.' };
    }
    try {
      const raw = await window.webui.call('git_set_system_identity', JSON.stringify({ name, email }));
      if (raw) {
        const res = JSON.parse(raw);
        await this.refreshStatus();
        return res;
      }
    } catch (e: any) {
      return { ok: false, error: e?.message || String(e) };
    }
    return { ok: false, error: 'Resposta vazia do backend.' };
  }

  /**
   * Lista contas detectadas no Git Credential Manager do SO
   */
  public async listGcmAccounts(provider: string = 'github'): Promise<string[]> {
    if (!this.hasWebUI()) return [];
    try {
      const raw = await window.webui.call('git_list_accounts', provider);
      if (raw) return JSON.parse(raw);
    } catch (e) {
      console.warn('[GitSyncManager] Falha ao listar contas GCM:', e);
    }
    return [];
  }

  /**
   * Dispara o assistente oficial de login no SO (Git Credential Manager)
   */
  public async launchGcmLogin(provider: string = 'github'): Promise<{ ok: boolean; error?: string }> {
    if (!this.hasWebUI()) {
      return { ok: false, error: 'Sem backend WebUI.' };
    }
    try {
      const raw = await window.webui.call('git_launch_login', provider);
      if (raw) {
        const res = JSON.parse(raw);
        await this.refreshStatus();
        return res;
      }
    } catch (e: any) {
      return { ok: false, error: e?.message || String(e) };
    }
    return { ok: false, error: 'Falha ao acionar login no SO.' };
  }

  /**
   * Inicializa o repositório Git local e configura o remote
   */
  public async initProjectRepo(remoteUrl?: string): Promise<{ ok: boolean; error?: string }> {
    if (!this.hasWebUI()) return { ok: false, error: 'Sem backend WebUI.' };
    const proj = this.projectManager.getCurrentProject();
    try {
      const raw = await window.webui.call('git_init_project', JSON.stringify({
        rootPath: proj.rootPath,
        remoteUrl: remoteUrl || proj.git?.remoteUrl || ''
      }));
      if (raw) {
        const res = JSON.parse(raw);
        await this.refreshStatus();
        return res;
      }
    } catch (e: any) {
      return { ok: false, error: e?.message || String(e) };
    }
    return { ok: false, error: 'Resposta vazia.' };
  }

  /**
   * Cria o repositório remoto privado e o vincula ao projeto de forma automática e transparente
   */
  public async createRemoteRepo(
    repoName: string,
    isPrivate: boolean = true,
    username: string = '',
    serverUrl: string = 'https://github.com',
    provider: 'github' | 'gitlab' | 'gitea' | 'custom' = 'github'
  ): Promise<{ ok: boolean; remoteUrl: string; error?: string }> {
    if (!this.hasWebUI()) return { ok: false, remoteUrl: '', error: 'Sem backend WebUI.' };
    const proj = this.projectManager.getCurrentProject();

    try {
      const payload = {
        provider,
        repoName: repoName || proj.name,
        isPrivate: isPrivate !== false, // privado por padrão
        username,
        serverUrl,
        rootPath: proj.rootPath
      };

      const raw = await window.webui.call('git_create_remote_repo', JSON.stringify(payload));
      if (raw) {
        const res = JSON.parse(raw);
        if (res.ok && res.remoteUrl) {
          // Atualiza as configurações Git no manifesto do projeto ativo
          const gitConfig: IProjectGitConfig = {
            enabled: true,
            provider,
            serverUrl,
            username,
            repositoryName: repoName || proj.name,
            remoteUrl: res.remoteUrl,
            isPrivate: true,
            autoCommitOnRun: proj.git?.autoCommitOnRun ?? true,
            autoCommitOnShutdown: proj.git?.autoCommitOnShutdown ?? true,
            lastSyncAt: new Date().toISOString()
          };
          this.projectManager.updateProjectMetadata({ git: gitConfig });
          await this.projectManager.saveCurrentProject();
        }
        await this.refreshStatus();
        return res;
      }
    } catch (e: any) {
      return { ok: false, remoteUrl: '', error: e?.message || String(e) };
    }
    return { ok: false, remoteUrl: '', error: 'Falha na chamada de criação de repositório remoto.' };
  }

  /**
   * Executa apenas commit local (sem push para o remote).
   * Utilizado a cada execução (F5) para salvar snapshot sem onerar a rede ou cotas do GitHub.
   */
  public async commitLocal(message?: string): Promise<{ ok: boolean; output: string; error?: string }> {
    if (!this.hasWebUI()) return { ok: false, output: '', error: 'Sem backend WebUI.' };
    const proj = this.projectManager.getCurrentProject();

    try {
      const payload = {
        rootPath: proj.rootPath,
        message: message || `Auto-commit on run: ${new Date().toISOString()}`
      };
      const raw = await window.webui.call('git_commit_local', JSON.stringify(payload));
      if (raw) {
        const res = JSON.parse(raw);
        if (res.ok) {
          this.bus.emit('git:committed', { timestamp: new Date().toISOString() });
        }
        await this.refreshStatus();
        return res;
      }
    } catch (e: any) {
      return { ok: false, output: '', error: e?.message || String(e) };
    }

    return { ok: false, output: '', error: 'Falha desconhecida no commit local.' };
  }

  /**
   * Executa envio (push) para o repositório remoto.
   */
  public async pushRemote(branch: string = 'main'): Promise<{ ok: boolean; output: string; error?: string }> {
    if (!this.hasWebUI()) return { ok: false, output: '', error: 'Sem backend WebUI.' };
    const proj = this.projectManager.getCurrentProject();

    this.isSyncing = true;
    this.bus.emit('git:syncing', { type: 'push' });

    try {
      const payload = {
        rootPath: proj.rootPath,
        branch
      };
      const raw = await window.webui.call('git_push_remote', JSON.stringify(payload));
      this.isSyncing = false;

      if (raw) {
        const res = JSON.parse(raw);
        if (res.ok) {
          if (proj.git) {
            proj.git.lastSyncAt = new Date().toISOString();
            this.projectManager.updateProjectMetadata({ git: proj.git });
          }
          this.bus.emit('git:synced', { timestamp: new Date().toISOString() });
        } else {
          this.bus.emit('git:error', { error: res.error });
        }
        await this.refreshStatus();
        return res;
      }
    } catch (e: any) {
      this.isSyncing = false;
      this.bus.emit('git:error', { error: e?.message || String(e) });
      return { ok: false, output: '', error: e?.message || String(e) };
    }

    this.isSyncing = false;
    return { ok: false, output: '', error: 'Falha desconhecida no push remoto.' };
  }

  /**
   * Executa commit e push para o repositório remoto
   */
  public async commitAndPush(message?: string, branch: string = 'main'): Promise<{ ok: boolean; output: string; error?: string }> {
    if (!this.hasWebUI()) return { ok: false, output: '', error: 'Sem backend WebUI.' };
    const proj = this.projectManager.getCurrentProject();

    this.isSyncing = true;
    this.bus.emit('git:syncing', { message });

    try {
      const payload = {
        rootPath: proj.rootPath,
        message: message || `Arestrix Auto-Save: ${new Date().toISOString()}`,
        branch
      };
      const raw = await window.webui.call('git_commit_push', JSON.stringify(payload));
      this.isSyncing = false;

      if (raw) {
        const res = JSON.parse(raw);
        if (res.ok) {
          if (proj.git) {
            proj.git.lastSyncAt = new Date().toISOString();
            this.projectManager.updateProjectMetadata({ git: proj.git });
          }
          this.bus.emit('git:synced', { timestamp: new Date().toISOString() });
        } else {
          this.bus.emit('git:error', { error: res.error });
        }
        await this.refreshStatus();
        return res;
      }
    } catch (e: any) {
      this.isSyncing = false;
      this.bus.emit('git:error', { error: e?.message || String(e) });
      return { ok: false, output: '', error: e?.message || String(e) };
    }

    this.isSyncing = false;
    return { ok: false, output: '', error: 'Falha desconhecida no commit & push.' };
  }

  /**
   * Gatilho disparado a cada execução do pipeline (F5).
   * Realiza apenas commit local para salvar o histórico sem gerar tráfego de rede nem estourar cotas remotas.
   */
  public async syncOnRun(): Promise<boolean> {
    const proj = this.projectManager.getCurrentProject();
    if (!proj.git || !proj.git.enabled || !proj.git.autoCommitOnRun) {
      return true; // Git sync desligado neste projeto
    }

    try {
      const time = new Date().toISOString();
      const msg = `Auto-commit on run: ${time}`;
      const res = await this.commitLocal(msg);
      if (!res.ok) {
        console.warn('[GitSyncManager] Aviso no commit local de execução:', res.error);
      }
      return res.ok;
    } catch (e) {
      console.warn('[GitSyncManager] Exceção durante auto-commit on run:', e);
      return false;
    }
  }

  /**
   * Inicia o timer de push temporizado se habilitado no projeto ativo.
   * Padrão: a cada 10 minutos (600.000 ms), se houver qualquer alteração local ou unpushed commits.
   */
  public startTimedPush(): void {
    this.stopTimedPush();

    const proj = this.projectManager.getCurrentProject();
    if (proj.git && proj.git.enabled === false) return;
    if (proj.git && proj.git.timedPushEnabled === false) return;

    const intervalMinutes = Math.max(1, proj.git?.timedPushIntervalMinutes ?? 10);
    const intervalMs = intervalMinutes * 60 * 1000;

    this.timedPushTimer = setInterval(() => {
      void this.triggerTimedPush();
    }, intervalMs);
  }

  /**
   * Para o timer de push temporizado.
   */
  public stopTimedPush(): void {
    if (this.timedPushTimer) {
      clearInterval(this.timedPushTimer);
      this.timedPushTimer = null;
    }
  }

  /**
   * Executa o push temporizado caso existam alterações locais ou commits pendentes de envio.
   */
  public async triggerTimedPush(): Promise<boolean> {
    if (this.isSyncing) return false;

    const proj = this.projectManager.getCurrentProject();
    if (proj.git && proj.git.enabled === false) return false;
    if (proj.git && proj.git.timedPushEnabled === false) return false;

    const st = await this.getStatus();
    if (!st.isRepo) return false;

    const hasChanges = st.hasUncommittedChanges || !!st.hasUnpushedCommits || (st.unpushedCommitsCount ?? 0) > 0;
    if (!hasChanges) {
      return false;
    }

    console.log('[GitSyncManager] Push temporizado: alterações detectadas. Enviando para remote...');
    const time = new Date().toISOString();
    const res = await this.commitAndPush(`Arestrix Timed Auto-Save: ${time}`);
    return res.ok;
  }

  /**
   * Acionamento manual de sincronização (Commit & Push) via botão da interface.
   */
  public async triggerManualPush(): Promise<{ ok: boolean; output: string; error?: string }> {
    if (this.isSyncing) {
      return { ok: false, output: '', error: 'Sincronização já em andamento.' };
    }
    const time = new Date().toISOString();
    return await this.commitAndPush(`Manual Sync: ${time}`);
  }

  /**
   * Gatilho disparado quando o sistema é encerrado (shutdown)
   */
  public async syncOnShutdown(): Promise<boolean> {
    const proj = this.projectManager.getCurrentProject();
    if (!proj.git || !proj.git.enabled || !proj.git.autoCommitOnShutdown) {
      return true;
    }

    try {
      const time = new Date().toISOString();
      const msg = `Auto-commit on shutdown: ${time}`;
      await this.commitAndPush(msg);
      return true;
    } catch (e) {
      console.warn('[GitSyncManager] Exceção durante auto-sync on shutdown:', e);
      return false;
    }
  }

  public getIsSyncing(): boolean {
    return this.isSyncing;
  }
}
