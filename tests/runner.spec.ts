import { describe, it, expect, beforeEach, beforeAll, afterAll } from 'vitest';
import { RunnerManager } from '../src/core/runner/RunnerManager.ts';
import { ProjectManager } from '../src/core/ProjectManager.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { Graph } from '../src/core/Graph.ts';
import { StandaloneRunnerModal } from '../src/ui/modals/StandaloneRunnerModal.ts';

// Mock de DOM para ambiente Node
class MockElement {
  tagName: string;
  className: string = '';
  id: string = '';
  type: string = '';
  title: string = '';
  style: Record<string, string> = {};
  children: MockElement[] = [];
  parentNode: MockElement | null = null;
  textContent: string = '';
  dataset: Record<string, string> = {};
  value: string = '';
  checked: boolean = false;
  disabled: boolean = false;
  private _listeners: Record<string, Function[]> = {};

  constructor(tagName: string = 'DIV') {
    this.tagName = tagName.toUpperCase();
  }

  get classList() {
    return {
      add: (...cls: string[]) => {
        const set = new Set(this.className.split(/\s+/).filter(Boolean));
        cls.forEach(c => set.add(c));
        this.className = Array.from(set).join(' ');
      },
      remove: (...cls: string[]) => {
        const set = new Set(this.className.split(/\s+/).filter(Boolean));
        cls.forEach(c => set.delete(c));
        this.className = Array.from(set).join(' ');
      },
      contains: (c: string) => this.className.split(/\s+/).includes(c),
      toggle: (c: string, force?: boolean) => {
        const exists = this.className.split(/\s+/).includes(c);
        const shouldAdd = force !== undefined ? force : !exists;
        if (shouldAdd) (this.classList as any).add(c);
        else (this.classList as any).remove(c);
      }
    };
  }

  appendChild(child: MockElement): MockElement {
    child.parentNode = this;
    this.children.push(child);
    return child;
  }

