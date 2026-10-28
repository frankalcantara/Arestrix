import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { ProjectLibraryStore } from '../src/core/libraries/ProjectLibraryStore.ts';

describe('ProjectLibraryStore - habilitação e pins por projeto', () => {
  let originalLocalStorage: any;
  let data: Map<string, string>;
  let store: ProjectLibraryStore;

  beforeEach(() => {
    originalLocalStorage = (globalThis as any).localStorage;
    data = new Map();
    (globalThis as any).localStorage = {
      getItem: (key: string) => data.get(key) ?? null,
      setItem: (key: string, value: string) => { data.set(key, value); },
      removeItem: (key: string) => { data.delete(key); }
    };
    store = new ProjectLibraryStore();
  });

  afterEach(() => {
    (globalThis as any).localStorage = originalLocalStorage;
  });

  it('retorna vazio quando o projeto ainda não tem preferências', () => {
    expect(store.load('project-1')).toEqual({
      enabledLibraryIds: [],
      pinnedAssetIds: [],
      updatedAt: undefined
    });
  });

  it('grava e lê bibliotecas habilitadas e assets fixados', () => {
    const ok = store.save('project-1', {
      enabledLibraryIds: ['library-arestrix', 'lib-minha-biblioteca', 'lib-minha-biblioteca'],
      pinnedAssetIds: ['asset-skill-a', 'asset-skill-a']
    });

    expect(ok).toBe(true);
    const loaded = store.load('project-1');
    expect(loaded.enabledLibraryIds).toEqual(['library-arestrix', 'lib-minha-biblioteca']);
    expect(loaded.pinnedAssetIds).toEqual(['asset-skill-a']);
    expect(loaded.updatedAt).toBeTruthy();
  });

  it('isola projetos diferentes e permite limpar', () => {
    store.save('project-1', { enabledLibraryIds: ['lib-a'], pinnedAssetIds: [] });
    store.save('project-2', { enabledLibraryIds: ['lib-b'], pinnedAssetIds: [] });

    expect(store.load('project-1').enabledLibraryIds).toEqual(['lib-a']);
    expect(store.load('project-2').enabledLibraryIds).toEqual(['lib-b']);

    store.clear('project-1');
    expect(store.load('project-1').enabledLibraryIds).toEqual([]);
    expect(store.load('project-2').enabledLibraryIds).toEqual(['lib-b']);
  });

  it('ignora conteúdo corrompido', () => {
    data.set('arestrix_project_libraries_v1:project-x', '{invalido');
    expect(store.load('project-x')).toEqual({
      enabledLibraryIds: [],
      pinnedAssetIds: [],
      updatedAt: undefined
    });
  });
});
