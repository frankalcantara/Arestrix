import { describe, it, expect, beforeEach, afterEach, vi } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { globalEventBus } from '../src/core/EventBus.ts';
import { PipelineRunner } from '../src/core/PipelineRunner.ts';

function collect(names: string[]) {
  const seen: Array<{ name: string; data: any }> = [];
  const handlers = names.map(name => {
    const h = (data: any) => seen.push({ name, data });
    globalEventBus.on(name, h);
    return { name, h };
  });
  return { seen, dispose: () => handlers.forEach(({ name, h }) => globalEventBus.off(name, h)) };
}

describe('PipelineRunner - ponte com o backend C++ (run_pipeline)', () => {
  let graph: Graph;

  beforeEach(() => {
    graph = new Graph(new EventBus());
    const a = graph.buildNode('a', 'Tarefa A', 'static', 0, 0);
    const b = graph.buildNode('b', 'Tarefa B', 'static', 200, 0);
    graph.addNode(a);
    graph.addNode(b);
    graph.createEdge('a', a.outputs[0].id, 'b', b.inputs[0].id);
  });

  afterEach(() => {
    delete (globalThis as any).webui;
    PipelineRunner.handleBackendEvent({ type: 'finished', success: true });   // libera o estado
  });

  it('envia o grafo completo (graph.toJSON) ao backend e aguarda o término assíncrono', async () => {
    const calls: Array<{ fn: string; arg: string }> = [];
    (globalThis as any).webui = {
      call: vi.fn(async (fn: string, arg: string) => {
        calls.push({ fn, arg });
        return fn === 'run_pipeline' ? JSON.stringify({ ok: true }) : '';
      })
    };

    const ok = await PipelineRunner.run(graph);
    expect(ok).toBe(true);
    expect(PipelineRunner.isRunning()).toBe(true);

    const run = calls.find(c => c.fn === 'run_pipeline')!;
    const schema = JSON.parse(run.arg);
    expect(schema.nodes.map((n: any) => n.id).sort()).toEqual(['a', 'b']);
    expect(schema.edges).toHaveLength(1);
    expect(schema.metadata.compilerConfig).toBeDefined();
  });

  it('não permite duas execuções simultâneas', async () => {
    (globalThis as any).webui = { call: async () => JSON.stringify({ ok: true }) };
    expect(await PipelineRunner.run(graph)).toBe(true);
    expect(await PipelineRunner.run(graph)).toBe(false);
  });

  it('propaga erro do backend e libera para nova execução', async () => {
    const { seen, dispose } = collect(['pipeline:error']);
    (globalThis as any).webui = { call: async () => JSON.stringify({ ok: false, error: 'Compilador C++20 nao encontrado' }) };

    expect(await PipelineRunner.run(graph)).toBe(false);
    expect(PipelineRunner.isRunning()).toBe(false);
    expect(seen[0].data.error).toContain('Compilador');
    dispose();
  });

  it('sem backend (modo navegador) orienta abrir o Studio nativo', async () => {
    const { seen, dispose } = collect(['pipeline:error']);
    expect(await PipelineRunner.run(graph)).toBe(false);
    expect(seen[0].data.stage).toBe('backend');
    dispose();
  });

  it('traduz eventos do backend para o EventBus (fases, logs, sucesso e erro)', () => {
    const { seen, dispose } = collect(['pipeline:compiling', 'pipeline:running', 'pipeline:log', 'pipeline:success', 'pipeline:error']);

    PipelineRunner.handleBackendEvent({ type: 'phase', phase: 'compiling', message: 'Compilando...' });
    PipelineRunner.handleBackendEvent({ type: 'phase', phase: 'running', message: 'Executando...' });
    PipelineRunner.handleBackendEvent({ type: 'log', line: '[Thread A] ok', isError: false });
    PipelineRunner.handleBackendEvent({ type: 'finished', success: true, elapsedMs: 1234 });
    PipelineRunner.handleBackendEvent({ type: 'finished', success: false, error: 'falhou' });

    expect(seen.map(s => s.name)).toEqual([
      'pipeline:compiling', 'pipeline:running', 'pipeline:log', 'pipeline:success', 'pipeline:error'
    ]);
    expect(seen[3].data.elapsedMs).toBe(1234);
    expect(seen[4].data.error).toBe('falhou');
    dispose();
  });

  it('entrega pedidos de interação ao modal e fecha no timeout/término', () => {
    const { seen, dispose } = collect([
      'pipeline:interaction', 'pipeline:interaction-timeout', 'pipeline:interaction-closed'
    ]);

    PipelineRunner.handleBackendEvent({
      type: 'interaction',
      request: { requestId: 'req-1', nodeId: 'ask', form: { fields: [] } }
    });
    PipelineRunner.handleBackendEvent({ type: 'interaction_timeout', requestId: 'req-1' });
    PipelineRunner.handleBackendEvent({ type: 'interaction_closed', requestId: 'req-1', reason: 'answered' });
    PipelineRunner.handleBackendEvent({ type: 'finished', success: true });

    expect(seen.map(s => s.name)).toEqual([
      'pipeline:interaction', 'pipeline:interaction-timeout', 'pipeline:interaction-closed',
      'pipeline:interaction-closed'
    ]);
    expect(seen[0].data.requestId).toBe('req-1');
    expect(seen[1].data).toBe('req-1');
    expect(seen[2].data.reason).toBe('answered');
    expect(seen[3].data.reason).toBe('finished');
    dispose();
  });

  it('anuncia no Console quantas trilhas independentes o grafo abre', async () => {
    const c = graph.createNode('c', 'Coletar fontes', 'static', 0, 300);
    const d = graph.createNode('d', 'Baixar imagens', 'static', 200, 300);
    graph.createEdge(c.id, c.outputs[0].id, d.id, d.inputs[0].id);

    const { seen, dispose } = collect(['pipeline:log']);
    (globalThis as any).webui = { call: async () => JSON.stringify({ ok: true }) };

    expect(await PipelineRunner.run(graph)).toBe(true);

    const line = seen.find(s => String(s.data.line).includes('trilhas independentes'))?.data.line;
    expect(line).toContain('2 trilhas independentes');
    expect(line).toContain('Tarefa A');
    expect(line).toContain('Coletar fontes');
    dispose();
  });

  it('não anuncia trilhas quando o grafo tem uma única entrada', async () => {
    const { seen, dispose } = collect(['pipeline:log']);
    (globalThis as any).webui = { call: async () => JSON.stringify({ ok: true }) };

    expect(await PipelineRunner.run(graph)).toBe(true);
    expect(seen.some(s => String(s.data.line).includes('trilhas independentes'))).toBe(false);
    dispose();
  });
});
