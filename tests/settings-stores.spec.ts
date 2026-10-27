import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { StudioSettingsStore } from '../src/core/settings/StudioSettingsStore.ts';
import { WorkspaceSettingsStore } from '../src/core/settings/WorkspaceSettingsStore.ts';
import { ProjectSettingsStore } from '../src/core/settings/ProjectSettingsStore.ts';
import type { IProjectManifest } from '../src/core/types.ts';

const storage = new Map<string, string>();

beforeEach(() => {
  storage.clear();
  (globalThis as any).localStorage = {
    getItem: (key: string) => storage.get(key) ?? null,
    setItem: (key: string, value: string) => { storage.set(key, value); },
    removeItem: (key: string) => { storage.delete(key); }
  };
});

afterEach(() => {
  delete (globalThis as any).localStorage;
});

const manifest = {
  projectId: 'p1', name: 'Projeto', version: '1.0.0', description: '', createdAt: '', updatedAt: '', rootPath: '.', pipelines: []
} as IProjectManifest;

describe('Settings stores', () => {
  it('persists Studio settings without secrets', async () => {
    const store = new StudioSettingsStore();
    const saved = await store.save({
      version: 1,
      language: 'en-US',
      appearance: { theme: 'dark', canvasBackground: 'dark' },
      defaults: { model: { provider: 'deepseek', model: 'deepseek-flash' } }
    });
    expect(saved).toBe(true);
    const loaded = await store.load();
    expect(loaded.language).toBe('en-US');
    expect(loaded.appearance.theme).toBe('dark');
    expect(JSON.stringify(loaded)).not.toMatch(/api[_-]?key|token|secret/i);
  });

  it('persists an explicit light canvas preference over the fallback', async () => {
    localStorage.setItem('arestrix_canvas_background', 'dark');
    const store = new StudioSettingsStore();
    const loaded = await store.load();
    expect(loaded.appearance.canvasBackground).toBe('dark');

    loaded.appearance.canvasBackground = 'light';
    expect(await store.save(loaded)).toBe(true);

    const reloaded = await store.load();
    expect(reloaded.appearance.canvasBackground).toBe('light');
  });

  it('persists Workspace settings and uses the local fallback', async () => {
    const manager = {
      getWorkspaceRoot: () => 'C:/projetos/teste',
      getWorkspaceDirectories: () => ({ root: 'C:/projetos/teste/.workspace', skills: 'C:/projetos/teste/.workspace/skills', mcps: 'C:/projetos/teste/.workspace/mcps', agents: 'C:/projetos/teste/.workspace/agents' })
    } as any;
    const store = new WorkspaceSettingsStore(manager);
    const loaded = await store.load();
    expect(loaded.rootPath).toBe('C:/projetos/teste');
    loaded.defaults.sandbox = { ...loaded.defaults.sandbox, maxMemoryMB: 2048 };
    loaded.security.autoVerifySkills = false;
    expect(await store.save(loaded)).toBe(true);
    const reloaded = await store.load();
    expect(reloaded.defaults.sandbox?.maxMemoryMB).toBe(2048);
    expect(reloaded.security.autoVerifySkills).toBe(false);
  });

  it('uses .workspace/workspace.config.json with the native backend', async () => {
    const writes: Array<{ filePath: string; content: string }> = [];
    const originalWindow = (globalThis as any).window;
    (globalThis as any).window = {
      webui: {
        call: async (name: string, payload: string) => {
          if (name === 'read_workspace_asset') {
            return JSON.stringify({
              content: JSON.stringify({
                properties: {
                  rootPath: 'C:/projetos/teste',
                  defaults: { sandbox: { maxMemoryMB: 1024 } },
                  security: { autoVerifySkills: true, strictSecurityMode: true }
                }
              })
            });
          }
          if (name === 'write_workspace_asset') {
            writes.push(JSON.parse(payload));
            return true;
          }
          return null;
        }
      }
    };

    try {
      const manager = {
        getWorkspaceRoot: () => 'C:/projetos/teste',
        getWorkspaceDirectories: () => ({
          root: 'C:/projetos/teste/.workspace',
          skills: 'C:/projetos/teste/.workspace/skills',
          mcps: 'C:/projetos/teste/.workspace/mcps',
          agents: 'C:/projetos/teste/.workspace/agents'
        })
      } as any;
      const store = new WorkspaceSettingsStore(manager);
      const loaded = await store.load();
      expect(loaded.defaults.sandbox?.maxMemoryMB).toBe(1024);

      expect(await store.save(loaded)).toBe(true);
      expect(writes[0].filePath).toBe('C:/projetos/teste/.workspace/workspace.config.json');
      const content = JSON.parse(writes[0].content);
      expect(content.properties.rootPath).toBe('C:/projetos/teste');
      expect(content.properties.directories.root).toBe('C:/projetos/teste/.workspace');
    } finally {
      if (originalWindow === undefined) {
        delete (globalThis as any).window;
      } else {
        (globalThis as any).window = originalWindow;
      }
    }
  });

  it('updates the project graph and saves the manifest', async () => {
    let current = { ...manifest, globalDefaults: undefined as any };
    let defaults: any = { model: { provider: 'deepseek', model: 'old' }, sandbox: { maxMemoryMB: 512 } };
    const graph = {
      getGlobalDefaults: () => defaults,
      updateGlobalDefaults: (patch: any) => { defaults = { ...defaults, ...patch, model: { ...defaults.model, ...(patch.model || {}) } }; }
    } as any;
    const manager = {
      getCurrentProject: () => current,
      updateProjectMetadata: (patch: any) => { current = { ...current, ...patch }; },
      saveCurrentProject: async () => true
    } as any;
    const store = new ProjectSettingsStore(manager, graph);
    const loaded = await store.load();
    expect(loaded.hasExplicitDefaults).toBe(false);
    expect(await store.saveDefaults({ model: { model: 'new-model' } })).toBe(true);
    expect(graph.getGlobalDefaults().model.model).toBe('new-model');
    expect(current.globalDefaults).toBeDefined();
  });
});
