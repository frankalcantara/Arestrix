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

    beforeEach(() => {
      originalDoc = (globalThis as any).document;
      originalWin = (globalThis as any).window;

      const elements = new Map<string, any>();
      (globalThis as any).document = {
        createElement: (tag: string) => {
          const el: any = {
            tagName: tag.toUpperCase(),
            className: '',
            id: '',
            style: {},
            children: [] as any[],
            classList: {
              add: (cls: string) => {
                if (!el.className.split(' ').includes(cls)) {
                  el.className = (el.className + ' ' + cls).trim();
                }
              },
              remove: (cls: string) => {
                el.className = el.className.split(' ').filter((c: string) => c !== cls).join(' ');
              },
              contains: (cls: string) => el.className.split(' ').includes(cls)
            },
            _html: '',
            set innerHTML(val: string) { this._html = val; },
            get innerHTML() { return this._html; },
            _listeners: {} as Record<string, Function[]>,
            addEventListener(ev: string, fn: Function) {
              this._listeners[ev] = this._listeners[ev] || [];
              this._listeners[ev].push(fn);
            },
            removeEventListener: () => {},
            dispatchEvent(e: any) {
              (this._listeners[e?.type || 'click'] || []).forEach(fn => fn(e));
            },
            click() { this.dispatchEvent({ type: 'click' }); },
            appendChild(child: any) {
              child.parentNode = this;
              this.children.push(child);
              return child;
            },
            removeChild(child: any) {
              const idx = this.children.indexOf(child);
              if (idx !== -1) this.children.splice(idx, 1);
              child.parentNode = null;
              return child;
            },
            getBoundingClientRect: () => ({ left: 100, top: 100, width: 800, height: 600, right: 900, bottom: 700 }),
            closest: (sel: string) => {
              let cur: any = el;
              while (cur) {
                if (cur.className && (cur.className.includes('modal-overlay') || cur.className.includes('modal-backdrop'))) {
                  return cur;
                }
                cur = cur.parentNode;
              }
              return null;
            },
            querySelector: (sel: string) => {
              if (sel.startsWith('#')) {
                const id = sel.slice(1);
                return elements.get(id) || null;
              }
              return null;
            },
            querySelectorAll: () => []
          };
          return el;
        },
        body: {
          appendChild: () => {},
          removeChild: () => {}
        }
      };

      (globalThis as any).window = {
        innerWidth: 1280,
        innerHeight: 800,
        addEventListener: () => {},
        removeEventListener: () => {}
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
      expect(html).toContain('✎ Editar Skill');
      expect(html).toContain('＋ Novo Skill');
    });
  });
});
