import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { EventBus, globalEventBus } from '../src/core/EventBus.ts';
import { LibraryRegistry } from '../src/core/libraries/LibraryRegistry.ts';
import { LibraryService } from '../src/core/libraries/LibraryService.ts';
import { LibraryAuditGate } from '../src/core/libraries/LibraryAuditGate.ts';
import { WorkspaceManager } from '../src/core/workspace/WorkspaceManager.ts';
import { SkillLedgerService } from '../src/core/skills/SkillLedgerService.ts';
import { McpLedgerService } from '../src/core/skills/McpLedgerService.ts';
import { LibraryAssetEditorModal } from '../src/ui/modals/LibraryAssetEditorModal.ts';
import {
  bindLibraryBrowser,
  renderLibraryBrowser,
  resetLibraryAssetEditorModalForTesting
} from '../src/ui/libraries/LibraryTree.ts';

const fakeStore = {
  load: async () => null,
  save: async () => true
} as any;

/**
 * DOM mínimo em memória para exercitar BaseModal, LibraryAssetEditorModal,
 * SkillEditorModal empilhado e eventos delegados do LibraryTree em Node/Vitest.
 */
class FakeElement {
  tagName: string;
  id: string = '';
  className: string = '';
  type: string = '';
  name: string = '';
  value: string = '';
  checked: boolean = false;
  disabled: boolean = false;
  title: string = '';
  textContent: string = '';
  style: Record<string, string> = {};
  dataset: Record<string, string> = {};
  attributes: Record<string, string> = {};
  children: FakeElement[] = [];
  parentNode: FakeElement | null = null;
  offsetLeft = 100;
  offsetTop = 100;
  offsetWidth = 860;
  offsetHeight = 680;
  scrollTop = 0;
  private _html = '';
  private _listeners: Record<string, Array<(e: any) => void>> = {};

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
        const shouldHave = force !== undefined ? force : !has;
        if (shouldHave) this.classList.add(cls);
        else this.classList.remove(cls);
        return shouldHave;
      },
      contains: (cls: string) => this.className.split(/\s+/).includes(cls)
    };
  }

  get innerHTML(): string {
    return this._html;
  }

  set innerHTML(val: string) {
    this._html = val;
    this.children = [];
    this.parseBasicNodes(val);
  }

  private parseBasicNodes(html: string): void {
    const tagRegex = /<(button|input|textarea|select|div|span|section|aside|header|a|label)\b([^>]*)>([\s\S]*?)<\/\1>|<(input)\b([^>]*)\/?\s*>/gi;
    let m: RegExpExecArray | null;
    while ((m = tagRegex.exec(html)) !== null) {
      const tag = (m[1] || m[4] || 'div').toUpperCase();
      const attrs = m[2] || m[5] || '';
      const inner = m[3] || '';
      const el = new FakeElement(tag);
      el.parentNode = this;

      const attrRegex = /([a-zA-Z0-9_-]+)(?:="([^"]*)")?/g;
      let am: RegExpExecArray | null;
      while ((am = attrRegex.exec(attrs)) !== null) {
        const key = am[1];
        const val = am[2] ?? '';
        el.attributes[key] = val;
        if (key === 'id') el.id = val;
        else if (key === 'class') el.className = val;
        else if (key === 'type') el.type = val;
        else if (key === 'name') el.name = val;
        else if (key === 'value') el.value = decodeHtml(val);
        else if (key === 'checked') el.checked = true;
        else if (key === 'disabled') el.disabled = true;
        else if (key.startsWith('data-')) {
          const camel = key.slice(5).replace(/-([a-z])/g, (_, ch) => ch.toUpperCase());
          el.dataset[camel] = decodeHtml(val);
        }
      }

      if (tag === 'TEXTAREA') {
        el.value = decodeHtml(inner);
      } else if (inner) {
        el.textContent = inner.replace(/<[^>]+>/g, '').trim();
        el._html = inner;
        if (inner.includes('<')) {
          el.parseBasicNodes(inner);
        }
      }

      this.children.push(el);
    }
  }

  get firstElementChild(): FakeElement | null {
    return this.children[0] || null;
  }

  appendChild(child: FakeElement): FakeElement {
    child.parentNode = this;
    this.children.push(child);
    return child;
  }

  removeChild(child: FakeElement): FakeElement {
    this.children = this.children.filter(c => c !== child);
    child.parentNode = null;
    return child;
  }

  remove(): void {
    this.parentNode?.removeChild(this);
  }

  replaceWith(next: FakeElement): void {
    if (!this.parentNode) return;
    const idx = this.parentNode.children.indexOf(this);
    if (idx !== -1) {
      next.parentNode = this.parentNode;
      this.parentNode.children[idx] = next;
    }
  }

  setAttribute(name: string, val: string): void {
    this.attributes[name] = val;
    if (name === 'id') this.id = val;
    if (name === 'class') this.className = val;
    if (name.startsWith('data-')) {
      const camel = name.slice(5).replace(/-([a-z])/g, (_, ch) => ch.toUpperCase());
      this.dataset[camel] = val;
    }
  }

  getAttribute(name: string): string | null {
    return name in this.attributes ? this.attributes[name] : null;
  }

  addEventListener(type: string, fn: (e: any) => void): void {
    if (!this._listeners[type]) this._listeners[type] = [];
    this._listeners[type].push(fn);
  }

  removeEventListener(type: string, fn: (e: any) => void): void {
    if (!this._listeners[type]) return;
    this._listeners[type] = this._listeners[type].filter(cb => cb !== fn);
  }

  dispatchEvent(ev: any): void {
    const event = {
      target: this,
      currentTarget: this,
      preventDefault: () => {},
      stopPropagation: () => {},
      ...ev
    };
    const list = [...(this._listeners[event.type] || [])];
    for (const fn of list) fn(event);
    if (event.bubbles !== false && this.parentNode) {
      this.parentNode.dispatchEvent({ ...event, target: event.target, currentTarget: this.parentNode });
    }
  }

  click(): void {
    this.dispatchEvent({ type: 'click', bubbles: true });
  }

  matches(selector: string): boolean {
    const parts = selector.split(',').map(s => s.trim());
    return parts.some(sel => this.matchesSingle(sel));
  }

  private matchesSingle(sel: string): boolean {
    if (sel.includes(' ')) {
      const tokens = sel.split(/\s+/);
      const last = tokens[tokens.length - 1];
      if (!this.matchesSingle(last)) return false;
      let cur = this.parentNode;
      const prefix = tokens.slice(0, -1).join(' ');
      while (cur) {
        if (cur.matchesSingle(prefix)) return true;
        cur = cur.parentNode;
      }
      return false;
    }

    if (sel.endsWith(':checked')) {
      if (!this.checked) return false;
      sel = sel.slice(0, -':checked'.length);
    }

    const attrMatch = sel.match(/\[([^\]=]+)(?:="([^"]*)")?\]/);
    if (attrMatch) {
      const attrName = attrMatch[1];
      const expected = attrMatch[2];
      if (!(attrName in this.attributes)) return false;
      if (expected !== undefined && this.attributes[attrName] !== expected) return false;
      sel = sel.replace(attrMatch[0], '');
    }

    if (!sel) return true;
    if (sel.startsWith('#')) return this.id === sel.slice(1);
    if (sel.startsWith('.')) {
      const classes = sel.split('.').filter(Boolean);
      return classes.every(c => this.classList.contains(c));
    }
    if (sel.includes('.')) {
      const [tag, ...classes] = sel.split('.');
      return this.tagName === tag.toUpperCase() && classes.every(c => this.classList.contains(c));
    }
    return this.tagName === sel.toUpperCase();
  }

  closest(selector: string): FakeElement | null {
    let cur: FakeElement | null = this;
    while (cur) {
      if (cur.matches(selector)) return cur;
      cur = cur.parentNode;
    }
    return null;
  }

  querySelectorAll(selector: string): FakeElement[] {
    const out: FakeElement[] = [];
    const walk = (node: FakeElement) => {
      for (const child of node.children) {
        if (child.matches(selector)) out.push(child);
        walk(child);
      }
    };
    walk(this);
    return out;
  }

  querySelector(selector: string): FakeElement | null {
    return this.querySelectorAll(selector)[0] || null;
  }

  getBoundingClientRect() {
    return { left: 0, top: 0, width: this.offsetWidth, height: this.offsetHeight };
  }

  focus() {}
  setSelectionRange() {}
}

