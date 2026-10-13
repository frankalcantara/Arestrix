import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { GlobalDefaultsModal } from '../src/ui/GlobalDefaultsModal.ts';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { DEFAULT_PIPELINE_DEFAULTS } from '../src/core/types.ts';
import { EnvironmentManager } from '../src/core/env/EnvironmentManager.ts';

describe('GlobalDefaultsModal - Abas, Teste de API e Limpeza de Textos Fixos', () => {
  let graph: Graph;
  let bus: EventBus;
  let originalDocument: any;
  let originalWindow: any;

  beforeEach(() => {
    bus = new EventBus();
    graph = new Graph(bus, { graphName: 'DefaultsTestGraph' });

    originalDocument = (globalThis as any).document;
    originalWindow = (globalThis as any).window;

    const elementsById = new Map<string, any>();
    (globalThis as any).document = {
      body: {
        appendChild: (el: any) => el
      },
      createElement: (tag: string) => {
        const el: any = {
          tagName: tag.toUpperCase(),
          className: '',
          id: '',
          style: {},
          children: [] as any[],
          classList: {
            add: (cls: string) => { el.className = (el.className + ' ' + cls).trim(); },
            remove: (cls: string) => { el.className = el.className.split(' ').filter((c: string) => c !== cls).join(' '); },
            contains: (cls: string) => el.className.includes(cls)
          },
          _html: '',
          set innerHTML(val: string) {
            this._html = val;
            elementsById.clear();
          },
          get innerHTML() {
            return this._html;
          },
          _listeners: {} as Record<string, Function[]>,
          addEventListener(event: string, fn: Function) {
            this._listeners[event] = this._listeners[event] || [];
            this._listeners[event].push(fn);
          },
          querySelector: (sel: string) => null,
          querySelectorAll: (sel: string) => []
        };
        return el;
      }
    };
  });

  afterEach(() => {
    (globalThis as any).document = originalDocument;
    (globalThis as any).window = originalWindow;
    vi.restoreAllMocks();
  });

  describe('1. Padrões Globais Livres de Textos Fixos', () => {
    it('deve possuir valores padrão limpos e neutros em DEFAULT_PIPELINE_DEFAULTS', () => {
      expect(DEFAULT_PIPELINE_DEFAULTS.model.skillPath).toBe('');
      expect(DEFAULT_PIPELINE_DEFAULTS.model.promptTemplate).toBe('');
      expect(DEFAULT_PIPELINE_DEFAULTS.sandbox.readOnlyMounts).toEqual([]);
      expect(DEFAULT_PIPELINE_DEFAULTS.sandbox.readWriteMounts).toEqual([]);
      expect(DEFAULT_PIPELINE_DEFAULTS.codeMode.customScripts).toEqual([]);
    });

    it('o grafo deve iniciar com defaults sem referências a projetos externos', () => {
      const defaults = graph.getGlobalDefaults();
      expect(defaults.model.skillPath).toBe('');
      expect(defaults.model.promptTemplate).toBe('');
      expect(defaults.sandbox.readOnlyMounts).toEqual([]);
      expect(defaults.sandbox.readWriteMounts).toEqual([]);
    });
  });

  describe('2. Estrutura de Abas e Componentes da Interface', () => {
    it('deve renderizar abas padrão Office no modal (Modelo & API, VM & Sandbox, Ferramentas, Memória)', () => {
      // Simula DOM para renderização do modal
      const bodyElements: any[] = [];
      let renderedHtml = '';

      (globalThis as any).document = {
        body: {
          appendChild: (el: any) => { bodyElements.push(el); return el; }
        },
        createElement: (tag: string) => {
          const el: any = {
            tagName: tag.toUpperCase(),
            className: '',
            style: {},
            children: [],
            classList: {
              add: (cls: string) => { el.className = (el.className + ' ' + cls).trim(); },
              remove: (cls: string) => { el.className = el.className.split(' ').filter((c: string) => c !== cls).join(' '); },
              contains: (cls: string) => el.className.includes(cls)
            },
            set innerHTML(val: string) {
              renderedHtml = val;
            },
            get innerHTML() {
              return renderedHtml;
            },
            addEventListener: () => {},
            querySelector: (sel: string) => null,
            querySelectorAll: (sel: string) => []
          };
          return el;
        }
      };

      const modal = new GlobalDefaultsModal(graph);
      modal.show();

      // Verifica a presença do sistema de abas estilo Office
      expect(renderedHtml).toContain('class="modal-tabs"');
      expect(renderedHtml).toContain('data-tab="model"');
      expect(renderedHtml).toContain('data-tab="sandbox"');
      expect(renderedHtml).toContain('data-tab="tools"');
      expect(renderedHtml).toContain('data-tab="memory"');

      // Verifica que cada aba tem seu painel correspondente
      expect(renderedHtml).toContain('id="pane-tab-model"');
      expect(renderedHtml).toContain('id="pane-tab-sandbox"');
      expect(renderedHtml).toContain('id="pane-tab-tools"');
      expect(renderedHtml).toContain('id="pane-tab-memory"');

      // Verifica botões de teste e implantação de chave
      expect(renderedHtml).toContain('id="btn-test-agent-api"');
      expect(renderedHtml).toContain('id="btn-toggle-deploy-key"');
      expect(renderedHtml).toContain('id="btn-submit-deploy-key"');
    });
  });

  describe('3. Integração com EnvironmentManager e Chaves de API', () => {
    it('deve implantar e persistir uma nova chave de API no ambiente via EnvironmentManager', async () => {
      const envManager = EnvironmentManager.getInstance();
      const testKeyName = 'TEST_LLM_KEY_' + Date.now();
      const testKeyValue = 'sk-test-secret-12345';

      // Backend C++ simulado: a persistencia real (Registro / ~/.config) e responsabilidade do `set_env_var` nativo.
      const calls: Array<{ fn: string; arg: string }> = [];
      (globalThis as any).window = {
        webui: { call: async (fn: string, arg: string) => { calls.push({ fn, arg }); return fn === 'set_env_var'; } }
      };
      try {
        const success = await envManager.setEnvVar(testKeyName, testKeyValue);
        expect(success).toBe(true);
        expect(calls[0].fn).toBe('set_env_var');
        expect(JSON.parse(calls[0].arg)).toEqual({ key: testKeyName, value: testKeyValue });

        const retrieved = await envManager.getEnvVar(testKeyName);
        expect(retrieved).toBe(testKeyValue);

        const hasKey = await envManager.hasEnvVar(testKeyName);
        expect(hasKey).toBe(true);
      } finally {
        delete (globalThis as any).window;
      }
    });

    it('deve mapear corretamente os provedores para suas variáveis de ambiente padrão', () => {
      const modal = new GlobalDefaultsModal(graph) as any;
      expect(modal.getEnvKeyForProvider('deepseek')).toBe('DEEPSEEK_API_KEY');
      expect(modal.getEnvKeyForProvider('anthropic')).toBe('ANTHROPIC_API_KEY');
      expect(modal.getEnvKeyForProvider('google')).toBe('GEMINI_API_KEY');
      expect(modal.getEnvKeyForProvider('openai')).toBe('OPENAI_API_KEY');
    });
  });

  describe('4. Ciclo de Atualização e Persistência das Configurações', () => {
    it('deve atualizar os padrões globais e emitir evento ao salvar novos valores', () => {
      let eventFired = false;
      bus.on('graph:defaults-updated', () => {
        eventFired = true;
      });

      graph.updateGlobalDefaults({
        model: {
          provider: 'anthropic',
          model: 'claude-opus-5.5',
          skillPath: '.agents/custom/SKILL.md',
          promptTemplate: 'Instruções personalizadas'
        },
        sandbox: {
          isolationLevel: 'microvm',
          network: 'offline',
          maxMemoryMB: 1024,
          timeoutSeconds: 600,
          readOnlyMounts: ['.agents/'],
          readWriteMounts: ['output/']
        }
      });

      const updated = graph.getGlobalDefaults();
      expect(updated.model.provider).toBe('anthropic');
      expect(updated.model.model).toBe('claude-opus-5.5');
      expect(updated.model.skillPath).toBe('.agents/custom/SKILL.md');
      expect(updated.sandbox.isolationLevel).toBe('microvm');
      expect(eventFired).toBe(true);
    });
  });
});
