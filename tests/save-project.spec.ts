import { describe, it, expect, beforeEach, afterEach, beforeAll, afterAll } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { ProjectManager } from '../src/core/ProjectManager.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { BackstageView } from '../src/ui/ribbon/BackstageView.ts';

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

        const dataAttrs = attrs.matchAll(/(data-[a-zA-Z0-9_\-]+)=["']([^"']*)["']/g);
        for (const dm of dataAttrs) {
          el.setAttribute(dm[1], dm[2]);
        }

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

describe('Tela Salvar Unificada e Projeto em Pasta (Estilo Office)', () => {
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
    if ((globalThis as any).document?.body) {
      (globalThis as any).document.body.children = [];
    }
  });

  afterEach(() => {
    if ((globalThis as any).window) {
      delete (globalThis as any).window.webui;
    }
  });

  it('deve salvar apenas um grafo (.json) e vinculá-lo ao projeto ativo quando salvo dentro da pasta do projeto', async () => {
    await pm.createProject({
      projectId: 'proj-financas',
      name: 'Financas2026',
      rootPath: 'C:/Projetos/Financas2026'
    }, graph);

    graph.addNode({
      id: 'node-auditoria',
      type: 'agent',
      label: 'Auditor Contábil',
      position: { x: 120, y: 80 },
      inputs: [],
      outputs: []
    });

    const res = await pm.saveGraphToFile('C:/Projetos/Financas2026/pipelines', 'subfluxo_auditoria', graph);
    expect(res.success).toBe(true);
    expect(res.filePath).toBe('C:/Projetos/Financas2026/pipelines/subfluxo_auditoria.json');
    expect(res.registeredInProject).toBe(true);

    const updatedProj = pm.getCurrentProject();
    const registered = updatedProj.pipelines.find(p => p.file === 'pipelines/subfluxo_auditoria.json');
    expect(registered).toBeDefined();
    expect(registered?.name).toBe('subfluxo_auditoria');

    // Limpa o grafo e verifica se loadPipeline carrega o grafo secundário salvo
    graph.clear();
    expect(graph.getAllNodes().length).toBe(0);

    const loaded = await pm.loadPipeline(graph, 'pipelines/subfluxo_auditoria.json');
    expect(loaded).toBe(true);
    expect(graph.getAllNodes().length).toBe(1);
    expect(graph.getNode('node-auditoria')?.label).toBe('Auditor Contábil');
  });

  it('deve salvar projeto completo em pasta estruturada via saveProjectToDirectory preservando o grafo ativo', async () => {
    graph.addNode({
      id: 'node-etl',
      type: 'static',
      label: 'Ingestão C++20',
      position: { x: 40, y: 60 },
      inputs: [],
      outputs: []
    });

    const res = await pm.saveProjectToDirectory('C:/Projetos/Workspaces', 'PipelineETL', graph);
    expect(res.success).toBe(true);
    expect(res.rootPath).toBe('C:/Projetos/Workspaces/PipelineETL');
    expect(res.manifestPath).toBe('C:/Projetos/Workspaces/PipelineETL/arestrix.project.json');
    expect(pm.getCurrentProject().name).toBe('PipelineETL');
    expect(graph.getAllNodes().length).toBe(1);
  });

  it('recusa exportar o workspace padrao sem pasta de projeto', async () => {
    (globalThis as any).window.webui = { call: async () => true };
    const res = await pm.exportProjectZip(graph, 'C:/Backups');
    expect(res.ok).toBe(false);
    expect(res.error).toContain('Salve o projeto numa pasta');
  });

  it('deve exportar a pasta do projeto como .arestrix.zip pelo backend', async () => {
    graph.addNode({
      id: 'node-export',
      type: 'static',
      label: 'Exportar',
      position: { x: 10, y: 20 },
      inputs: [],
      outputs: []
    });
    await pm.saveProjectToDirectory('C:/Projetos/Workspaces', 'PipelineETL', graph);

    const calls: Array<{ method: string; payload: any }> = [];
    (globalThis as any).window.webui = {
      call: async (method: string, payload?: string) => {
        calls.push({ method, payload: payload ? JSON.parse(payload) : null });
        if (method === 'export_project_zip') {
          return JSON.stringify({ ok: true, path: 'C:/Backups/PipelineETL.arestrix.zip' });
        }
        return true;
      }
    };

    const res = await pm.exportProjectZip(graph, 'C:/Backups');
    expect(res).toEqual({ ok: true, path: 'C:/Backups/PipelineETL.arestrix.zip' });

    const exportCall = calls.find(call => call.method === 'export_project_zip');
    expect(exportCall?.payload).toMatchObject({
      rootPath: 'C:/Projetos/Workspaces/PipelineETL',
      targetDir: 'C:/Backups',
      projectName: 'PipelineETL'
    });
  });

  it('deve listar discos do sistema e subdiretórios com fallbacks resilientes', async () => {
    const drives = await pm.listDrives();
    expect(Array.isArray(drives)).toBe(true);
    expect(drives.length).toBeGreaterThanOrEqual(1);

    const subs = await pm.listSubdirectories('C:/');
    expect(Array.isArray(subs)).toBe(true);
  });

  it('deve renderizar a ação de exportar o projeto no Backstage', async () => {
    const fakeCodeModal = { show: () => {} } as any;
    const fakeDefaultsModal = { show: () => {} } as any;
    const backstage = new BackstageView(graph, fakeCodeModal, fakeDefaultsModal);

    await backstage.open('export');

    const overlay = document.querySelector('.office-backstage-overlay');
    expect(overlay).not.toBeNull();
    expect(overlay?.querySelector('#btn-backstage-export-project-zip')).not.toBeNull();
    expect(overlay?.innerHTML || '').toContain('Projeto (.arestrix.zip)');
  });

  it('deve renderizar o menu único "Salvar" no Backstage (Estilo Office) com seleção Projeto vs Grafo e navegação de pastas', async () => {
    const fakeCodeModal = { show: () => {} } as any;
    const fakeDefaultsModal = { show: () => {} } as any;
    const backstage = new BackstageView(graph, fakeCodeModal, fakeDefaultsModal);

    await backstage.open('save');

    expect(backstage.isOpen()).toBe(true);
    const overlay = document.querySelector('.office-backstage-overlay');
    expect(overlay).not.toBeNull();

    // Item de navegação único "Salvar" ativo na barra lateral
    const activeItem = overlay?.querySelector('.backstage-nav-item.active');
    expect(activeItem?.getAttribute('data-section')).toBe('saveas');
    expect(overlay?.querySelector('#nav-backstage-save')).not.toBeNull();

    // Cartões de escopo: Projeto Completo vs Apenas um Grafo do Projeto
    const targetProjectBtn = overlay?.querySelector('#btn-saveas-target-project');
    const targetGraphBtn = overlay?.querySelector('#btn-saveas-target-graph');
    expect(targetProjectBtn).not.toBeNull();
    expect(targetGraphBtn).not.toBeNull();
    expect(targetProjectBtn?.classList.contains('active')).toBe(true);

    // Botões de navegação e discos
    expect(overlay?.querySelector('.btn-saveas-drive')).not.toBeNull();
    expect(overlay?.querySelector('#btn-saveas-up-dir')).not.toBeNull();
    expect(overlay?.querySelector('#btn-saveas-refresh-dir')).not.toBeNull();
    expect(overlay?.querySelector('#btn-saveas-browse-native')).not.toBeNull();

    // Inputs e botão de salvar
    expect(overlay?.querySelector('#saveas-input-filename')).not.toBeNull();
    expect(overlay?.querySelector('#saveas-select-format')).toBeNull();
    expect(overlay?.querySelector('#btn-saveas-submit')).not.toBeNull();

    // Alterna para "Apenas um Grafo do Projeto" e verifica mudança automática para .json
    targetGraphBtn?.click();
    await new Promise(r => setTimeout(r, 20));

    const activeGraphBtn = overlay?.querySelector('#btn-saveas-target-graph');
    expect(activeGraphBtn?.classList.contains('active')).toBe(true);
    const preview = overlay?.querySelector('#saveas-path-preview');
    expect(preview?.textContent).toContain('.json');

    backstage.close();
    expect(backstage.isOpen()).toBe(false);
  });
});
