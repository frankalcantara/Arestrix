import { describe, it, expect, beforeEach, afterEach, beforeAll, afterAll } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { CodeModal } from '../src/ui/CodeModal.ts';

// =========================================================================
// Mocks DOM para execução determinística e isolada no ambiente Node (Vitest)
// =========================================================================

class MockElement {
  tagName: string;
  className: string = '';
  id: string = '';
  type: string = '';
  title: string = '';
  checked: boolean = false;
  textContent: string = '';
  style: Record<string, string> = {};
  children: MockElement[] = [];
  parentNode: MockElement | null = null;
  attributes: Record<string, string> = {};
  private _html: string = '';
  private _listeners: Record<string, Function[]> = {};

  get classList() {
    return {
      add: (...classes: string[]) => {
        const set = new Set(this.className.split(/\s+/).filter(Boolean));
        classes.forEach(c => set.add(c));
        this.className = Array.from(set).join(' ');
      },
      remove: (...classes: string[]) => {
        const set = new Set(this.className.split(/\s+/).filter(Boolean));
        classes.forEach(c => set.delete(c));
        this.className = Array.from(set).join(' ');
      },
      contains: (cls: string) => {
        return this.className.split(/\s+/).includes(cls);
      },
      toggle: (cls: string, force?: boolean) => {
        const has = this.className.split(/\s+/).includes(cls);
        const shouldAdd = force !== undefined ? force : !has;
        if (shouldAdd) {
          this.classList.add(cls);
        } else {
          this.classList.remove(cls);
        }
        return shouldAdd;
      }
    };
  }

  constructor(tagName: string = 'DIV') {
    this.tagName = tagName.toUpperCase();
  }

  get innerHTML(): string {
    if (this.children.length === 0) return this._html || this.textContent;
    return this.children.map(c => {
      const cls = c.className ? ` class="${c.className}"` : '';
      const id = c.id ? ` id="${c.id}"` : '';
      return `<${c.tagName.toLowerCase()}${id}${cls}>${c.innerHTML || c.textContent}</${c.tagName.toLowerCase()}>`;
    }).join('');
  }

  set innerHTML(html: string) {
    this._html = html;
    this.children = [];
    parseHTMLToMock(html, this);
  }

  appendChild(child: MockElement) {
    child.parentNode = this;
    this.children.push(child);
    return child;
  }

  removeChild(child: MockElement) {
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

  click() {
    (this._listeners['click'] || []).forEach(fn => fn({ currentTarget: this, target: this, preventDefault: () => {}, stopPropagation: () => {} }));
  }

  querySelector(selector: string): MockElement | null {
    for (const child of this.children) {
      if (child.matches(selector)) return child;
      const found = child.querySelector(selector);
      if (found) return found;
    }
    return null;
  }

  querySelectorAll(selector: string): MockElement[] {
    const list: MockElement[] = [];
    for (const child of this.children) {
      if (child.matches(selector)) list.push(child);
      list.push(...child.querySelectorAll(selector));
    }
    return list;
  }

  getAttribute(attr: string): string | null {
    return this.attributes[attr] || null;
  }

  setAttribute(attr: string, val: string) {
    this.attributes[attr] = val;
  }

  matches(sel: string): boolean {
    if (sel.includes(',')) {
      return sel.split(',').map(s => s.trim()).some(part => this.matches(part));
    }
    if (sel.startsWith('#')) return this.id === sel.slice(1);
    if (sel.startsWith('.')) return this.className.split(/\s+/).includes(sel.slice(1));
    if (sel.startsWith('[') && sel.endsWith(']')) {
      const match = sel.slice(1, -1).match(/^([a-zA-Z0-9\-_]+)(?:="([^"]+)")?$/);
      if (match) {
        const [, attr, val] = match;
        return val !== undefined ? this.attributes[attr] === val : attr in this.attributes;
      }
    }
    return this.tagName.toLowerCase() === sel.toLowerCase();
  }

  closest(sel: string): MockElement | null {
    let curr: MockElement | null = this;
    while (curr) {
      if (curr.matches(sel)) return curr;
      curr = curr.parentNode;
    }
    return null;
  }
}

function parseHTMLToMock(html: string, root: MockElement) {
  const tokenRegex = /<(\/)?([a-zA-Z0-9\-]+)([^>]*?)>|([^<]+)/g;
  const stack: MockElement[] = [root];
  let match: RegExpExecArray | null;

  while ((match = tokenRegex.exec(html)) !== null) {
    const [, isClosing, tagName, attrs, text] = match;
    if (text) {
      const trimmed = text.trim();
      if (trimmed && stack.length > 0) {
        const top = stack[stack.length - 1];
        top.textContent = top.textContent ? `${top.textContent} ${trimmed}` : trimmed;
      }
      continue;
    }

    if (isClosing) {
      if (stack.length > 1) stack.pop();
    } else {
      const el = new MockElement(tagName);
      if (attrs) {
        const attrRegex = /([a-zA-Z0-9\-]+)(?:="([^"]*?)")?/g;
        let attrMatch;
        while ((attrMatch = attrRegex.exec(attrs)) !== null) {
          const [, name, val] = attrMatch;
          const v = val !== undefined ? val : '';
          el.setAttribute(name, v);
          if (name === 'id') el.id = v;
          if (name === 'class') el.className = v;
        }
      }
      const parent = stack[stack.length - 1];
      parent.appendChild(el);
      const selfClosing = [
        'input', 'img', 'br', 'hr',
        'rect', 'path', 'circle', 'line', 'polyline', 'polygon', 'ellipse', 'stop', 'use'
      ].includes(tagName.toLowerCase());
      if (!selfClosing) stack.push(el);
    }
  }
}

