import { describe, it, expect, beforeEach, beforeAll, afterAll } from 'vitest';
import { GitSyncManager } from '../src/core/git/GitSyncManager.ts';
import { ProjectManager } from '../src/core/ProjectManager.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { GitAccountModal } from '../src/ui/modals/GitAccountModal.ts';

// Mock simples de DOM para o ambiente Node do Vitest
class MockElement {
  tagName: string;
  className: string = '';
  id: string = '';
  type: string = '';
  title: string = '';
  style: Record<string, string> = {};
  children: MockElement[] = [];
  parentNode: MockElement | null = null;
  textContent: string = '';
  private _listeners: Record<string, Function[]> = {};

  constructor(tagName: string = 'DIV') {
    this.tagName = tagName.toUpperCase();
  }

  get classList() {
    return {
      add: (...cls: string[]) => {
        const set = new Set(this.className.split(/\s+/).filter(Boolean));
        cls.forEach(c => set.add(c));
        this.className = Array.from(set).join(' ');
      },
      remove: (...cls: string[]) => {
        const set = new Set(this.className.split(/\s+/).filter(Boolean));
        cls.forEach(c => set.delete(c));
        this.className = Array.from(set).join(' ');
      },
      contains: (c: string) => this.className.split(/\s+/).includes(c)
    };
  }

  appendChild(child: MockElement): MockElement {
    child.parentNode = this;
    this.children.push(child);
    return child;
  }

  removeChild(child: MockElement): MockElement {
    const idx = this.children.indexOf(child);
    if (idx !== -1) {
      this.children.splice(idx, 1);
      child.parentNode = null;
    }
    return child;
  }

  addEventListener(event: string, fn: Function) {
    this._listeners[event] = this._listeners[event] || [];
    this._listeners[event].push(fn);
  }

  querySelector(selector: string): MockElement | null {
    if (selector.startsWith('#')) {
      const id = selector.slice(1);
      if (this.id === id) return this;
      for (const child of this.children) {
        const found = child.querySelector(selector);
        if (found) return found;
      }
    }
    return null;
  }

  set innerHTML(_val: string) {}
}

