import { describe, it, expect } from 'vitest';
import { ConnectionView, getEdgeBaseStyle } from '../src/view/ConnectionView.ts';
import { THEME } from '../src/view/Theme.ts';
import { IEdgeData } from '../src/core/types.ts';

function css(hex: number): string {
  return `#${hex.toString(16).padStart(6, '0').toUpperCase()}`;
}

describe('Aresta de chamada (kind = call)', () => {
  it('é desenhada tracejada em violeta, sem virar feedback', () => {
    const edge: IEdgeData = {
      id: 'e_call',
      sourceNodeId: 'a',
      sourcePortId: 'a_out_call',
      targetNodeId: 'b',
      targetPortId: 'b_in_0',
      kind: 'call',
      callSettings: { maxCalls: 1, onError: 'fail' }
    };

    const view = new ConnectionView(edge);
    const style = view.getBaseStyle();
    expect(style.dashArray).not.toBeNull();
    expect(style.stroke).toBe(css(THEME.edge.callColor));
    expect(view.edgeData.isFeedbackLoop).toBeFalsy();
    expect(getEdgeBaseStyle(edge)).toEqual(style);
  });

  it('uma aresta de fluxo comum continua sólida e neutra', () => {
    const edge: IEdgeData = {
      id: 'e_flow',
      sourceNodeId: 'a',
      sourcePortId: 'a_out_0',
      targetNodeId: 'b',
      targetPortId: 'b_in_0'
    };

    const view = new ConnectionView(edge);
    const style = view.getBaseStyle();
    expect(style.dashArray).toBeNull();
    expect(style.stroke).toBe(css(THEME.edge.defaultColor));
  });

  it('uma aresta de feedback usa o tracejado âmbar mesmo sem kind = call', () => {
    const edge: IEdgeData = {
      id: 'e_feedback',
      sourceNodeId: 'b',
      sourcePortId: 'b_out_0',
      targetNodeId: 'a',
      targetPortId: 'a_in_0',
      isFeedbackLoop: true
    };

    const style = getEdgeBaseStyle(edge);
    expect(style.stroke).toBe(css(THEME.edge.feedbackColor));
    expect(style.dashArray).not.toBeNull();
  });
});
