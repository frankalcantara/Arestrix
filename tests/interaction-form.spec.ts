import { describe, it, expect } from 'vitest';
import { validateInteractionAnswers } from '../src/ui/modals/interactionForm.ts';
import type { IFormField } from '../src/core/types.ts';

const fields: IFormField[] = [
  { id: 'tipo', type: 'choice', label: 'Tipo', required: true, options: [
    { value: 'pf', label: 'Pessoa física' },
    { value: 'pj', label: 'Pessoa jurídica' }
  ] },
  { id: 'obs', type: 'text', label: 'Observação', required: false },
  { id: 'idade', type: 'number', label: 'Idade', required: false },
  { id: 'aceite', type: 'boolean', label: 'Aceite', required: false },
  { id: 'tags', type: 'multi_choice', label: 'Tags', required: false, options: [
    { value: 'a', label: 'A' },
    { value: 'b', label: 'B' }
  ] }
];

describe('validateInteractionAnswers (formulário seguro)', () => {
  it('aceita escolha válida e converte número digitado', () => {
    const result = validateInteractionAnswers(fields, {
      tipo: 'pj',
      obs: 'Cliente empresarial',
      idade: '42',
      aceite: false,
      tags: ['a', 'b']
    });
    expect(result.ok).toBe(true);
    if (result.ok) {
      expect(result.answers).toEqual({
        tipo: 'pj', obs: 'Cliente empresarial', idade: 42, aceite: false, tags: ['a', 'b']
      });
    }
  });

  it('exige campo obrigatório vazio', () => {
    const result = validateInteractionAnswers(fields, { tipo: '   ' });
    expect(result.ok).toBe(false);
    if (!result.ok) {
      expect(result.code).toBe('required');
      expect(result.fieldId).toBe('tipo');
    }
  });

  it('rejeita opção declarada inexistente', () => {
    const result = validateInteractionAnswers(fields, { tipo: 'invalido' });
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.code).toBe('invalid_choice');
  });

  it('rejeita número não numérico', () => {
    const result = validateInteractionAnswers(fields, { tipo: 'pf', idade: 'abc' });
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.code).toBe('invalid_number');
  });

  it('rejeita múltipla escolha fora das opções', () => {
    const result = validateInteractionAnswers(fields, { tipo: 'pf', tags: ['a', 'x'] });
    expect(result.ok).toBe(false);
    if (!result.ok) expect(result.code).toBe('invalid_multi');
  });

  it('omite opcionais vazios e aplica defaultValue', () => {
    const withDefault: IFormField[] = [
      { id: 'tipo', type: 'choice', label: 'Tipo', required: true, defaultValue: 'pf',
        options: [{ value: 'pf', label: 'PF' }, { value: 'pj', label: 'PJ' }] },
      { id: 'obs', type: 'text', label: 'Observação', required: false }
    ];
    const result = validateInteractionAnswers(withDefault, { obs: '' });
    expect(result.ok).toBe(true);
    if (result.ok) expect(result.answers).toEqual({ tipo: 'pf' });
  });
});
