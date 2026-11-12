/**
 * Política de licenças para ativos de biblioteca.
 *
 * O objetivo é normalizar o que vem de fontes externas (GitHub, manifestos,
 * pacotes) para identificadores SPDX previsíveis e aplicar a allowlist da fonte.
 * A licença de ativos criados pelo próprio usuário não é restringida por esta
 * política; ela apenas é normalizada.
 */

export const PROPRIETARY_LICENSE = 'LicenseRef-Proprietary';

export const DEFAULT_LICENSE_ALLOWLIST = [
  'MIT',
  'Apache-2.0',
  'BSD-3-Clause',
  'BSD-2-Clause',
  'ISC',
  'MPL-2.0',
  '0BSD',
  'Unlicense',
  'CC0-1.0'
];

const LICENSE_ALIASES: Record<string, string> = {
  mit: 'MIT',
  'mit license': 'MIT',
  apache: 'Apache-2.0',
  'apache 2': 'Apache-2.0',
  'apache 2.0': 'Apache-2.0',
  'apache license 2.0': 'Apache-2.0',
  'apache-2': 'Apache-2.0',
  'bsd': 'BSD-3-Clause',
  'bsd 3-clause': 'BSD-3-Clause',
  'bsd-3': 'BSD-3-Clause',
  isc: 'ISC',
  'mpl-2.0': 'MPL-2.0',
  'mozilla public license 2.0': 'MPL-2.0',
  '0bsd': '0BSD',
  unlicense: 'Unlicense',
  'the unlicense': 'Unlicense',
  'cc0-1.0': 'CC0-1.0',
  'cc0 1.0': 'CC0-1.0',
  proprietary: PROPRIETARY_LICENSE,
  commercial: PROPRIETARY_LICENSE,
  'licenseref-proprietary': PROPRIETARY_LICENSE
};

function normalizeSingleLicense(value: string): string {
  const trimmed = value.trim();
  if (!trimmed) return '';
  const leading = trimmed.match(/^\(+/)?.[0] || '';
  const trailing = trimmed.match(/\)+$/)?.[0] || '';
  const core = trimmed.slice(leading.length, trimmed.length - trailing.length).trim();
  if (!core) return trimmed;
  return leading + (LICENSE_ALIASES[core.toLowerCase()] || core) + trailing;
}

/** Normaliza aliases comuns para identificadores SPDX e preserva expressões AND/OR/WITH. */
export function normalizeLicense(raw: unknown): string {
  const value = String(raw ?? '').trim();
  if (!value) return 'NOASSERTION';
  const parts = value.split(/\s+(AND|OR|WITH)\s+/i);
  if (parts.length === 1) return normalizeSingleLicense(value);
  return parts
    .map((part, index) => index % 2 === 1 ? part.toUpperCase() : normalizeSingleLicense(part))
    .join(' ');
}

export function normalizeLicenseAllowlist(raw: readonly unknown[] | undefined): string[] {
  const list = Array.isArray(raw) && raw.length > 0 ? raw : DEFAULT_LICENSE_ALLOWLIST;
  return Array.from(new Set(list.map(normalizeLicense).filter(license => license && license !== 'NOASSERTION')));
}

export interface ILicenseValidation {
  ok: boolean;
  license: string;
  reason?: string;
}

function expressionIdentifiers(license: string): string[] {
  return (license.match(/[A-Za-z0-9.+-]+/g) || [])
    .filter(token => !['AND', 'OR', 'WITH'].includes(token.toUpperCase()));
}

/**
 * Valida uma licença contra a allowlist. Expressões são aceitas apenas quando
 * todos os identificadores citados estão permitidos; isso é conservador para
 * `OR`, mas evita liberar uma expressão que mistura licença proibida.
 */
export function validateLicense(
  raw: unknown,
  allowlist: readonly unknown[] = DEFAULT_LICENSE_ALLOWLIST
): ILicenseValidation {
  const license = normalizeLicense(raw);
  if (license === 'NOASSERTION') {
    return { ok: false, license, reason: 'Licença ausente ou não declarada.' };
  }
  if (license.length > 128) {
    return { ok: false, license, reason: 'Expressão de licença longa demais.' };
  }
  const allowed = new Set(normalizeLicenseAllowlist(allowlist));
  if (allowed.has(license)) return { ok: true, license };
  const identifiers = expressionIdentifiers(license);
  if (identifiers.length > 0 && identifiers.every(identifier => allowed.has(identifier))) {
    return { ok: true, license };
  }
  return {
    ok: false,
    license,
    reason: `Licença "${license}" não está na allowlist.`
  };
}