function decodeHtml(str: string): string {
  return str
    .replace(/&lt;/g, '<')
    .replace(/&gt;/g, '>')
    .replace(/&quot;/g, '"')
    .replace(/&#039;/g, "'")
    .replace(/&amp;/g, '&');
}

describe('LibraryAssetEditorModal & Biblioteca de Ativos (Edição em Modal Office)', () => {
  let originalDocument: any;
  let originalWindow: any;
  let windowListeners: Record<string, Array<(e: any) => void>>;
  let body: FakeElement;
  let registry: LibraryRegistry;
  let service: LibraryService;

  const dispatchWindowKeydown = (key: string, extra: Record<string, any> = {}) => {
    let stoppedImmediate = false;
    const event = {
      key,
      preventDefault: () => {},
      stopPropagation: () => {},
      stopImmediatePropagation: () => { stoppedImmediate = true; },
      ...extra
    };
    const list = [...(windowListeners['keydown'] || [])];
    for (const fn of list) {
      if (stoppedImmediate) break;
      fn(event);
    }
  };

  beforeEach(async () => {
    originalDocument = (globalThis as any).document;
    originalWindow = (globalThis as any).window;
    windowListeners = {};
    body = new FakeElement('BODY');

    (globalThis as any).document = {
      body,
      createElement: (tag: string) => new FakeElement(tag),
      querySelectorAll: (sel: string) => body.querySelectorAll(sel),
      querySelector: (sel: string) => body.querySelector(sel),
      addEventListener: () => {},
      removeEventListener: () => {}
    };

    (globalThis as any).window = {
      innerWidth: 1440,
      innerHeight: 900,
      addEventListener: (type: string, fn: (e: any) => void) => {
        if (!windowListeners[type]) windowListeners[type] = [];
        windowListeners[type].push(fn);
      },
      removeEventListener: (type: string, fn: (e: any) => void) => {
        if (!windowListeners[type]) return;
        windowListeners[type] = windowListeners[type].filter(cb => cb !== fn);
      },
      confirm: () => true,
      requestAnimationFrame: (cb: Function) => cb()
    };

    const bus = new EventBus();
    registry = LibraryRegistry.getInstance(bus);
    registry.resetForTesting(bus);
    const workspace = WorkspaceManager.getInstance(bus);
    workspace.resetForTesting(bus);
    SkillLedgerService.resetForTesting(bus);
    McpLedgerService.resetForTesting(bus);

    service = new LibraryService(
      registry,
      workspace,
      LibraryAuditGate.getInstance(),
      fakeStore
    );
    (LibraryService as any).instance = service;
    await service.importBuiltinSkills();
  });

  afterEach(() => {
    resetLibraryAssetEditorModalForTesting();
    (globalThis as any).document = originalDocument;
    (globalThis as any).window = originalWindow;
  });

  it('abre o modal Office com abas adequadas para skill, mcp, agent-template e pipeline', async () => {
    const modal = new LibraryAssetEditorModal();

    const skill = registry.listAssets('library-arestrix').find(a => a.kind === 'skill')!;
    modal.openAsset({ asset: skill, libraryName: 'Biblioteca Arestrix', isReadOnlySource: true, onSave: async () => ({ ok: true }) });
    expect(modal.isModalOpen()).toBe(true);
    const skillTabs = (modal as any).dialog.querySelectorAll('[data-modal-tab]').map((el: FakeElement) => el.dataset.modalTab);
    expect(skillTabs).toEqual(['general', 'skill']);
    expect((modal as any).dialog.innerHTML).toContain('office-info-callout');

    const mcp = registry.listAssets('library-arestrix').find(a => a.kind === 'mcp')!;
    modal.openAsset({ asset: mcp, libraryName: 'Biblioteca Arestrix', isReadOnlySource: true, onSave: async () => ({ ok: true }) });
    const mcpTabs = (modal as any).dialog.querySelectorAll('[data-modal-tab]').map((el: FakeElement) => el.dataset.modalTab);
    expect(mcpTabs).toEqual(['general', 'mcp', 'mcp-tools']);

    const agent = registry.listAssets('library-arestrix').find(a => a.kind === 'agent-template')!;
    modal.openAsset({ asset: agent, libraryName: 'Biblioteca Arestrix', isReadOnlySource: true, onSave: async () => ({ ok: true }) });
    const agentTabs = (modal as any).dialog.querySelectorAll('[data-modal-tab]').map((el: FakeElement) => el.dataset.modalTab);
    expect(agentTabs).toEqual(['general', 'model', 'skills', 'runtime']);

    const pipeline = await service.saveTemplateToLibrary({
      id: 'pipe_modal_test',
      name: 'Pipeline Modal',
      taskType: 'subflow',
      description: 'Pipeline teste',
      inputs: [{ name: 'in', type: 'flow' }],
      outputs: [{ name: 'out', type: 'flow' }],
      lambdaBodyTemplate: '',
      assetKind: 'pipeline',
      childGraph: {
        version: '1.0.0',
        metadata: { graphName: 'Pipeline Modal' } as any,
        nodes: [],
        edges: []
      }
    }, { scope: 'workspace' });
    modal.openAsset({ asset: pipeline!, libraryName: 'Minha biblioteca', isReadOnlySource: false, onSave: async () => ({ ok: true }) });
    const pipeTabs = (modal as any).dialog.querySelectorAll('[data-modal-tab]').map((el: FakeElement) => el.dataset.modalTab);
    expect(pipeTabs).toEqual(['general', 'pipeline']);

    modal.destroy();
  });

  it('abre o editor Markdown (SkillEditorModal) sobre o modal de edição e Escape fecha apenas o topo da pilha', () => {
    const modal = new LibraryAssetEditorModal();
    const skill = registry.listAssets('library-arestrix').find(a => a.kind === 'skill')!;
    let savedMarkdown = '';

    modal.openAsset({
      asset: skill,
      libraryName: 'Biblioteca Arestrix',
      isReadOnlySource: true,
      onSave: async ({ payload }) => {
        if (payload.kind === 'skill') savedMarkdown = payload.markdownContent;
        return { ok: true };
      }
    });

    // Alterna para a aba Skill & Markdown
    modal.switchTab('skill');
    const openMdBtn = (modal as any).dialog.querySelector('#btn-open-skill-md-editor') as FakeElement;
    expect(openMdBtn).not.toBeNull();
    openMdBtn.click();

    const skillEditor = modal.getSkillEditorForTesting();
    expect(skillEditor).toBeDefined();
    expect(skillEditor!.isModalOpen()).toBe(true);
    expect(modal.isModalOpen()).toBe(true);

    // Z-index do SkillEditorModal deve estar acima do LibraryAssetEditorModal
    const parentZ = Number((modal as any).dialog.style.zIndex || 0);
    const childZ = Number((skillEditor as any).dialog.style.zIndex || 0);
    expect(childZ).toBeGreaterThan(parentZ);

    // Escape fecha APENAS o SkillEditorModal; o modal de edição permanece aberto
    dispatchWindowKeydown('Escape');
    expect(skillEditor!.isModalOpen()).toBe(false);
    expect(modal.isModalOpen()).toBe(true);

    // Reabre o editor Markdown, edita conteúdo e salva de volta no rascunho do modal de edição
    const openMdBtnAgain = (modal as any).dialog.querySelector('#btn-open-skill-md-editor') as FakeElement;
    openMdBtnAgain.click();
    const textarea = (skillEditor as any).dialog.querySelector('#skill-editor-textarea') as FakeElement;
    const updatedMd = '---\nname: Skill Editada\ndescription: Conteúdo editado no modal empilhado\n---\n\n# Nova Diretriz\n';
    textarea.value = updatedMd;
    textarea.dispatchEvent({ type: 'input' });

    const saveMdBtn = (skillEditor as any).dialog.querySelector('#btn-save-skill-editor') as FakeElement;
    saveMdBtn.click();

    // SkillEditorModal fecha e devolve o foco ao modal de edição com o novo Markdown
    expect(skillEditor!.isModalOpen()).toBe(false);
    expect(modal.isModalOpen()).toBe(true);

    // Salva no modal principal de edição
    const saveAssetBtn = (modal as any).dialog.querySelector('#btn-lae-save') as FakeElement;
    saveAssetBtn.click();

    expect(savedMarkdown).toBe(updatedMd);
    modal.destroy();
  });

  it('permite abrir o editor Markdown sobre o modal também para skills de um Template de Agente', () => {
    const modal = new LibraryAssetEditorModal();
    const agent = registry.listAssets('library-arestrix').find(
      a => a.kind === 'agent-template' && a.payload.kind === 'agent-template' && (a.payload.template.agentSettings?.skills?.length || 0) > 0
    )!;
    expect(agent).toBeDefined();

    modal.openAsset({
      asset: agent,
      libraryName: 'Biblioteca Arestrix',
      isReadOnlySource: true,
      onSave: async () => ({ ok: true })
    });

    modal.switchTab('skills');
    const editMdBtn = (modal as any).dialog.querySelector('.lae-edit-template-skill-md') as FakeElement;
    expect(editMdBtn).not.toBeNull();
    editMdBtn.click();

    const skillEditor = modal.getSkillEditorForTesting();
    expect(skillEditor?.isModalOpen()).toBe(true);
    expect(modal.isModalOpen()).toBe(true);

    modal.destroy();
  });

  it('botão Editar na Biblioteca de Ativos abre o modal sem sair do Backstage e sem inserir vértices no grafo', async () => {
    const onReturnToStudio = vi.fn();
    const executeSpy = vi.fn();
    const fakeGraph = {
      getAllNodes: () => [],
      commandManager: { execute: executeSpy }
    } as any;

    const nodeSelectedSpy = vi.fn();
    const openModelModalSpy = vi.fn();
    const unsubSelected = globalEventBus.on('node:selected', nodeSelectedSpy);
    const unsubModel = globalEventBus.on('node:open-model-modal', openModelModalSpy);

    const modal = new LibraryAssetEditorModal();
    const root = new FakeElement('DIV');
    const browserOptions = {
      registry,
      graph: fakeGraph,
      onReturnToStudio,
      assetEditorModal: modal
    };

    root.innerHTML = renderLibraryBrowser(browserOptions);
    bindLibraryBrowser(root as any, browserOptions);

    // Seleciona um ativo MCP da Biblioteca Arestrix
    const mcp = registry.listAssets('library-arestrix').find(a => a.kind === 'mcp')!;
    const selectBtn = new FakeElement('BUTTON');
    selectBtn.setAttribute('data-asset-select', mcp.id);
    selectBtn.setAttribute('data-library-id', 'library-arestrix');
    root.appendChild(selectBtn);
    selectBtn.click();

    // Clica em Editar
    const editBtn = root.querySelector('[data-asset-action="edit"]')!;
    expect(editBtn).not.toBeNull();
    editBtn.click();

    // Aguarda microtask da ação assíncrona
    await Promise.resolve();

    // Modal deve estar aberto, Backstage não fechou, nenhum vértice foi inserido no grafo
    expect(modal.isModalOpen()).toBe(true);
    expect(onReturnToStudio).not.toHaveBeenCalled();
    expect(executeSpy).not.toHaveBeenCalled();
    expect(nodeSelectedSpy).not.toHaveBeenCalled();
    expect(openModelModalSpy).not.toHaveBeenCalled();

    // Cancelar antes de salvar não cria cópia indevida em Minha biblioteca
    modal.hide();
    expect(registry.list('workspace').find(l => l.name === 'Minha biblioteca')).toBeUndefined();

    // Reabre, altera o nome e salva -> cria cópia em Minha biblioteca mantendo original intacto
    const editBtn2 = root.querySelector('[data-asset-action="edit"]')!;
    editBtn2.click();
    await Promise.resolve();

    const nameInput = (modal as any).dialog.querySelector('#lae-name') as FakeElement;
    nameInput.value = 'Meu MCP Customizado';
    nameInput.dispatchEvent({ type: 'input' });

    const saveBtn = (modal as any).dialog.querySelector('#btn-lae-save') as FakeElement;
    saveBtn.click();
    await new Promise(resolve => setTimeout(resolve, 20));

    expect(modal.isModalOpen()).toBe(false);
    expect(onReturnToStudio).not.toHaveBeenCalled();
    expect(executeSpy).not.toHaveBeenCalled();

    const ownLib = registry.list('workspace').find(l => l.name === 'Minha biblioteca');
    expect(ownLib).toBeDefined();
    const copiedAssets = registry.listAssets(ownLib!.id);
    expect(copiedAssets).toHaveLength(1);
    expect(copiedAssets[0].name).toBe('Meu MCP Customizado');
    expect(copiedAssets[0].derivedFrom?.assetId).toBe(mcp.id);

    // Original na Biblioteca Arestrix continua intocado
    expect(registry.getAsset(mcp.id)?.name).toBe(mcp.name);

    unsubSelected();
    unsubModel();
    modal.destroy();
  });

  it('atualiza metadados e sanitiza segredos MCP via LibraryService.updateDerivedAsset', async () => {
    const originalMcp = registry.listAssets('library-arestrix').find(a => a.kind === 'mcp')!;
    const derived = await service.deriveAsset(originalMcp.id);
    expect(derived).not.toBeNull();

    const res = await service.updateDerivedAsset(
      derived!.id,
      {
        kind: 'mcp',
        transport: 'stdio',
        command: 'npx',
        args: ['-y', '@example/mcp@1.0.0'],
        env: { OK_REF: '${MCP_TOKEN}', LITERAL_BAD: 'valor-literal-nao-referencia' }
      },
      {
        name: 'MCP Atualizado',
        description: 'Nova descrição',
        domain: 'Integração',
        subdomain: 'APIs',
        version: '1.2.0',
        license: 'MIT',
        changelog: 'Ajuste de comando e descrição'
      }
    );

    expect(res.ok).toBe(true);
    expect(res.asset?.name).toBe('MCP Atualizado');
    expect(res.asset?.description).toBe('Nova descrição');
    expect(res.asset?.domain).toBe('Integração');
    expect(res.asset?.subdomain).toBe('APIs');
    expect(res.asset?.version).toBe('1.2.0');
    expect(res.asset?.changelog).toBe('Ajuste de comando e descrição');
    if (res.asset?.payload.kind === 'mcp') {
      expect(res.asset.payload.env).toEqual({ OK_REF: '${MCP_TOKEN}' });
    }
  });
});
