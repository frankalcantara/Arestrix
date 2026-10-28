/**
 * Arestrix - Ícones vetoriais Lucide para a camada SVG.
 *
 * Converte ícones do pacote `lucide` (traços SVG 24x24) em grupos SVG nativos,
 * mantendo nitidez em qualquer zoom sem depender da fonte de emoji do sistema.
 */

/** Formato de nó de ícone do lucide 1.x: lista plana de [tag, attrs]. */
export type LucideIconNode = Array<[string, Record<string, string | number | undefined>]>;

const LUCIDE_STROKE_WIDTH = 2.25;

function attrsToString(attrs: Record<string, string | number | undefined>): string {
  return Object.entries(attrs)
    .filter(([key, value]) => key !== 'key' && value !== undefined)
    .map(([key, value]) => `${key}="${String(value).replace(/"/g, '&quot;')}"`)
    .join(' ');
}

/**
 * Gera a marcação SVG completa de um ícone lucide (útil também para HTML/tooltips).
 */
export function lucideToSvgMarkup(icon: LucideIconNode, strokeWidth: number = LUCIDE_STROKE_WIDTH, color: string = '#000000'): string {
  const inner = icon.map(([tag, attrs]) => `<${tag} ${attrsToString(attrs)}/>`).join('');
  return `<svg xmlns="http://www.w3.org/2000/svg" width="24" height="24" viewBox="0 0 24 24" fill="none" stroke="${color}" stroke-width="${strokeWidth}" stroke-linecap="round" stroke-linejoin="round">${inner}</svg>`;
}

/**
 * Cria um grupo SVG nativo com os traços do ícone Lucide, escalado e colorido para exibição no DOM SVG.
 */
export function createLucideSvgElement(
  icon: LucideIconNode,
  size: number = 18,
  color: string = '#000000',
  strokeWidth: number = LUCIDE_STROKE_WIDTH
): SVGGElement {
  const g = document.createElementNS('http://www.w3.org/2000/svg', 'g');
  g.setAttribute('class', 'arestrix-lucide-icon');

  const children = icon;
  children.forEach(([tag, attrs]) => {
    const el = document.createElementNS('http://www.w3.org/2000/svg', tag);
    Object.entries(attrs).forEach(([k, v]) => {
      if (k !== 'key' && v !== undefined) el.setAttribute(k, String(v));
    });
    el.setAttribute('stroke', color);
    el.setAttribute('stroke-width', String(strokeWidth));
    el.setAttribute('stroke-linecap', 'round');
    el.setAttribute('stroke-linejoin', 'round');
    el.setAttribute('fill', 'none');
    g.appendChild(el);
  });

  const scale = size / 24;
  g.setAttribute('transform', `scale(${scale})`);
  return g;
}
