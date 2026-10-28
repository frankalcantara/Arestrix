import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { ProjectManager } from '../src/core/ProjectManager.ts';
import { AssistantPanel } from '../src/ui/AssistantPanel.ts';
import { setLocale } from '../src/i18n/index.ts';

vi.mock('../src/core/PipelineRunner.ts', () => ({
  PipelineRunner: { run: vi.fn().mockResolvedValue(true) }
}));

import { PipelineRunner } from '../src/core/PipelineRunner.ts';

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
    vi.mocked(PipelineRunner.run).mockClear();
    vi.mocked(PipelineRunner.run).mockResolvedValue(true);
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

    panel.onEvent({ type: 'confirm', sessionId: 's1', tool: 'update_project', params: { patch: { name: 'X' } } });
    expect(root.innerHTML).toContain('alterar as propriedades do projeto ativo');

    // Eventos de outra sessão não entram nesta conversa.
    const before = panel.items.length;
    panel.onEvent({ type: 'text', sessionId: 'outra', text: 'intrusa' });
    expect(panel.items.length).toBe(before);
  });

  it('aplica update_project no ProjectManager e sincroniza o metadata do grafo', async () => {
    const panel: any = new AssistantPanel(stubRoot(), graph, bus);
    panel.onEvent({
      type: 'op',
      op: { op: 'update_project', patch: { name: 'Projeto Novo', version: '2.0.0', description: 'Nova descrição', outputDir: 'Resultados' } }
    });
    await panel.opQueue;

    const project = ProjectManager.getInstance(bus).getCurrentProject();
    expect(project.name).toBe('Projeto Novo');
    expect(project.version).toBe('2.0.0');
    expect(project.description).toBe('Nova descrição');
    expect(project.outputDir).toBe('Resultados');
    expect(graph.getRootSchema().metadata.graphName).toBe('Projeto Novo');
  });

  it('entrega run_pipeline ao PipelineRunner oficial para aplicar a governança', async () => {
    const panel: any = new AssistantPanel(stubRoot(), graph, bus);
    panel.onEvent({ type: 'op', op: { op: 'run_pipeline' } });
    await panel.opQueue;
    expect(PipelineRunner.run).toHaveBeenCalledWith(graph);

    vi.mocked(PipelineRunner.run).mockResolvedValueOnce(false);
    panel.onEvent({ type: 'op', op: { op: 'run_pipeline' } });
    await panel.opQueue;
    expect(panel.items.some((item: any) => item.kind === 'error' && item.code === 'run_rejected')).toBe(true);
  });

  it('aplica set_child_graph no subgrafo certo e desfaz a criação do childGraph', async () => {
    const sub = node('sub', 'Subfluxo');
    sub.type = 'subflow';
    graph.addNode(sub);
    const panel: any = new AssistantPanel(stubRoot(), graph, bus);
    panel.onEvent({
      type: 'op',
      op: {
        op: 'set_child_graph',
        path: ['sub'],
        childGraph: {
          version: '1.0.0',
          metadata: { graphName: 'Subfluxo_Subflow' },
          nodes: [node('inner', 'Dentro')],
          edges: []
        }
      }
    });
    panel.onEvent({ type: 'done', ok: true });
    await panel.opQueue;

    expect(graph.getChildGraph(['sub'])?.nodes.map(n => n.id)).toEqual(['inner']);
    expect(graph.undo()).toBe(true);
    expect(graph.getChildGraph(['sub'])).toBeNull();
    expect(graph.redo()).toBe(true);
    expect(graph.getChildGraph(['sub'])?.nodes.map(n => n.id)).toEqual(['inner']);
  });

  it('mostra atividades, erros e confirmações pelo i18n nos dois idiomas', async () => {
    for (const [locale, expected] of [
      ['pt-BR', ['Ligou "A" a "B"', 'A IA padrão do Studio não está configurada', 'Recusado: Executar o pipeline',
                 'Consultou as skills aprovadas do workspace', 'Anexou a skill "Revisor" ao vértice "Escritor"',
                 'Alterou o projeto "Demo"', 'Anexou o servidor MCP "Eco" ao vértice "Agente"']],
      ['en-US', ['Connected "A" to "B"', 'The Studio default AI is not configured', 'Declined: Run the pipeline',
                 'Checked the approved workspace skills', 'Attached the skill "Revisor" to vertex "Escritor"',
                 'Changed the project "Demo"', 'Attached MCP server "Eco" to vertex "Agente"']]
    ] as const) {
      await setLocale(locale);
      const root = stubRoot();
      const panel: any = new AssistantPanel(root, graph, bus);
      panel.items = [
        { kind: 'activity', activity: { tool: 'connect', params: { from: 'A', to: 'B' }, status: 'ok' } },
        { kind: 'error', code: 'model_not_configured' },
        { kind: 'activity', activity: { tool: 'run_pipeline', params: {}, status: 'declined' } },
        { kind: 'activity', activity: { tool: 'list_skills', params: {}, status: 'ok' } },
        { kind: 'activity', activity: { tool: 'attach_skill', params: { skill: 'Revisor', label: 'Escritor' }, status: 'ok' } },
        { kind: 'activity', activity: { tool: 'attach_mcp', params: { server: 'Eco', label: 'Agente' }, status: 'ok' } },
        { kind: 'activity', activity: { tool: 'update_project', params: { name: 'Demo' }, status: 'ok' } }
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

  it('leva o banner da IA padrão às propriedades do Studio, não aos padrões do projeto', async () => {
    status = { configured: false, provider: '', model: '', supportsTools: false, providers: [] };
    const handlers: Record<string, () => void> = {};
    const root: any = {
      innerHTML: '',
      querySelector: (selector: string) => selector === '#assistant-open-defaults'
        ? { addEventListener: (_event: string, handler: () => void) => { handlers[selector] = handler; } }
        : null,
      querySelectorAll: () => []
    };
    const opened: any[] = [];
    bus.on('properties:open', (payload: any) => opened.push(payload));

    const panel: any = new AssistantPanel(root, graph, bus);
    await panel.refresh();
    expect(root.innerHTML).toContain('A IA padrão do Studio não está configurada');
    handlers['#assistant-open-defaults']();
    expect(opened).toEqual([{ scope: 'studio', section: 'studio-model' }]);
  });
});
