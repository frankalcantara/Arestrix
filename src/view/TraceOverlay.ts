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
  /** Maior número de tarefas executando ao mesmo tempo, calculado dos startedAt/endedAt. */
  maxConcurrency: number;
  callList: ITraceCall[];
}

/**
 * Pico de concorrência observado no trace. Um run com duas trilhas independentes mostra 2.
 *
 * Eventos no mesmo instante são resolvidos com o término antes do início: tarefas encadeadas
 * (uma termina quando a outra começa) não contam como simultâneas. Tarefas sem startedAt são
 * ignoradas; sem endedAt, valem como instante único.
 */
function computeMaxConcurrency(tasks: ITraceTask[]): number {
  const events: Array<{ at: number; delta: number }> = [];
  for (const task of tasks) {
    if (typeof task.startedAt !== 'number' || !Number.isFinite(task.startedAt)) continue;
    const start = task.startedAt;
    const end = typeof task.endedAt === 'number' && task.endedAt >= start ? task.endedAt : start;
    events.push({ at: start, delta: 1 });
    events.push({ at: end, delta: -1 });
  }
  events.sort((a, b) => a.at - b.at || a.delta - b.delta);

  let current = 0;
  let peak = 0;
  for (const event of events) {
    current += event.delta;
    if (current > peak) peak = current;
  }
  return peak;
}

/**
 * Overlay do ultimo trace: reaplica no canvas o RESULTADO das tasks (sem deixar
 * os vértices em modo de execução) e destaca as arestas de chamada que realmente
 * foram percorridas. A fonte e o trace.json do runtime; a UI nao inventa caminhos.
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

    const nodeResults = new Map<string, { outcome: 'success' | 'error'; durationMs: number }>();
    for (const task of tasks) {
      if (!task.nodeId) continue;
      const failed = task.status === 'error' || task.status === 'aborted';
      if (failed) {
        nodeResults.set(task.nodeId, { outcome: 'error', durationMs: task.durationMs ?? 0 });
      } else if (!nodeResults.has(task.nodeId)) {
        nodeResults.set(task.nodeId, { outcome: 'success', durationMs: task.durationMs ?? 0 });
      }
    }
    const finishedAt = Date.now();
    nodeResults.forEach((result, nodeId) => {
      this.viewport.setNodeRunRecord(nodeId, { ...result, finishedAt });
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
      maxConcurrency: computeMaxConcurrency(tasks),
      callList: calls
    };
    // O ConsolePanel consome este resumo; o overlay visual ja foi aplicado.
    this.bus.emit('trace:summary', summary);
  }
}
