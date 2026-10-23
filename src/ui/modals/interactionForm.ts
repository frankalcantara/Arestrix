import type { IFormField } from '../../core/types.ts';

/**
 * Validação do formulário de interação no cliente.
 *
 * O backend valida de novo (fonte de verdade), mas esta camada evita um round-trip
 * e dá retorno imediato ao usuário. Nenhum HTML do modelo entra aqui: só tipos,
 * opções declaradas e valores primitivos.
 */
export type InteractionValidationCode =
  | 'required'
  | 'invalid_text'
  | 'invalid_number'
  | 'invalid_boolean'
  | 'invalid_choice'
  | 'invalid_multi';

export interface IInteractionValidationSuccess {
  ok: true;
  answers: Record<string, unknown>;
}

export interface IInteractionValidationFailure {
  ok: false;
  code: InteractionValidationCode;
  fieldId: string;
  fieldLabel: string;
}

export type InteractionValidationResult =
  | IInteractionValidationSuccess
  | IInteractionValidationFailure;

function isPresent(value: unknown): boolean {
  if (value === undefined || value === null) return false;
  if (typeof value === 'string') return value.trim().length > 0;
  if (Array.isArray(value)) return value.length > 0;
  return true;
}

function optionValues(field: IFormField): string[] {
  return (field.options || []).map(option => option.value);
}

function failure(field: IFormField, code: InteractionValidationCode): IInteractionValidationFailure {
  return { ok: false, code, fieldId: field.id, fieldLabel: field.label || field.id };
}

export function validateInteractionAnswers(
  fields: IFormField[],
  raw: Record<string, unknown>
): InteractionValidationResult {
  const answers = Object.create(null) as Record<string, unknown>;

  for (const field of fields) {
    let value = raw[field.id];
    if (!isPresent(value) && field.defaultValue !== undefined && field.defaultValue !== null) {
      value = field.defaultValue;
    }

    if (!isPresent(value)) {
      if (field.required) return failure(field, 'required');
      continue;
    }

    switch (field.type) {
      case 'text':
      case 'textarea':
      case 'date':
        if (typeof value !== 'string') return failure(field, 'invalid_text');
        answers[field.id] = value;
        break;

      case 'number': {
        if (typeof value !== 'number' && typeof value !== 'string') return failure(field, 'invalid_number');
        const numeric = typeof value === 'number' ? value : Number(value);
        if (!Number.isFinite(numeric)) return failure(field, 'invalid_number');
        answers[field.id] = numeric;
        break;
      }

      case 'boolean':
        if (typeof value !== 'boolean') return failure(field, 'invalid_boolean');
        answers[field.id] = value;
        break;

      case 'choice': {
        if (typeof value !== 'string') return failure(field, 'invalid_choice');
        const options = optionValues(field);
        if (options.length > 0 && !options.includes(value)) return failure(field, 'invalid_choice');
        answers[field.id] = value;
        break;
      }

      case 'multi_choice': {
        if (!Array.isArray(value)) return failure(field, 'invalid_multi');
        const options = optionValues(field);
        const selected: string[] = [];
        for (const item of value) {
          if (typeof item !== 'string') return failure(field, 'invalid_multi');
          if (options.length > 0 && !options.includes(item)) return failure(field, 'invalid_multi');
          selected.push(item);
        }
        if (field.required && selected.length === 0) return failure(field, 'required');
        answers[field.id] = selected;
        break;
      }

      default:
        return failure(field, 'invalid_text');
    }
  }

  return { ok: true, answers };
}
