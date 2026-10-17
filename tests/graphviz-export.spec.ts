import { describe, it, expect, beforeEach, beforeAll, afterAll } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { GraphvizGenerator } from '../src/core/GraphvizGenerator.ts';
import { GraphvizExportModal } from '../src/ui/modals/GraphvizExportModal.ts';

class MockElement {
  tagName: string;
  className: string = '';
  id: string = '';
  type: string = '';
  title: string = '';
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
      contains: (cls: string) => this.className.split(/\s+/).includes(cls)
    };
  }

  set innerHTML(html: string) {
    this._html = html;
    this.children = [];
    parseHTMLToMock(html, this);
  }

  get innerHTML(): string {
    return this._html;
  }

  constructor(tagName: string) {
    this.tagName = tagName;
  }

  setAttribute(k: string, v: string) {
    this.attributes[k] = v;
    if (k === 'id') this.id = v;
    if (k === 'class') this.className = v;
    if (k === 'type') this.type = v;
  }

  getAttribute(k: string): string | null {
    return this.attributes[k] ?? null;
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

  addEventListener(ev: string, cb: Function) {
    if (!this._listeners[ev]) this._listeners[ev] = [];
    this._listeners[ev].push(cb);
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

  matches(sel: string): boolean {
    if (sel.startsWith('#')) return this.id === sel.slice(1);
    if (sel.startsWith('.')) return this.className.split(/\s+/).includes(sel.slice(1));
    return this.tagName.toLowerCase() === sel.toLowerCase();
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
      if (stack.length > 1) {
        stack.pop();
      }
    } else {
      const el = new MockElement(tagName);
      if (attrs) {
        const attrRegex = /([a-zA-Z0-9\-]+)(?:=["']([^"']*)["'])?/g;
        let attrMatch: RegExpExecArray | null;
        while ((attrMatch = attrRegex.exec(attrs)) !== null) {
          el.setAttribute(attrMatch[1], attrMatch[2] ?? '');
        }
      }
      const parent = stack[stack.length - 1];
      parent.appendChild(el);
      const isVoid = /^(input|img|br|hr|meta|link)$/i.test(tagName) || attrs.endsWith('/');
      if (!isVoid) {
        stack.push(el);
      }
    }
  }
}

describe('Graphviz Export & Renderizador WASM Offline', () => {
  let graph: Graph;
  let bus: EventBus;
  const savedDocument = (globalThis as any).document;

  beforeAll(() => {
    const body = new MockElement('body');
    (globalThis as any).document = {
      body,
      createElement: (tag: string) => new MockElement(tag),
      getElementById: (id: string) => body.querySelector(`#${id}`)
    };
  });

  afterAll(() => {
    (globalThis as any).document = savedDocument;
  });

  beforeEach(() => {
    bus = new EventBus();
    graph = new Graph(bus);
  });

  it('deve gerar especificação DOT válida para o grafo atual', () => {
    const nodeA = graph.createNode('node_1', 'Agente Alpha', 'agent', 0, 0);
    const nodeB = graph.createNode('node_2', 'Decisor', 'condition', 200, 0);
    graph.createEdge(nodeA.id, nodeA.outputs[0].id, nodeB.id, nodeB.inputs[0].id);

    const dot = GraphvizGenerator.generateDot(graph);
    expect(dot).toContain('digraph');
    expect(dot).toContain('Agente Alpha');
    expect(dot).toContain('Decisor');
    expect(dot).toContain(`"${nodeA.id}" -> "${nodeB.id}"`);
  });

  it('deve renderizar DOT diretamente para SVG usando Graphviz WASM offline', async () => {
    const nodeA = graph.createNode('node_1', 'Início', 'static', 0, 0);
    const nodeB = graph.createNode('node_2', 'Fim', 'static', 200, 0);
    graph.createEdge(nodeA.id, nodeA.outputs[0].id, nodeB.id, nodeB.inputs[0].id);

    const svg = await GraphvizGenerator.renderSvg(graph);
    expect(svg).toBeDefined();
    expect(svg).toContain('<svg');
    expect(svg).toContain('</svg>');
    expect(svg).toContain('Início');
    expect(svg).toContain('Fim');
  });

  it('deve instanciar GraphvizExportModal e renderizar botões de ação e zoom', async () => {
    const modal = new GraphvizExportModal(graph);
    expect(modal).toBeDefined();

    // Renderiza e abre o modal
    await modal.show();

    const domModal = (globalThis as any).document.getElementById('modal-export-graphviz');
    expect(domModal).toBeDefined();
    expect(domModal.style.display).toBe('flex');

    // Botões essenciais
    expect(domModal.querySelector('#btn-graphviz-download-svg')).toBeDefined();
    expect(domModal.querySelector('#btn-graphviz-download-dot')).toBeDefined();
    expect(domModal.querySelector('#btn-graphviz-copy-svg')).toBeDefined();
    expect(domModal.querySelector('#btn-graphviz-open-tab')).toBeDefined();
    expect(domModal.querySelector('#btn-graphviz-zoom-in')).toBeDefined();
    expect(domModal.querySelector('#btn-graphviz-zoom-out')).toBeDefined();

    // Fecha o modal
    modal.hide();
    expect(domModal.style.display).toBe('none');
  });
});
