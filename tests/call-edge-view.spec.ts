import { describe, it, expect } from 'vitest';
import { ConnectionView } from '../src/view/ConnectionView.ts';
import { THEME } from '../src/view/Theme.ts';
import { IEdgeData } from '../src/core/types.ts';

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

    const view = new ConnectionView(edge, 800, 600);
    expect(view.line.material.dashed).toBe(true);
    expect(view.line.material.color.getHex()).toBe(THEME.edge.callColor);
    expect(view.edgeData.isFeedbackLoop).toBeFalsy();
    view.dispose();
  });

  it('uma aresta de fluxo comum continua sólida e neutra', () => {
    const edge: IEdgeData = {
      id: 'e_flow',
      sourceNodeId: 'a',
      sourcePortId: 'a_out_0',
      targetNodeId: 'b',
      targetPortId: 'b_in_0'
    };

    const view = new ConnectionView(edge, 800, 600);
    expect(view.line.material.dashed).toBe(false);
    expect(view.line.material.color.getHex()).toBe(THEME.edge.defaultColor);
    view.dispose();
  });
});
