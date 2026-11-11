import { describe, expect, it } from 'vitest';
import { DEFAULT_PROJECT_MEMORY_CONFIG } from '../src/core/types.ts';
import {
  MEMORY_SCENARIOS,
  memoryScenarioOf,
  memoryScenarioPreset
} from '../src/core/memory/MemoryScenarios.ts';

describe('MemoryScenarios', () => {
  it('deriva Consultar e Decidir do padrão do projeto', () => {
    expect(DEFAULT_PROJECT_MEMORY_CONFIG.enabled).toBe(true);
    expect(memoryScenarioOf(DEFAULT_PROJECT_MEMORY_CONFIG)).toBe('consult_decide');
  });

  it('reconhece cada preset pela assinatura', () => {
    expect(memoryScenarioOf(memoryScenarioPreset('consult'))).toBe('consult');
    expect(memoryScenarioOf(memoryScenarioPreset('consult_decide'))).toBe('consult_decide');
    expect(memoryScenarioOf(memoryScenarioPreset('learn'))).toBe('learn');
  });

  it('vira Personalizado quando uma propriedade avança', () => {
    const budget = { ...memoryScenarioPreset('consult_decide'), contextBudgetTokens: 600 };
    expect(memoryScenarioOf(budget)).toBe('custom');

    const provider = {
      ...memoryScenarioPreset('consult_decide'),
      gate: { ...(memoryScenarioPreset('consult_decide').gate as object), provider: 'deepseek' }
    };
    expect(memoryScenarioOf(provider)).toBe('custom');

    const embeddings = { ...memoryScenarioPreset('consult'), embeddingEnabled: true };
    expect(memoryScenarioOf(embeddings)).toBe('custom');
  });

  it('presets carregam o porteiro e o modo corretos', () => {
    expect(memoryScenarioPreset('consult').gate?.mode).toBe('brief');
    expect(memoryScenarioPreset('consult_decide').gate?.mode).toBe('enforce');
    expect(memoryScenarioPreset('consult_decide').gate?.provider).toBe('jev');
    expect(memoryScenarioPreset('learn').mode).toBe('learn');
    expect(memoryScenarioPreset('learn').recordLessons).toBe(true);
    expect(MEMORY_SCENARIOS).toHaveLength(3);
  });
});
