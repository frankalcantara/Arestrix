import { describe, it, expect } from 'vitest';
import { sanitizeEnvReferences } from '../src/core/env/EnvironmentManager.ts';

describe('sanitizeEnvReferences', () => {
  it('mantem referencias a variaveis de ambiente (${NOME}, %NOME%, $NOME) e valores vazios', () => {
    expect(sanitizeEnvReferences({
      A: '${CRM_TOKEN}',
      B: '%CRM_TOKEN%',
      C: '$CRM_TOKEN',
      D: '',
      E: '  '
    })).toEqual({
      A: '${CRM_TOKEN}',
      B: '%CRM_TOKEN%',
      C: '$CRM_TOKEN',
      D: '',
      E: ''
    });
  });

  it('descarta valores literais (segredos digitados) e entradas nao-string', () => {
    const env: Record<string, any> = { OK: '${TOKEN}', RUIM: 'segredo-literal-digitado' };
    const out = sanitizeEnvReferences(env);
    expect(out).toEqual({ OK: '${TOKEN}' });
    expect(JSON.stringify(out)).not.toContain('segredo-literal-digitado');
  });

  it('tolera undefined e null', () => {
    expect(sanitizeEnvReferences(undefined)).toEqual({});
    expect(sanitizeEnvReferences(null)).toEqual({});
  });

  it('nao aceita nomes de variavel fora do formato de referencia', () => {
    expect(sanitizeEnvReferences({ X: 'DEEPSEEK_API_KEY' })).toEqual({});
    expect(sanitizeEnvReferences({ X: '${1NAO}' })).toEqual({});
  });
});
