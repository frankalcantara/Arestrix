import { describe, it, expect, vi } from 'vitest';
import { Graph } from '../src/core/Graph.ts';
import { EventBus } from '../src/core/EventBus.ts';
import { TraceOverlay } from '../src/view/TraceOverlay.ts';

describe('TraceOverlay (Fase 4F)', () => {
  it('reaplica tasks e destaca a aresta de chamada percorrida', () => {
    const bus = new EventBus();
    const graph = new Graph(bus);
    const caller = graph.createNode('caller', 'Caller', 'agent', 0, 0);
    const target = graph.createNode('target', 'Target', 'agent', 200, 0);
    graph.addEdge({
      id: 'call_target',
      sourceNodeId: caller.id,
      sourcePortId: caller.outputs[2].id,
      targetNodeId: target.id,
      targetPortId: target.inputs[0].id,
      kind: 'call',
      callSettings: { maxCalls: 1, onError: 'fail' }
    });

    const viewport = {
      setNodeRunRecord: vi.fn(),
      setEdgeExecutionState: vi.fn()
    };
    new TraceOverlay(bus, graph, viewport as any);
    const summaries: any[] = [];
    bus.on('trace:summary', (summary: any) => summaries.push(summary));

    bus.emit('trace:loaded', {
      runId: 'run-test',
      status: 'success',
      durationMs: 120,
      tasks: [
        { nodeId: 'caller', status: 'ok' },
        { nodeId: 'target', status: 'ok' }
      ],
      calls: [
        { callId: 'caller/target#1', callerId: 'caller', targetId: 'target', status: 'ok', durationMs: 40 }
      ],
      interactions: []
    });

    expect(viewport.setNodeRunRecord).toHaveBeenCalledWith(
      'caller',
      expect.objectContaining({ outcome: 'success' })
    );
    expect(viewport.setNodeRunRecord).toHaveBeenCalledWith(
      'target',
      expect.objectContaining({ outcome: 'success' })
    );
    expect(viewport.setEdgeExecutionState).toHaveBeenCalledWith('call_target', 'completed');
    expect(summaries).toHaveLength(1);
    expect(summaries[0].calls).toBe(1);
    expect(summaries[0].callList[0].targetId).toBe('target');
  });

  it('marca erro na task e na aresta', () => {
    const bus = new EventBus();
    const graph = new Graph(bus);
    const caller = graph.createNode('caller', 'Caller', 'agent', 0, 0);
    const target = graph.createNode('target', 'Target', 'agent', 200, 0);
    graph.addEdge({
      id: 'call_target',
      sourceNodeId: caller.id,
      sourcePortId: caller.outputs[2].id,
      targetNodeId: target.id,
      targetPortId: target.inputs[0].id,
      kind: 'call',
      callSettings: { maxCalls: 1, onError: 'fail' }
    });

    const viewport = {
      setNodeRunRecord: vi.fn(),
      setEdgeExecutionState: vi.fn()
    };
    new TraceOverlay(bus, graph, viewport as any);
    bus.emit('trace:loaded', {
      runId: 'run-error',
      status: 'failed',
      tasks: [{ nodeId: 'target', status: 'error' }],
      calls: [{ callId: 'c1', callerId: 'caller', targetId: 'target', status: 'error' }],
      interactions: []
    });

    expect(viewport.setNodeRunRecord).toHaveBeenCalledWith(
      'target',
      expect.objectContaining({ outcome: 'error' })
    );
    expect(viewport.setEdgeExecutionState).toHaveBeenCalledWith('call_target', 'error');
  });

  it('mede o pico de concorrência a partir dos horários do trace', () => {
    const bus = new EventBus();
    const graph = new Graph(bus);
    const viewport = { setNodeRunRecord: vi.fn(), setEdgeExecutionState: vi.fn() };
    new TraceOverlay(bus, graph, viewport as any);

    const summaries: any[] = [];
    bus.on('trace:summary', (summary: any) => summaries.push(summary));

    bus.emit('trace:loaded', {
      runId: 'run-parallel',
      status: 'success',
      tasks: [
        { nodeId: 'a', status: 'ok', startedAt: 0, endedAt: 100 },
        { nodeId: 'b', status: 'ok', startedAt: 50, endedAt: 150 },
        // começa exatamente quando b termina: encadeada, não simultânea
        { nodeId: 'c', status: 'ok', startedAt: 150, endedAt: 200 }
      ],
      calls: [],
      interactions: []
    });

    expect(summaries[0].maxConcurrency).toBe(2);
  });

  it('trilhas encadeadas ficam em concorrência 1', () => {
    const bus = new EventBus();
    const graph = new Graph(bus);
    const viewport = { setNodeRunRecord: vi.fn(), setEdgeExecutionState: vi.fn() };
    new TraceOverlay(bus, graph, viewport as any);

    const summaries: any[] = [];
    bus.on('trace:summary', (summary: any) => summaries.push(summary));

    bus.emit('trace:loaded', {
      runId: 'run-serial',
      status: 'success',
      tasks: [
        { nodeId: 'a', status: 'ok', startedAt: 0, endedAt: 100 },
        { nodeId: 'b', status: 'ok', startedAt: 100, endedAt: 200 }
      ],
      calls: [],
      interactions: []
    });

    expect(summaries[0].maxConcurrency).toBe(1);
  });
});
