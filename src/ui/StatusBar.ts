import { Graph } from '../core/Graph.ts';
import { Viewport } from '../view/Viewport.ts';
import { EventBus } from '../core/EventBus.ts';
import { t, getLocale, toggleLocale } from '../i18n/index.ts';

/**
 * Arestrix Studio - Microsoft Office Status Bar
 * Barra de status autêntica no padrão Microsoft Office.
 * Exibe status em tempo real, modo de compilação, estatísticas e o clássico Zoom Slider.
 */
export class StatusBar {
  private container: HTMLElement;
  private graph: Graph;
  private viewport: Viewport;
  private bus: EventBus;

  private statusDotEl!: HTMLElement;
  private statusTextEl!: HTMLElement;
  private statsTextEl!: HTMLElement;
  private zoomSliderEl!: HTMLInputElement;
  private zoomLabelEl!: HTMLElement;

  constructor(parent: HTMLElement, graph: Graph, viewport: Viewport, bus: EventBus) {
    this.graph = graph;
    this.viewport = viewport;
    this.bus = bus;

    this.container = document.createElement('footer');
    this.container.className = 'office-statusbar';
    parent.appendChild(this.container);

    this.render();
    this.bindEvents();
    this.updateStats();
  }

  public getContainer(): HTMLElement {
    return this.container;
  }

  private render(): void {
    const isPt = getLocale() === 'pt-BR';
    this.container.innerHTML = `
      <div class="office-statusbar-left">
        <span class="office-status-dot status-ready" id="sb-status-dot" title="${t('statusbar.system_status_tip')}"></span>
        <span class="office-status-text" id="sb-status-text">${t('statusbar.ready')}</span>
        <span class="office-status-sep">|</span>
        <span class="office-status-stats" id="sb-stats-text">0 vértices, 0 conexões</span>
      </div>

      <div class="office-statusbar-center">
        <span class="office-status-mode">${t('statusbar.mode')}</span>
      </div>

      <div class="office-statusbar-right">
        <button id="sb-btn-grid" class="office-sb-btn" title="${t('statusbar.grid_tip')}">
          <span class="office-sb-icon">⊞</span> ${t('statusbar.grid')}
        </button>
        <button id="sb-btn-fit" class="office-sb-btn" title="${t('statusbar.fit_tip')}">
          <span class="office-sb-icon">🎯</span> ${t('statusbar.fit')}
        </button>
        <button id="sb-btn-lang" class="office-sb-btn office-sb-lang" title="${t('statusbar.language_tip')}">
          <span>🌐</span> ${isPt ? 'PT-BR' : 'EN-US'}
        </button>

        <div class="office-zoom-control">
          <button id="sb-zoom-out" class="office-zoom-btn" title="${t('statusbar.zoom_out_tip')}">−</button>
          <input type="range" id="sb-zoom-slider" class="office-zoom-slider" min="20" max="350" value="100" title="${t('statusbar.zoom_slider_tip')}" />
          <button id="sb-zoom-in" class="office-zoom-btn" title="${t('statusbar.zoom_in_tip')}">+</button>
          <span id="sb-zoom-label" class="office-zoom-label" title="${t('statusbar.zoom_reset_tip')}">100%</span>
        </div>
      </div>
    `;

    this.statusDotEl = this.container.querySelector('#sb-status-dot') as HTMLElement;
    this.statusTextEl = this.container.querySelector('#sb-status-text') as HTMLElement;
    this.statsTextEl = this.container.querySelector('#sb-stats-text') as HTMLElement;
    this.zoomSliderEl = this.container.querySelector('#sb-zoom-slider') as HTMLInputElement;
    this.zoomLabelEl = this.container.querySelector('#sb-zoom-label') as HTMLElement;
  }

  private bindEvents(): void {
    // Zoom Slider e botões
    this.zoomSliderEl.addEventListener('input', () => {
      const zoomVal = parseInt(this.zoomSliderEl.value, 10) / 100;
      this.viewport.getCameraController().setZoom(zoomVal);
      this.zoomLabelEl.textContent = `${this.zoomSliderEl.value}%`;
    });

    this.container.querySelector('#sb-zoom-out')?.addEventListener('click', () => {
      this.viewport.getCameraController().zoomOut();
    });

    this.container.querySelector('#sb-zoom-in')?.addEventListener('click', () => {
      this.viewport.getCameraController().zoomIn();
    });

    this.zoomLabelEl.addEventListener('click', () => {
      this.viewport.getCameraController().reset();
    });

    // Botões de Grade e Ajuste
    this.container.querySelector('#sb-btn-grid')?.addEventListener('click', () => {
      const isVisible = this.viewport.toggleGrid();
      const btn = this.container.querySelector('#sb-btn-grid') as HTMLElement;
      if (btn) {
        btn.classList.toggle('active', isVisible);
      }
    });

    this.container.querySelector('#sb-btn-fit')?.addEventListener('click', () => {
      this.viewport.zoomToFit();
    });

    this.container.querySelector('#sb-btn-lang')?.addEventListener('click', async () => {
      await toggleLocale();
    });

    this.bus.on('locale:changed', () => {
      this.render();
      this.bindEvents();
      this.updateStats();
    });

    // Eventos da Câmera (sincronização bidirecional de zoom)
    this.bus.on('camera:zoom-changed', ({ zoom }: { zoom: number }) => {
      const pct = Math.round(zoom * 100);
      this.zoomSliderEl.value = pct.toString();
      this.zoomLabelEl.textContent = `${pct}%`;
    });

    // Atualização de Estatísticas do Grafo
    const update = () => this.updateStats();
    this.bus.on('node:added', update);
    this.bus.on('node:removed', update);
    this.bus.on('edge:added', update);
    this.bus.on('edge:removed', update);
    this.bus.on('graph:loaded', update);
    this.bus.on('graph:cleared', update);

    // Eventos do Pipeline
    this.bus.on('pipeline:compiling', () => {
      this.setStatus(t('statusbar.compiling'), 'status-compiling');
    });

    this.bus.on('pipeline:running', () => {
      this.setStatus(t('statusbar.running'), 'status-running');
    });

    this.bus.on('pipeline:success', (data: { elapsedMs: number }) => {
      this.setStatus(`${t('common.success')} (${data.elapsedMs}ms)`, 'status-ready');
    });

    this.bus.on('pipeline:error', () => {
      this.setStatus(t('statusbar.error'), 'status-error');
    });
  }

  private updateStats(): void {
    const nodeCount = this.graph.getAllNodes().length;
    const edgeCount = this.graph.getAllEdges().length;
    this.statsTextEl.textContent = t('statusbar.stats', { nodes: nodeCount, edges: edgeCount });
  }

  public setStatus(text: string, dotClass: 'status-ready' | 'status-compiling' | 'status-running' | 'status-error'): void {
    this.statusTextEl.textContent = text;
    this.statusDotEl.className = `office-status-dot ${dotClass}`;
  }
}
