import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { CANVAS_PALETTES, canvasBackgroundForAppTheme, loadCanvasBackground, saveCanvasBackground } from '../src/view/CanvasPreferences.ts';

describe('Preferências do canvas', () => {
  let previousStorage: any;

  beforeEach(() => {
    previousStorage = (globalThis as any).localStorage;
    const data = new Map<string, string>();
    (globalThis as any).localStorage = {
      getItem: (key: string) => data.get(key) ?? null,
      setItem: (key: string, value: string) => data.set(key, value),
      removeItem: (key: string) => data.delete(key)
    };
  });

  afterEach(() => {
    (globalThis as any).localStorage = previousStorage;
  });

  it('usa o fundo claro por padrão', () => {
    expect(loadCanvasBackground()).toBe('light');
  });

  it('persiste e restaura o fundo preto', () => {
    saveCanvasBackground('dark');
    expect(loadCanvasBackground()).toBe('dark');
    saveCanvasBackground('light');
    expect(loadCanvasBackground()).toBe('light');
  });

  it('ignora valores inválidos no storage', () => {
    (globalThis as any).localStorage.setItem('arestrix_canvas_background', 'purple');
    expect(loadCanvasBackground()).toBe('light');
  });

  it('define preto puro no fundo escuro e linhas de grade visíveis', () => {
    expect(CANVAS_PALETTES.dark.background).toBe(0x000000);
    expect(CANVAS_PALETTES.dark.gridMajor).not.toBe(CANVAS_PALETTES.dark.background);
    expect(CANVAS_PALETTES.dark.gridMinor).not.toBe(CANVAS_PALETTES.dark.background);
  });

  it('deriva o fundo do canvas do tema resolvido', () => {
    expect(canvasBackgroundForAppTheme('light')).toBe('light');
    expect(canvasBackgroundForAppTheme('dark')).toBe('dark');
  });
});
