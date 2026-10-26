import * as THREE from 'three';
import { Text } from 'troika-three-text';
import font500Url from '../../../third_party/fonts/inter/inter-latin-500-normal.woff?url';
import font600Url from '../../../third_party/fonts/inter/inter-latin-600-normal.woff?url';
import font700Url from '../../../third_party/fonts/inter/inter-latin-700-normal.woff?url';

// Compatibilidade com ambientes sem self nativo (ex.: runner Node/Vitest)
if (typeof self === 'undefined' && typeof globalThis !== 'undefined') {
  (globalThis as any).self = globalThis;
}

/**
 * Arestrix - Fábrica de Tipografia Vetorial SDF (Fase 15.2)
 *
 * Substitui os antigos sprites rasterizados (canvas 2D -> CanvasTexture) por texto
 * renderizado via Signed Distance Fields (troika-three-text):
 * - Nítido em qualquer nível de zoom (sem borrão de magnificação nem mipmap esmaecido)
 * - Sem distorção de proporção (glifos medidos e posicionados em unidades de mundo)
 * - Fonte Inter embarcada localmente (OFL, third_party/fonts/inter) — sem dependência de CDN
 *
 * REGRA GLOBAL: nenhum outline/blur/stroke é aplicado ao texto (zero sombras/glows).
 * Destaque somente por cor, peso tipográfico ou opacidade.
 */

export type NodeFontWeight = 500 | 600 | 700;

const FONT_BY_WEIGHT: Record<NodeFontWeight, string> = {
  500: font500Url,
  600: font600Url,
  700: font700Url
};

export interface INodeLabelOptions {
  text: string;
  size: number;
  color: string | number;
  weight?: NodeFontWeight;
  anchorX?: 'left' | 'center' | 'right';
  anchorY?: 'top' | 'top-baseline' | 'middle' | 'bottom-baseline' | 'bottom';
  maxWidth?: number;
  letterSpacing?: number;
  lineHeight?: number;
  textAlign?: 'left' | 'right' | 'center';
  userData?: Record<string, unknown>;
  z?: number;
}

/**
 * Retorna a URL da fonte embarcada para o peso solicitado.
 */
export function fontUrlForWeight(weight: NodeFontWeight = 600): string {
  return FONT_BY_WEIGHT[weight] ?? FONT_BY_WEIGHT[600];
}

/**
 * Cria um rótulo vetorial SDF pronto para ser adicionado a um THREE.Group.
 */
export function createNodeLabel(opts: INodeLabelOptions): Text {
  const label = new Text();
  label.text = opts.text;
  label.font = fontUrlForWeight(opts.weight);
  label.fontSize = opts.size;
  label.color = opts.color;
  label.anchorX = opts.anchorX ?? 'left';
  label.anchorY = opts.anchorY ?? 'middle';
  label.letterSpacing = opts.letterSpacing ?? 0;
  label.lineHeight = opts.lineHeight ?? 1.2;
  label.textAlign = opts.textAlign ?? 'left';
  if (opts.maxWidth !== undefined) {
    label.maxWidth = opts.maxWidth;
    label.overflowWrap = 'break-word';
  } else {
    label.whiteSpace = 'nowrap';
  }

  // Garantia explícita da regra global: zero contorno, desfoque ou traço no texto
  label.outlineWidth = 0;
  label.outlineBlur = 0;
  label.strokeWidth = 0;

  label.position.z = opts.z ?? 5;
  label.userData = { ...(opts.userData ?? {}), isVectorText: true };
  // Texto não participa do raycast: a seleção é feita pelo corpo/hitboxes do nó
  label.raycast = () => {};
  label.sync();
  return label;
}

/**
 * Atualiza texto e/ou cor de um rótulo existente e reagenda a tipografia.
 */
export function updateNodeLabel(label: Text, text?: string, color?: string | number): void {
  if (text !== undefined) label.text = text;
  if (color !== undefined) label.color = color;
  label.sync();
}

export function disposeNodeLabel(label: Text | undefined | null): void {
  if (!label) return;
  label.parent?.remove(label);
  label.dispose();
}

/**
 * Estimativa de largura média de glifo da Inter (em fração do tamanho da fonte).
 * Usada para truncamento determinístico antes da tipografia assíncrona do troika.
 * 0.6 dá margem de segurança para maiúsculas e acentos sem estourar a largura do cartão.
 */
const INTER_AVG_CHAR_WIDTH = 0.6;

/**
 * Trunca um texto para caber em `maxLines` linhas de largura `maxWidth` com reticências.
 * Retorna o texto final (com `\n` entre as linhas) e se houve truncamento
 * (para exibir o texto completo em tooltip).
 *
 * O SVG não quebra texto automaticamente, e o troika também respeita as quebras
 * explícitas: por isso a quebra é feita aqui, preferindo espaços para não cortar palavras.
 */
export function fitTextToLines(
  text: string,
  fontSize: number,
  maxWidth: number,
  maxLines: number
): { text: string; truncated: boolean } {
  const clean = (text ?? '').replace(/\s+/g, ' ').trim();
  const charsPerLine = Math.max(4, Math.floor(maxWidth / (fontSize * INTER_AVG_CHAR_WIDTH)));
  const lineLimit = Math.max(1, maxLines);

  const lines: string[] = [];
  let rest = clean;

  while (rest.length > 0 && lines.length < lineLimit) {
    if (rest.length <= charsPerLine) {
      lines.push(rest);
      rest = '';
      break;
    }

    // Prefere o último espaço da faixa; se ele estiver muito no começo, corta no limite.
    const candidate = rest.slice(0, charsPerLine + 1);
    const spaceIndex = candidate.lastIndexOf(' ');
    const cut = spaceIndex > charsPerLine * 0.5 ? spaceIndex : charsPerLine;

    lines.push(rest.slice(0, cut).trimEnd());
    rest = rest.slice(cut).trimStart();
  }

  const truncated = rest.length > 0;
  if (truncated && lines.length > 0) {
    const lastIndex = lines.length - 1;
    lines[lastIndex] = lines[lastIndex].slice(0, Math.max(1, charsPerLine - 1)).trimEnd() + '…';
  }

  return { text: lines.join('\n'), truncated };
}

/**
 * Converte um número hexadecimal de cor three.js para string CSS (#rrggbb).
 */
export function hexToCss(hex: number): string {
  return `#${new THREE.Color(hex).getHexString()}`;
}
