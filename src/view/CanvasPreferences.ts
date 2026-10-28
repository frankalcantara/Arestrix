import { THEME } from './Theme.ts';
import { resolveAppTheme, type AppTheme } from './AppTheme.ts';

export type CanvasBackground = 'light' | 'dark';

export interface CanvasPalette {
  /** Fundo do scene WebGL. */
  background: number;
  /** Linha principal da grade. */
  gridMajor: number;
  /** Linha secundária da grade. */
  gridMinor: number;
  /** Fundo CSS do container (evita flash branco atrás do canvas). */
  containerCss: string;
}

/**
 * Paletas do canvas. O "dark" é preto puro, com linhas de grade em ardósia para
 * manter a leitura sem transformar a grade em ruído.
 */
export const CANVAS_PALETTES: Record<CanvasBackground, CanvasPalette> = {
  light: {
    background: THEME.canvasBackground,
    gridMajor: THEME.gridMajor,
    gridMinor: THEME.gridMinor,
    containerCss: '#FFFFFF'
  },
  dark: {
    background: 0x000000,
    gridMajor: 0x334155,
    gridMinor: 0x1e293b,
    containerCss: '#000000'
  }
};

const STORAGE_KEY = 'arestrix_canvas_background';

/** O canvas acompanha o tema resolvido quando o tema muda. */
export function canvasBackgroundForAppTheme(theme: AppTheme): CanvasBackground {
  return resolveAppTheme(theme) === 'dark' ? 'dark' : 'light';
}

export function loadCanvasBackground(): CanvasBackground {
  try {
    if (typeof localStorage === 'undefined') return 'light';
    const stored = localStorage.getItem(STORAGE_KEY);
    return stored === 'dark' ? 'dark' : 'light';
  } catch {
    return 'light';
  }
}

export function saveCanvasBackground(background: CanvasBackground): void {
  try {
    if (typeof localStorage === 'undefined') return;
    localStorage.setItem(STORAGE_KEY, background);
  } catch {
    // Preferência visual é best-effort; o canvas continua funcional sem storage.
  }
}
