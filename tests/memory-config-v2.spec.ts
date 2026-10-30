import { describe, it, expect } from 'vitest';
import {
  applyMemoryMode,
  memoryModeOf,
  memoryModePatch,
  memoryThemeOf,
  memoryThemesOf
} from '../src/core/memory/MemoryConfig.ts';
import { DEFAULT_NODE_MEMORY_CONFIG, type INodeMemoryConfig } from '../src/core/types.ts';

function config(partial: Partial<INodeMemoryConfig>): INodeMemoryConfig {
  return { ...DEFAULT_NODE_MEMORY_CONFIG, ...partial };
}

describe('modelo de memória v2: modo + tema', () => {
  it('deriva o modo do formato antigo', () => {
    expect(memoryModeOf(config({ enabled: false }))).toBe('off');
    expect(memoryModeOf(config({ enabled: true, semanticLessons: true, recordLessons: false }))).toBe('consult');
    expect(memoryModeOf(config({ enabled: true, semanticLessons: true, recordLessons: true }))).toBe('learn');
  });

  it('o campo mode tem precedência sobre os campos antigos', () => {
    expect(memoryModeOf(config({ mode: 'learn', enabled: false }))).toBe('learn');
    expect(memoryModeOf(config({ mode: 'off', enabled: true, recordLessons: true }))).toBe('off');
  });

  it('resolve o tema por theme, domains e fallback', () => {
    expect(memoryThemeOf(config({ theme: 'svg_typography', domains: ['antigo'] }))).toBe('svg_typography');
    expect(memoryThemeOf(config({ domains: ['markdown', 'revisao'] }))).toBe('markdown');
    expect(memoryThemeOf(config({}), 'node-id')).toBe('node-id');
  });

  it('combina tema principal e temas extras sem duplicar', () => {
    expect(memoryThemesOf(config({ theme: 'svg_typography', readThemes: ['validation', 'svg_typography'] })))
      .toEqual(['svg_typography', 'validation']);
    expect(memoryThemesOf(config({ domains: ['markdown'] }))).toEqual(['markdown']);
  });

  it('aplica o modo aos campos derivados de compatibilidade', () => {
    const learn = applyMemoryMode({ theme: 'svg_typography' }, 'learn');
    expect(learn.mode).toBe('learn');
    expect(learn.enabled).toBe(true);
    expect(learn.semanticLessons).toBe(true);
    expect(learn.recordLessons).toBe(true);

    const consult = applyMemoryMode({ theme: 'svg_typography' }, 'consult');
    expect(consult.enabled).toBe(true);
    expect(consult.semanticLessons).toBe(true);
    expect(consult.recordLessons).toBe(false);

    const off = applyMemoryMode({ theme: 'svg_typography' }, 'off');
    expect(off.enabled).toBe(false);
    expect(off.semanticLessons).toBe(false);
    expect(off.recordLessons).toBe(false);
  });

  it('não introduz persistência por vértice', () => {
    const patch = memoryModePatch('learn');
    expect(patch.persistent).toBeUndefined();
    expect(patch.projectId).toBeUndefined();
    expect(Object.prototype.hasOwnProperty.call(patch, 'measurementMode')).toBe(false);
    expect(Object.prototype.hasOwnProperty.call(patch, 'recordMetrics')).toBe(false);
  });
});
