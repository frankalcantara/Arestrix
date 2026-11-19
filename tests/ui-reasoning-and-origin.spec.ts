import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { NodeModelModal } from '../src/ui/modals/NodeModelModal.ts';
import { GlobalDefaultsModal } from '../src/ui/GlobalDefaultsModal.ts';
import { Inspector } from '../src/ui/Inspector.ts';
import { TemplateModal } from '../src/ui/TemplateModal.ts';
import { LibraryRegistry } from '../src/core/libraries/LibraryRegistry.ts';
import { LibraryService } from '../src/core/libraries/LibraryService.ts';
import { LibraryAuditGate } from '../src/core/libraries/LibraryAuditGate.ts';
import { WorkspaceManager } from '../src/core/workspace/WorkspaceManager.ts';
import { AgentTemplateRegistry } from '../src/core/templates/AgentTemplateRegistry.ts';
import { INodeData } from '../src/core/types.ts';

class SimpleElement {
  tagName: string;
  id = '';
  className = '';
  value = '';
  checked = false;
  disabled = false;
  style: Record<string, string> = {};
  dataset: Record<string, string> = {};
  children: SimpleElement[] = [];
  parentNode: SimpleElement | null = null;
  attributes: Record<string, string> = {};
  offsetLeft = 0;
  offsetTop = 0;
  offsetWidth = 360;
  offsetHeight = 600;
  private _html = '';
  private _listeners: Record<string, Function[]> = {};

  constructor(tagName = 'DIV') {
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
      toggle: (cls: string, force?: boolean) => {
        const has = this.className.split(/\s+/).includes(cls);
        const next = force !== undefined ? force : !has;
        if (next) this.classList.add(cls);
        else this.classList.remove(cls);
      },
      contains: (cls: string) => this.className.split(/\s+/).includes(cls)
    };
  }

  get options(): SimpleElement[] {
    return this.children.filter(c => c.tagName === 'OPTION');
  }

  get innerHTML(): string {
    return this._html;
  }

  set innerHTML(val: string) {
    this._html = val;
    this.children = [];
    parseSimpleHtml(val, this);
  }

  appendChild(child: SimpleElement) {
    child.parentNode = this;
    this.children.push(child);
    return child;
  }

  remove() {
    if (this.parentNode) {
      this.parentNode.children = this.parentNode.children.filter(c => c !== this);
      this.parentNode = null;
    }
  }

  setAttribute(k: string, v: string) {
    this.attributes[k] = v;
    if (k === 'id') this.id = v;
    if (k === 'class') this.className = v;
    if (k === 'value') this.value = v;
    if (k.startsWith('data-')) {
      const camel = k.slice(5).replace(/-([a-z])/g, (_, ch) => ch.toUpperCase());
      this.dataset[camel] = v;
    }
  }

  getAttribute(k: string): string | null {
    return this.attributes[k] ?? null;
  }

  addEventListener(event: string, fn: Function) {
    this._listeners[event] = this._listeners[event] || [];
    this._listeners[event].push(fn);
  }

  removeEventListener(event: string, fn: Function) {
    if (!this._listeners[event]) return;
    this._listeners[event] = this._listeners[event].filter(f => f !== fn);
  }

  dispatchEvent(ev: any) {
    const type = ev?.type || 'click';
    (this._listeners[type] || []).forEach(fn => fn({ target: this, currentTarget: this, ...ev }));
  }

  click() {
    this.dispatchEvent({ type: 'click' });
  }

  closest(sel: string): SimpleElement | null {
    let cur: SimpleElement | null = this;
    while (cur) {
      if (matchesSimpleSelector(cur, sel)) return cur;
      cur = cur.parentNode;
    }
    return null;
  }

  querySelector(sel: string): SimpleElement | null {
    return this.querySelectorAll(sel)[0] || null;
  }

  querySelectorAll(sel: string): SimpleElement[] {
    const results: SimpleElement[] = [];
    const visit = (node: SimpleElement) => {
      for (const child of node.children) {
        if (matchesSimpleSelector(child, sel)) results.push(child);
        visit(child);
      }
    };
    visit(this);
    return results;
  }
}

