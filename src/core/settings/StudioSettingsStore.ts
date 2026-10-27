import { getLocale } from '../../i18n/index.ts';
import { loadCanvasBackground } from '../../view/CanvasPreferences.ts';
import type { IStudioSettings } from './types.ts';

const STORAGE_KEY = 'arestrix_studio_settings_v1';

function defaultSettings(): IStudioSettings {
  const theme = typeof document !== 'undefined'
    ? (document.documentElement.dataset.theme === 'dark' ? 'dark' : 'light')
    : 'light';

  return {
    version: 1,
    language: getLocale(),
    appearance: {
      theme,
      canvasBackground: loadCanvasBackground()
    },
    defaults: {}
  };
}

function normalize(raw: unknown): IStudioSettings {
  const base = defaultSettings();
  if (!raw || typeof raw !== 'object') return base;
  const input = raw as Partial<IStudioSettings>;
  const appearance = (input.appearance || {}) as Partial<IStudioSettings['appearance']>;
  return {
    version: 1,
    language: input.language === 'en-US' ? 'en-US' : input.language === 'pt-BR' ? 'pt-BR' : base.language,
    appearance: {
      theme: appearance.theme === 'dark' || appearance.theme === 'system' ? appearance.theme : 'light',
      canvasBackground: appearance.canvasBackground === 'dark'
        ? 'dark'
        : appearance.canvasBackground === 'light'
          ? 'light'
          : base.appearance.canvasBackground
    },
    defaults: input.defaults && typeof input.defaults === 'object' ? input.defaults : {}
  };
}

/**
 * Persistência das propriedades do Studio.
 *
 * A gravação canônica é feita por `studio.config.json` quando o backend oferece
 * `get_studio_settings`/`save_studio_settings`; enquanto isso, o WebView mantém
 * uma cópia local para não bloquear a evolução da UI. Nenhum segredo entra aqui.
 */
export class StudioSettingsStore {
  public async load(): Promise<IStudioSettings> {
    const backend = this.backend();
    if (backend) {
      try {
        const raw = await backend.call('get_studio_settings', '');
        if (raw) {
          const parsed = typeof raw === 'string' ? JSON.parse(raw) : raw;
          return normalize(parsed?.settings || parsed);
        }
      } catch {
        // Backend ainda não expõe o endpoint; segue para o fallback local.
      }
    }

    try {
      if (typeof localStorage === 'undefined') return defaultSettings();
      const raw = localStorage.getItem(STORAGE_KEY);
      return raw ? normalize(JSON.parse(raw)) : defaultSettings();
    } catch {
      return defaultSettings();
    }
  }

  public async save(settings: IStudioSettings): Promise<boolean> {
    const normalized = normalize(settings);
    let localOk = false;
    try {
      if (typeof localStorage !== 'undefined') {
        localStorage.setItem(STORAGE_KEY, JSON.stringify(normalized));
        localOk = true;
      }
    } catch {
      localOk = false;
    }

    const backend = this.backend();
    if (backend) {
      try {
        const response = await backend.call('save_studio_settings', JSON.stringify({ settings: normalized }));
        return response === true || response === 'true' || localOk;
      } catch {
        // O fallback local continua válido.
      }
    }
    return localOk;
  }

  private backend(): any {
    if (typeof window === 'undefined') return null;
    const webui = (window as any).webui;
    return webui && typeof webui.call === 'function' ? webui : null;
  }
}
