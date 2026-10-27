export type AppTheme = 'light' | 'dark' | 'system';

const STORAGE_KEY = 'arestrix_studio_settings_v1';
let activePreference: AppTheme = 'light';
let systemListenerInstalled = false;

function systemTheme(): 'light' | 'dark' {
  if (typeof window === 'undefined' || typeof window.matchMedia !== 'function') return 'light';
  return window.matchMedia('(prefers-color-scheme: dark)').matches ? 'dark' : 'light';
}

export function resolveAppTheme(theme: AppTheme): 'light' | 'dark' {
  return theme === 'system' ? systemTheme() : theme;
}

export function applyAppTheme(theme: AppTheme): 'light' | 'dark' {
  activePreference = theme;
  const resolved = resolveAppTheme(theme);
  if (typeof document !== 'undefined') {
    document.documentElement.dataset.theme = resolved;
    document.documentElement.style.colorScheme = resolved;
  }

  if (!systemListenerInstalled && typeof window !== 'undefined' && typeof window.matchMedia === 'function') {
    systemListenerInstalled = true;
    window.matchMedia('(prefers-color-scheme: dark)').addEventListener?.('change', () => {
      if (activePreference === 'system') applyAppTheme('system');
    });
  }
  return resolved;
}

/** Lê a preferência antes do primeiro render para evitar flash de tema. */
export function loadStoredAppTheme(): AppTheme {
  try {
    if (typeof localStorage === 'undefined') return 'light';
    const raw = localStorage.getItem(STORAGE_KEY);
    if (!raw) return 'light';
    const parsed = JSON.parse(raw);
    const theme = parsed?.appearance?.theme;
    return theme === 'dark' || theme === 'system' ? theme : 'light';
  } catch {
    return 'light';
  }
}
