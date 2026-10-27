import { describe, it, expect, afterEach } from 'vitest';
import { applyAppTheme, resolveAppTheme } from '../src/view/AppTheme.ts';

afterEach(() => {
  delete (globalThis as any).document;
  delete (globalThis as any).window;
});

describe('AppTheme', () => {
  it('applies the dark theme to the document root', () => {
    (globalThis as any).document = { documentElement: { dataset: {}, style: {} } };
    (globalThis as any).window = { matchMedia: () => ({ matches: false, addEventListener: () => {} }) };

    expect(applyAppTheme('dark')).toBe('dark');
    expect((globalThis as any).document.documentElement.dataset.theme).toBe('dark');
    expect((globalThis as any).document.documentElement.style.colorScheme).toBe('dark');
  });

  it('resolves system preference through matchMedia', () => {
    (globalThis as any).window = {
      matchMedia: () => ({ matches: true, addEventListener: () => {} })
    };
    expect(resolveAppTheme('system')).toBe('dark');
  });
});
