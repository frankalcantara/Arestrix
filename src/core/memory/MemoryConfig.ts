import { DEFAULT_NODE_MEMORY_CONFIG, type INodeMemoryConfig, type VertexMemoryMode } from '../types.ts';

/**
 * Mapeia o modelo antigo (enabled/semanticLessons/recordLessons) para o novo
 * modo do vértice. O novo campo `mode` tem precedência quando presente.
 */
export function memoryModeOf(config?: Partial<INodeMemoryConfig> | null): VertexMemoryMode {
  if (!config) return 'off';
  if (config.mode === 'off' || config.mode === 'consult' || config.mode === 'learn') {
    return config.mode;
  }
  if (!config.enabled) return 'off';
  return config.recordLessons ? 'learn' : 'consult';
}

/** Tema principal: theme → domains[0] → fallback. */
export function memoryThemeOf(
  config?: Partial<INodeMemoryConfig> | null,
  fallback = ''
): string {
  const theme = (config?.theme || '').trim();
  if (theme) return theme;
  const first = (config?.domains || []).find((item) => item && item.trim());
  return first ? first.trim() : fallback;
}

/** Tema principal + temas extras de leitura, sem duplicatas. */
export function memoryThemesOf(
  config?: Partial<INodeMemoryConfig> | null,
  fallback = ''
): string[] {
  const themes: string[] = [];
  const add = (value?: string) => {
    const theme = (value || '').trim();
    if (theme && !themes.includes(theme)) themes.push(theme);
  };
  add(memoryThemeOf(config, fallback));
  for (const theme of config?.readThemes || []) add(theme);
  if (themes.length === 0) {
    for (const theme of config?.domains || []) add(theme);
  }
  return themes;
}

/** Campos derivados para compatibilidade com o runtime e o formato antigo. */
export function memoryModePatch(mode: VertexMemoryMode): Partial<INodeMemoryConfig> {
  return {
    mode,
    enabled: mode !== 'off',
    semanticLessons: mode !== 'off',
    recordLessons: mode === 'learn'
  };
}

/** Configuração efetiva do vértice, com o modo aplicado. */
export function applyMemoryMode(
  config: Partial<INodeMemoryConfig> | undefined,
  mode: VertexMemoryMode
): INodeMemoryConfig {
  return {
    ...DEFAULT_NODE_MEMORY_CONFIG,
    ...(config || {}),
    ...memoryModePatch(mode)
  };
}

/** Perfis prontos de embeddings para o painel do projeto. */
export interface EmbeddingProfile {
  id: string;
  name: string;
  model: string;
  dimensions: number;
  baseUrl: string;
}

export const EMBEDDING_PROFILES: EmbeddingProfile[] = [
  {
    id: 'embeddinggemma-2',
    name: 'EmbeddingGemma 2 (768d)',
    model: 'embeddinggemma-2',
    dimensions: 768,
    baseUrl: 'http://127.0.0.1:11434/v1'
  },
  {
    id: 'qwen3-embedding-4b',
    name: 'Qwen3-Embedding-4B (2560d)',
    model: 'qwen3-embedding-4b',
    dimensions: 2560,
    baseUrl: 'http://127.0.0.1:11434/v1'
  }
];

export function embeddingProfileFor(model?: string, dimensions?: number): EmbeddingProfile | undefined {
  return EMBEDDING_PROFILES.find((profile) =>
    profile.model === (model || '') ||
    (profile.dimensions === dimensions && profile.model === (model || ''))
  );
}
