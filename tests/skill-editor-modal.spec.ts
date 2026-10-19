import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import {
  SkillEditorModal,
  extractFrontmatter,
  renderMarkdown
} from '../src/ui/modals/SkillEditorModal.ts';
import { NodeModelModal } from '../src/ui/modals/NodeModelModal.ts';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';

describe('SkillEditorModal & Integração com NodeModelModal (Fase 15.2)', () => {
  describe('1. Validação de Frontmatter e Renderizador Markdown', () => {
    it('deve extrair e validar frontmatter com campos name e description', () => {
      const validMd = `---
name: PesquisaArxiv
description: Busca artigos científicos no arXiv e sintetiza conclusões.
---

# Conteúdo do Skill
`;
      const res = extractFrontmatter(validMd);
      expect(res.valid).toBe(true);
      expect(res.hasFrontmatter).toBe(true);
      expect(res.name).toBe('PesquisaArxiv');
      expect(res.description).toContain('Busca artigos');
    });

    it('deve rejeitar frontmatter incompleto sem o campo name', () => {
      const incompleteMd = `---
description: Apenas descrição sem nome
---

# Conteúdo
`;
      const res = extractFrontmatter(incompleteMd);
      expect(res.valid).toBe(false);
      expect(res.hasFrontmatter).toBe(true);
      expect(res.name).toBeUndefined();
    });

    it('deve renderizar Markdown estruturado com escape seguro de HTML', () => {
      const md = `
# Título Principal
## Subtítulo
Este é um texto com **negrito** e *itálico* e \`código inline\`.

- Item A
- Item B

\`\`\`typescript
const x = 42;
\`\`\`
<script>alert('xss')</script>
`;
      const html = renderMarkdown(md);

      expect(html).toContain('<h1 class="md-h1">Título Principal</h1>');
      expect(html).toContain('<h2 class="md-h2">Subtítulo</h2>');
      expect(html).toContain('<strong>negrito</strong>');
      expect(html).toContain('<em>itálico</em>');
      expect(html).toContain('<code class="md-inline-code">código inline</code>');
      expect(html).toContain('<pre class="md-code-block">');
      expect(html).toContain('<li class="md-li">Item A</li>');
      // Script tags devem estar sanitizadas/escapadas
      expect(html).not.toContain('<script>');
      expect(html).toContain('&lt;script&gt;');
    });
  });

  describe('2. Ciclo de Vida do SkillEditorModal e Integração', () => {
    let originalDoc: any;
    let originalWin: any;
    let windowListeners: Record<string, Function[]> = {};

    class MockElement {
      tagName: string;
      className: string = '';
      id: string = '';
      type: string = '';
      value: string = '';
      title: string = '';
      checked: boolean = false;
      textContent: string = '';
      style: Record<string, string> = {};
      children: MockElement[] = [];
      parentNode: MockElement | null = null;
      attributes: Record<string, string> = {};
      offsetLeft: number = 100;
      offsetTop: number = 100;
      offsetWidth: number = 800;
      offsetHeight: number = 600;
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

      removeEventListener(event: string, fn: Function) {
        if (this._listeners[event]) {
          this._listeners[event] = this._listeners[event].filter(f => f !== fn);
        }
      }

      dispatchEvent(e: any) {
        (this._listeners[e?.type || 'click'] || []).forEach(fn => fn(e));
      }

      click() {
        this.dispatchEvent({ type: 'click', preventDefault: () => {}, stopPropagation: () => {} });
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

      getBoundingClientRect() {
        return { left: 100, top: 100, width: 800, height: 600, right: 900, bottom: 700 };
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
              if (name === 'value') el.value = v;
            }
          }
          const parent = stack[stack.length - 1];
          parent.appendChild(el);
          const selfClosing = ['input', 'img', 'br', 'hr'].includes(tagName.toLowerCase());
          if (!selfClosing) stack.push(el);
        }
      }
    }

    beforeEach(() => {
      originalDoc = (globalThis as any).document;
      originalWin = (globalThis as any).window;
      windowListeners = {};

      const mockBody = new MockElement('BODY');

      (globalThis as any).document = {
        createElement: (tag: string) => new MockElement(tag),
        body: mockBody,
        querySelector: (sel: string) => mockBody.querySelector(sel),
        querySelectorAll: (sel: string) => mockBody.querySelectorAll(sel)
      };

      (globalThis as any).window = {
        innerWidth: 1280,
        innerHeight: 800,
        addEventListener: (ev: string, fn: Function) => {
          windowListeners[ev] = windowListeners[ev] || [];
          windowListeners[ev].push(fn);
        },
        removeEventListener: (ev: string, fn: Function) => {
          if (windowListeners[ev]) {
            windowListeners[ev] = windowListeners[ev].filter(f => f !== fn);
          }
        },
        dispatchEvent: (e: any) => {
          (windowListeners[e?.type || 'click'] || []).forEach(fn => fn(e));
        }
      };
    });

    afterEach(() => {
      (globalThis as any).document = originalDoc;
      (globalThis as any).window = originalWin;
    });

    it('deve inicializar SkillEditorModal com opções de caminho e modo somente leitura', () => {
      const modal = new SkillEditorModal();
      expect(modal).toBeDefined();

      let saved = false;
      modal.openSkill({
        path: '.agents/pesquisa/SKILL.md',
        initialContent: '# Teste Inicial',
        nodeLabel: 'Pesquisador',
        readOnly: true,
        onSave: () => { saved = true; }
      });

      expect((modal as any).currentPath).toBe('.agents/pesquisa/SKILL.md');
      expect((modal as any).isReadOnly).toBe(true);
      expect((modal as any).currentContent).toBe('# Teste Inicial');

      modal.destroy();
    });

    it('deve abrir ocupando toda a área da tela por padrão e permitir alterar o tamanho / restaurar', () => {
      const modal = new SkillEditorModal();
      modal.openSkill({
        path: '.agents/escrita/SKILL.md',
        initialContent: '# Escrita Criativa'
      });

      const controller = modal.getController();
      expect(controller).toBeDefined();
      expect(controller?.isMaximized()).toBe(true);

      const dialog = (modal as any).dialog as MockElement;
      expect(dialog.classList.contains('is-maximized')).toBe(true);

      // Permite restaurar e alterar o tamanho
      controller?.restore();
      expect(controller?.isMaximized()).toBe(false);
      expect(dialog.classList.contains('is-maximized')).toBe(false);

      modal.destroy();
    });

    it('deve salvar o skill e fechar o modal ao clicar no botão salvar', async () => {
      const modal = new SkillEditorModal();
      let savedPath = '';
      let savedContent = '';

      modal.openSkill({
        path: '.agents/assistente/SKILL.md',
        initialContent: '# Assistente V1',
        onSave: (path, content) => {
          savedPath = path;
          savedContent = content;
        }
      });

      expect((modal as any).isOpen).toBe(true);

      const dialog = (modal as any).dialog as MockElement;
      const saveBtn = dialog.querySelector('#btn-save-skill-editor') as MockElement;
      expect(saveBtn).not.toBeNull();

      // Altera o conteúdo no textarea
      const textarea = dialog.querySelector('#skill-editor-textarea') as MockElement;
      if (textarea) textarea.value = '# Assistente V2 Atualizado';

      saveBtn.click();

      expect(savedPath).toBe('.agents/assistente/SKILL.md');
      expect(savedContent).toBe('# Assistente V2 Atualizado');
      // Modal DEVE estar fechado
      expect((modal as any).isOpen).toBe(false);

      modal.destroy();
    });

    it('deve salvar o skill e fechar o modal ao pressionar a tecla de atalho Ctrl+S', async () => {
      const modal = new SkillEditorModal();
      let savedPath = '';
      let savedContent = '';

      modal.openSkill({
        path: '.agents/analista/SKILL.md',
        initialContent: '# Analista V1',
        onSave: (path, content) => {
          savedPath = path;
          savedContent = content;
        }
      });

      expect((modal as any).isOpen).toBe(true);

      const dialog = (modal as any).dialog as MockElement;
      const textarea = dialog.querySelector('#skill-editor-textarea') as MockElement;
      if (textarea) textarea.value = '# Analista V2 via Ctrl+S';

      // Simula evento Ctrl+S na janela
      const keyEvent = {
        type: 'keydown',
        key: 's',
        ctrlKey: true,
        preventDefault: vi.fn(),
        stopPropagation: vi.fn()
      };
      (globalThis as any).window.dispatchEvent(keyEvent);

      expect(keyEvent.preventDefault).toHaveBeenCalled();
      expect(savedPath).toBe('.agents/analista/SKILL.md');
      expect(savedContent).toBe('# Analista V2 via Ctrl+S');
      // Modal DEVE estar fechado
      expect((modal as any).isOpen).toBe(false);

      modal.destroy();
    });

    it('deve disponibilizar botões de Editar e Novo Skill no NodeModelModal', () => {
      const bus = new EventBus();
      const graph = new Graph(bus, { graphName: 'Test', projectId: 'test' });
      const nodeData = {
        id: 'node_test_redator',
        type: 'agent' as const,
        label: 'Redator Especialista',
        position: { x: 0, y: 0 },
        agentSettings: {
          inheritModel: false,
          model: {
            provider: 'anthropic',
            model: 'claude-opus-5.5',
            skillPath: '.agents/redator/SKILL.md'
          }
        }
      };
      graph.addNode(nodeData);

      const modelModal = new NodeModelModal(graph);
      modelModal.open(nodeData.id);

      const overlay = (modelModal as any).overlay;
      expect(overlay).toBeDefined();

      // O HTML gerado deve conter os botões de ação do skill
      const html = overlay.innerHTML;
      expect(html).toContain('id="btn-edit-skill-modal"');
      expect(html).toContain('id="btn-new-skill-modal"');
      expect(html).toContain('Editar Skill');
      expect(html).toContain('Novo Skill');
      // Endpoint de servidor local disponível no vértice (quando não herda o modelo global)
      expect(html).toContain('id="node-local-base-url"');
      expect(html).toContain('id="btn-node-discover-models"');
    });
  });
});
