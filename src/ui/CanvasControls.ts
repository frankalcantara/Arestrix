import { Viewport } from '../view/Viewport.ts';
import { EventBus } from '../core/EventBus.ts';

/**
 * Arestrix Studio - Canvas Controls (Visio HUD)
 * Mini-barra de ferramentas flutuante sobre o canvas no padrão Microsoft Visio / Office.
 */
export class CanvasControls {
  private container: HTMLElement;
  private viewport: Viewport;
  private bus: EventBus;

  constructor(parent: HTMLElement, viewport: Viewport, bus: EventBus) {
    this.viewport = viewport;
    this.bus = bus;

    this.container = document.createElement('div');
    this.container.className = 'office-canvas-controls';
    parent.appendChild(this.container);

    this.render();
    this.bindEvents();
  }

  private render(): void {
    this.container.innerHTML = `
      <button id="canvas-ctrl-fit" class="canvas-hud-btn" title="Ajustar ao Conteúdo (Ctrl+0)">
        <span>🎯</span>
      </button>
      <button id="canvas-ctrl-zoom-in" class="canvas-hud-btn" title="Aumentar Zoom (Ctrl +)">
        <span>➕</span>
      </button>
      <button id="canvas-ctrl-zoom-out" class="canvas-hud-btn" title="Diminuir Zoom (Ctrl -)">
        <span>➖</span>
      </button>
      <button id="canvas-ctrl-reset" class="canvas-hud-btn" title="Redefinir Zoom para 100%">
        <span>1:1</span>
      </button>
      <div class="canvas-hud-sep"></div>
      <button id="canvas-ctrl-grid" class="canvas-hud-btn active" title="Alternar Grade Visio">
        <span>⊞</span>
      </button>
    `;
  }

  private bindEvents(): void {
    this.container.querySelector('#canvas-ctrl-fit')?.addEventListener('click', () => {
      this.viewport.zoomToFit();
    });

    this.container.querySelector('#canvas-ctrl-zoom-in')?.addEventListener('click', () => {
      this.viewport.getCameraController().zoomIn();
    });

    this.container.querySelector('#canvas-ctrl-zoom-out')?.addEventListener('click', () => {
      this.viewport.getCameraController().zoomOut();
    });

    this.container.querySelector('#canvas-ctrl-reset')?.addEventListener('click', () => {
      this.viewport.getCameraController().reset();
    });

    const gridBtn = this.container.querySelector('#canvas-ctrl-grid') as HTMLElement;
    gridBtn?.addEventListener('click', () => {
      const isVisible = this.viewport.toggleGrid();
      gridBtn.classList.toggle('active', isVisible);
    });

    // Escuta evento do grafo para atualizar visibilidade se necessário
    this.bus.on('graph:loaded', () => {
      gridBtn?.classList.add('active');
    });
  }
}
