import { describe, it, expect, beforeEach } from 'vitest';
import { EventBus } from '../src/core/EventBus.ts';
import { LibraryRegistry } from '../src/core/libraries/LibraryRegistry.ts';

describe('LibraryRegistry - Bibliotecas de Assets (Fase 1)', () => {
  let bus: EventBus;
  let registry: LibraryRegistry;

  beforeEach(() => {
    bus = new EventBus();
    registry = LibraryRegistry.getInstance(bus);
    registry.resetForTesting(bus);
  });

  it('expõe a Biblioteca Arestrix embutida e não permite removê-la', () => {
    const libraries = registry.list();
    expect(libraries).toHaveLength(1);
    expect(libraries[0].id).toBe('library-arestrix');
    expect(libraries[0].isBuiltin).toBe(true);
    expect(registry.remove('library-arestrix')).toBe(false);
  });

  it('cria uma biblioteca local no escopo Workspace por padrão', () => {
    const created = registry.create({
      name: 'Skills da Equipe de Dados',
      description: 'Habilidades internas de dados'
    });

    expect(created).not.toBeNull();
    expect(created?.id).toMatch(/^lib-skills-da-equipe-de-dados/);
    expect(created?.kind).toBe('local');
    expect(created?.scope).toBe('workspace');
    expect(registry.list('workspace').map(library => library.name)).toContain('Skills da Equipe de Dados');
    expect(registry.list('studio').map(library => library.name)).not.toContain('Skills da Equipe de Dados');
  });

  it('cria uma biblioteca a partir de uma fonte de repositório', () => {
    const created = registry.create({
      name: 'Skills da Comunidade',
      scope: 'studio',
      kind: 'repository',
      sourceId: 'repo-community-skills'
    });

    expect(created?.kind).toBe('repository');
    expect(created?.sourceId).toBe('repo-community-skills');
    expect(registry.get(created!.id)?.sourceId).toBe('repo-community-skills');
  });

  it('recusa nomes vazios ou duplicados', () => {
    expect(registry.create({ name: '   ' })).toBeNull();
    expect(registry.create({ name: 'Minha Biblioteca' })).not.toBeNull();
    expect(registry.create({ name: 'minha biblioteca' })).toBeNull();
  });

  it('remove apenas bibliotecas criadas pelo usuário e emite eventos', () => {
    let changed = 0;
    let removedId = '';
    bus.on('library:changed', () => { changed += 1; });
    bus.on('library:removed', (payload: { id: string }) => { removedId = payload.id; });

    const created = registry.create({ name: 'Temporária' });
    expect(created).not.toBeNull();
    expect(registry.remove(created!.id)).toBe(true);
    expect(registry.get(created!.id)).toBeUndefined();
    expect(removedId).toBe(created!.id);
    expect(changed).toBeGreaterThanOrEqual(2);
    expect(registry.remove(created!.id)).toBe(false);
  });

  it('migra fontes GitHub legadas para o modelo de bibliotecas', () => {
    const store = new Map<string, string>();
    Object.defineProperty(globalThis, 'localStorage', {
      configurable: true,
      value: {
        getItem: (key: string) => store.get(key) ?? null,
        setItem: (key: string, value: string) => { store.set(key, value); },
        removeItem: (key: string) => { store.delete(key); }
      }
    });

    localStorage.setItem('arestrix_github_skills_repos', JSON.stringify([
      { id: 'legacy-team', name: 'Skills Legadas', repo: 'acme/legacy-skills', license: 'MIT' },
      { id: 'library-arestrix', name: 'Arestrix', repo: 'arestrix/default' },
      { id: 'invalid', name: 'Inválida', repo: '../fora' }
    ]));

    (registry as any).loadPersistedConfig();

    const sources = registry.listSources();
    expect(sources).toHaveLength(1);
    expect(sources[0].github).toEqual({ repo: 'acme/legacy-skills', ref: 'main' });
    expect(sources[0].licenseAllowlist).toEqual(['MIT']);
    expect(registry.list().map(library => library.name)).toContain('Skills Legadas');
    expect(localStorage.getItem('arestrix_github_skills_repos')).toBeNull();
  });
});
