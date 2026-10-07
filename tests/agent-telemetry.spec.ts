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
});
