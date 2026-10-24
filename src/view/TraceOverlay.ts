import { Graph } from '../core/Graph.ts';
import { EventBus } from '../core/EventBus.ts';
import { Viewport } from './Viewport.ts';

export interface ITraceTask {
  id?: number;
  nodeId: string;
  label?: string;
  status: string;
  startedAt?: number;
  endedAt?: number;
  durationMs?: number;
  parentCallId?: string;
}

export interface ITraceCall {
  id?: number;
  callId: string;
  callerId: string;
  targetId: string;
  label?: string;
  kind?: string;
  depth?: number;
  status: string;
  startedAt?: number;
  endedAt?: number;
  durationMs?: number;
  parentCallId?: string;
}

export interface ITraceInteraction {
  requestId: string;
  nodeId: string;
  status: string;
  timestamp?: number;
  parentCallId?: string;
}

export interface ITrace {
  version?: number;
  runId: string;
  status: string;
  startedAt?: number;
  updatedAt?: number;
  endedAt?: number;
  durationMs?: number;
  tasks?: ITraceTask[];
  calls?: ITraceCall[];
  interactions?: ITraceInteraction[];
}

export interface ITraceSummary {
  runId: string;
  status: string;
  durationMs: number;
  tasks: number;
  calls: number;
  interactions: number;
  callList: ITraceCall[];
}

/**
 * Overlay do ultimo trace: reaplica no canvas as tasks concluidas/com erro e
 * destaca as arestas de chamada que realmente foram percorridas. A fonte e o
 * trace.json do runtime; a UI nao inventa caminhos.
 */
export class TraceOverlay {
  private bus: EventBus;
  private graph: Graph;
  private viewport: Viewport;

  constructor(bus: EventBus, graph: Graph, viewport: Viewport) {
    this.bus = bus;
    this.graph = graph;
    this.viewport = viewport;
    bus.on('trace:loaded', (trace: ITrace) => this.apply(trace));
  }

  public apply(trace: ITrace): void {
    if (!trace || typeof trace.runId !== 'string') return;

    const tasks = Array.isArray(trace.tasks) ? trace.tasks : [];
    const calls = Array.isArray(trace.calls) ? trace.calls : [];
    const interactions = Array.isArray(trace.interactions) ? trace.interactions : [];

    const nodeState = new Map<string, 'completed' | 'error'>();
    for (const task of tasks) {
      if (!task.nodeId) continue;
      const failed = task.status === 'error' || task.status === 'aborted';
      if (failed) nodeState.set(task.nodeId, 'error');
      else if (!nodeState.has(task.nodeId)) nodeState.set(task.nodeId, 'completed');
    }
    nodeState.forEach((state, nodeId) => {
      this.viewport.setNodeExecutionState(nodeId, state);
    });

    const edgeState = new Map<string, 'completed' | 'error'>();
    for (const call of calls) {
      const edge = this.graph.getAllEdges().find(
        e => e.kind === 'call' && e.sourceNodeId === call.callerId && e.targetNodeId === call.targetId
      );
      if (!edge) continue;
      const failed = call.status === 'error' || call.status === 'aborted';
      if (failed) edgeState.set(edge.id, 'error');
      else if (!edgeState.has(edge.id)) edgeState.set(edge.id, 'completed');
    }
    edgeState.forEach((state, edgeId) => {
      this.viewport.setEdgeExecutionState(edgeId, state);
    });

    const summary: ITraceSummary = {
      runId: trace.runId,
      status: trace.status,
      durationMs: trace.durationMs ?? 0,
      tasks: tasks.length,
      calls: calls.length,
      interactions: interactions.length,
      callList: calls
    };
    // O ConsolePanel consome este resumo; o overlay visual ja foi aplicado.
    this.bus.emit('trace:summary', summary);
  }
}
