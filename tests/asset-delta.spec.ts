import { describe, expect, it } from 'vitest';
import { diffAgentSettings, diffGraphSchemas } from '../src/core/libraries/AssetDelta.ts';
import type { IGraphSchema, INodeAgentSettings } from '../src/core/types.ts';

function graph(overrides: Partial<IGraphSchema> = {}): IGraphSchema {
  return {
    version: '1.0.0',
    metadata: { graphName: 'Teste' } as any,
    nodes: [],
    edges: [],
    ...overrides
  };
}

describe('AssetDelta - diff do aviso de atualização', () => {
  it('conta nós e conexões adicionados, removidos e alterados', () => {
    const current = graph({
      nodes: [
        { id: 'a', label: 'A', type: 'static', position: { x: 0, y: 0 }, inputs: [], outputs: [], parameters: {} },
        { id: 'b', label: 'B', type: 'static', position: { x: 100, y: 0 }, inputs: [], outputs: [], parameters: {} }
      ],
      edges: [
        { id: 'e1', sourceNodeId: 'a', sourcePortId: 'a_out', targetNodeId: 'b', targetPortId: 'b_in', kind: 'flow' }
      ]
    });
    const next = graph({
      nodes: [
        { id: 'a', label: 'A alterado', type: 'static', position: { x: 0, y: 0 }, inputs: [], outputs: [], parameters: {} },
        { id: 'c', label: 'C', type: 'static', position: { x: 200, y: 0 }, inputs: [], outputs: [], parameters: {} }
      ],
      edges: [
        { id: 'e1', sourceNodeId: 'a', sourcePortId: 'a_out', targetNodeId: 'c', targetPortId: 'c_in', kind: 'flow' },
        { id: 'e2', sourceNodeId: 'a', sourcePortId: 'a_out', targetNodeId: 'c', targetPortId: 'c_in', kind: 'flow' }
      ]
    });

    expect(diffGraphSchemas(current, next)).toEqual({
      nodesAdded: 1,
      nodesRemoved: 1,
      nodesChanged: 1,
      edgesAdded: 1,
      edgesRemoved: 0
    });
  });

  it('lista as partes alteradas de agentSettings', () => {
    const base: INodeAgentSettings = {
      inheritSandbox: true,
      inheritModel: true,
      model: { modelId: 'modelo-a' },
      inheritCodeMode: true,
      skills: [{ id: 's1' } as any]
    };
    const next: INodeAgentSettings = {
      ...base,
      model: { modelId: 'modelo-b' },
      skills: [{ id: 's1' } as any, { id: 's2' } as any]
    };

    expect(diffAgentSettings(base, next)).toEqual(['Modelo', 'Skills']);
  });
});
