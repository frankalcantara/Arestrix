import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { EventBus } from '../src/core/EventBus.ts';
import { LibraryRegistry } from '../src/core/libraries/LibraryRegistry.ts';
import { PropertiesController } from '../src/ui/properties/PropertiesController.ts';

describe('PropertiesController - criação de bibliotecas e fontes', () => {
  let bus: EventBus;
  let libraries: LibraryRegistry;
  let controller: PropertiesController;
  const createdLibraryIds: string[] = [];
  const createdSourceIds: string[] = [];

  beforeEach(() => {
    bus = new EventBus();
    libraries = LibraryRegistry.getInstance(bus);
    libraries.resetForTesting(bus);
    // A fonte de teste é única por execução para não colidir com o storage local.
    controller = new PropertiesController({} as any, {} as any, {} as any);
  });

  afterEach(() => {
    for (const id of createdLibraryIds.splice(0)) libraries.remove(id);
    for (const id of createdSourceIds.splice(0)) libraries.removeSource(id);
  });

  it('cria uma biblioteca local vazia no escopo informado', async () => {
    const ok = await (controller as any).saveLibraries('workspace', {
      libraryName: 'Biblioteca de Teste Local',
      libraryDescription: 'Criada pelo teste'
    });

    expect(ok).toBe(true);
    const created = libraries.list('workspace').find(library => library.name === 'Biblioteca de Teste Local');
    expect(created).toBeDefined();
    expect(created?.kind).toBe('local');
    if (created) createdLibraryIds.push(created.id);
  });

  it('cadastra uma fonte de repositório e cria uma biblioteca ligada a ela', async () => {
    const repoPath = `acme/library-test-${Date.now()}`;
    const ok = await (controller as any).saveLibraries('studio', {
      libraryName: 'Biblioteca de Teste com Repositório',
      libraryDescription: 'Criada a partir de uma fonte',
      repoPath,
      repoName: 'Fonte de Teste',
      repoLicense: 'MIT'
    });

    expect(ok).toBe(true);
    const source = libraries.listSources().find(item => item.github?.repo === repoPath);
    expect(source).toBeDefined();
    if (source) createdSourceIds.push(source.id);

    const created = libraries.list('studio').find(library => library.name === 'Biblioteca de Teste com Repositório');
    expect(created).toBeDefined();
    expect(created?.kind).toBe('repository');
    expect(created?.sourceId).toBe(source?.id);
    if (created) createdLibraryIds.push(created.id);
  });
});
