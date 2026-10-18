import { Graph } from '../core/Graph.ts';
import { EventBus } from '../core/EventBus.ts';
import { IBreadcrumbItem } from '../core/types.ts';
import { t } from '../i18n/index.ts';

export class BreadcrumbsBar {
  private container: HTMLElement;
  private graph: Graph;
  private bus: EventBus;

  constructor(parent: HTMLElement, graph: Graph, bus: EventBus) {
    this.graph = graph;
    this.bus = bus;

    this.container = document.createElement('nav');
    this.container.className = 'breadcrumbs-bar';
    parent.appendChild(this.container);

    this.render();
    this.bindEvents();
  }

  private render(): void {
    const breadcrumbs = this.graph.getBreadcrumbs();
    const isAtRoot = this.graph.isAtRoot();

    let html = `<div class="breadcrumbs-trail">`;

    breadcrumbs.forEach((item: IBreadcrumbItem, index: number) => {
      const isLast = index === breadcrumbs.length - 1;
      const icon = index === 0 ? '🏠' : '📂';
      
      html += `
        <button class="breadcrumb-item ${isLast ? 'active' : ''}" data-index="${index}">
          <span class="breadcrumb-icon">${icon}</span>
          <span class="breadcrumb-text">${this.escapeHtml(item.title)}</span>
        </button>
      `;

      if (!isLast) {
        html += `<span class="breadcrumb-separator">›</span>`;
      }
    });

    html += `</div>`;

    if (!isAtRoot) {
      html += `
        <button id="btn-navigate-up" class="btn-navigate-up" title="${t('breadcrumbs.up_level_tip')}">
          <span>${t('breadcrumbs.up_level')}</span>
        </button>
      `;
    }

    this.container.innerHTML = html;

    // Eventos de clique nos breadcrumbs
    const items = this.container.querySelectorAll('.breadcrumb-item');
    items.forEach(btn => {
      btn.addEventListener('click', (e) => {
        const idx = parseInt((e.currentTarget as HTMLElement).getAttribute('data-index') || '0', 10);
        this.graph.navigateToBreadcrumb(idx);
      });
    });

    this.container.querySelector('#btn-navigate-up')?.addEventListener('click', () => {
      this.graph.navigateUp();
    });
  }

  private bindEvents(): void {
    this.bus.on('graph:hierarchy-changed', () => {
      this.render();
    });
    this.bus.on('graph:loaded', () => {
      this.render();
    });
    this.bus.on('graph:cleared', () => {
      this.render();
    });
    this.bus.on('locale:changed', () => {
      this.render();
    });
  }

  private escapeHtml(text: string): string {
    return text
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
}
