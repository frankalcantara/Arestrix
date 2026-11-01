import {
  DEFAULT_NODE_MEMORY_CONFIG,
  type INodeMemoryConfig,
  type INodeSkill,
  type VertexMemoryMode
} from '../types.ts';

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

export type MemoryThemeOrigin = 'explicit' | 'skill' | 'label' | 'id' | 'fallback';

/**
 * Entrada do resolvedor de tema efetivo. `memory` é a configuração efetiva do
 * vértice; skillName/label/id vêm do vértice e nunca sobrescrevem um tema
 * explícito.
 */
export interface MemoryThemeSource {
  memory?: Partial<INodeMemoryConfig> | null;
  skillName?: string;
  label?: string;
  id?: string;
}

/**
 * Forma de identificador usada por tema e lentes. Determinística e idêntica ao
 * C++: apenas a-z/0-9, separadores viram `_`, sem `_` nas pontas. Acentos e
 * outros caracteres não-ASCII viram separador (e são colapsados).
 */
export function memoryThemeIdentifier(value?: string | null): string {
  return (value || '')
    .normalize('NFD')
    .replace(/[\u0300-\u036f]/g, '')
    .toLowerCase()
    .replace(/[^a-z0-9]+/g, '_')
    .replace(/^_+|_+$/g, '');
}

/** Nome da skill ativa; cai para o id da skill e depois para vazio. */
export function activeSkillNameOf(
  settings?: { skills?: INodeSkill[]; activeSkillId?: string } | null
): string {
  const skills = settings?.skills || [];
  if (skills.length === 0) return '';
  const activeId = (settings?.activeSkillId || '').trim();
  const active = activeId ? skills.find((skill) => skill.id === activeId) : undefined;
  const chosen = active || skills[0];
  return (chosen?.name || chosen?.id || '').trim();
}

function normalizeThemeSource(
  source?: MemoryThemeSource | Partial<INodeMemoryConfig> | null
): MemoryThemeSource {
  if (!source) return {};
  if (Object.prototype.hasOwnProperty.call(source, 'memory')) return source as MemoryThemeSource;
  return { memory: source as Partial<INodeMemoryConfig> };
}

/**
 * Tema efetivo e sua origem: tema explícito (ou legado `domains[0]`) → nome da
 * skill ativa → rótulo do vértice → id do vértice → fallback.
 */
export function memoryThemeSourceOf(
  source?: MemoryThemeSource | Partial<INodeMemoryConfig> | null,
  fallback = ''
): { theme: string; origin: MemoryThemeOrigin } {
  const normalized = normalizeThemeSource(source);
  const memory = normalized.memory;
  const explicit = (memory?.theme || '').trim();
  if (explicit) return { theme: explicit, origin: 'explicit' };
  const legacy = (memory?.domains || []).find((item) => item && item.trim());
  if (legacy) return { theme: legacy.trim(), origin: 'explicit' };
  const skill = memoryThemeIdentifier(normalized.skillName);
  if (skill) return { theme: skill, origin: 'skill' };
  const label = memoryThemeIdentifier(normalized.label);
  if (label) return { theme: label, origin: 'label' };
  const id = memoryThemeIdentifier(normalized.id);
  if (id) return { theme: id, origin: 'id' };
  return { theme: (fallback || '').trim(), origin: 'fallback' };
}

/** Tema principal efetivo (API histórica: aceita config ou MemoryThemeSource). */
export function memoryThemeOf(
  source?: MemoryThemeSource | Partial<INodeMemoryConfig> | null,
  fallback = ''
): string {
  return memoryThemeSourceOf(source, fallback).theme;
}

/** Tema principal + temas extras de leitura, sem duplicatas. */
export function memoryThemesOf(
  source?: MemoryThemeSource | Partial<INodeMemoryConfig> | null,
  fallback = ''
): string[] {
  const normalized = normalizeThemeSource(source);
  const themes: string[] = [];
  const add = (value?: string) => {
    const theme = (value || '').trim();
    if (theme && !themes.includes(theme)) themes.push(theme);
  };
  add(memoryThemeSourceOf(normalized, fallback).theme);
  for (const theme of normalized.memory?.readThemes || []) add(theme);
  if (themes.length === 0) {
    for (const theme of normalized.memory?.domains || []) add(theme);
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
