import { describe, it, expect, beforeEach, beforeAll, afterAll } from 'vitest';
import { NodeView } from '../src/view/NodeView.ts';
import { THEME } from '../src/view/Theme.ts';
import { INodeData } from '../src/core/types.ts';
import { CanvasTooltip } from '../src/ui/CanvasTooltip.ts';
import { MockSvgElement, installMockSvgDocument } from './helpers/mockSvgDom.ts';

describe('NodeView - Vértice lógico com camada vetorial única', () => {
  let sampleNode: INodeData;
  let testSvgRoot: MockSvgElement;
  let tooltip: CanvasTooltip;
  let restoreDocument: () => void;

  beforeAll(() => {
    restoreDocument = installMockSvgDocument();
  });

  afterAll(() => {
    restoreDocument();
  });

  beforeEach(() => {
    testSvgRoot = new MockSvgElement('svg');
    tooltip = new CanvasTooltip();

    sampleNode = {
      id: 'node-test-1',
      type: 'agent',
      label: 'Agente Code Mode de Teste com Título Longo para Validação',
      position: { x: 100, y: 150 },
      inputs: [
        { id: 'p_in_1', name: 'in', direction: 'input', type: 'flow' },
        { id: 'p_in_2', name: 'prompt_in', direction: 'input', type: 'data', dataType: 'std::string' }
      ],
      outputs: [
        { id: 'p_out_1', name: 'out', direction: 'output', type: 'flow' },
        { id: 'p_out_2', name: 'artifact_out', direction: 'output', type: 'data', dataType: 'AgentState' }
      ]
    };
  });

  it('deve adotar proporções legíveis e escala tipográfica precisa do THEME.typography', () => {
    expect(THEME.nodeBody.width).toBe(340);
    expect(THEME.nodeBody.headerHeight).toBe(44);
    expect(THEME.typography.titleSize).toBe(18);
    expect(THEME.typography.badgeSize).toBe(13);
    expect(THEME.typography.portSize).toBe(13);
    expect(THEME.typography.titleColor).toBe('#0F172A');
  });

  it('deve delegar todo o desenho do vértice ao SvgNodeView, sem malhas Three.js duplicadas', () => {
    const nv = new NodeView(sampleNode, testSvgRoot as any, tooltip);
    const group = nv.svgNodeView.group;

    expect(group.getAttribute('class')).toContain('arestrix-svg-vertex');
    expect(group.querySelector('.vertex-body')).not.toBeNull();
    expect(group.querySelector('.vertex-header')).not.toBeNull();
    expect(group.querySelector('.vertex-header-label')).not.toBeNull();
    expect(group.querySelectorAll('.vertex-title-line').length).toBeGreaterThan(0);
    expect(group.querySelectorAll('.vertex-port').length).toBe(4);
    expect(group.querySelectorAll('.vertex-action-btn').length).toBe(5);

    // Não existe mais um segundo grupo visual (Three.js) para o mesmo vértice.
    expect((nv as any).group).toBeUndefined();

    nv.dispose();
  });

  it('deve encaminhar a telemetria para o SvgNodeView e atualizar uma única pílula de status', () => {
    const nv = new NodeView(sampleNode, testSvgRoot as any, tooltip);

    nv.handleTelemetry({
      nodeId: sampleNode.id,
      type: 'status',
      timestamp: Date.now(),
      payload: { status: 'running' }
    });
    expect(nv.getExecutionState()).toBe('running');
    expect(nv.svgNodeView.group.querySelector('.vertex-exec-status-pill')).not.toBeNull();
    expect(nv.svgNodeView.group.querySelector('.vertex-telemetry-badge')).not.toBeNull();

    nv.handleTelemetry({
      nodeId: sampleNode.id,
      type: 'status',
      timestamp: Date.now(),
      payload: { status: 'settled' }
    });
    expect(nv.getExecutionState()).toBe('completed');

    const statusText = nv.svgNodeView.group.querySelector('.vertex-exec-status-text');
    expect(statusText?.textContent).toContain('✓ Concluído');

    // A mudança de status substitui a área anterior; nunca empilha uma segunda.
    expect(nv.svgNodeView.group.querySelectorAll('.vertex-telemetry-badge')).toHaveLength(1);
    expect(nv.svgNodeView.group.querySelectorAll('.vertex-exec-status-pill')).toHaveLength(1);

    nv.dispose();
  });

  it('deve controlar o nível de detalhe (LOD) de acordo com o nível de zoom da câmera', () => {
    const nv = new NodeView(sampleNode, testSvgRoot as any, tooltip);
    const actionBar = nv.svgNodeView.group.querySelector('.vertex-action-bar') as any;

    // Zoom normal (1.0) -> detalhes completos visíveis
    nv.setDetailLevel(1.0);
    expect(nv.isDetailVisible()).toBe(true);
    expect(actionBar.style.display).not.toBe('none');

    // Zoom muito afastado (0.3 < limiar 0.45) -> oculta portas secundárias e barra de ações
    nv.setDetailLevel(0.3);
    expect(nv.isDetailVisible()).toBe(false);
    expect(actionBar.style.display).toBe('none');

    // Zoom aproximado novamente (0.8) -> restaura detalhes
    nv.setDetailLevel(0.8);
    expect(nv.isDetailVisible()).toBe(true);
    expect(actionBar.style.display).toBe('block');

    nv.dispose();
  });

  it('deve derivar a âncora mundial das portas a partir da geometria do SvgNodeView', () => {
    const nv = new NodeView(sampleNode, testSvgRoot as any, tooltip);
    const h = NodeView.computeHeight(sampleNode);
    const b = THEME.nodeBody;
    const t = THEME.typography;

    // Y do SVG é invertido em relação ao mundo (para cima).
    const svgTop = -h / 2 + b.headerHeight + b.titleGap + t.titleSize * 1.25 * b.titleMaxLines + 12;
    const firstInputY = svgTop + 0.5 * b.portSpacing;

    const input = nv.getPortWorldPosition('p_in_1');
    expect(input.x).toBeCloseTo(sampleNode.position.x - b.width / 2, 5);
    expect(input.y).toBeCloseTo(sampleNode.position.y - firstInputY, 5);

    const output = nv.getPortWorldPosition('p_out_1');
    expect(output.x).toBeCloseTo(sampleNode.position.x + b.width / 2, 5);
    expect(output.y).toBeCloseTo(sampleNode.position.y - firstInputY, 5);

    nv.dispose();
  });

  it('deve calcular altura do nó dinamicamente para acomodar múltiplas portas sem sobreposição', () => {
    const manyPortsNode: INodeData = {
      ...sampleNode,
      inputs: [
        { id: 'i1', name: 'in1', direction: 'input', type: 'flow' },
        { id: 'i2', name: 'in2', direction: 'input', type: 'data' },
        { id: 'i3', name: 'in3', direction: 'input', type: 'data' },
        { id: 'i4', name: 'in4', direction: 'input', type: 'data' },
        { id: 'i5', name: 'in5', direction: 'input', type: 'data' }
      ]
    };

    const hStandard = NodeView.computeHeight(sampleNode);
    const hExpanded = NodeView.computeHeight(manyPortsNode);

    expect(hExpanded).toBeGreaterThan(hStandard);
    expect(hStandard).toBeGreaterThanOrEqual(THEME.nodeBody.height);
  });
});