describe('Modal Exportador Taskflow C++20 com Editor Monaco Fullscreen', () => {
  let originalDocument: any;
  let originalWindow: any;
  let mockBody: MockElement;
  let graph: Graph;
  let modal: CodeModal;

  beforeAll(() => {
    originalDocument = (globalThis as any).document;
    originalWindow = (globalThis as any).window;

    mockBody = new MockElement('BODY');
    const mockDoc = {
      createElement: (tag: string) => new MockElement(tag),
      body: mockBody,
      getElementById: (id: string) => mockBody.querySelector(`#${id}`)
    };

    (globalThis as any).document = mockDoc;
    (globalThis as any).window = {
      addEventListener: () => {},
      removeEventListener: () => {},
      navigator: { clipboard: { writeText: async () => {} } }
    };
  });

  afterAll(() => {
    (globalThis as any).document = originalDocument;
    (globalThis as any).window = originalWindow;
  });

  beforeEach(() => {
    graph = new Graph();
    modal = new CodeModal(graph);
  });

  afterEach(() => {
    modal.destroy();
  });

  it('1. Deve instanciar o CodeModal e possuir overlay no DOM', () => {
    expect(modal).toBeDefined();
    const overlay = (modal as any).modalOverlay as MockElement;
    expect(overlay).toBeDefined();
    expect(overlay.classList.contains('hidden')).toBe(true);
  });

  it('2. Deve abrir em tela cheia com a classe code-modal-fullscreen-dialog e maximização', async () => {
    await modal.show();
    const overlay = (modal as any).modalOverlay as MockElement;
    expect(overlay.classList.contains('hidden')).toBe(false);

    const dialog = overlay.querySelector('.code-modal-fullscreen-dialog') as MockElement;
    expect(dialog).toBeDefined();
    expect(dialog).not.toBeNull();

    // Verifica título e identificação do editor Monaco
    expect(overlay.innerHTML).toContain('Exportador Taskflow C++20');
    expect(overlay.innerHTML).toContain('Editor Monaco Integrado (Microsoft)');
    expect(overlay.innerHTML).toContain('MSVC C++20 Taskflow');
  }, 15000);

  it('3. Deve equipar o modal com as 4 abas de código e container do editor Monaco', async () => {
    await modal.show();
    const overlay = (modal as any).modalOverlay as MockElement;

    expect(overlay.querySelector('[data-tab="cpp"]')).not.toBeNull();
    expect(overlay.querySelector('[data-tab="msvc"]')).not.toBeNull();
    expect(overlay.querySelector('[data-tab="cmake"]')).not.toBeNull();
    expect(overlay.querySelector('[data-tab="makefile"]')).not.toBeNull();

    // Container dedicado onde o Monaco é montado
    const monacoContainer = overlay.querySelector('#monaco-code-editor-container');
    expect(monacoContainer).not.toBeNull();
  });

  it('4. Deve gerar código dinâmico C++20 Taskflow a partir dos nós e arestas do grafo', async () => {
    // Adiciona nós e arestas ao grafo
    const n1 = graph.createNode('node_1', 'Agente Coletor', 'agent', 0, 0);
    const n2 = graph.createNode('node_2', 'Filtro Concorrente', 'condition', 100, 0);
    graph.createEdge(n1.id, n1.outputs[0].id, n2.id, n2.inputs[0].id);

    await modal.show();
    const cache = (modal as any).codeCache;

    expect(cache.cpp).toContain('#include <taskflow/taskflow.hpp>');
    expect(cache.cpp).toContain('tf::Executor executor;');
    expect(cache.cpp).toContain('tf::Taskflow taskflow');
    expect(cache.cpp).toContain('task_node_1');
    expect(cache.cpp).toContain('task_node_2');
    expect(cache.cpp).toContain('precede(task_node_2)');

    expect(cache.msvc_ps1).toContain('cl.exe');
    expect(cache.msvc_ps1).toContain('/std:c++20');
    expect(cache.cmake).toContain('find_package(Taskflow REQUIRED)');
    expect(cache.make).toContain('CXX = cl.exe');
  });

  it('5. Deve alternar entre abas de arquivo e atualizar arquivo e linguagem exibidos', async () => {
    await modal.show();
    const overlay = (modal as any).modalOverlay as MockElement;

    const msvcTab = overlay.querySelector('[data-tab="msvc"]') as MockElement;
    msvcTab.click();

    expect((modal as any).currentTab).toBe('msvc');
    expect(overlay.innerHTML).toContain('build_msvc.ps1');
    expect(overlay.innerHTML).toContain('POWERSHELL');
  });

  it('6. Deve suportar alternância de tema (Claro vs Escuro) no editor', async () => {
    await modal.show();
    const overlay = (modal as any).modalOverlay as MockElement;

    const themeBtn = overlay.querySelector('#btn-toggle-editor-theme') as MockElement;
    expect(themeBtn).not.toBeNull();
    expect((modal as any).theme).toBe('vs');

    themeBtn.click();
    expect((modal as any).theme).toBe('vs-dark');

    const dialog = overlay.querySelector('.code-modal-fullscreen-dialog') as MockElement;
    expect(dialog.classList.contains('theme-dark')).toBe(true);
  });

  it('7. Deve possuir botões de ação: Abrir no VS Code, Baixar Arquivo, Copiar Código e Gerar', async () => {
    await modal.show();
    const overlay = (modal as any).modalOverlay as MockElement;

    expect(overlay.querySelector('#btn-open-vscode')).not.toBeNull();
    expect(overlay.querySelector('#btn-download-code-file')).not.toBeNull();
    expect(overlay.querySelector('#btn-copy-code')).not.toBeNull();
    const generateBtn = overlay.querySelector('#btn-generate-disk');
    expect(generateBtn).not.toBeNull();
    expect(generateBtn?.textContent).toContain('Gerar');
    expect(overlay.querySelector('#btn-close-code-modal')).not.toBeNull();
  });

  it('8. Deve alternar entre múltiplas abas e retornar à aba inicial preservando o código sem recriar o DOM', async () => {
    await modal.show();
    const overlay = (modal as any).modalOverlay as MockElement;

    // Aba inicial: cpp
    expect((modal as any).currentTab).toBe('cpp');
    const initialCppCode = modal.getActiveCode();
    expect(initialCppCode).toContain('#include <taskflow/taskflow.hpp>');

    // Alterna para cmake
    const cmakeTab = overlay.querySelector('[data-tab="cmake"]') as MockElement;
    cmakeTab.click();
    expect((modal as any).currentTab).toBe('cmake');
    const cmakeCode = modal.getActiveCode();
    expect(cmakeCode).toContain('find_package(Taskflow REQUIRED)');

    // Alterna para makefile
    const makefileTab = overlay.querySelector('[data-tab="makefile"]') as MockElement;
    makefileTab.click();
    expect((modal as any).currentTab).toBe('makefile');
    const makeCode = modal.getActiveCode();
    expect(makeCode).toContain('CXX = cl.exe');

    // Retorna para cpp e verifica se o código permanece íntegro
    const cppTab = overlay.querySelector('[data-tab="cpp"]') as MockElement;
    cppTab.click();
    expect((modal as any).currentTab).toBe('cpp');
    const returnedCppCode = modal.getActiveCode();
    expect(returnedCppCode).toBe(initialCppCode);
    expect(returnedCppCode.length).toBeGreaterThan(0);
  });

  it('9. Deve refletir a pasta de Output configurada como propriedade do projeto (padrão Output)', () => {
    expect(modal.getOutputDir()).toBe('Output');
  });
});

