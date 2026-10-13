import i18next from 'i18next';
import ptBR from './locales/pt-BR.json';
import enUS from './locales/en-US.json';
import { SupportedLocale, SUPPORTED_LOCALES } from './types.ts';
import { globalEventBus } from '../core/EventBus.ts';

const STORAGE_KEY = 'arestrix_locale';

/**
 * Detecta o idioma inicial da aplicação com prioridade para:
 * 1. Escolha persistida explicitamente pelo usuário no localStorage
 * 2. Preferência do navegador (se idioma padrão começar com 'en', adota 'en-US')
 * 3. Fallback canônico: 'pt-BR' (Português do Brasil)
 */
function detectInitialLocale(): SupportedLocale {
  try {
    if (typeof localStorage !== 'undefined') {
      const stored = localStorage.getItem(STORAGE_KEY) as SupportedLocale | null;
      if (stored === 'pt-BR' || stored === 'en-US') {
        return stored;
      }
    }
  } catch (_) {}

  try {
    if (typeof navigator !== 'undefined' && typeof navigator.language === 'string') {
      const navLang = navigator.language.toLowerCase();
      if (navLang.startsWith('en')) {
        return 'en-US';
      }
    }
  } catch (_) {}

  return 'pt-BR';
}

const initialLocale = detectInitialLocale();

// Inicialização síncrona dos recursos compilados
i18next.init({
  lng: initialLocale,
  fallbackLng: 'pt-BR',
  resources: {
    'pt-BR': {
      translation: ptBR
    },
    'en-US': {
      translation: enUS
    }
  },
  interpolation: {
    escapeValue: false // No escaping required for plain DOM text injections
  },
  returnNull: false,
  returnEmptyString: false
});

// Atualiza o atributo lang do documento HTML caso exista
if (typeof document !== 'undefined' && document.documentElement) {
  document.documentElement.lang = initialLocale;
}

/**
 * Traduz uma chave utilizando o i18next.
 * Suporta chaves aninhadas (ex: 'ribbon.buttons.save_now'), interpolação ({ name: 'X' })
 * e valores de fallback opcionais.
 */
export function t(
  key: string,
  optionsOrDefault?: Record<string, any> | string,
  extraOptionOrDefault?: Record<string, any> | string
): string {
  let options: Record<string, any> | undefined;
  let defaultValue: string | undefined;

  if (typeof optionsOrDefault === 'string') {
    defaultValue = optionsOrDefault;
    if (typeof extraOptionOrDefault === 'object' && extraOptionOrDefault !== null) {
      options = extraOptionOrDefault;
    }
  } else if (typeof optionsOrDefault === 'object' && optionsOrDefault !== null) {
    options = optionsOrDefault;
    if (typeof extraOptionOrDefault === 'string') {
      defaultValue = extraOptionOrDefault;
    }
  }

  const finalOpts = defaultValue
    ? { defaultValue, ...(options || {}) }
    : options;

  const result = i18next.t(key, finalOpts);
  if (result === undefined || result === null || result === key) {
    return defaultValue ?? key;
  }
  return result;
}

/**
 * Retorna o código do idioma ativo ('pt-BR' | 'en-US').
 */
export function getLocale(): SupportedLocale {
  const current = (i18next.language || 'pt-BR') as SupportedLocale;
  return current === 'en-US' ? 'en-US' : 'pt-BR';
}

/**
 * Altera o idioma ativo da aplicação, persiste no localStorage e notifica os componentes.
 */
export async function setLocale(locale: SupportedLocale): Promise<void> {
  if (locale !== 'pt-BR' && locale !== 'en-US') {
    locale = 'pt-BR';
  }

  if (locale === getLocale()) {
    return;
  }

  await i18next.changeLanguage(locale);

  try {
    if (typeof localStorage !== 'undefined') {
      localStorage.setItem(STORAGE_KEY, locale);
    }
  } catch (_) {}

  if (typeof document !== 'undefined' && document.documentElement) {
    document.documentElement.lang = locale;
  }

  // Notifica o EventBus global da aplicação
  globalEventBus.emit('locale:changed', locale);
}

/**
 * Alterna entre Português e Inglês.
 */
export async function toggleLocale(): Promise<SupportedLocale> {
  const next: SupportedLocale = getLocale() === 'pt-BR' ? 'en-US' : 'pt-BR';
  await setLocale(next);
  return next;
}

/**
 * Registra um ouvinte para alterações de idioma.
 */
export function onLocaleChange(callback: (locale: SupportedLocale) => void): () => void {
  const handler = (lng: string) => {
    callback(lng as SupportedLocale);
  };
  i18next.on('languageChanged', handler);
  return () => {
    i18next.off('languageChanged', handler);
  };
}

export { i18next, SUPPORTED_LOCALES };
export type { SupportedLocale };
