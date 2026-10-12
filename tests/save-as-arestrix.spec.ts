import { describe, it, expect, beforeEach, beforeAll, afterAll } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { ProjectManager } from '../src/core/ProjectManager.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { BackstageView } from '../src/ui/ribbon/BackstageView.ts';
import { IArestrixBundle } from '../src/core/types.ts';

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
      }
    };
  }

  constructor(tagName: string = 'DIV') {
    this.tagName = tagName.toUpperCase();
  }

  get innerHTML(): string {
    return this._html;
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

  getAttribute(attr: string): string | null {
    return this.attributes[attr] ?? null;
  }

  setAttribute(attr: string, val: string) {
    this.attributes[attr] = val;
  }

  addEventListener(event: string, fn: Function) {
    this._listeners[event] = this._listeners[event] || [];
    this._listeners[event].push(fn);
  }

  click() {
    (this._listeners['click'] || []).forEach(fn => fn({ preventDefault: () => {}, stopPropagation: () => {} }));
  }

  contains(child: any): boolean {
    if (child === this) return true;
    return this.children.some(c => c.contains(child));
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
    if (sel.startsWith('.')) {
      const classes = sel.slice(1).split('.');
      return classes.every(c => this.className.split(/\s+/).includes(c));
    }
    if (sel.includes('[')) {
      const match = sel.match(/\[([a-zA-Z0-9_\-]+)(?:=["']?([^"'\]]+)["']?)?\]/);
      if (match) {
        const attr = match[1];
        const val = match[2];
        if (val !== undefined) return this.attributes[attr] === val;
        return this.attributes[attr] !== undefined;
      }
    }
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
        const idMatch = attrs.match(/id=["']([^"']+)["']/);
        if (idMatch) el.id = idMatch[1];

        const classMatch = attrs.match(/class=["']([^"']+)["']/);
        if (classMatch) el.className = classMatch[1];

        const titleMatch = attrs.match(/title=["']([^"']+)["']/);
        if (titleMatch) el.title = titleMatch[1];

        const typeMatch = attrs.match(/type=["']([^"']+)["']/);
        if (typeMatch) el.type = typeMatch[1];

        const dataSection = attrs.match(/data-section=["']([^"']+)["']/);
        if (dataSection) el.setAttribute('data-section', dataSection[1]);

        const dataDrive = attrs.match(/data-drive=["']([^"']+)["']/);
        if (dataDrive) el.setAttribute('data-drive', dataDrive[1]);

        if (attrs.includes('checked')) el.checked = true;
      }
      stack[stack.length - 1].appendChild(el);

      const isSelfClosing = attrs?.endsWith('/') || ['img', 'br', 'hr', 'input'].includes(tagName.toLowerCase());
      if (!isSelfClosing) {
        stack.push(el);
      }
    }
  }
}