function matchesSimpleSelector(el: SimpleElement, sel: string): boolean {
  return sel.split(',').some(part => {
    const s = part.trim();
    if (!s) return false;
    if (s.startsWith('#')) return el.id === s.slice(1);
    if (s.startsWith('.')) return el.classList.contains(s.slice(1).split('.')[0]);
    const attrMatch = /^\[([a-z0-9_-]+)(?:="([^"]*)")?\]$/i.exec(s);
    if (attrMatch) {
      const attrVal = el.attributes[attrMatch[1]];
      return attrMatch[2] !== undefined ? attrVal === attrMatch[2] : attrVal !== undefined;
    }
    return el.tagName.toLowerCase() === s.toLowerCase();
  });
}

function parseSimpleHtml(html: string, root: SimpleElement): void {
  const stack: SimpleElement[] = [root];
  const tokenRx = /<\/?([a-zA-Z0-9-]+)([^>]*)>/g;
  let match: RegExpExecArray | null;
  while ((match = tokenRx.exec(html)) !== null) {
    const full = match[0];
    const tag = match[1];
    const attrs = match[2] || '';
    if (full.startsWith('</')) {
      if (stack.length > 1) stack.pop();
      continue;
    }
    const el = new SimpleElement(tag);
    const attrRx = /([a-zA-Z0-9_-]+)(?:="([^"]*)")?/g;
    let a: RegExpExecArray | null;
    while ((a = attrRx.exec(attrs)) !== null) {
      const name = a[1];
      const val = a[2] ?? '';
      el.setAttribute(name, val);
      if (name === 'checked') el.checked = true;
      if (name === 'disabled') el.disabled = true;
    }
    stack[stack.length - 1].appendChild(el);
    const selfClosing = full.endsWith('/>') || ['input', 'img', 'br', 'hr'].includes(tag.toLowerCase());
    if (!selfClosing) stack.push(el);
  }
}

