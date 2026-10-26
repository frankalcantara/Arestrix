import { describe, it, expect } from 'vitest';
import * as THREE from 'three';
import { CameraController } from '../src/view/CameraController.ts';

/**
 * Regressão: o zoom pela roda do mouse passava coordenadas já relativas ao canvas
 * para screenToWorld(), que desconta o rect.left/top novamente. Com o canvas
 * deslocado (ribbon/breadcrumbs), o ponto sob o cursor "fugia" ao ampliar.
 */
describe('CameraController - zoom ancorado no cursor', () => {
  it('mantém o ponto do mundo sob o cursor estável mesmo com o canvas deslocado', () => {
    const camera = new THREE.OrthographicCamera(-400, 400, 300, -300, 0.1, 3000);
    camera.position.set(0, 0, 1000);
    camera.updateProjectionMatrix();
    camera.updateMatrixWorld(true);

    const handlers: Record<string, Function[]> = {};
    const domElement: any = {
      clientWidth: 800,
      clientHeight: 600,
      style: {},
      addEventListener: (type: string, fn: Function) => {
        (handlers[type] ||= []).push(fn);
      },
      removeEventListener: (type: string, fn: Function) => {
        handlers[type] = (handlers[type] || []).filter(h => h !== fn);
      },
      getBoundingClientRect: () => ({ left: 200, top: 100, width: 800, height: 600 })
    };

    const controller = new CameraController(camera, domElement, 600);

    // Ponto no quadrante superior esquerdo do canvas, em coordenadas de cliente.
    const clientX = 200 + 200;
    const clientY = 100 + 100;
    const before = controller.screenToWorld(clientX, clientY);

    handlers['wheel'].forEach(handler =>
      handler({ clientX, clientY, deltaY: -100, preventDefault: () => {} })
    );

    camera.updateMatrixWorld(true);
    const after = controller.screenToWorld(clientX, clientY);

    expect(camera.zoom).toBeGreaterThan(1);
    expect(after.x).toBeCloseTo(before.x, 4);
    expect(after.y).toBeCloseTo(before.y, 4);

    controller.dispose();
    expect((handlers['wheel'] || []).length).toBe(0);
  });
});
