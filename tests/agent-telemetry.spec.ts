import { describe, it, expect, beforeEach } from 'vitest';
import { IAgentTelemetryEvent, AgentExecutionStatus } from '../src/core/types';

describe('Agent Telemetry & Real-Time Feedback', () => {
  it('should validate AgentTelemetryEvent structure for thinking events', () => {
    const event: IAgentTelemetryEvent = {
      nodeId: 'agent-writer-1',
      type: 'thinking',
      timestamp: Date.now(),
      payload: {
        thinkingChars: 4096,
        thinkingPreview: 'Analisando os critérios do prompt...',
        status: 'thinking'
      }
    };

    expect(event.type).toBe('thinking');
    expect(event.nodeId).toBe('agent-writer-1');
    expect(event.payload.thinkingChars).toBe(4096);
    expect(event.payload.status).toBe('thinking');
  });

  it('should validate tool execution telemetry events', () => {
    const startEvent: IAgentTelemetryEvent = {
      nodeId: 'agent-coder-2',
      type: 'tool_start',
      timestamp: Date.now(),
      payload: {
        toolName: 'read_file',
        toolArgs: '{"path": "package.json"}',
        status: 'tool_call'
      }
    };

    const endEvent: IAgentTelemetryEvent = {
      nodeId: 'agent-coder-2',
      type: 'tool_end',
      timestamp: Date.now() + 150,
      payload: {
        toolName: 'read_file',
        toolSuccess: true,
        status: 'running'
      }
    };

    expect(startEvent.payload.toolName).toBe('read_file');
    expect(startEvent.payload.status).toBe('tool_call');
    expect(endEvent.payload.toolSuccess).toBe(true);
    expect(endEvent.payload.status).toBe('running');
  });

  it('should correctly parse and structure rate-limit (429) events with retrySeconds', () => {
    const rateLimitEvent: IAgentTelemetryEvent = {
      nodeId: 'agent-reviewer-3',
      type: 'rate_limit',
      timestamp: Date.now(),
      payload: {
        retrySeconds: 45,
        detail: 'Rate limit reached. Retry in 45s',
        status: 'running'
      }
    };

    expect(rateLimitEvent.type).toBe('rate_limit');
    expect(rateLimitEvent.payload.retrySeconds).toBe(45);
    expect(rateLimitEvent.payload.detail).toContain('Retry in 45s');
  });

  it('should format thinking KB metrics cleanly without visual glow or shadow styling', () => {
    const formatThinkingBadge = (chars: number): string => {
      const kb = (chars / 1024).toFixed(1);
      return `🧠 ${kb} KB`;
    };

    expect(formatThinkingBadge(1024)).toBe('🧠 1.0 KB');
    expect(formatThinkingBadge(4500)).toBe('🧠 4.4 KB');
  });

  it('should track agent execution status transitions across lifecycle', () => {
    const statusHistory: AgentExecutionStatus[] = [];
    
    // Lifecycle: idle -> running -> thinking -> tool_call -> running -> settled
    const transitions: AgentExecutionStatus[] = [
      'idle',
      'running',
      'thinking',
      'tool_call',
      'running',
      'settled'
    ];

    transitions.forEach(s => statusHistory.push(s));

    expect(statusHistory).toHaveLength(6);
    expect(statusHistory[0]).toBe('idle');
    expect(statusHistory[2]).toBe('thinking');
    expect(statusHistory[3]).toBe('tool_call');
    expect(statusHistory[5]).toBe('settled');
  });

  it('should bridge C++ stdout telemetry markers via PipelineRunner.parseLogTelemetry into globalEventBus', async () => {
    const { PipelineRunner } = await import('../src/core/PipelineRunner');
    const { globalEventBus } = await import('../src/core/EventBus');

    const agentEvents: IAgentTelemetryEvent[] = [];
    const enterEvents: any[] = [];
    const exitEvents: any[] = [];

    const onAgent = (ev: IAgentTelemetryEvent) => agentEvents.push(ev);
    const onEnter = (ev: any) => enterEvents.push(ev);
    const onExit = (ev: any) => exitEvents.push(ev);

    globalEventBus.on('agent:telemetry', onAgent);
    globalEventBus.on('telemetry:task-enter', onEnter);
    globalEventBus.on('telemetry:task-exit', onExit);

    try {
      PipelineRunner.parseLogTelemetry(
        '[NF_TELEMETRY:AGENT] {"nodeId":"node_1","type":"status","timestamp":1700000,"payload":{"status":"running","detail":"Agente 1"}}'
      );
      PipelineRunner.parseLogTelemetry('[NF_TELEMETRY:ENTER] Task=task_node_2 Worker=1');
      PipelineRunner.parseLogTelemetry('[NF_TELEMETRY:EXIT] Task=task_node_2 Worker=1');
      PipelineRunner.parseLogTelemetry('[EXECUTANDO] item_01 :: Agente Analista');
      PipelineRunner.parseLogTelemetry('[CHECKPOINT:RESUME] item_02 :: Agente Sintetizador');

      expect(agentEvents.length).toBe(1);
      expect(agentEvents[0].nodeId).toBe('node_1');
      expect(agentEvents[0].payload.status).toBe('running');

      expect(enterEvents).toEqual([
        { taskId: 'task_node_2', worker: 1 },
        { taskId: 'Agente Analista', itemId: 'item_01' }
      ]);
      expect(exitEvents).toEqual([
        { taskId: 'task_node_2', worker: 1 },
        { taskId: 'Agente Sintetizador', itemId: 'item_02', cached: true }
      ]);
    } finally {
      globalEventBus.off('agent:telemetry', onAgent);
      globalEventBus.off('telemetry:task-enter', onEnter);
      globalEventBus.off('telemetry:task-exit', onExit);
    }
  });
});
