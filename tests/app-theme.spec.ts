import { describe, it, expect, afterEach } from 'vitest';
import { APP_THEME_CHANGED_EVENT, applyAppTheme, resolveAppTheme } from '../src/view/AppTheme.ts';

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

  it('notifica o canvas quando o tema resolvido muda', () => {
    const events: CustomEvent[] = [];
    (globalThis as any).document = {
      documentElement: { dataset: {}, style: {} },
      dispatchEvent: (event: CustomEvent) => {
        events.push(event);
        return true;
      }
    };
    (globalThis as any).window = {
      matchMedia: () => ({ matches: false, addEventListener: () => {} })
    };

    applyAppTheme('dark');

    expect(events).toHaveLength(1);
    expect(events[0].type).toBe(APP_THEME_CHANGED_EVENT);
    expect(events[0].detail).toEqual({ theme: 'dark' });
  });
});
