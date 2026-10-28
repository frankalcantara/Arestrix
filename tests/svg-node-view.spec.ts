import { describe, it, expect, beforeEach, beforeAll, afterAll } from 'vitest';
import { SvgNodeView } from '../src/view/SvgNodeView.ts';
import { THEME, getRelativeLuminance, getContrastRatio, getContrastTextColor } from '../src/view/Theme.ts';
import { INodeData } from '../src/core/types.ts';
import { CanvasTooltip } from '../src/ui/CanvasTooltip.ts';
import { EventBus } from '../src/core/EventBus.ts';

// Mock DOM para execução determinística no ambiente Node/Vitest
class MockSvgElement {
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

describe('SvgNodeView - Vértices Vetoriais SVG e Contraste WCAG 2.1', () => {
  let sampleNode: INodeData;
  let testSvgRoot: any;
  let tooltip: CanvasTooltip;
  let bus: EventBus;
  let origDoc: any;

  beforeAll(() => {
    origDoc = (globalThis as any).document;
    (globalThis as any).document = {
      createElementNS: (_ns: string, tag: string) => new MockSvgElement(tag),
      createElement: (tag: string) => new MockSvgElement(tag),
      body: new MockSvgElement('body')
    };
  });

  afterAll(() => {
    (globalThis as any).document = origDoc;
  });

  beforeEach(() => {
    bus = new EventBus();
    tooltip = new CanvasTooltip();
    testSvgRoot = new MockSvgElement('svg');

    sampleNode = {
      id: 'node-svg-test-1',
      type: 'agent',
      label: 'Agente Code Mode Autônomo com Raciocínio Profundo',
      position: { x: 200, y: 100 },
      inputs: [
        { id: 'in_1', name: 'prompt', direction: 'input', type: 'data', dataType: 'string' },
        { id: 'in_2', name: 'trigger', direction: 'input', type: 'flow' }
      ],
      outputs: [
        { id: 'out_1', name: 'response', direction: 'output', type: 'data', dataType: 'json' },
        { id: 'out_2', name: 'next', direction: 'output', type: 'flow' }
      ]
    };
  });

  describe('1. Garantia de Contraste WCAG 2.1 na Barra de Título (Header)', () => {
    it('deve calcular luminância relativa e selecionar cor de texto com maior contraste', () => {
      // Cores escuras corporativas Office (devem receber texto branco #FFFFFF)
      expect(getContrastTextColor(0x0f6cbd)).toBe('#FFFFFF'); // Office Blue (contraste ~6.1:1)
      expect(getContrastTextColor(0xc43e1c)).toBe('#FFFFFF'); // Rust / Orange
      expect(getContrastTextColor(0x5c2d91)).toBe('#FFFFFF'); // Teams Purple (contraste ~9.7:1)
      expect(getContrastTextColor(0x107c41)).toBe('#FFFFFF'); // Excel Green (contraste ~5.0:1)
      expect(getContrastTextColor(0x008272)).toBe('#FFFFFF'); // SharePoint Teal
      expect(getContrastTextColor(0x4f46e5)).toBe('#FFFFFF'); // Indigo Fluent

      // Cores claras ou pastéis (devem receber texto escuro Slate #0F172A)
      expect(getContrastTextColor('#FFFFFF')).toBe('#0F172A'); // Branco puro
      expect(getContrastTextColor('#FEF08A')).toBe('#0F172A'); // Amarelo claro
      expect(getContrastTextColor('#A7F3D0')).toBe('#0F172A'); // Menta claro
      expect(getContrastTextColor('#BAE6FD')).toBe('#0F172A'); // Azul celeste claro
      expect(getContrastTextColor('#F1F5F9')).toBe('#0F172A'); // Cinza ardósia claro
    });

    it('deve garantir razão de contraste superior a 4.5:1 (WCAG AA) para todas as tarefas padrão', () => {
      Object.entries(THEME.nodeTypes).forEach(([typeKey, typeDef]) => {
        const bgLum = getRelativeLuminance(typeDef.color);
        const textColor = getContrastTextColor(typeDef.color);
        const textLum = textColor === '#FFFFFF' ? 1.0 : 0.013;
        const ratio = getContrastRatio(bgLum, textLum);

        // Toda combinação de barra de título e fonte deve superar 4.5:1
        expect(ratio).toBeGreaterThanOrEqual(4.5);
      });
    });
  });

  describe('2. Renderização Vetorial SVG dos Vértices', () => {
    it('deve gerar elementos nativos SVG (rect, path, text, g) sem rasterização BMP', () => {
      const svgView = new SvgNodeView(sampleNode, testSvgRoot, tooltip, bus);

      expect(svgView.group).toBeDefined();
      expect(svgView.group.tagName.toLowerCase()).toBe('g');
      expect(svgView.group.getAttribute('class')).toContain('arestrix-svg-vertex');

      // Verifica presença dos elementos fundamentais do vértice
      const body = svgView.group.querySelector('.vertex-body');
      const header = svgView.group.querySelector('.vertex-header');
      const headerLabel = svgView.group.querySelector('.vertex-header-label');
      const actionBar = svgView.group.querySelector('.vertex-action-bar');
      const ports = svgView.group.querySelectorAll('.vertex-port');

      expect(body).not.toBeNull();
      expect(header).not.toBeNull();
      expect(headerLabel).not.toBeNull();
      expect(actionBar).not.toBeNull();
      expect(ports.length).toBe(4);

      svgView.dispose();
    });

    it('deve aplicar tipografia limpa sem sombras nem filtros de blur (Regra Global de Projeto)', () => {
      const svgView = new SvgNodeView(sampleNode, testSvgRoot, tooltip, bus);

      const allTexts = svgView.group.querySelectorAll('text');
      expect(allTexts.length).toBeGreaterThan(0);

      allTexts.forEach(textEl => {
        // Estrita conformidade: textShadow nulo e sem filtros
        expect(textEl.style.textShadow).toBe('none');
        expect(textEl.style.filter || 'none').toBe('none');
        expect(textEl.getAttribute('filter')).toBeNull();
      });

      svgView.dispose();
    });

    it('deve atualizar posição e rótulo dinamicamente no SVG', () => {
      const svgView = new SvgNodeView(sampleNode, testSvgRoot, tooltip, bus);

      // Atualiza coordenadas (em SVG o eixo Y do mundo é invertido para -Y)
      svgView.updatePosition(450, 320);
      expect(svgView.group.getAttribute('transform')).toBe('translate(450, -320)');

      // Atualiza rótulo
      svgView.updateLabel('Novo Título do Vértice Processado');
      const titleLines = svgView.group.querySelectorAll('.vertex-title-line');
      expect(titleLines.length).toBeGreaterThan(0);
      expect(titleLines[0].textContent).toContain('Novo Título do Vértice');

      svgView.dispose();
    });

    it('deve quebrar títulos longos em até 2 linhas e truncar com reticências + tooltip', () => {
      const wrapped = new SvgNodeView(sampleNode, testSvgRoot, tooltip, bus);
      const wrappedLines = wrapped.group.querySelectorAll('.vertex-title-line');
      expect(wrappedLines.length).toBe(2);
      expect(wrappedLines[1].textContent).not.toContain('…');
      // Um título que cabe em 2 linhas não deve ganhar tooltip redundante.
      expect(wrapped.group.querySelector('.vertex-title-group')!.querySelector('title')).toBeNull();
      wrapped.dispose();

      const longLabel = 'Agente Code Mode Autônomo com Raciocínio Profundo e Ferramentas Externas de Longa Execução';
      const longNode: INodeData = { ...sampleNode, id: 'node-svg-test-long', label: longLabel };
      const truncated = new SvgNodeView(longNode, testSvgRoot, tooltip, bus);
      const truncatedLines = truncated.group.querySelectorAll('.vertex-title-line');

      expect(truncatedLines.length).toBe(2);
      expect(truncatedLines[1].textContent).toContain('…');
      const titleTooltip = truncated.group.querySelector('.vertex-title-group')!.querySelector('title');
      expect(titleTooltip?.textContent).toBe(longLabel);
      truncated.dispose();
    });

    it('deve refletir estados de execução (idle, running, completed) com anéis vetoriais coloridos', () => {
      const svgView = new SvgNodeView(sampleNode, testSvgRoot, tooltip, bus);
      const ring = svgView.group.querySelector('.vertex-selection-ring') as any;

      // Estado Idle (anel oculto)
      expect(ring.style.display).toBe('none');

      // Estado Running (anel verde esmeralda visível)
      svgView.setExecutionState('running');
      expect(ring.style.display).toBe('block');
      expect(ring.getAttribute('stroke')).toBe('#10B981');

      // Estado Completed (verde escuro)
      svgView.setExecutionState('completed');
      expect(ring.style.display).toBe('block');
      expect(ring.getAttribute('stroke')).toBe('#059669');

      // Seleção normal
      svgView.setExecutionState('idle');
      svgView.setSelected(true);
      expect(ring.style.display).toBe('block');
      expect(ring.getAttribute('stroke')).toBe('#0F6CBD');

      svgView.dispose();
    });

    it('deve equipar barra de ações com 5 botões Lucide vetoriais e tooltips', () => {
      const svgView = new SvgNodeView(sampleNode, testSvgRoot, tooltip, bus);

      const actionButtons = svgView.group.querySelectorAll('.vertex-action-btn');
      expect(actionButtons.length).toBe(5);

      const actionTypes = Array.from(actionButtons).map(b => b.getAttribute('data-action'));
      expect(actionTypes).toEqual(['sandbox', 'model', 'tools', 'memory', 'skills']);

      svgView.dispose();
    });

    it('deve selecionar e iniciar arraste ao clicar em qualquer elemento visual do vértice', () => {
      const svgView = new SvgNodeView(sampleNode, testSvgRoot, tooltip, bus);
      const events: string[] = [];
      bus.on('node:selected', () => events.push('selected'));
      bus.on('node:start-drag', () => events.push('drag'));

      // Alvos pintados por cima do corpo: antes só o rect do corpo e o path do header
      // respondiam ao ponteiro, então clicar no texto/ícone exigia repetir o clique.
      const targets = [
        svgView.group.querySelector('.vertex-title-line'),
        svgView.group.querySelector('.vertex-header-label'),
        svgView.group.querySelector('.arestrix-lucide-icon'),
        svgView.group.querySelector('.vertex-header'),
        svgView.group.querySelector('.vertex-body')
      ];
      targets.forEach(target => expect(target).not.toBeNull());

      targets.forEach(target => {
        svgView.group.dispatchEvent({
          type: 'pointerdown',
          button: 0,
          altKey: false,
          target,
          clientX: 10,
          clientY: 20,
          stopPropagation() {}
        });
      });

      expect(events.filter(e => e === 'selected').length).toBe(targets.length);
      expect(events.filter(e => e === 'drag').length).toBe(targets.length);

      svgView.dispose();
    });

    it('deve manter portas e botões de ação isolados da seleção/arraste do vértice', () => {
      const svgView = new SvgNodeView(sampleNode, testSvgRoot, tooltip, bus);
      const events: string[] = [];
      bus.on('node:selected', () => events.push('selected'));
      bus.on('node:start-drag', () => events.push('drag'));

      const portHitbox = svgView.group.querySelector('.vertex-port-hitbox');
      const actionButton = svgView.group.querySelector('.vertex-action-btn');
      const actionIcon = actionButton?.querySelector('.arestrix-lucide-icon');

      [portHitbox, actionButton, actionIcon].forEach(target => {
        svgView.group.dispatchEvent({
          type: 'pointerdown',
          button: 0,
          altKey: false,
          target,
          clientX: 10,
          clientY: 20,
          stopPropagation() {}
        });
      });

      expect(events).toEqual([]);

      svgView.dispose();
    });

    it('deve encaminhar o duplo clique sobre o vértice (inclusive no texto) para entrar em Subflow', () => {
      const svgView = new SvgNodeView(sampleNode, testSvgRoot, tooltip, bus);
      const payloads: any[] = [];
      bus.on('node:double-click', (payload: any) => payloads.push(payload));

      const titleLine = svgView.group.querySelector('.vertex-title-line');
      const actionButton = svgView.group.querySelector('.vertex-action-btn');

      [titleLine, svgView.group.querySelector('.vertex-body')].forEach(target => {
        svgView.group.dispatchEvent({
          type: 'dblclick',
          button: 0,
          altKey: false,
          target,
          stopPropagation() {}
        });
      });
      expect(payloads).toEqual([{ nodeId: sampleNode.id }, { nodeId: sampleNode.id }]);

      // Botões da barra de ações não devem navegar para o subflow.
      svgView.group.dispatchEvent({
        type: 'dblclick',
        button: 0,
        altKey: false,
        target: actionButton,
        stopPropagation() {}
      });
      expect(payloads).toHaveLength(2);

      svgView.dispose();
    });

    it('deve pedir seleção sincronizada ao clicar em um botão da barra de ações', () => {
      const svgView = new SvgNodeView(sampleNode, testSvgRoot, tooltip, bus);
      const selections: string[] = [];
      bus.on('node:select-request', ({ nodeId }: { nodeId: string }) => selections.push(nodeId));

      const actionButton = svgView.group.querySelector('.vertex-action-btn')!;
      const hitbox = actionButton.children[actionButton.children.length - 1];
      hitbox.dispatchEvent({ type: 'click', stopPropagation() {} });

      expect(selections).toEqual([sampleNode.id]);

      svgView.dispose();
    });

    it('deve calcular altura dinâmica proporcional para acomodar portas sem colisão', () => {
      const standardHeight = SvgNodeView.computeHeight(sampleNode);
      expect(standardHeight).toBeGreaterThanOrEqual(THEME.nodeBody.height);

      const multiPortNode: INodeData = {
        ...sampleNode,
        inputs: [
          { id: 'i1', name: 'in1', direction: 'input', type: 'data' },
          { id: 'i2', name: 'in2', direction: 'input', type: 'data' },
          { id: 'i3', name: 'in3', direction: 'input', type: 'data' },
          { id: 'i4', name: 'in4', direction: 'input', type: 'data' },
          { id: 'i5', name: 'in5', direction: 'input', type: 'data' },
          { id: 'i6', name: 'in6', direction: 'input', type: 'data' }
        ]
      };

      const expandedHeight = SvgNodeView.computeHeight(multiPortNode);
      expect(expandedHeight).toBeGreaterThan(standardHeight);
    });

    it('deve exibir pílula de status ao vivo no canto superior esquerdo e animar anel perimetral sem text-shadow', () => {
      const svgView = new SvgNodeView(sampleNode, testSvgRoot, tooltip, bus);
      const ring = svgView.group.querySelector('.vertex-selection-ring') as any;

      // Estado inicial Idle (sem pílula de status)
      expect(svgView.group.querySelector('.vertex-exec-status-pill')).toBeNull();

      // Estado Queued (Na Fila)
      svgView.setExecutionState('queued');
      let statusPill = svgView.group.querySelector('.vertex-exec-status-pill') as any;
      let statusText = svgView.group.querySelector('.vertex-exec-status-text') as any;
      expect(statusPill).not.toBeNull();
      expect(statusText.style.textShadow).toBe('none');
      expect(statusText.style.filter).toBe('none');
      expect(statusText.textContent).toContain('Na fila');
      expect(svgView.group.querySelector('.vertex-exec-spinner')).toBeNull();
      expect(svgView.group.getAttribute('class')).toContain('state-queued');

      // Estado Running (Executando com spinner e anel animado)
      svgView.setExecutionState('running');
      statusPill = svgView.group.querySelector('.vertex-exec-status-pill') as any;
      statusText = svgView.group.querySelector('.vertex-exec-status-text') as any;
      const spinner = svgView.group.querySelector('.vertex-exec-spinner') as any;
      expect(statusPill).not.toBeNull();
      expect(spinner).not.toBeNull();
      expect(ring.getAttribute('class')).toContain('ring-animated');
      expect(ring.getAttribute('stroke')).toBe('#10B981');

      // Sub-atividade Thinking via telemetria (índigo #6366F1)
      svgView.handleTelemetry({
        nodeId: sampleNode.id,
        type: 'thinking',
        timestamp: Date.now(),
        payload: { thinkingChars: 2048 }
      });
      statusText = svgView.group.querySelector('.vertex-exec-status-text') as any;
      expect(ring.getAttribute('stroke')).toBe('#6366F1');
      expect(statusText.textContent).toContain('Pensando');

      // Sub-atividade Tool Call via telemetria (ciano/azul #0284C7)
      svgView.handleTelemetry({
        nodeId: sampleNode.id,
        type: 'tool_start',
        timestamp: Date.now(),
        payload: { toolName: 'search_db' }
      });
      statusText = svgView.group.querySelector('.vertex-exec-status-text') as any;
      expect(ring.getAttribute('stroke')).toBe('#0284C7');
      expect(statusText.textContent).toContain('search_db');

      // Conclusão via telemetria (settled -> completed)
      svgView.handleTelemetry({
        nodeId: sampleNode.id,
        type: 'status',
        timestamp: Date.now(),
        payload: { status: 'settled' }
      });
      statusText = svgView.group.querySelector('.vertex-exec-status-text') as any;
      expect(svgView.getExecutionState()).toBe('completed');
      expect(ring.getAttribute('stroke')).toBe('#059669');
      expect(ring.getAttribute('class')).not.toContain('ring-animated');
      expect(svgView.group.querySelector('.vertex-exec-spinner')).toBeNull();
      expect(statusText.textContent).toContain('✓ Concluído');

      // Erro via telemetria (error -> #EF4444)
      svgView.handleTelemetry({
        nodeId: sampleNode.id,
        type: 'status',
        timestamp: Date.now(),
        payload: { status: 'error', detail: 'Falha de execução' }
      });
      statusText = svgView.group.querySelector('.vertex-exec-status-text') as any;
      expect(svgView.getExecutionState()).toBe('error');
      expect(ring.getAttribute('stroke')).toBe('#EF4444');
      expect(statusText.textContent).toContain('✖ Erro');

      svgView.dispose();
    });

    it('deve registrar o resultado da última execução sem manter o vértice em modo de execução', () => {
      const svgView = new SvgNodeView(sampleNode, testSvgRoot, tooltip, bus);

      svgView.setExecutionState('running');
      svgView.setRunRecord({ outcome: 'success', durationMs: 3900, finishedAt: Date.now() });
      svgView.setExecutionState('idle');

      // O vértice volta ao modo de edição: opaco, state-idle e sem a pílula de execução.
      expect(svgView.getExecutionState()).toBe('idle');
      expect(svgView.group.getAttribute('class')).toContain('state-idle');
      expect(svgView.group.style.opacity).toBe('1');
      expect(svgView.group.querySelector('.vertex-exec-status-pill')).toBeNull();

      // O resultado permanece como um selo compacto no header.
      const badge = svgView.group.querySelector('.vertex-run-badge') as any;
      expect(badge).not.toBeNull();
      expect(badge.getAttribute('data-run-outcome')).toBe('success');
      const badgeText = badge.querySelector('text') as any;
      expect(badgeText.textContent).toContain('3.9s');
      expect(badgeText.style.textShadow).toBe('none');

      // Uma nova execução substitui o selo anterior (nunca empilha).
      svgView.setRunRecord({ outcome: 'error', durationMs: 1200, finishedAt: Date.now() });
      const badges = svgView.group.querySelectorAll('.vertex-run-badge');
      expect(badges).toHaveLength(1);
      expect(badges[0].getAttribute('data-run-outcome')).toBe('error');
      expect(badges[0].querySelector('text')?.textContent).toContain('1.2s');

      // Limpeza explícita remove o registro.
      svgView.setRunRecord(null);
      expect(svgView.group.querySelector('.vertex-run-badge')).toBeNull();

      svgView.dispose();
    });

    it('deve exibir selos INÍCIO/FIM apenas quando a topologia é ambígua', () => {
      const svgView = new SvgNodeView(sampleNode, testSvgRoot, tooltip, bus);

      // Sem papel definido o vértice é apenas um cartão.
      expect(svgView.getFlowRole()).toBeNull();
      expect(svgView.group.querySelector('.vertex-flow-badges')).toBeNull();

      // Entrada de trilha: um selo, fora do cartão, legível e sem sombra.
      svgView.setFlowRole('start');
      const startBadges = svgView.group.querySelectorAll('.vertex-flow-badge');
      expect(startBadges).toHaveLength(1);
      expect(startBadges[0].getAttribute('class')).toContain('vertex-flow-badge-start');
      expect(startBadges[0].querySelector('text')?.textContent).toBe('INÍCIO');
      expect(startBadges[0].querySelector('text')?.style.textShadow).toBe('none');
      expect(startBadges[0].querySelector('title')?.textContent).toContain('Início de trilha');

      // Repetir o mesmo papel não empilha selos.
      svgView.setFlowRole('start');
      expect(svgView.group.querySelectorAll('.vertex-flow-badge')).toHaveLength(1);

      // Vértice que é entrada e saída ao mesmo tempo recebe os dois lados.
      svgView.setFlowRole('both');
      const bothBadges = svgView.group.querySelectorAll('.vertex-flow-badge');
      expect(bothBadges).toHaveLength(2);
      expect(bothBadges.map(b => b.getAttribute('class'))).toEqual([
        expect.stringContaining('vertex-flow-badge-start'),
        expect.stringContaining('vertex-flow-badge-end')
      ]);
      expect(bothBadges[1].querySelector('text')?.textContent).toBe('FIM');

      // Topologia inequívoca volta a limpar tudo.
      svgView.setFlowRole(null);
      expect(svgView.group.querySelector('.vertex-flow-badges')).toBeNull();

      svgView.dispose();
    });

    it('destaca a trilha com uma faixa colorida no rodapé do cartão', () => {
      const svgView = new SvgNodeView(sampleNode, testSvgRoot, tooltip, bus);

      expect(svgView.getTrailIndex()).toBeNull();
      expect(svgView.group.querySelector('.vertex-trail-accent')).toBeNull();

      svgView.setTrailHighlight(1);
      const accent = svgView.group.querySelector('.vertex-trail-accent') as any;
      expect(accent).not.toBeNull();
      expect(accent.getAttribute('fill')).toBe(THEME.trailPalette[1]);
      expect(accent.getAttribute('pointer-events')).toBe('none');
      expect(svgView.group.getAttribute('data-trail-index')).toBe('1');

      // Repetir o mesmo índice não empilha faixas.
      svgView.setTrailHighlight(1);
      expect(svgView.group.querySelectorAll('.vertex-trail-accent')).toHaveLength(1);

      // Desligar o destaque devolve o cartão ao estado normal.
      svgView.setTrailHighlight(null);
      expect(svgView.group.querySelector('.vertex-trail-accent')).toBeNull();
      expect(svgView.group.getAttribute('data-trail-index')).toBeNull();

      svgView.dispose();
    });

    it('usa a cor da trilha no selo INÍCIO para ligar a faixa à entrada', () => {
      const svgView = new SvgNodeView(sampleNode, testSvgRoot, tooltip, bus);
      svgView.setFlowRole('start');

      const badge = svgView.group.querySelector('.vertex-flow-badge-start') as any;
      expect(badge.querySelector('rect')?.getAttribute('stroke')).toBe(THEME.flowBadge.start.border);

      svgView.setTrailHighlight(2);
      const tinted = svgView.group.querySelector('.vertex-flow-badge-start') as any;
      expect(tinted.querySelector('rect')?.getAttribute('stroke')).toBe(THEME.trailPalette[2]);
      expect(tinted.querySelector('text')?.getAttribute('fill')).toBe(THEME.trailPalette[2]);

      svgView.setTrailHighlight(null);
      const restored = svgView.group.querySelector('.vertex-flow-badge-start') as any;
      expect(restored.querySelector('rect')?.getAttribute('stroke')).toBe(THEME.flowBadge.start.border);

      svgView.dispose();
    });
  });
});
