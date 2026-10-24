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
      setNodeExecutionState: vi.fn(),
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

    expect(viewport.setNodeExecutionState).toHaveBeenCalledWith('caller', 'completed');
    expect(viewport.setNodeExecutionState).toHaveBeenCalledWith('target', 'completed');
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
      setNodeExecutionState: vi.fn(),
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

    expect(viewport.setNodeExecutionState).toHaveBeenCalledWith('target', 'error');
    expect(viewport.setEdgeExecutionState).toHaveBeenCalledWith('call_target', 'error');
  });
});
