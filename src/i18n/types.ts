export type SupportedLocale = 'pt-BR' | 'en-US';

export interface ILocaleOption {
  code: SupportedLocale;
  name: string;
  flag: string;
  nativeName: string;
}

export const SUPPORTED_LOCALES: readonly ILocaleOption[] = [
  {
    code: 'pt-BR',
    name: 'Português (Brasil)',
    nativeName: 'Português do Brasil',
    flag: '🇧🇷'
  },
  {
    code: 'en-US',
    name: 'English (US)',
    nativeName: 'US English',
    flag: '🇺🇸'
  }
];
