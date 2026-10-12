import { describe, it, expect, beforeEach, beforeAll, afterAll } from 'vitest';
import * as THREE from 'three';
import { NodeView } from '../src/view/NodeView.ts';
import { THEME } from '../src/view/Theme.ts';
import { INodeData } from '../src/core/types.ts';

describe('NodeView - Tipografia Vetorial SDF e Geometria Nítida (Fase 15.2)', () => {
  let sampleNode: INodeData;
  let origDoc: any;
  let origWin: any;

  beforeAll(() => {
    origDoc = (globalThis as any).document;
    origWin = (globalThis as any).window;

    if (typeof (globalThis as any).document === 'undefined') {
      (globalThis as any).document = {
        createElement: (tag: string) => ({
          tagName: tag.toUpperCase(),
          getContext: () => ({
            fillRect: () => {},
            clearRect: () => {},
            getImageData: () => ({ data: new Uint8Array(4) }),
            putImageData: () => {},
            createImageData: () => ({ data: new Uint8Array(4) }),
            setTransform: () => {},
            drawImage: () => {},
            save: () => {},
            fillText: () => {},
            restore: () => {},
            beginPath: () => {},
            moveTo: () => {},
            lineTo: () => {},
            closePath: () => {},
            stroke: () => {},
            translate: () => {},
            scale: () => {},
            rotate: () => {},
            arc: () => {},
            fill: () => {},
            measureText: () => ({ width: 10 })
          }),
          style: {},
          width: 256,
          height: 256,
          addEventListener: () => {},
          removeEventListener: () => {},
          setAttribute: () => {},
          getAttribute: () => null
        }),
        body: { appendChild: () => {}, removeChild: () => {} }
      };
    }

    if (typeof (globalThis as any).window === 'undefined') {
      (globalThis as any).window = {
        innerWidth: 1280,
        innerHeight: 800,
        devicePixelRatio: 1,
        addEventListener: () => {},
        removeEventListener: () => {}
      };
    }
  });

  afterAll(() => {
    (globalThis as any).document = origDoc;
    (globalThis as any).window = origWin;
  });

  beforeEach(() => {
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

  it('deve instanciar o nó com tipografia vetorial sem recorrer a CanvasTexture rasterizado', () => {
    const nv = new NodeView(sampleNode);
    expect(nv.group).toBeDefined();

    // Varre todos os objetos e materiais da cena do nó
    let canvasTextureCount = 0;
    let vectorTextCount = 0;
    let hasOutlineOrBlur = false;

    nv.group.traverse((obj: any) => {
      if (obj.material?.map instanceof THREE.CanvasTexture) {
        canvasTextureCount++;
      }
      if (obj.userData?.isVectorText) {
        vectorTextCount++;
        // Garante estrita conformidade com a regra: zero sombras/glows em texto
        if (obj.outlineWidth > 0 || obj.outlineBlur > 0 || obj.strokeWidth > 0) {
          hasOutlineOrBlur = true;
        }
      }
    });

    // Zero texturas de canvas geradas (antigos sprites raster eliminados)
    expect(canvasTextureCount).toBe(0);
    // Presença de rótulos vetoriais SDF (tipo, título e 4 portas)
    expect(vectorTextCount).toBeGreaterThanOrEqual(6);
    // Nenhuma sombra, blur ou contorno em tipografia
    expect(hasOutlineOrBlur).toBe(false);

    nv.dispose();
  });

  it('deve controlar o nível de detalhe (LOD) de acordo com o nível de zoom da câmera', () => {
    const nv = new NodeView(sampleNode);

    // Zoom normal (1.0) -> detalhes completos visíveis
    nv.setDetailLevel(1.0);
    expect(nv.isDetailVisible()).toBe(true);

    // Zoom muito afastado (0.3 < limiar 0.45) -> oculta portas secundárias e barra de ações
    nv.setDetailLevel(0.3);
    expect(nv.isDetailVisible()).toBe(false);

    // Zoom aproximado novamente (0.8) -> restaura detalhes
    nv.setDetailLevel(0.8);
    expect(nv.isDetailVisible()).toBe(true);

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
