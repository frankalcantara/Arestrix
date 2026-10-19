import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { LocalModelsModal } from '../src/ui/modals/LocalModelsModal.ts';

describe('LocalModelsModal - catálogo e download de modelos locais', () => {
  let originalDocument: any;
  let originalWindow: any;

  beforeEach(() => {
    originalDocument = (globalThis as any).document;
    originalWindow = (globalThis as any).window;

    (globalThis as any).document = {
      body: { appendChild: (el: any) => el },
      createElement: (tag: string) => {
        const el: any = {
          tagName: tag.toUpperCase(),
          className: '',
          style: {},
          children: [] as any[],
          classList: {
            _items: new Set<string>(),
            add(...items: string[]) { items.forEach(i => this._items.add(i)); },
            remove(...items: string[]) { items.forEach(i => this._items.delete(i)); },
            contains(item: string) { return this._items.has(item); }
          },
          _html: '',
          set innerHTML(value: string) { this._html = value; },
          get innerHTML() { return this._html; },
          addEventListener: () => {},
          removeEventListener: () => {},
          appendChild(child: any) { this.children.push(child); return child; },
          setAttribute: () => {},
          querySelector: () => null,
          querySelectorAll: () => []
        };
        return el;
      }
    };
    (globalThis as any).window = {
      innerWidth: 1280,
      innerHeight: 800,
      addEventListener: () => {},
      removeEventListener: () => {}
    };
  });

  afterEach(() => {
    (globalThis as any).document = originalDocument;
    (globalThis as any).window = originalWindow;
    vi.restoreAllMocks();
  });

  it('consulta o estado do runtime e guarda o catálogo', async () => {
    const calls: string[] = [];
    (globalThis as any).window.webui = {
      call: async (fn: string) => {
        calls.push(fn);
        return JSON.stringify({
          dataRoot: 'C:/dados',
          modelsDir: 'C:/dados/models',
          manifest: { components: { 'llama.cpp': { version: 'b11669' } } },
          catalog: { models: [{ id: 'm1', name: 'Qwen2.5 7B Instruct' }] },
          models: [{ id: 'm1', name: 'Qwen2.5 7B Instruct', sizeBytes: 100, installed: false }],
          downloads: []
        });
      }
    };

    const modal = new LocalModelsModal();
    modal.show();
    await new Promise(resolve => setTimeout(resolve, 0));

    expect(calls).toContain('local_models_state');
    expect((modal as any).state.models).toHaveLength(1);
    expect((modal as any).state.manifest.components['llama.cpp'].version).toBe('b11669');
  });

  it('não quebra quando a bridge nativa não existe', async () => {
    const modal = new LocalModelsModal();
    modal.show();
    await new Promise(resolve => setTimeout(resolve, 0));
    expect((modal as any).overlay.classList.contains('hidden')).toBe(false);
  });
});
