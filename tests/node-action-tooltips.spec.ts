import { describe, it, expect, beforeEach, afterEach, beforeAll, afterAll, vi } from 'vitest';
import { NodeView } from '../src/view/NodeView.ts';
import { NODE_ACTIONS, getNodeAction } from '../src/view/NodeActions.ts';
import { CanvasTooltip, renderTooltipHtml } from '../src/ui/CanvasTooltip.ts';
import { INodeData } from '../src/core/types.ts';

describe('Ações do Nó e Tooltips Informativos (Fase 15.2)', () => {
  let sampleNode: INodeData;
  let origDoc: any;
  let origWin: any;

  beforeAll(() => {
    origDoc = (globalThis as any).document;
    origWin = (globalThis as any).window;

    if (typeof (globalThis as any).document === 'undefined') {
      (globalThis as any).document = {
        createElement: (tag: string) => ({
          tagName: tag.toUpperCase(),
          getContext: () => ({
            fillRect: () => {},
            clearRect: () => {},
            getImageData: () => ({ data: new Uint8Array(4) }),
            putImageData: () => {},
            createImageData: () => ({ data: new Uint8Array(4) }),
            setTransform: () => {},
            drawImage: () => {},
            save: () => {},
            fillText: () => {},
            restore: () => {},
            beginPath: () => {},
            moveTo: () => {},
            lineTo: () => {},
            closePath: () => {},
            stroke: () => {},
            translate: () => {},
            scale: () => {},
            rotate: () => {},
            arc: () => {},
            fill: () => {},
            measureText: () => ({ width: 10 })
          }),
          style: {},
          width: 256,
          height: 256,
          addEventListener: () => {},
          removeEventListener: () => {},
          setAttribute: () => {},
          getAttribute: () => null
        }),
        body: { appendChild: () => {}, removeChild: () => {} }
      };
    }

    if (typeof (globalThis as any).window === 'undefined') {
      (globalThis as any).window = {
        innerWidth: 1280,
        innerHeight: 800,
        devicePixelRatio: 1,
        addEventListener: () => {},
        removeEventListener: () => {}
      };
    }
  });

  afterAll(() => {
    (globalThis as any).document = origDoc;
    (globalThis as any).window = origWin;
  });

  beforeEach(() => {
    sampleNode = {
      id: 'node-action-test',
      type: 'agent',
      label: 'Agente de Teste de Ações',
      position: { x: 0, y: 0 },
      inputs: [{ id: 'in_1', name: 'input', direction: 'input', type: 'data', dataType: 'string' }],
      outputs: [{ id: 'out_1', name: 'output', direction: 'output', type: 'data' }]
    };
  });

  it('deve conter catálogo com 5 ações rápidas claramente identificáveis e semântica correta', () => {
    expect(NODE_ACTIONS.length).toBe(5);

    const types = NODE_ACTIONS.map(a => a.type);
    expect(types).toContain('sandbox');
    expect(types).toContain('model');
    expect(types).toContain('tools');
    expect(types).toContain('memory');
    expect(types).toContain('skills');

    // Memória usa ícone 'database' (substituindo o antigo disquete 💾)
    const memoryAction = getNodeAction('memory');
    expect(memoryAction?.iconName).toBe('database');
    expect(memoryAction?.label).toContain('Memória');

    // Nova ação de Skills aponta para o modal de skills
    const skillsAction = getNodeAction('skills');
    expect(skillsAction?.iconName).toBe('book-open-text');
    expect(skillsAction?.event).toBe('node:open-skills-modal');
  });

  it('deve equipar todo hitbox de ação e de porta do nó com metadados de tooltip', () => {
    const nv = new NodeView(sampleNode);

    const hitboxesWithTooltip: any[] = [];
    nv.group.traverse((obj: any) => {
      if (obj.userData?.isActionHitbox || obj.userData?.isPortHitbox) {
        if (obj.userData?.tooltip) {
          hitboxesWithTooltip.push(obj.userData);
        }
      }
    });

    // 5 botões de ação + 2 portas = 7 alvos com tooltip
    expect(hitboxesWithTooltip.length).toBe(7);

    const actionTooltips = hitboxesWithTooltip.filter(u => u.isActionHitbox);
    expect(actionTooltips.length).toBe(5);
    actionTooltips.forEach(u => {
      expect(typeof u.tooltip).toBe('string');
      expect(u.tooltip.length).toBeGreaterThan(0);
      expect(typeof u.tooltipDescription).toBe('string');
    });

    const portTooltips = hitboxesWithTooltip.filter(u => u.isPortHitbox);
    expect(portTooltips.length).toBe(2);

    nv.dispose();
  });

  it('deve formatar marcação HTML do tooltip com escape seguro de caracteres', () => {
    const html = renderTooltipHtml({
      title: 'Modelo <IA> & Teste',
      description: 'Configuração "especial" do agente'
    });

    expect(html).toContain('Modelo &lt;IA&gt; &amp; Teste');
    expect(html).toContain('Configuração &quot;especial&quot; do agente');
    expect(html).not.toContain('<script>');
  });

  it('deve gerenciar ciclo de vida do CanvasTooltip com exibição temporizada e ocultação', () => {
    vi.useFakeTimers();

    // Mock simples de document.createElement e document.body para o teste
    const originalDoc = (globalThis as any).document;
    const createdElements: any[] = [];
    (globalThis as any).document = {
      createElement: (tag: string) => {
        const el = {
          tagName: tag.toUpperCase(),
          className: '',
          style: {} as any,
          innerHTML: '',
          setAttribute: () => {},
          getBoundingClientRect: () => ({ width: 140, height: 40, left: 0, top: 0 }),
          parentNode: null as any
        };
        createdElements.push(el);
        return el;
      },
      body: {
        appendChild: (el: any) => { el.parentNode = (globalThis as any).document.body; },
        removeChild: (el: any) => { el.parentNode = null; }
      }
    };

    const tooltip = new CanvasTooltip(200);
    expect(tooltip.isVisible()).toBe(false);

    // Agenda tooltip
    tooltip.show('action_sandbox', { title: 'Sandbox', description: 'Isolamento' }, 300, 200);
    expect(tooltip.isVisible()).toBe(false);

    // Avança o timer
    vi.advanceTimersByTime(250);
    expect(tooltip.isVisible()).toBe(true);
    expect(tooltip.getCurrentKey()).toBe('action_sandbox');

    // Oculta
    tooltip.hide();
    expect(tooltip.isVisible()).toBe(false);
    expect(tooltip.getCurrentKey()).toBeNull();

    tooltip.dispose();
    vi.useRealTimers();
    (globalThis as any).document = originalDoc;
  });
});
