import { describe, it, expect } from 'vitest';
import {
  activeSkillNameOf,
  applyMemoryMode,
  claimAgentTargetFromForm,
  EMBEDDING_PROFILES,
  embeddingProfileFor,
  memoryModeOf,
  memoryModePatch,
  memoryThemeIdentifier,
  memoryThemeOf,
  memoryThemeSourceOf,
  memoryThemesOf
} from '../src/core/memory/MemoryConfig.ts';
import {
  DEFAULT_NODE_MEMORY_CONFIG,
  type INodeMemoryConfig,
  type INodeSkill
} from '../src/core/types.ts';

function config(partial: Partial<INodeMemoryConfig>): INodeMemoryConfig {
  const base = { ...DEFAULT_NODE_MEMORY_CONFIG };
  delete base.mode;
  return { ...base, ...partial };
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

  it('resolve o tema efetivo na ordem explícito, skill, rótulo e id', () => {
    expect(memoryThemeSourceOf({
      memory: config({ theme: 'svg_typography' }),
      skillName: 'Revisor de SVG',
      label: 'Auditor',
      id: 'auditor'
    })).toEqual({ theme: 'svg_typography', origin: 'explicit' });

    expect(memoryThemeSourceOf({
      memory: config({}),
      skillName: 'Revisor de SVG',
      label: 'Auditor',
      id: 'auditor'
    })).toEqual({ theme: 'revisor_de_svg', origin: 'skill' });

    expect(memoryThemeSourceOf({
      memory: config({}),
      label: 'Auditor de Conteúdo',
      id: 'auditor'
    })).toEqual({ theme: 'auditor_de_conteudo', origin: 'label' });

    expect(memoryThemeSourceOf({
      memory: config({}),
      label: '!!!',
      id: 'auditor'
    })).toEqual({ theme: 'auditor', origin: 'id' });

    expect(memoryThemeSourceOf({ memory: config({}) }, 'fallback-id'))
      .toEqual({ theme: 'fallback-id', origin: 'fallback' });
  });

  it('normaliza identificadores como o C++', () => {
    expect(memoryThemeIdentifier('Revisor de SVG')).toBe('revisor_de_svg');
    expect(memoryThemeIdentifier('  Ação  ')).toBe('acao');
    expect(memoryThemeIdentifier('Gerador_Baseline')).toBe('gerador_baseline');
    expect(memoryThemeIdentifier('!!!')).toBe('');
  });

  it('usa a skill ativa e cai para a primeira', () => {
    const skills: INodeSkill[] = [
      { id: 'a', name: 'Alpha', description: '', markdownContent: '', systemPrompt: '', tools: [] },
      { id: 'b', name: 'Beta', description: '', markdownContent: '', systemPrompt: '', tools: [] }
    ];
    expect(activeSkillNameOf({ skills, activeSkillId: 'b' })).toBe('Beta');
    expect(activeSkillNameOf({ skills, activeSkillId: 'missing' })).toBe('Alpha');
    expect(activeSkillNameOf({ skills: [] })).toBe('');
  });

  it('combina temas a partir de uma fonte com skill, rótulo e id', () => {
    expect(memoryThemesOf({
      memory: config({}),
      skillName: 'Revisor de SVG',
      label: 'Auditor',
      id: 'auditor'
    })).toEqual(['revisor_de_svg']);
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

  it('perfis guardam 768 dimensões e o Qwen3 trunca de 2560', () => {
    const gemma = EMBEDDING_PROFILES.find((profile) => profile.id === 'embeddinggemma-2');
    const qwen = EMBEDDING_PROFILES.find((profile) => profile.id === 'qwen3-embedding-4b');
    expect(gemma?.dimensions).toBe(768);
    expect(gemma?.truncateDim).toBe(0);
    expect(gemma?.revision).toBe('');
    expect(qwen?.dimensions).toBe(768);
    expect(qwen?.truncateDim).toBe(768);
    expect(qwen?.revision).toBe('');
    expect(embeddingProfileFor('qwen3-embedding-4b')).toBe(qwen);
  });

  it('normaliza o alvo do agente de claims vindo do formulário', () => {
    expect(claimAgentTargetFromForm('')).toBeUndefined();
    expect(claimAgentTargetFromForm(undefined)).toBeUndefined();
    expect(claimAgentTargetFromForm('nao-e-json')).toBeUndefined();
    expect(claimAgentTargetFromForm(JSON.stringify({
      id: ' claims ',
      graph: ' pipelines/main.json ',
      node: ' extrator '
    }))).toEqual({
      id: 'claims',
      graph: 'pipelines/main.json',
      node: 'extrator'
    });
    expect(claimAgentTargetFromForm(JSON.stringify({ id: 'claims', graph: '', node: '' })))
      .toEqual({ id: 'claims' });
    expect(claimAgentTargetFromForm(JSON.stringify({ id: '', graph: '  ', node: '' })))
      .toBeUndefined();
  });
});
