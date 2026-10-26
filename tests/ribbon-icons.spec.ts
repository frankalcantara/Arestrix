import { describe, it, expect } from 'vitest';
import { RIBBON_ICON_NAMES, ribbonIcon } from '../src/ui/ribbon/RibbonIcons.ts';

describe('Ícones vetoriais da Ribbon', () => {
  it('tem um SVG para cada ícone do catálogo', () => {
    expect(RIBBON_ICON_NAMES.length).toBeGreaterThan(40);
    for (const name of RIBBON_ICON_NAMES) {
      expect(ribbonIcon(name), name).toContain('<svg');
      expect(ribbonIcon(name), name).toContain('aria-hidden="true"');
    }
  });

  it('gera SVG acessível com o tamanho solicitado', () => {
    const svg = ribbonIcon('agent', 30);
    expect(svg).toContain('<svg');
    expect(svg).toContain('viewBox="0 0 20 20"');
    expect(svg).toContain('width="30"');
    expect(svg).toContain('height="30"');
    expect(svg).toContain('aria-hidden="true"');
    expect(svg).not.toMatch(/[\u{1F300}-\u{1FAFF}]/u);
  });

  it('renderiza os estados play e stop sem depender de emoji', () => {
    expect(ribbonIcon('play')).toContain('<path');
    expect(ribbonIcon('stop')).toContain('<rect');
  });

  it('aceita classe extra para destaque de cor', () => {
    expect(ribbonIcon('trash', 16, 'ribbon-icon-danger')).toContain(
      'class="ribbon-glyph ribbon-icon-danger"'
    );
  });
});
