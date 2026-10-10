import { describe, it, expect, beforeEach, afterEach, beforeAll, afterAll } from 'vitest';
import { ModalWindowManager } from '../src/ui/modals/ModalWindowManager.ts';
import { BaseModal } from '../src/ui/modals/BaseModal.ts';

// =========================================================================
// Mocks DOM para execução determinística e isolada no ambiente Node (Vitest)
// =========================================================================

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
  attributes: Record<string, string> = {};
  private _html: string = '';
  private _listeners: Record<string, Function[]> = {};

  constructor(tagName: string = 'DIV') {
    this.tagName = tagName.toUpperCase();
  }

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
      }
    };
  }

  get offsetWidth(): number {
    return parseInt(this.style.width || '0') || 480;
  }
  get offsetHeight(): number {
    return parseInt(this.style.height || '0') || 320;
  }
  get offsetLeft(): number {
    return parseInt(this.style.left || '0') || 100;
  }
  get offsetTop(): number {
    return parseInt(this.style.top || '0') || 100;
  }

  getBoundingClientRect() {
    return {
      left: this.offsetLeft,
      top: this.offsetTop,
      width: this.offsetWidth,
      height: this.offsetHeight,
      right: this.offsetLeft + this.offsetWidth,
      bottom: this.offsetTop + this.offsetHeight
    };
  }

  appendChild(child: MockElement): MockElement {
    if (child.parentNode) {
      child.parentNode.removeChild(child);
    }
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

  insertBefore(newNode: MockElement, refNode: MockElement): MockElement {
    if (newNode.parentNode) {
      newNode.parentNode.removeChild(newNode);
    }
    newNode.parentNode = this;
    const idx = this.children.indexOf(refNode);
    if (idx !== -1) {
      this.children.splice(idx, 0, newNode);
    } else {
      this.children.push(newNode);
    }
    return newNode;
  }

  addEventListener(event: string, fn: Function) {
    this._listeners[event] = this._listeners[event] || [];
    this._listeners[event].push(fn);
  }

  removeEventListener(event: string, fn: Function) {
    if (this._listeners[event]) {
      this._listeners[event] = this._listeners[event].filter(f => f !== fn);
    }
  }

  dispatchEvent(event: any) {
    const list = this._listeners[event.type] || [];
    list.forEach(fn => fn(event));
    return true;
  }

  click() {
    (this._listeners['click'] || []).forEach(fn => fn({ preventDefault: () => {}, stopPropagation: () => {} }));
  }

  setAttribute(name: string, value: string) {
    this.attributes[name] = value;
    if (name === 'id') this.id = value;
    if (name === 'class') this.className = value;
    if (name === 'type') this.type = value;
    if (name === 'title') this.title = value;
  }

  getAttribute(name: string): string | null {
    if (name === 'id') return this.id || null;
    if (name === 'class') return this.className || null;
    return this.attributes[name] ?? null;
  }

  setPointerCapture(_id: number) {}
  releasePointerCapture(_id: number) {}

  closest(selector: string): MockElement | null {
    let current: MockElement | null = this;
    while (current) {
      if (current.matchesSelector(selector)) return current;
      current = current.parentNode;
    }
    return null;
  }

  private matchesSingle(sel: string): boolean {
    if (!sel) return false;
    if (sel.startsWith('[') && sel.endsWith(']')) {
      const attr = sel.slice(1, -1);
      return this.attributes[attr] !== undefined;
    }
    if (sel.startsWith('#')) {
      return this.id === sel.slice(1);
    }
    const parts = sel.split(/(?=[.#])/);
    for (const part of parts) {
      if (part.startsWith('.')) {
        if (!this.classList.contains(part.slice(1))) return false;
      } else if (part.startsWith('#')) {
        if (this.id !== part.slice(1)) return false;
      } else {
        if (this.tagName.toLowerCase() !== part.toLowerCase()) return false;
      }
    }
    return true;
  }

  matchesSelector(selector: string): boolean {
    if (selector.includes(',')) {
      return selector.split(',').some(s => this.matchesSelector(s.trim()));
    }
    return this.matchesSingle(selector);
  }

  querySelector(selector: string): MockElement | null {
    for (const child of this.children) {
      if (child.matchesSelector(selector)) return child;
      const found = child.querySelector(selector);
      if (found) return found;
    }
    return null;
  }

  querySelectorAll(selector: string): MockElement[] {
    const results: MockElement[] = [];
    for (const child of this.children) {
      if (child.matchesSelector(selector)) results.push(child);
      results.push(...child.querySelectorAll(selector));
    }
    return results;
  }

  get innerHTML(): string {
    return this._html;
  }

  set innerHTML(html: string) {
    this._html = html;
    this.children = [];
    this.textContent = '';
    parseHTMLToMock(html, this);
  }
}

function parseHTMLToMock(html: string, root: MockElement) {
  const tokenRegex = /<(\/)?([a-zA-Z0-9\-]+)([^>]*?)>|([^<]+)/g;
  const stack: MockElement[] = [root];
  let match: RegExpExecArray | null;

  while ((match = tokenRegex.exec(html)) !== null) {
    const [full, isClosing, tagName, attrs, text] = match;
    if (text) {
      const trimmed = text.trim();
      if (trimmed && stack.length > 0) {
        const top = stack[stack.length - 1];
        top.textContent = top.textContent ? `${top.textContent} ${trimmed}` : trimmed;
      }
      continue;
    }

    if (isClosing) {
      if (stack.length > 1) {
        stack.pop();
      }
    } else {
      const el = new MockElement(tagName);
      if (attrs) {
        const idMatch = attrs.match(/id=["']([^"']+)["']/);
        if (idMatch) el.id = idMatch[1];

        const classMatch = attrs.match(/class=["']([^"']+)["']/);
        if (classMatch) el.className = classMatch[1];

        const titleMatch = attrs.match(/title=["']([^"']+)["']/);
        if (titleMatch) el.title = titleMatch[1];

        const typeMatch = attrs.match(/type=["']([^"']+)["']/);
        if (typeMatch) el.type = typeMatch[1];
      }
      stack[stack.length - 1].appendChild(el);

      const isSelfClosing = full.endsWith('/>') || ['img', 'br', 'hr', 'input'].includes(tagName.toLowerCase());
      if (!isSelfClosing) {
        stack.push(el);
      }
    }
  }
}

describe('ModalWindowManager & BaseModal (Fase 15.1 - Window Management Engine)', () => {
  let originalDocument: any;
  let originalWindow: any;
  let mockDoc: any;
  let mockWin: any;
  let container: any;
  let dialog: any;
  let header: any;

  beforeAll(() => {
    originalDocument = (globalThis as any).document;
    originalWindow = (globalThis as any).window;

    const mockBody = new MockElement('BODY');
    mockDoc = {
      createElement: (tag: string) => new MockElement(tag),
      body: mockBody,
      querySelector: (selector: string) => mockBody.querySelector(selector),
      querySelectorAll: (selector: string) => mockBody.querySelectorAll(selector)
    };

    mockWin = {
      innerWidth: 1280,
      innerHeight: 800,
      addEventListener: () => {},
      removeEventListener: () => {},
      requestAnimationFrame: (cb: Function) => cb()
    };

    (globalThis as any).document = mockDoc;
    (globalThis as any).window = mockWin;
  });

  afterAll(() => {
    (globalThis as any).document = originalDocument;
    (globalThis as any).window = originalWindow;
  });

  beforeEach(() => {
    mockDoc.body.children = [];
    mockWin.innerWidth = 1280;
    mockWin.innerHeight = 800;

    container = mockDoc.createElement('div');
    container.className = 'modal-overlay';
    mockDoc.body.appendChild(container);

    dialog = mockDoc.createElement('div');
    dialog.className = 'modal-dialog';
    container.appendChild(dialog);

    header = mockDoc.createElement('div');
    header.className = 'modal-header';
    header.innerHTML = `
      <div class="modal-title-group">
        <h2 class="modal-title">Título de Teste</h2>
      </div>
      <button class="modal-close-btn" id="btn-close-test">&times;</button>
    `;
    dialog.appendChild(header);
  });

  afterEach(() => {
    if (container && container.parentNode) {
      container.parentNode.removeChild(container);
    }
  });

  describe('1. Geração Padronizada da Barra de Títulos (createTitleBarHTML)', () => {
    it('deve gerar HTML com ícone, título, subtítulo e controles de janela', () => {
      const html = ModalWindowManager.createTitleBarHTML({
        title: 'Configurações de IA',
        subtitle: 'Nó: Redator (ID: n1)',
        icon: '🧠',
        closeBtnId: 'btn-close-custom'
      });

      expect(html).toContain('Configurações de IA');
      expect(html).toContain('Nó: Redator (ID: n1)');
      expect(html).toContain('🧠');
      expect(html).toContain('window-btn-maximize');
      expect(html).toContain('id="btn-close-custom"');
      expect(html).toContain('fluent-header-draggable');
    });

    it('deve suportar badges contextuais na barra de títulos', () => {
      const html = ModalWindowManager.createTitleBarHTML({
        title: 'Memória Persistente',
        badgeText: 'ATIVO',
        badgeClass: 'badge-active'
      });

      expect(html).toContain('badge-active');
      expect(html).toContain('ATIVO');
    });
  });

  describe('2. Aprimoramento e Acoplamento da Janela (attach)', () => {
    it('deve adicionar classes fluent-window e injetar o resize-handle e maximize button', () => {
      const controller = ModalWindowManager.attach({
        dialog,
        header
      });

      expect(dialog.classList.contains('fluent-window')).toBe(true);
      expect(dialog.classList.contains('fluent-window-resizable')).toBe(true);
      expect(header.classList.contains('fluent-window-header')).toBe(true);
      expect(header.classList.contains('fluent-header-draggable')).toBe(true);

      const maximizeBtn = header.querySelector('.window-btn-maximize');
      expect(maximizeBtn).not.toBeNull();

      const resizeHandle = dialog.querySelector('.window-resize-handle');
      expect(resizeHandle).not.toBeNull();

      controller.destroy();
    });

    it('deve elevar o z-index ao interagir com a janela (bringToFront)', () => {
      const controller = ModalWindowManager.attach({ dialog, header });
      const initialZIndex = parseInt(dialog.style.zIndex || '0');

      controller.bringToFront();
      const updatedZIndex = parseInt(dialog.style.zIndex || '0');

      expect(updatedZIndex).toBeGreaterThan(initialZIndex);
      controller.destroy();
    });

    it('deve alternar entre maximizado e restaurado com toggleMaximize', () => {
      const controller = ModalWindowManager.attach({ dialog, header });

      expect(controller.isMaximized()).toBe(false);

      controller.maximize();
      expect(controller.isMaximized()).toBe(true);
      expect(dialog.classList.contains('is-maximized')).toBe(true);
      expect(dialog.style.left).toBe('12px');
      expect(dialog.style.top).toBe('12px');

      controller.restore();
      expect(controller.isMaximized()).toBe(false);
      expect(dialog.classList.contains('is-maximized')).toBe(false);

      controller.destroy();
    });

    it('deve permitir posicionamento e redimensionamento explícitos com limites', () => {
      const controller = ModalWindowManager.attach({
        dialog,
        header,
        minWidth: 400,
        minHeight: 300
      });

      controller.setPosition(250, 150);
      expect(dialog.style.left).toBe('250px');
      expect(dialog.style.top).toBe('150px');

      // Aplica tamanho menor que o mínimo permitido -> deve travar no mínimo
      controller.setSize(200, 150);
      expect(dialog.style.width).toBe('400px');
      expect(dialog.style.height).toBe('300px');

      controller.destroy();
    });

    it('deve restringir coordenadas para não sumir da viewport (clamping)', () => {
      const controller = ModalWindowManager.attach({ dialog, header });

      // Tenta colocar coordenadas negativas
      controller.setPosition(-500, -200);
      expect(parseInt(dialog.style.left)).toBeGreaterThanOrEqual(0);
      expect(parseInt(dialog.style.top)).toBeGreaterThanOrEqual(0);

      // Tenta colocar coordenadas muito além da tela
      controller.setPosition(5000, 4000);
      expect(parseInt(dialog.style.left)).toBeLessThan(1280);
      expect(parseInt(dialog.style.top)).toBeLessThan(800);

      controller.destroy();
    });
  });

  describe('3. Classe Base Reutilizável (BaseModal)', () => {
    class ConcreteTestModal extends BaseModal {
      protected renderBody(): string {
        return '<div class="test-body-content">Conteúdo customizado do modal</div>';
      }
    }

    it('deve instanciar, renderizar conteúdo e fornecer o controller de janela', () => {
      const testModal = new ConcreteTestModal({
        id: 'test-custom-modal',
        title: 'Modal Concreto de Teste',
        subtitle: 'Subtítulo do teste',
        icon: '⭐'
      });

      testModal.open();

      const overlayEl = mockDoc.querySelector('#test-custom-modal-overlay') as any;
      expect(overlayEl).not.toBeNull();
      expect(overlayEl.classList.contains('hidden')).toBe(false);

      const titleEl = overlayEl.querySelector('.modal-title');
      expect(titleEl?.textContent).toBe('Modal Concreto de Teste');

      const bodyEl = overlayEl.querySelector('.test-body-content');
      expect(bodyEl?.textContent).toBe('Conteúdo customizado do modal');

      const ctrl = testModal.getController();
      expect(ctrl).toBeDefined();

      testModal.hide();
      expect(overlayEl.classList.contains('hidden')).toBe(true);

      testModal.destroy();
    });
  });
});
