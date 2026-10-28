import { describe, it, expect, beforeEach } from 'vitest';
import { EventBus } from '../src/core/EventBus.ts';
import { LibraryRegistry } from '../src/core/libraries/LibraryRegistry.ts';
import { LibraryService } from '../src/core/libraries/LibraryService.ts';
import { LibraryAuditGate } from '../src/core/libraries/LibraryAuditGate.ts';
import { WorkspaceManager } from '../src/core/workspace/WorkspaceManager.ts';
import { SkillLedgerService } from '../src/core/skills/SkillLedgerService.ts';
import { McpLedgerService } from '../src/core/skills/McpLedgerService.ts';
import { renderLibraryBrowser } from '../src/ui/libraries/LibraryTree.ts';

const fakeStore = {
  load: async () => null,
  save: async () => true
} as any;

describe('LibraryTree - árvore de bibliotecas/assets', () => {
  let registry: LibraryRegistry;
  let service: LibraryService;

  beforeEach(async () => {
    const bus = new EventBus();
    registry = LibraryRegistry.getInstance(bus);
    registry.resetForTesting(bus);
    const workspace = WorkspaceManager.getInstance(bus);
    workspace.resetForTesting(bus);
    SkillLedgerService.resetForTesting(bus);
    McpLedgerService.resetForTesting(bus);
    service = new LibraryService(
      registry,
      workspace,
      LibraryAuditGate.getInstance(),
      fakeStore
    );
    await service.importBuiltinSkills();
  });

  it('renderiza a árvore com bibliotecas, assets e ações de catálogo', () => {
    const html = renderLibraryBrowser({ registry, scope: 'studio' });

    expect(html).toContain('properties-library-browser');
    expect(html).toContain('data-library-select="library-arestrix"');
    expect(html).toContain('Biblioteca Arestrix');
    expect(html).toContain('data-asset-select=');
    expect(html).toContain('Redação Técnica e Conceitual');
    expect(html).toContain('data-library-action="import"');
    expect(html).toContain('data-library-action="export"');
    expect(html).toContain('data-library-action="clone"');
    expect(html).toContain('data-library-action="import-file"');
  });

  it('usa o escopo do Workspace sem expor bibliotecas de outros escopos', () => {
    registry.create({ name: 'Biblioteca do Workspace', scope: 'workspace' });
    const html = renderLibraryBrowser({ registry, scope: 'workspace' });

    expect(html).toContain('Biblioteca do Workspace');
    expect(html).toContain('data-library-select="library-arestrix"');
  });

  it('mostra templates de vértice salvos na biblioteca', async () => {
    await service.saveTemplateToLibrary({
      id: 'template_ui_test',
      name: 'Template de UI',
      taskType: 'agent',
      description: 'Template para teste da árvore',
      inputs: [{ name: 'in', type: 'flow' }],
      outputs: [{ name: 'out', type: 'flow' }],
      lambdaBodyTemplate: '// corpo'
    }, { scope: 'workspace' });

    const html = renderLibraryBrowser({ registry, scope: 'workspace' });
    expect(html).toContain('Template de UI');
    expect(html).toContain('data-asset-select=');
  });
});
