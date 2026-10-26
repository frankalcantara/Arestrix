/**
 * Mock mínimo de DOM SVG para testes determinísticos em Node/Vitest.
 *
 * A camada vetorial usa apenas APIs síncronas de SVG (createElementNS,
 * setAttribute, appendChild, querySelector...), então um mock estrutural
 * dispensa jsdom e mantém os testes rápidos.
 */

export class MockSvgElement {
  tagName: string;
  className: string = '';
  style: Record<string, string> = {};
  children: MockSvgElement[] = [];
  parentNode: MockSvgElement | null = null;
  textContent: string = '';
  attributes: Record<string, string> = {};
  private _listeners: Record<string, Function[]> = {};

  constructor(tagName: string = 'g') {
    this.tagName = tagName.toLowerCase();
  }

  setAttribute(name: string, value: string) {
    this.attributes[name] = value;
    if (name === 'class') this.className = value;
  }

  getAttribute(name: string): string | null {
    if (name === 'class') return this.className || null;
    return this.attributes[name] ?? null;
  }

  removeAttribute(name: string) {
    delete this.attributes[name];
  }

  appendChild(child: MockSvgElement): MockSvgElement {
    if (child.parentNode) {
      child.parentNode.removeChild(child);
    }
    child.parentNode = this;
    this.children.push(child);
    return child;
  }

  removeChild(child: MockSvgElement): MockSvgElement {
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

  querySelector(selector: string): MockSvgElement | null {
    const all = this.querySelectorAll(selector);
    return all.length > 0 ? all[0] : null;
  }

  querySelectorAll(selector: string): MockSvgElement[] {
    const results: MockSvgElement[] = [];
    const check = (el: MockSvgElement) => {
      let match = false;
      if (selector.startsWith('.')) {
        const cls = selector.slice(1);
        if (el.className && el.className.split(/\s+/).includes(cls)) match = true;
      } else if (selector.startsWith('[')) {
        const attrMatch = selector.match(/\[([a-zA-Z0-9_-]+)(?:="([^"]+)")?\]/);
        if (attrMatch) {
          const [, attrName, attrVal] = attrMatch;
          if (attrVal !== undefined) {
            if (el.getAttribute(attrName) === attrVal) match = true;
          } else {
            if (el.getAttribute(attrName) !== null) match = true;
          }
        }
      } else {
        if (el.tagName === selector.toLowerCase()) match = true;
      }
      if (match) results.push(el);
      el.children.forEach(c => check(c));
    };

    this.children.forEach(c => check(c));
    return results;
  }
}

/**
 * Instala um `document` mínimo baseado em MockSvgElement e devolve a função
 * de restauração do documento original.
 */
export function installMockSvgDocument(): () => void {
  const original = (globalThis as any).document;
  (globalThis as any).document = {
    createElementNS: (_ns: string, tag: string) => new MockSvgElement(tag),
    createElement: (tag: string) => new MockSvgElement(tag),
    body: new MockSvgElement('body')
  };
  return () => {
    (globalThis as any).document = original;
  };
}
