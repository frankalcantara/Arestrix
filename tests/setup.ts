/**
 * Deterministic test locale.
 *
 * The product detects the initial locale from `navigator.language` and falls
 * back to pt-BR. Node exposes the host locale (pt-BR on a Brazilian Windows
 * machine, en-US on most CI runners), so assertions that expect the canonical
 * Portuguese UI would fail only on CI. Force pt-BR before any test imports the
 * i18n module; tests that exercise en-US call setLocale() explicitly.
 */
const TEST_LOCALE = 'pt-BR';

try {
  const navigatorObject = (globalThis as { navigator?: { language?: string } }).navigator;
  if (navigatorObject && typeof navigatorObject === 'object') {
    Object.defineProperty(navigatorObject, 'language', {
      value: TEST_LOCALE,
      configurable: true
    });
  } else {
    Object.defineProperty(globalThis, 'navigator', {
      value: { language: TEST_LOCALE },
      configurable: true
    });
  }
} catch {
  Object.defineProperty(globalThis, 'navigator', {
    value: { language: TEST_LOCALE },
    configurable: true
  });
}
