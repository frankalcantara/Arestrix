/**
 * Declarações mínimas para troika-three-text (o pacote não publica tipos).
 * Cobre apenas a superfície usada pelo Arestrix.
 */
declare module 'troika-three-text' {
  import { Mesh, Material, Color } from 'three';

  export class Text extends Mesh {
    text: string;
    font: string | null;
    fontSize: number;
    fontWeight: number | string;
    color: string | number | Color;
    anchorX: number | string;
    anchorY: number | string;
    maxWidth: number;
    lineHeight: number | string;
    letterSpacing: number;
    textAlign: 'left' | 'right' | 'center' | 'justify';
    overflowWrap: 'normal' | 'break-word';
    whiteSpace: 'normal' | 'nowrap';
    outlineWidth: number | string;
    outlineBlur: number | string;
    outlineOpacity: number;
    strokeWidth: number | string;
    fillOpacity: number;
    depthOffset: number;
    sdfGlyphSize: number;
    material: Material;
    textRenderInfo: {
      blockBounds: [number, number, number, number];
      visibleBounds: [number, number, number, number];
    } | null;
    sync(callback?: () => void): void;
    dispose(): void;
  }

  export function configureTextBuilder(config: {
    defaultFontURL?: string | null;
    unicodeFontsURL?: string | null;
    sdfGlyphSize?: number;
    sdfExponent?: number;
    sdfMargin?: number;
    textureWidth?: number;
    useWorker?: boolean;
  }): void;

  export function preloadFont(
    options: { font?: string; characters?: string | string[]; sdfGlyphSize?: number },
    callback?: () => void
  ): void;
}