describe('UI Updates: ReasoningEffort, Bundled Origin, Restore & 2-Level Taxonomy', () => {
  let originalDoc: any;
  let originalWin: any;
  let bus: EventBus;
  let graph: Graph;

  beforeEach(async () => {
    originalDoc = (globalThis as any).document;
    originalWin = (globalThis as any).window;
    const body = new SimpleElement('BODY');
    (globalThis as any).document = {
      body,
      createElement: (tag: string) => new SimpleElement(tag),
      querySelector: (sel: string) => body.querySelector(sel),
      querySelectorAll: (sel: string) => body.querySelectorAll(sel)
    };
    (globalThis as any).window = {
      innerWidth: 1280,
      innerHeight: 800,
      confirm: () => true,
      addEventListener: () => {},
      removeEventListener: () => {}
    };
    bus = new EventBus();
    graph = new Graph(bus, { graphName: 'UITest', projectId: 'ui-test' });
    const registry = LibraryRegistry.getInstance(bus);
    registry.resetForTesting(bus);
    const workspace = WorkspaceManager.getInstance(bus);
    workspace.resetForTesting(bus);
    const service = new LibraryService(
      registry,
      workspace,
      LibraryAuditGate.getInstance(),
      { load: async () => null, save: async () => true } as any
    );
    await service.importBuiltinSkills();
  });

  afterEach(() => {
    (globalThis as any).document = originalDoc;
    (globalThis as any).window = originalWin;
  });

  it('NodeModelModal renderiza e salva reasoningEffort respeitando inheritModel', async () => {
    const node: INodeData = {
      id: 'agent_effort_1',
      label: 'Agente Effort',
      type: 'agent',
      position: { x: 0, y: 0 },
      inputs: [{ id: 'in_0', name: 'in', direction: 'input', type: 'flow' }],
      outputs: [{ id: 'out_0', name: 'out', direction: 'output', type: 'flow' }],
      parameters: {},
      agentSettings: {
        inheritSandbox: true,
        inheritModel: false,
        inheritCodeMode: true,
        model: {
          provider: 'anthropic',
          model: 'claude-sonnet-4-5',
          reasoningEffort: 'high',
          temperature: 0.3
        }
      }
    };
    graph.addNode(node);

    const modal = new NodeModelModal(graph);
    modal.open(node.id, 'parameters');
    const dialog = (modal as any).dialog as SimpleElement;
    const select = dialog.querySelector('#node-reasoning-effort') as SimpleElement;
    expect(select).not.toBeNull();
    expect(dialog.innerHTML).toContain('value="none"');
    expect(dialog.innerHTML).toContain('value="low"');
    expect(dialog.innerHTML).toContain('value="medium"');
    expect(dialog.innerHTML).toContain('value="high"');
    expect(dialog.innerHTML).toContain('value="max"');

    select.value = 'max';
    await (modal as any).saveNodeModel(node, false);
    expect(graph.getNode(node.id)?.agentSettings?.model?.reasoningEffort).toBe('max');

    // Quando inheritModel = true, model override deve ficar undefined
    await (modal as any).saveNodeModel(graph.getNode(node.id)!, true);
    expect(graph.getNode(node.id)?.agentSettings?.inheritModel).toBe(true);
    expect(graph.getNode(node.id)?.agentSettings?.model).toBeUndefined();
  });

  it('GlobalDefaultsModal renderiza e salva reasoningEffort padrão do projeto', async () => {
    const modal = new GlobalDefaultsModal(graph);
    modal.show('model');
    await Promise.resolve();
    const overlay = (modal as any).overlay as SimpleElement;
    const select = overlay.querySelector('#def-reasoning-effort') as SimpleElement;
    expect(select).not.toBeNull();
    select.value = 'high';
    await (modal as any).saveValues();
    expect(graph.getGlobalDefaults().model.reasoningEffort).toBe('high');
  });

  it('Inspector preserva origin/presetId/templateId/librarySource em derivePresetAgent, exibe Nativo Arestrix e restaura padrão via UpdateNodeCommand', () => {
    const parent = new SimpleElement('DIV') as any;
    const inspector = new Inspector(parent, graph, bus);
    const registry = AgentTemplateRegistry.getInstance();
    registry.registerTemplate({
      id: 'bundled_test_tpl',
      name: 'Bundled Reviewer',
      category: 'Agentes',
      icon: '🛠️',
      taskType: 'agent',
      description: 'Agent bundled',
      inputs: [{ name: 'in', type: 'flow' }],
      outputs: [{ name: 'out', type: 'flow' }],
      parameters: {},
      bundledInProduct: true,
      origin: {
        bundledInProduct: true,
        sourceKind: 'builtin',
        libraryId: 'library-arestrix',
        assetId: 'asset-bundled-test',
        templateId: 'bundled_test_tpl',
        repo: 'EveryInc/compound-engineering-plugin',
        url: 'https://github.com/EveryInc/compound-engineering-plugin',
        license: 'MIT',
        installVersion: '2.10.0',
        installHash: 'abc123'
      },
      agentSettings: {
        inheritSandbox: false,
        inheritModel: false,
        inheritCodeMode: false,
        model: { provider: 'anthropic', model: 'claude-sonnet-4-5', reasoningEffort: 'high', temperature: 0.2 }
      },
      generateLambdaBody: () => ''
    });

    const node: INodeData = {
      id: 'agent_bundled_1',
      label: 'Bundled Reviewer',
      type: 'agent',
      position: { x: 10, y: 20 },
      inputs: [{ id: 'in_0', name: 'in', direction: 'input', type: 'flow' }],
      outputs: [{ id: 'out_0', name: 'out', direction: 'output', type: 'flow' }],
      parameters: {},
      presetId: 'bundled_test_tpl',
      templateId: 'bundled_test_tpl',
      presetLocked: true,
      librarySource: {
        libraryId: 'library-arestrix',
        assetId: 'asset-bundled-test',
        hash: 'abc123',
        version: '2.10.0',
        bundledInProduct: true,
        originRepo: 'EveryInc/compound-engineering-plugin',
        originUrl: 'https://github.com/EveryInc/compound-engineering-plugin',
        originLicense: 'MIT'
      },
      origin: {
        bundledInProduct: true,
        sourceKind: 'builtin',
        libraryId: 'library-arestrix',
        assetId: 'asset-bundled-test',
        templateId: 'bundled_test_tpl',
        repo: 'EveryInc/compound-engineering-plugin',
        url: 'https://github.com/EveryInc/compound-engineering-plugin',
        license: 'MIT',
        installVersion: '2.10.0',
        installHash: 'abc123'
      },
      agentSettings: {
        inheritSandbox: false,
        inheritModel: false,
        inheritCodeMode: false,
        model: { provider: 'openai', model: 'gpt-4o', reasoningEffort: 'low', temperature: 0.9 }
      }
    };
    graph.addNode(node);

    // Renderiza no Inspector
    (inspector as any).renderNode(node);
    const html = (inspector as any).container.innerHTML as string;
    expect(html).toContain('Nativo Arestrix');
    expect(html).toContain('EveryInc/compound-engineering-plugin');
    expect(html).toContain('MIT');
    expect(html).toContain('href="https://github.com/EveryInc/compound-engineering-plugin"');
    expect(html).toContain('id="btn-restore-install-default"');

    // Deriva o preset desbloqueando mas preservando presetId, templateId, librarySource e origin
    (inspector as any).derivePresetAgent(node);
    const derived = graph.getAllNodes().find(n => n.id !== node.id)!;
    expect(derived.presetLocked).toBe(false);
    expect(derived.presetId).toBe('bundled_test_tpl');
    expect(derived.templateId).toBe('bundled_test_tpl');
    expect(derived.librarySource?.assetId).toBe('asset-bundled-test');
    expect(derived.origin?.repo).toBe('EveryInc/compound-engineering-plugin');

    // Restaura padrão de instalação no nó modificado e verifica Undo
    (inspector as any).restoreInstallDefault(node);
    const restored = graph.getNode(node.id)!;
    expect(restored.agentSettings?.model?.reasoningEffort).toBe('high');
    expect(restored.agentSettings?.model?.model).toBe('claude-sonnet-4-5');

    graph.commandManager.undo();
    expect(graph.getNode(node.id)?.agentSettings?.model?.reasoningEffort).toBe('low');
  });

  it('TemplateModal suporta busca, taxonomia de 2 níveis e anexa librarySource e origin ao inserir template bundled da grade', () => {
    const registry = AgentTemplateRegistry.getInstance();
    registry.registerTemplate({
      id: 'grid_bundled_tpl',
      name: 'Grid Bundled Agent',
      category: 'Agentes',
      icon: '🧠',
      taskType: 'agent',
      description: 'Template nativo na grade',
      domain: 'Engenharia de Software',
      subdomain: 'Arquitetura & Backend',
      bundledInProduct: true,
      origin: {
        bundledInProduct: true,
        sourceKind: 'builtin',
        libraryId: 'library-arestrix',
        assetId: 'asset-grid-bundled',
        templateId: 'grid_bundled_tpl',
        repo: 'EveryInc/compound-engineering-plugin',
        url: 'https://github.com/EveryInc/compound-engineering-plugin',
        license: 'MIT',
        installVersion: '1.2.0',
        installHash: 'hash123'
      },
      inputs: [{ name: 'in', type: 'flow' }],
      outputs: [{ name: 'out', type: 'flow' }],
      parameters: {},
      generateLambdaBody: () => ''
    });

    const viewport = {
      getCameraCenter: () => ({ x: 100, y: 200 }),
      selectNode: () => {}
    } as any;
    const modal = new TemplateModal(graph, viewport);
    modal.show();

    const overlay = (modal as any).overlay as SimpleElement;
    expect(overlay.innerHTML).toContain('id="template-search-input"');
    expect(overlay.innerHTML).toContain('id="template-domain-select"');
    expect(overlay.innerHTML).toContain('id="template-subdomain-select"');
    expect(overlay.innerHTML).toContain('id="btn-restore-builtin-templates"');
    expect(overlay.innerHTML).toContain('Nativo Arestrix');

    (modal as any).insertTemplateNode('grid_bundled_tpl');
    const created = graph.getAllNodes().find(n => n.templateId === 'grid_bundled_tpl');
    expect(created).toBeDefined();
    expect(created?.librarySource?.bundledInProduct).toBe(true);
    expect(created?.librarySource?.originRepo).toBe('EveryInc/compound-engineering-plugin');
    expect(created?.origin?.bundledInProduct).toBe(true);
    expect(created?.origin?.license).toBe('MIT');
  });
});
