import type { IProjectMemoryConfig } from '../types.ts';
import { memoryModeOf } from './MemoryConfig.ts';

/**
 * Cenários de memória do projeto. O cenário não é persistido: ele é derivado
 * da configuração efetiva. Se qualquer propriedade sair da assinatura de um
 * preset, o estado vira `custom` ("Personalizado", uso interno).
 */
export type MemoryScenarioId = 'consult' | 'consult_decide' | 'learn';
export type MemoryScenarioState = MemoryScenarioId | 'custom';

export interface IMemoryScenario {
  id: MemoryScenarioId;
  title: string;
  description: string;
  memory: Partial<IProjectMemoryConfig>;
}

const DECIDE_GATE = {
  mode: 'enforce' as const,
  provider: 'jev',
  model: 'jev-latest',
  maxSearches: 2,
  minConfidence: 0.6,
  fallback: 'brief' as const,
  maxBriefChars: 1200
};

const BASE = {
  enabled: true,
  persistent: false,
  contextBudgetTokens: 800,
  consolidationMode: 'off' as const,
  embeddingEnabled: false,
  localOnly: false,
  claimExtraction: false
};

export const MEMORY_SCENARIOS: IMemoryScenario[] = [
  {
    id: 'consult',
    title: 'Consultar',
    description: 'Injeta o brief curado, sem ferramentas de memória. Menor custo.',
    memory: {
      ...BASE,
      mode: 'consult',
      gate: { mode: 'brief' }
    }
  },
  {
    id: 'consult_decide',
    title: 'Consultar e Decidir',
    description: 'O brief entra e o porteiro decide se precisa buscar mais memória.',
    memory: {
      ...BASE,
      mode: 'consult',
      gate: { ...DECIDE_GATE }
    }
  },
  {
    id: 'learn',
    title: 'Aprender com aprovação',
    description: 'Consulta, decide e registra propostas; nada é promovido sem aprovação.',
    memory: {
      ...BASE,
      mode: 'learn',
      recordLessons: true,
      semanticLessons: true,
      gate: { ...DECIDE_GATE }
    }
  }
];

export function memoryScenarioPreset(id: MemoryScenarioId): Partial<IProjectMemoryConfig> {
  const found = MEMORY_SCENARIOS.find(scenario => scenario.id === id);
  return found ? { ...found.memory, gate: { ...(found.memory.gate as object) } } : {};
}

function claimExtractionOf(config?: Partial<IProjectMemoryConfig> | null): boolean {
  const value = config?.claimExtraction;
  return typeof value === 'boolean' ? value : !!value?.enabled;
}

function gateMatches(
  config: Partial<IProjectMemoryConfig> | null | undefined,
  expected: Record<string, unknown>
): boolean {
  const gate = (config?.gate || {}) as Record<string, unknown>;
  return Object.entries(expected).every(([key, value]) => {
    const actual = gate[key];
    if (actual === undefined) {
      // Ausente conta como o valor padrão do runtime.
      return value === 'off' || value === '' || value === 0 || value === false;
    }
    return actual === value;
  });
}

export function memoryScenarioOf(
  config?: Partial<IProjectMemoryConfig> | null
): MemoryScenarioState {
  if (!config) return 'custom';
  const mode = memoryModeOf(config);
  const budget = config.contextBudgetTokens ?? 800;
  const consolidation = config.consolidationMode ?? 'off';
  const embeddings = !!config.embeddingEnabled;
  const localOnly = !!config.localOnly;
  const claimExtraction = claimExtractionOf(config);
  const common =
    budget === 800 && consolidation === 'off' && !embeddings && !localOnly && !claimExtraction;

  if (mode === 'consult' && common && gateMatches(config, { mode: 'brief' })) {
    return 'consult';
  }
  if (
    mode === 'consult' &&
    common &&
    gateMatches(config, {
      mode: 'enforce',
      provider: 'jev',
      model: 'jev-latest',
      maxSearches: 2,
      minConfidence: 0.6,
      fallback: 'brief',
      maxBriefChars: 1200
    })
  ) {
    return 'consult_decide';
  }
  if (
    mode === 'learn' &&
    common &&
    gateMatches(config, {
      mode: 'enforce',
      provider: 'jev',
      model: 'jev-latest',
      maxSearches: 2,
      minConfidence: 0.6,
      fallback: 'brief',
      maxBriefChars: 1200
    })
  ) {
    return 'learn';
  }
  return 'custom';
}