describe('Pacotes de Projeto .arestrix e Tela Salvar Como (Estilo Office)', () => {
  let graph: Graph;
  let bus: EventBus;
  let pm: ProjectManager;
  let originalDoc: any;
  let originalWin: any;

  beforeAll(() => {
    originalDoc = (globalThis as any).document;
    originalWin = (globalThis as any).window;

    const mockBody = new MockElement('BODY');
    (globalThis as any).document = {
      createElement: (tag: string) => new MockElement(tag),
      body: mockBody,
      querySelector: (selector: string) => mockBody.querySelector(selector),
      querySelectorAll: (selector: string) => mockBody.querySelectorAll(selector)
    };

    (globalThis as any).window = {
      addEventListener: () => {},
      removeEventListener: () => {}
    };
  });

  afterAll(() => {
    (globalThis as any).document = originalDoc;
    (globalThis as any).window = originalWin;
  });

  beforeEach(() => {
    bus = new EventBus();
    graph = new Graph(bus);
    pm = ProjectManager.getInstance(bus);
    pm.resetForTesting(bus);
  });

  it('deve exportar um IArestrixBundle contendo manifesto, grafos/pipelines, variáveis e defaults', () => {
    graph.addNode({
      id: 'node-1',
      type: 'agent',
      label: 'Agente Analista',
      position: { x: 100, y: 150 },
      inputs: [],
      outputs: []
    });

    const bundle: IArestrixBundle = pm.exportArestrixBundle(graph);

    expect(bundle.format).toBe('arestrix-package');
    expect(bundle.version).toBe('1.0.0');
    expect(bundle.manifest).toBeDefined();
    expect(bundle.manifest.projectId).toBe('default-workspace');
    expect(bundle.pipelines).toBeDefined();
    expect(bundle.pipelines['main']).toBeDefined();
    expect(bundle.pipelines['main'].nodes.length).toBe(1);
    expect(bundle.pipelines['main'].nodes[0].label).toBe('Agente Analista');
  });

  it('deve comprimir e descomprimir o pacote com algoritmo Gzip preservando integridade estrutural', async () => {
    graph.addNode({
      id: 'task-a',
      type: 'static',
      label: 'Processamento Batch',
      position: { x: 50, y: 50 },
      inputs: [],
      outputs: []
    });

    const originalBundle = pm.exportArestrixBundle(graph);
    const compressed = await pm.compressArestrixBundle(originalBundle);

    expect(compressed).toBeInstanceOf(Uint8Array);
    expect(compressed.length).toBeGreaterThan(0);

    const decompressed = await pm.decompressArestrixBundle(compressed);

    expect(decompressed.format).toBe('arestrix-package');
    expect(decompressed.manifest.name).toBe(originalBundle.manifest.name);
    expect(decompressed.pipelines['main'].nodes.length).toBe(1);
    expect(decompressed.pipelines['main'].nodes[0].id).toBe('task-a');
  });

  it('deve salvar projeto como pacote .arestrix e registrar no histórico recente', async () => {
    let savedEventFired = false;
    let exportedEventFired = false;

    bus.on('project:saved', () => { savedEventFired = true; });
    bus.on('project:exported', () => { exportedEventFired = true; });

    const result = await pm.saveProjectAsArestrix('C:/Projetos/Testes', 'pipeline_financeiro', graph);

    expect(result.success).toBe(true);
    expect(result.filePath).toBe('C:/Projetos/Testes/pipeline_financeiro.arestrix');
    expect(savedEventFired).toBe(true);
    expect(exportedEventFired).toBe(true);

    const recents = pm.listRecentProjects();
    expect(recents.length).toBeGreaterThanOrEqual(1);
    expect(recents[0].name).toBe('pipeline_financeiro');
    expect(recents[0].manifestPath).toBe('C:/Projetos/Testes/pipeline_financeiro.arestrix');
  });

  it('deve carregar pacote IArestrixBundle completo restaurando manifesto e nós no canvas', async () => {
    const customBundle: IArestrixBundle = {
      format: 'arestrix-package',
      version: '1.0.0',
      exportedAt: new Date().toISOString(),
      manifest: {
        projectId: 'audit-proj-2026',
        name: 'Auditoria Agentes 2026',
        version: '1.2.0',
        description: 'Auditoria de conformidade de agentes',
        createdAt: new Date().toISOString(),
        updatedAt: new Date().toISOString(),
        rootPath: 'C:/Projetos/Auditoria',
        pipelines: [{ id: 'main', name: 'Main', file: 'pipelines/main.json', isPrimary: true }]
      },
      pipelines: {
        main: {
          version: '1.0',
          metadata: { graphName: 'Auditoria Agentes 2026', projectId: 'audit-proj-2026' },
          nodes: [{
            id: 'audit-node',
            type: 'agent',
            label: 'Auditor C++20',
            position: { x: 200, y: 300 },
            inputs: [],
            outputs: []
          }],
          edges: []
        }
      }
    };

    const ok = await pm.loadArestrixBundle(customBundle, graph);

    expect(ok).toBe(true);
    expect(pm.getCurrentProject().name).toBe('Auditoria Agentes 2026');
    expect(graph.getAllNodes().length).toBe(1);
    expect(graph.getNode('audit-node')?.label).toBe('Auditor C++20');
  });

  it('deve listar discos do sistema e subdiretórios com fallbacks resilientes', async () => {
    const drives = await pm.listDrives();
    expect(Array.isArray(drives)).toBe(true);
    expect(drives.length).toBeGreaterThanOrEqual(1);

    const subs = await pm.listSubdirectories('C:/');
    expect(Array.isArray(subs)).toBe(true);
  });

  it('deve renderizar a tela "Salvar Como" no Backstage com controles de discos, pastas e botão de salvar', async () => {
    const fakeCodeModal = { show: () => {} } as any;
    const fakeDefaultsModal = { show: () => {} } as any;
    const backstage = new BackstageView(graph, fakeCodeModal, fakeDefaultsModal);

    await backstage.open('saveas');

    expect(backstage.isOpen()).toBe(true);
    const overlay = document.querySelector('.office-backstage-overlay');
    expect(overlay).not.toBeNull();

    // Item de navegação ativo
    const activeItem = overlay?.querySelector('.backstage-nav-item.active');
    expect(activeItem?.getAttribute('data-section')).toBe('saveas');

    // Botões de navegação e discos
    expect(overlay?.querySelector('.btn-saveas-drive')).not.toBeNull();
    expect(overlay?.querySelector('#btn-saveas-up-dir')).not.toBeNull();
    expect(overlay?.querySelector('#btn-saveas-refresh-dir')).not.toBeNull();
    expect(overlay?.querySelector('#btn-saveas-browse-native')).not.toBeNull();

    // Inputs e botão de salvar
    expect(overlay?.querySelector('#saveas-input-filename')).not.toBeNull();
    expect(overlay?.querySelector('#saveas-select-format')).not.toBeNull();
    expect(overlay?.querySelector('#btn-saveas-submit')).not.toBeNull();

    backstage.close();
    expect(backstage.isOpen()).toBe(false);
  });
});
