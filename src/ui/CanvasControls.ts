import { Viewport } from '../view/Viewport.ts';
import { EventBus } from '../core/EventBus.ts';
import { t } from '../i18n/index.ts';

/**
 * Arestrix Studio - Canvas Controls (HUD)
 * Mini-barra de ferramentas flutuante sobre o canvas no padrão Microsoft Office.
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
    this.bindDomEvents();
    this.bindBusEvents();
  }

  private render(): void {
    const isFollow = this.viewport.isFollowingExecution();
    this.container.innerHTML = `
      <button id="canvas-ctrl-layout" class="canvas-hud-btn" title="${t('canvas_controls.auto_layout_tip')}">
        <span>✨</span>
      </button>
      <button id="canvas-ctrl-follow" class="canvas-hud-btn ${isFollow ? 'active' : ''}" title="${t('canvas_controls.follow_exec_tip')}">
        <span>📡</span>
      </button>
      <button id="canvas-ctrl-clear-exec" class="canvas-hud-btn" title="${t('canvas_controls.clear_exec_tip')}">
        <span>🧹</span>
      </button>
      <div class="canvas-hud-sep"></div>
      <button id="canvas-ctrl-fit" class="canvas-hud-btn" title="${t('canvas_controls.fit_tip')}">
        <span>🎯</span>
      </button>
      <button id="canvas-ctrl-zoom-in" class="canvas-hud-btn" title="${t('canvas_controls.zoom_in_tip')}">
        <span>➕</span>
      </button>
      <button id="canvas-ctrl-zoom-out" class="canvas-hud-btn" title="${t('canvas_controls.zoom_out_tip')}">
        <span>➖</span>
      </button>
      <button id="canvas-ctrl-reset" class="canvas-hud-btn" title="${t('canvas_controls.reset_tip')}">
        <span>1:1</span>
      </button>
      <div class="canvas-hud-sep"></div>
      <button id="canvas-ctrl-grid" class="canvas-hud-btn active" title="${t('canvas_controls.grid_tip')}">
        <span>⊞</span>
      </button>
    `;
  }

  private bindDomEvents(): void {
    this.container.querySelector('#canvas-ctrl-layout')?.addEventListener('click', () => {
      this.viewport.autoLayout();
    });

    const followBtn = this.container.querySelector('#canvas-ctrl-follow') as HTMLElement;
    followBtn?.addEventListener('click', () => {
      const enabled = this.viewport.toggleFollowExecution();
      followBtn.classList.toggle('active', enabled);
    });

    this.container.querySelector('#canvas-ctrl-clear-exec')?.addEventListener('click', () => {
      this.viewport.resetExecutionVisuals();
    });

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
  }

  private bindBusEvents(): void {
    this.bus.on('graph:loaded', () => {
      const gridBtn = this.container.querySelector('#canvas-ctrl-grid') as HTMLElement;
      gridBtn?.classList.add('active');
    });

    this.bus.on('canvas:follow-execution', (enabled: boolean) => {
      const followBtn = this.container.querySelector('#canvas-ctrl-follow') as HTMLElement;
      followBtn?.classList.toggle('active', enabled);
    });

    this.bus.on('locale:changed', () => {
      this.render();
      this.bindDomEvents();
    });
  }
}
