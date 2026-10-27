import { describe, it, expect } from 'vitest';
import { SettingsResolver } from '../src/core/settings/SettingsResolver.ts';
import type { IProjectSettings, IStudioSettings, IWorkspaceSettings } from '../src/core/settings/types.ts';
import type { IProjectManifest } from '../src/core/types.ts';

const manifest = {
  projectId: 'p1',
  name: 'Projeto',
  version: '1.0.0',
  description: '',
  createdAt: '',
  updatedAt: '',
  rootPath: '.',
  pipelines: []
} as IProjectManifest;

function studio(): IStudioSettings {
  return {
    version: 1,
    language: 'pt-BR',
    appearance: { theme: 'light', canvasBackground: 'light' },
    defaults: { model: { provider: 'deepseek', model: 'studio-model' } }
  };
}

function workspace(): IWorkspaceSettings {
  return {
    version: 1,
    rootPath: '.',
    directories: { root: '.workspace', skills: '.workspace/skills', mcps: '.workspace/mcps', agents: '.workspace/agents', graphs: '.workspace/graphs' },
    defaults: { sandbox: { maxMemoryMB: 1024 } },
    security: { autoVerifySkills: true, strictSecurityMode: true }
  };
}

function project(defaults: IProjectSettings['defaults'] = {}): IProjectSettings {
  return { version: 1, manifest, defaults, hasExplicitDefaults: Object.keys(defaults).length > 0 };
}

describe('SettingsResolver', () => {
  it('resolves model from Studio when the project has no explicit override', () => {
    const result = new SettingsResolver().resolve({ studio: studio(), workspace: workspace(), project: project() });
    expect(result.model.value.provider).toBe('deepseek');
    expect(result.model.value.model).toBe('studio-model');
    expect(result.model.origin).toBe('studio');
    expect(result.sandbox.value.maxMemoryMB).toBe(1024);
    expect(result.sandbox.origin).toBe('workspace');
  });

  it('lets the project override Studio and Workspace defaults', () => {
    const result = new SettingsResolver().resolve({
      studio: studio(),
      workspace: workspace(),
      project: project({ model: { model: 'project-model' }, sandbox: { isolationLevel: 'microvm' } })
    });
    expect(result.model.value.model).toBe('project-model');
    expect(result.model.origin).toBe('project');
    expect(result.sandbox.value.isolationLevel).toBe('microvm');
    expect(result.sandbox.value.maxMemoryMB).toBe(1024);
    expect(result.sandbox.origin).toBe('project');
  });

  it('applies a vertex override only when the inherit flag is off', () => {
    const inherited = new SettingsResolver().resolveForVertex(
      { studio: studio(), workspace: workspace(), project: project() },
      { inheritModel: true, model: { model: 'vertex-model' } } as any
    );
    expect(inherited.model.value.model).toBe('studio-model');
    expect(inherited.model.origin).toBe('studio');

    const overridden = new SettingsResolver().resolveForVertex(
      { studio: studio(), workspace: workspace(), project: project() },
      { inheritModel: false, model: { model: 'vertex-model' } } as any
    );
    expect(overridden.model.value.model).toBe('vertex-model');
    expect(overridden.model.origin).toBe('vertex');
  });

  it('keeps memory defaults when no layer overrides them', () => {
    const result = new SettingsResolver().resolve({ studio: studio(), workspace: workspace(), project: project() });
    expect(result.memory.origin).toBe('default');
    expect(result.memory.value).toBeDefined();
  });
});