describe('Git Auto-Save & Synchronization System', () => {
  let bus: EventBus;
  let gitManager: GitSyncManager;
  let projectManager: ProjectManager;
  let originalDocument: any;
  let originalWindow: any;
  let originalLocalStorage: any;
  let storage: Record<string, string> = {};

  beforeAll(() => {
    originalDocument = (globalThis as any).document;
    originalWindow = (globalThis as any).window;
    originalLocalStorage = (globalThis as any).localStorage;

    const mockBody = new MockElement('BODY');
    (globalThis as any).document = {
      createElement: (tag: string) => new MockElement(tag),
      body: mockBody,
      getElementById: (id: string) => mockBody.querySelector(`#${id}`)
    };

    (globalThis as any).window = {
      addEventListener: () => {},
      removeEventListener: () => {}
    };

    (globalThis as any).localStorage = {
      getItem: (k: string) => storage[k] ?? null,
      setItem: (k: string, v: string) => { storage[k] = v; },
      removeItem: (k: string) => { delete storage[k]; },
      clear: () => { storage = {}; }
    };
  });

  afterAll(() => {
    (globalThis as any).document = originalDocument;
    (globalThis as any).window = originalWindow;
    (globalThis as any).localStorage = originalLocalStorage;
  });

  beforeEach(() => {
    storage = {};
    bus = new EventBus();
    projectManager = ProjectManager.getInstance(bus);
    gitManager = GitSyncManager.getInstance(bus);
  });

  describe('Detecção de Serviços Comerciais & Avisos de Governança', () => {
    it('deve identificar provedores comerciais com precisão (GitHub, GitLab, Bitbucket)', () => {
      expect(gitManager.isCommercialService('github')).toBe(true);
      expect(gitManager.isCommercialService('https://github.com')).toBe(true);
      expect(gitManager.isCommercialService('https://github.com/usuario/projeto.git')).toBe(true);
      expect(gitManager.isCommercialService('https://gitlab.com')).toBe(true);
      expect(gitManager.isCommercialService('bitbucket.org')).toBe(true);
    });

    it('não deve classificar instâncias privadas ou self-hosted como comerciais', () => {
      expect(gitManager.isCommercialService('gitea.meuservidor.local')).toBe(false);
      expect(gitManager.isCommercialService('https://gitlab.meudominio.org.br')).toBe(false);
      expect(gitManager.isCommercialService('git://192.168.1.100/repo.git')).toBe(false);
    });

    it('deve gerenciar consentimento do aviso comercial via localStorage', () => {
      expect(gitManager.hasAcknowledgedCommercialWarning('gitlab_test')).toBe(false);
      gitManager.acknowledgeCommercialWarning('gitlab_test');
      expect(gitManager.hasAcknowledgedCommercialWarning('gitlab_test')).toBe(true);
    });
  });

  describe('Configuração do Manifesto do Projeto & Segurança de Dados', () => {
    it('deve inicializar novo projeto com modo 100% local por padrão (Git desabilitado / opt-in)', () => {
      const manifest = projectManager.createDefaultManifest();
      expect(manifest.git).toBeDefined();
      expect(manifest.git?.enabled).toBe(false);
      expect(manifest.git?.provider).toBe('github');
      expect(manifest.git?.isPrivate).toBe(true);
      expect(manifest.git?.autoCommitOnRun).toBe(true);
      expect(manifest.git?.autoCommitOnShutdown).toBe(true);
    });

    it('GARANTIA DE SEGURANÇA: nenhum campo de senha, token ou segredo deve existir no manifesto do projeto', () => {
      const manifest = projectManager.createDefaultManifest();
      const keys = Object.keys(manifest.git || {});
      expect(keys).not.toContain('token');
      expect(keys).not.toContain('password');
      expect(keys).not.toContain('pat');
      expect(keys).not.toContain('secret');
      expect(keys).not.toContain('apiKey');
    });

    it('deve persistir configurações Git no projeto ativo', async () => {
      projectManager.updateProjectMetadata({
        git: {
          enabled: true,
          provider: 'github',
          serverUrl: 'https://github.com',
          username: 'frankalcantara',
          repositoryName: 'projeto-teste',
          remoteUrl: 'https://github.com/frankalcantara/projeto-teste.git',
          isPrivate: true,
          autoCommitOnRun: true,
          autoCommitOnShutdown: true
        }
      });

      const current = projectManager.getCurrentProject();
      expect(current.git?.enabled).toBe(true);
      expect(current.git?.username).toBe('frankalcantara');
      expect(current.git?.repositoryName).toBe('projeto-teste');
      expect(current.git?.remoteUrl).toBe('https://github.com/frankalcantara/projeto-teste.git');
      expect(current.git?.isPrivate).toBe(true);
    });

    it('deve incluir suporte a push temporizado (10 min por padrão)', () => {
      const manifest = projectManager.createDefaultManifest();
      expect(manifest.git?.timedPushEnabled).toBe(true);
      expect(manifest.git?.timedPushIntervalMinutes).toBe(10);
    });
  });

  describe('Push Temporizado & Manual e Execução Local F5', () => {
    it('deve expor métodos de controle startTimedPush e stopTimedPush', () => {
      expect(typeof gitManager.startTimedPush).toBe('function');
      expect(typeof gitManager.stopTimedPush).toBe('function');
      gitManager.startTimedPush();
      gitManager.stopTimedPush();
    });

    it('triggerTimedPush deve retornar false se não houver alterações pendentes ou git desativado', async () => {
      const res = await gitManager.triggerTimedPush();
      expect(res).toBe(false);
    });

    it('triggerManualPush deve avisar quando Git estiver desativado (modo local)', async () => {
      projectManager.updateProjectMetadata({ git: { enabled: false } });
      const res = await gitManager.triggerManualPush();
      expect(res.ok).toBe(false);
      expect(res.error).toContain('Sincronização Git desativada para este projeto');
    });

    it('triggerManualPush deve tentar enviar quando Git estiver explicitamente habilitado', async () => {
      projectManager.updateProjectMetadata({ git: { enabled: true, provider: 'github' } });
      const res = await gitManager.triggerManualPush();
      // Em ambiente de teste sem WebUI backend nativo
      expect(res.ok).toBe(false);
      expect(res.error).toContain('Sem backend WebUI');
    });

    it('syncOnRun deve retornar true imediatamente no modo local sem tocar no Git', async () => {
      projectManager.updateProjectMetadata({ git: { enabled: false } });
      const res = await gitManager.syncOnRun();
      expect(res).toBe(true);
    });
  });

  describe('GitAccountModal (Interface & Interação)', () => {
    it('deve instanciar o modal de conta Git corretamente', () => {
      const modal = new GitAccountModal(bus);
      expect(modal).toBeDefined();
    });

    it('deve abrir e fechar o modal sem erros', async () => {
      const modal = new GitAccountModal(bus);
      await modal.open();
      expect(modal).toBeDefined();
      modal.hide();
    });
  });
});
