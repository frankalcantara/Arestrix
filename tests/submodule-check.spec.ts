import { describe, expect, it } from 'vitest';
// @ts-expect-error - modulo .mjs sem tipos
import {
  STATUS,
  applySubmoduleUpdate,
  buildJsonReport,
  hasAlerts,
  hasUpdates,
  inspectSubmodule,
  parseGitmodules,
  renderReport
} from '../scripts/lib/submodules.mjs';

const NOW = new Date('2026-01-05T08:00:00Z');

function entry(overrides: Record<string, unknown> = {}) {
  return {
    name: 'third_party/demo',
    path: 'third_party/demo',
    url: 'https://github.com/example/demo',
    trackedBranch: '',
    branch: 'main',
    referenceSkill: false,
    status: STATUS.UP_TO_DATE,
    localRev: '1111111111111111111111111111111111111111',
    localShort: '1111111',
    localDescribe: 'v1.0.0',
    localVersion: '1.0.0',
    remoteRev: '1111111111111111111111111111111111111111',
    remoteShort: '1111111',
    remoteDescribe: 'v1.0.0',
    remoteVersion: '1.0.0',
    behind: 0,
    ahead: 0,
    commits: [],
    applied: false,
    error: '',
    ...overrides
  };
}

const UPDATABLE = entry({
  status: STATUS.UPDATE_AVAILABLE,
  remoteRev: '2222222222222222222222222222222222222222',
  remoteShort: '2222222',
  remoteDescribe: 'v1.1.0',
  remoteVersion: '1.1.0',
  behind: 3,
  commits: ['2222222 feat: new thing', '3333333 fix: old thing']
});

describe('parseGitmodules', () => {
  it('le paths, urls, branches e ignora comentarios e CRLF', () => {
    const text = [
      '# comentario global',
      '[submodule "third_party/alpha"]',
      '\tpath = third_party/alpha',
      '\turl = https://github.com/example/alpha.git',
      '; comentario',
      '[submodule "beta"]',
      '\tpath = third_party/beta',
      '\turl = "https://github.com/example/beta.git" # inline',
      '\tbranch = master',
      '[submodule "sem-url"]',
      '\tpath = third_party/sem-url'
    ].join('\r\n');

    const parsed = parseGitmodules(text);
    expect(parsed).toHaveLength(2);
    expect(parsed[0]).toMatchObject({
      name: 'third_party/alpha',
      path: 'third_party/alpha',
      url: 'https://github.com/example/alpha.git',
      trackedBranch: ''
    });
    expect(parsed[1]).toMatchObject({
      name: 'beta',
      path: 'third_party/beta',
      url: 'https://github.com/example/beta.git',
      trackedBranch: 'master'
    });
  });

  it('marca os submódulos de skill de referencia', () => {
    const parsed = parseGitmodules(
      [
        '[submodule "SkillSpector"]',
        '\tpath = third_party/SkillSpector',
        '\turl = https://github.com/NVIDIA/SkillSpector',
        '[submodule "security-audit-skill"]',
        '\tpath = third_party/security-audit-skill/',
        '\turl = https://github.com/cloudflare/security-audit-skill'
      ].join('\n')
    );
    expect(parsed.map((sub: any) => sub.referenceSkill)).toEqual([true, true]);
    expect(parsed[1].path).toBe('third_party/security-audit-skill');
  });

  it('retorna vazio para um .gitmodules sem submódulos válidos', () => {
    expect(parseGitmodules('')).toEqual([]);
    expect(parseGitmodules('[submodule "x"]\n\turl = y\n')).toEqual([]);
  });
});

describe('inspectSubmodule', () => {
  it('reporta submódulo ausente sem tocar no git', () => {
    const result = inspectSubmodule({
      root: 'C:/caminho/que/nao/existe',
      sub: { name: 'demo', path: 'third_party/demo', url: 'u', trackedBranch: '', referenceSkill: false }
    });
    expect(result.status).toBe(STATUS.MISSING);
    expect(result.error).toContain('git submodule update --init');
  });
});

describe('renderReport', () => {
  it('lista as atualizações, commits e o checklist de validação', () => {
    const skill = { ...UPDATABLE, path: 'third_party/SkillSpector', referenceSkill: true };
    const markdown = renderReport([UPDATABLE, skill, entry()], { now: NOW });
    expect(markdown).toContain('`third_party/demo`');
    expect(markdown).toContain('3 new commit(s)');
    expect(markdown).toContain('2222222 feat: new thing');
    expect(markdown).toContain('python scripts/check_third_party_licenses.py');
    expect(markdown).toContain('npm run check:skill-references');
    expect(markdown).not.toContain('2026-01-05T');
  });

  it('avisa quando um submódulo de skill muda, mas não para os demais', () => {
    const skill = { ...UPDATABLE, path: 'third_party/SkillSpector', referenceSkill: true };
    expect(renderReport([skill], { now: NOW })).toContain('skill-reference-review.md');
    expect(renderReport([UPDATABLE], { now: NOW })).not.toContain('skill-reference-review.md');
  });

  it('não sugere atualização automática para submódulos divergentes', () => {
    const diverged = { ...UPDATABLE, status: STATUS.DIVERGED, ahead: 2 };
    const markdown = renderReport([diverged], { now: NOW });
    expect(markdown).toContain('Diverged submodules are **not** updated automatically');
    expect(markdown).toContain('2 local commit(s)');
  });

  it('mostra o estado de repositório sem submódulos', () => {
    expect(renderReport([], { now: NOW })).toContain('No submodules were found');
  });

  it('mantém JSON e Markdown consistentes', () => {
    const results = [UPDATABLE, entry()];
    const json = buildJsonReport(results, { now: NOW, branch: 'main' });
    expect(json.hasUpdates).toBe(true);
    expect(json.hasAlerts).toBe(false);
    expect(json.branch).toBe('main');
    expect(json.generatedAt).toBe('2026-01-05T08:00:00.000Z');
    expect(json.submodules[0]).toMatchObject({
      path: 'third_party/demo',
      status: STATUS.UPDATE_AVAILABLE,
      behind: 3,
      pinned: UPDATABLE.localRev,
      upstream: UPDATABLE.remoteRev
    });
    expect(hasUpdates(results)).toBe(true);
    expect(hasAlerts(results)).toBe(false);
    expect(hasAlerts([{ status: STATUS.DIVERGED }])).toBe(true);
    expect(hasAlerts([{ status: STATUS.ERROR }])).toBe(true);
  });
});

describe('applySubmoduleUpdate', () => {
  it('falha sem efeito colateral quando o diretório não existe', () => {
    const result = applySubmoduleUpdate({
      root: 'C:/caminho/que/nao/existe',
      sub: { path: 'third_party/demo' },
      remoteRev: '2222222'
    });
    expect(result.ok).toBe(false);
    expect(result.error).toBeTruthy();
  });
});