  removeChild(child: MockElement): MockElement {
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

  setAttribute(name: string, val: string) {
    (this as any)[name] = val;
  }

  getAttribute(name: string): string | null {
    return (this as any)[name] || null;
  }

  querySelector(selector: string): MockElement | null {
    if (selector.startsWith('#')) {
      const id = selector.slice(1);
      if (this.id === id) return this;
      for (const c of this.children) {
        const found = c.querySelector(selector);
        if (found) return found;
      }
    }
    if (selector.startsWith('.')) {
      const cls = selector.slice(1);
      if (this.classList.contains(cls)) return this;
      for (const c of this.children) {
        const found = c.querySelector(selector);
        if (found) return found;
      }
    }
    return null;
  }

  querySelectorAll(selector: string): MockElement[] {
    const res: MockElement[] = [];
    const check = (node: MockElement) => {
      if (selector.startsWith('.')) {
        const cls = selector.slice(1);
        if (node.classList.contains(cls)) res.push(node);
      } else if (selector.startsWith('#')) {
        const id = selector.slice(1);
        if (node.id === id) res.push(node);
      }
      node.children.forEach(check);
    };
    check(this);
    return res;
  }

  set innerHTML(html: string) {
    this.children = [];
    if (html.includes('id="runner-screen-content"')) {
      const sc = new MockElement('DIV');
      sc.id = 'runner-screen-content';
      this.appendChild(sc);
    }
    if (html.includes('id="btn-close-modal-standalone-runner"')) {
      const btn = new MockElement('BUTTON');
      btn.id = 'btn-close-modal-standalone-runner';
      this.appendChild(btn);
    }
  }

  get innerHTML(): string {
    return '';
  }
}

describe('Standalone Runner Engine & Manager', () => {
  let bus: EventBus;
  let pm: ProjectManager;
  let runner: RunnerManager;
  let graph: Graph;

  beforeAll(() => {
    (global as any).document = {
      createElement: (tag: string) => new MockElement(tag),
      body: new MockElement('BODY'),
      querySelectorAll: () => []
    };
    (global as any).window = {
      addEventListener: () => {},
      removeEventListener: () => {}
    };
  });

  afterAll(() => {
    delete (global as any).document;
    delete (global as any).window;
  });

  beforeEach(() => {
    RunnerManager.resetInstance();
    ProjectManager.resetInstance();
    bus = new EventBus();
    pm = ProjectManager.getInstance(bus);
    runner = RunnerManager.getInstance(bus, pm);
    graph = new Graph(bus);
  });

  it('deve inicializar o runner com os nós do grafo em estado "waiting"', () => {
    graph.createNode('node_1', 'Tarefa 1', 'static', 100, 100);
    graph.createNode('node_2', 'Agente Financeiro', 'agent', 300, 100);

    runner.initGraph(graph);
    const state = runner.getState();

    expect(state.nodes.length).toBe(2);
    expect(state.nodes[0].status).toBe('waiting');
    expect(state.nodes[1].status).toBe('waiting');
    expect(state.status).toBe('idle');
  });

  it('deve atualizar o status para compiling e depois running', () => {
    graph.createNode('node_1', 'Tarefa 1', 'static', 100, 100);
    runner.initGraph(graph);

    bus.emit('pipeline:compiling', 'Compilando código Taskflow C++20...');
    let state = runner.getState();
    expect(state.status).toBe('compiling');
    expect(state.progressPercent).toBe(10);

    bus.emit('pipeline:running', 'Executando pipeline...');
    state = runner.getState();
    expect(state.status).toBe('running');
    expect(state.progressPercent).toBeGreaterThanOrEqual(25);
  });

  it('deve rastrear início e fim de etapas individuais via telemetria', () => {
    const n1 = graph.createNode('ProcessarDados', 'ProcessarDados', 'static', 100, 100);
    runner.initGraph(graph);

    bus.emit('pipeline:running', 'Executando');
    bus.emit('telemetry:task-enter', { taskId: 'ProcessarDados', worker: 0 });

    let state = runner.getState();
    const nodeState = state.nodes.find(n => n.id === n1.id);
    expect(nodeState?.status).toBe('running');

    bus.emit('telemetry:task-exit', { taskId: 'ProcessarDados', worker: 0 });
    state = runner.getState();
    expect(nodeState?.status).toBe('success');
  });

  it('deve completar com progresso 100% no evento de pipeline:success', () => {
    graph.createNode('node_final', 'Etapa Final', 'static', 100, 100);
    runner.initGraph(graph);

    bus.emit('pipeline:compiling', 'Compilando');
    bus.emit('pipeline:running', 'Executando');
    bus.emit('pipeline:success', { stdout: 'OK', elapsedMs: 1250 });

    const state = runner.getState();
    expect(state.status).toBe('success');
    expect(state.progressPercent).toBe(100);
    expect(state.elapsedMs).toBe(1250);
  });

  it('deve registrar erro no evento pipeline:error', () => {
    graph.createNode('node_falha', 'Etapa Falha', 'static', 100, 100);
    runner.initGraph(graph);

    bus.emit('pipeline:running', 'Executando');
    bus.emit('pipeline:error', { stage: 'runtime', error: 'Timeout ao conectar no gateway LLM' });

    const state = runner.getState();
    expect(state.status).toBe('error');
    expect(state.diagnostics.errors.length).toBe(1);
    expect(state.diagnostics.errors[0].message).toContain('Timeout');
  });

  it('deve agregar telemetria de tokens, thinking e chamadas de ferramenta', () => {
    runner.resetDiagnostics();

    // 1. Tokens
    bus.emit('agent:telemetry', {
      type: 'usage',
      prompt_tokens: 150,
      completion_tokens: 80,
      total_tokens: 230
    });

    // 2. Thinking
    bus.emit('agent:telemetry', {
      type: 'thinking',
      agentName: 'AuditorContabil',
      thought: 'Analisando balancete do mês 10...'
    });

    // 3. Ferramenta
    bus.emit('agent:telemetry', {
      type: 'tool_call',
      toolName: 'read_file',
      args: { path: 'balancete.csv' },
      isError: false
    });

    // 4. Rate limit 429
    bus.emit('agent:telemetry', {
      type: 'rate_limit',
      retry: true
    });

    const diag = runner.getDiagnostics();
    expect(diag.totalTokens).toBe(230);
    expect(diag.promptTokens).toBe(150);
    expect(diag.completionTokens).toBe(80);
    expect(diag.thinkingLogs.length).toBe(1);
    expect(diag.thinkingLogs[0].agentName).toBe('AuditorContabil');
    expect(diag.toolCallsCount).toBe(1);
    expect(diag.toolCalls[0].toolName).toBe('read_file');
    expect(diag.rateLimitRetries).toBe(1);
  });

  it('deve permitir configurar e recuperar parâmetros operacionais', () => {
    runner.setParameter('data_inicio', '2026-01-01');
    runner.setParameter('modo_estrito', true);

    expect(runner.getParameter('data_inicio')).toBe('2026-01-01');
    expect(runner.getParameter('modo_estrito')).toBe(true);
    expect(runner.getParameters()).toEqual({
      data_inicio: '2026-01-01',
      modo_estrito: true
    });
  });

  it('deve instanciar StandaloneRunnerModal com as abas de Progresso, Diagnóstico e Operação', () => {
    graph.createNode('node_teste', 'Tarefa Teste', 'static', 100, 100);
    const modal = new StandaloneRunnerModal(graph);

    expect(modal).toBeDefined();
    modal.open('progress');
    modal.open('diagnostics');
    modal.open('operations');
    modal.open('export');
    modal.hide();
  });
});
