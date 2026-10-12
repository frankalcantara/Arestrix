/**
 * Arestrix - Tooltip HTML para elementos do canvas WebGL (Fase 15.2)
 *
 * Um único elemento <div class="canvas-tooltip"> reaproveitado para ações do nó, portas
 * e títulos truncados. Exibido após um pequeno atraso de hover e posicionado acima do
 * alvo (coordenadas de tela), com contenção na viewport.
 *
 * REGRA GLOBAL: texto sem sombra/glow (text-shadow: none). Contraste por cor e peso.
 */

export interface ICanvasTooltipContent {
  title: string;
  description?: string;
}

export class CanvasTooltip {
  private el: HTMLElement | null = null;
  private showTimer: ReturnType<typeof setTimeout> | null = null;
  private currentKey: string | null = null;
  private visible: boolean = false;
  private readonly delayMs: number;

  constructor(delayMs: number = 120) {
    this.delayMs = delayMs;
  }

  private ensureElement(): HTMLElement | null {
    if (this.el) return this.el;
    if (typeof document === 'undefined' || !document.createElement) return null;

    const el = document.createElement('div');
    el.className = 'canvas-tooltip';
    el.setAttribute('role', 'tooltip');
    el.style.position = 'fixed';
    el.style.pointerEvents = 'none';
    el.style.display = 'none';
    el.style.zIndex = '4000';
    document.body.appendChild(el);
    this.el = el;
    return el;
  }

  /**
   * Agenda a exibição do tooltip para o alvo identificado por `key`.
   * (x, y) é o ponto de ancoragem em pixels de tela (centro superior do alvo).
   * Se o mesmo alvo já está visível/agendado, apenas reposiciona.
   */
  show(key: string, content: ICanvasTooltipContent, x: number, y: number): void {
    if (this.currentKey === key) {
      if (this.visible) this.position(x, y);
      return;
    }

    this.hide();
    this.currentKey = key;

    this.showTimer = setTimeout(() => {
      this.showTimer = null;
      const el = this.ensureElement();
      if (!el || this.currentKey !== key) return;
      el.innerHTML = renderTooltipHtml(content);
      el.style.display = 'block';
      this.visible = true;
      this.position(x, y);
    }, this.delayMs);
  }

  hide(): void {
    if (this.showTimer) {
      clearTimeout(this.showTimer);
      this.showTimer = null;
    }
    this.currentKey = null;
    this.visible = false;
    if (this.el) {
      this.el.style.display = 'none';
    }
  }

  isVisible(): boolean {
    return this.visible;
  }

  getCurrentKey(): string | null {
    return this.currentKey;
  }

  getElement(): HTMLElement | null {
    return this.el;
  }

  private position(x: number, y: number): void {
    const el = this.el;
    if (!el) return;

    const margin = 8;
    const gap = 10;
    const rect = typeof el.getBoundingClientRect === 'function'
      ? el.getBoundingClientRect()
      : ({ width: 0, height: 0 } as DOMRect);
    const w = rect.width || 0;
    const h = rect.height || 0;
    const viewW = typeof window !== 'undefined' ? window.innerWidth : 1280;

    let left = Math.round(x - w / 2);
    left = Math.max(margin, Math.min(viewW - w - margin, left));

    // Acima do alvo; se não couber, abaixo
    let top = Math.round(y - h - gap);
    if (top < margin) {
      top = Math.round(y + gap + 24);
    }

    el.style.left = `${left}px`;
    el.style.top = `${top}px`;
  }

  dispose(): void {
    this.hide();
    this.el?.parentNode?.removeChild(this.el);
    this.el = null;
  }
}

function escapeHtml(value: string): string {
  return value
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

export function renderTooltipHtml(content: ICanvasTooltipContent): string {
  const desc = content.description
    ? `<div class="canvas-tooltip-desc">${escapeHtml(content.description)}</div>`
    : '';
  return `<div class="canvas-tooltip-title">${escapeHtml(content.title)}</div>${desc}`;
}
