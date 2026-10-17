import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { ProjectManager } from '../src/core/ProjectManager.ts';
import { AssistantPanel } from '../src/ui/AssistantPanel.ts';
import { setLocale } from '../src/i18n/index.ts';

// Raiz mínima: o painel só escreve innerHTML e procura elementos para ligar eventos.
function stubRoot(): any {
  return { innerHTML: '', querySelector: () => null, querySelectorAll: () => [] };
}

function node(id: string, label: string): any {
  return {
    id, label, type: 'static', position: { x: 0, y: 0, z: 0 },
    inputs: [{ id: `${id}_in_0`, name: 'in', direction: 'input', type: 'flow' }],
    outputs: [{ id: `${id}_out_0`, name: 'out', direction: 'output', type: 'flow' }],
    parameters: {}
  };
}

describe('Aba Assistente (interface)', () => {
  let originalWindow: any;
  let calls: Array<[string, any]>;
  let status: any;
  let graph: Graph;
  let bus: EventBus;

  beforeEach(async () => {
    await setLocale('pt-BR');
    originalWindow = (globalThis as any).window;
    calls = [];
    status = { configured: true, provider: 'deepseek', model: 'deepseek-chat', supportsTools: true, providers: [] };
    (globalThis as any).window = {
      webui: {
        call: async (name: string, arg?: any) => {
          calls.push([name, arg]);
          if (name === 'assistant_status') return JSON.stringify(status);
          if (name === 'assistant_sessions') return '[]';
          if (name === 'assistant_send') return JSON.stringify({ ok: true, sessionId: 's1' });
          return 'true';
        }
      }
    };
    bus = new EventBus();
    ProjectManager.getInstance(bus).resetForTesting(bus);
    graph = new Graph(bus);
  });

  afterEach(async () => {
    (globalThis as any).window = originalWindow;
    await setLocale('pt-BR');
  });

  it('aplica as operações do backend no canvas e desfaz a resposta inteira de uma vez', async () => {
    const panel: any = new AssistantPanel(stubRoot(), graph, bus);
    panel.onEvent({ type: 'op', op: { op: 'add_node', node: node('a', 'A') } });
    panel.onEvent({ type: 'op', op: { op: 'add_node', node: node('b', 'B') } });
    panel.onEvent({ type: 'op', op: { op: 'add_edge', edge: { id: 'e1', sourceNodeId: 'a', sourcePortId: 'a_out_0', targetNodeId: 'b', targetPortId: 'b_in_0' } } });
    panel.onEvent({ type: 'op', op: { op: 'update_node', id: 'a', patch: { label: 'Coletor' } } });
    panel.onEvent({ type: 'op', op: { op: 'move_node', id: 'b', x: 300, y: 40 } });
    panel.onEvent({ type: 'done', ok: true });
    await panel.opQueue;

    expect(graph.getAllNodes().map(n => n.id).sort()).toEqual(['a', 'b']);
    expect(graph.getNode('a')!.label).toBe('Coletor');
    expect(graph.getNode('b')!.position.x).toBe(300);
    expect(graph.getAllEdges()).toHaveLength(1);

    expect(graph.undo()).toBe(true);
    expect(graph.getAllNodes()).toHaveLength(0);
    expect(graph.getAllEdges()).toHaveLength(0);
    expect(graph.redo()).toBe(true);
    expect(graph.getAllNodes()).toHaveLength(2);
    expect(graph.getNode('a')!.label).toBe('Coletor');
  });

  it('envia a mensagem com o grafo e o idioma, e repassa a confirmação ao backend', async () => {
    const root = stubRoot();
    const panel: any = new AssistantPanel(root, graph, bus);
    await panel.refresh();
    graph.addNode(node('x', 'X'));
    await panel.send('Crie um projeto');
    const sent = JSON.parse(calls.find(c => c[0] === 'assistant_send')![1]);
    expect(sent.text).toBe('Crie um projeto');
    expect(sent.locale).toBe('pt-BR');
    expect(sent.graph.nodes.map((n: any) => n.id)).toEqual(['x']);
    expect(panel.sessionId).toBe('s1');

    panel.onEvent({ type: 'confirm', sessionId: 's1', tool: 'create_project', params: { name: 'Triagem <b>' } });
    expect(root.innerHTML).toContain('O Assistente quer criar o projeto "Triagem &lt;b&gt;"');
    await panel.answerConfirm(panel.items.length - 1, false);
    expect(calls.find(c => c[0] === 'assistant_confirm')![1]).toBe('false');
    expect(root.innerHTML).toContain('Recusado.');

    // Eventos de outra sessão não entram nesta conversa.
    const before = panel.items.length;
    panel.onEvent({ type: 'text', sessionId: 'outra', text: 'intrusa' });
    expect(panel.items.length).toBe(before);
  });

  it('mostra atividades, erros e confirmações pelo i18n nos dois idiomas', async () => {
    for (const [locale, expected] of [
      ['pt-BR', ['Ligou "A" a "B"', 'A IA padrão do Studio não está configurada', 'Recusado: Executar o pipeline']],
      ['en-US', ['Connected "A" to "B"', 'The Studio default AI is not configured', 'Declined: Run the pipeline']]
    ] as const) {
      await setLocale(locale);
      const root = stubRoot();
      const panel: any = new AssistantPanel(root, graph, bus);
      panel.items = [
        { kind: 'activity', activity: { tool: 'connect', params: { from: 'A', to: 'B' }, status: 'ok' } },
        { kind: 'error', code: 'model_not_configured' },
        { kind: 'activity', activity: { tool: 'run_pipeline', params: {}, status: 'declined' } }
      ];
      panel.render();
      for (const text of expected) expect(root.innerHTML).toContain(text);
      expect(root.innerHTML).not.toContain('assistant.');
    }
  });

  it('destaca o provedor sem suporte a ferramentas e bloqueia o envio', async () => {
    status = { configured: true, provider: 'jev', model: 'j1', supportsTools: false, providers: [] };
    const root = stubRoot();
    const panel: any = new AssistantPanel(root, graph, bus);
    await panel.refresh();
    expect(root.innerHTML).toContain('assistant-banner unsupported');
    expect(root.innerHTML).toContain('O provedor jev não suporta o Assistente');
    expect(root.innerHTML).toMatch(/id="assistant-send" disabled/);
    await panel.send('oi');
    expect(calls.some(c => c[0] === 'assistant_send')).toBe(false);
  });
});
