import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { ImportPiModal } from '../src/ui/modals/ImportPiModal.ts';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';

describe('ImportPiModal Tab Switching & Directory Picker', () => {
  let originalDocument: any;

  beforeEach(() => {
    originalDocument = (globalThis as any).document;

    const elementsById = new Map<string, any>();

    (globalThis as any).document = {
      createElement: (tag: string) => {
        const el: any = {
          tagName: tag.toUpperCase(),
          className: '',
          id: '',
          style: {},
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
          click() {
            (this._listeners['click'] || []).forEach(fn => fn({ preventDefault: () => {} }));
          },
          querySelector(selector: string) {
            const idMatch = selector.match(/^#([a-zA-Z0-9_-]+)$/);
            if (idMatch) {
              const targetId = idMatch[1];
              if (this._html.includes(`id="${targetId}"`)) {
                if (!elementsById.has(targetId)) {
                  elementsById.set(targetId, {
                    id: targetId,
                    value: targetId === 'pi-dir-input' ? 'C:/Projetos/TestDir' : '',
                    _listeners: {} as Record<string, Function[]>,
                    addEventListener(event: string, fn: Function) {
                      this._listeners[event] = this._listeners[event] || [];
                      this._listeners[event].push(fn);
                    },
                    click() {
                      (this._listeners['click'] || []).forEach(fn => fn({ preventDefault: () => {} }));
                    }
                  });
                }
                return elementsById.get(targetId);
              }
            }
            return null;
          },
          querySelectorAll: () => []
        };
        return el;
      },
      body: {
        appendChild: () => {}
      }
    };
  });

  afterEach(() => {
    (globalThis as any).document = originalDocument;
  });

  it('deve alternar entre as abas Projeto Completo e Diálogo Individual bidirecionalmente', async () => {
    const bus = new EventBus();
    const graph = new Graph(bus, { graphName: 'Test', projectId: 'test' });
    const modal = new ImportPiModal(graph);

    await modal.show();

    const modalEl = (modal as any).modal as HTMLElement;
    expect(modalEl).toBeDefined();

    // 1. Aba inicial deve ser 'project'
    expect((modal as any).activeTab).toBe('project');
    expect(modalEl.querySelector('#pi-dir-input')).not.toBeNull();

    // 2. Clica em Diálogo Individual
    const chatTabBtn = modalEl.querySelector('#pi-tab-chat') as any;
    expect(chatTabBtn).not.toBeNull();
    chatTabBtn.click();

    expect((modal as any).activeTab).toBe('chat');
    expect(modalEl.querySelector('#pi-conversation-input')).not.toBeNull();
    expect(modalEl.querySelector('#pi-dir-input')).toBeNull();

    // 3. Clica de volta em Projeto Completo
    const projectTabBtn = modalEl.querySelector('#pi-tab-project') as any;
    expect(projectTabBtn).not.toBeNull();
    projectTabBtn.click();

    expect((modal as any).activeTab).toBe('project');
    expect(modalEl.querySelector('#pi-dir-input')).not.toBeNull();
    expect(modalEl.querySelector('#pi-conversation-input')).toBeNull();
  });

  it('deve atualizar o diretório ao selecionar atalhos ou navegar pastas', async () => {
    const bus = new EventBus();
    const graph = new Graph(bus, { graphName: 'Test', projectId: 'test' });
    const modal = new ImportPiModal(graph);

    await modal.show();
    expect((modal as any).selectedDirPath).toBeDefined();

    // Rescan em novo caminho
    await modal.rescanPath('C:/Projetos/OutroProjeto');
    expect((modal as any).selectedDirPath).toBe('C:/Projetos/OutroProjeto');
  });
});
