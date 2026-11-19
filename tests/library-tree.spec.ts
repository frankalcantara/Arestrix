import { describe, it, expect, beforeEach } from 'vitest';
import { EventBus } from '../src/core/EventBus.ts';
import { LibraryRegistry } from '../src/core/libraries/LibraryRegistry.ts';
import { LibraryService } from '../src/core/libraries/LibraryService.ts';
import { LibraryAuditGate } from '../src/core/libraries/LibraryAuditGate.ts';
import { WorkspaceManager } from '../src/core/workspace/WorkspaceManager.ts';
import { SkillLedgerService } from '../src/core/skills/SkillLedgerService.ts';
import { McpLedgerService } from '../src/core/skills/McpLedgerService.ts';
import {
  buildEditableNodeFromAsset,
  isBundledAssetModified,
  renderLibraryBrowser
} from '../src/ui/libraries/LibraryTree.ts';

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

  it('renderiza a árvore com bibliotecas, domínios e ações de catálogo', () => {
    const html = renderLibraryBrowser({ registry, scope: 'studio' });

    expect(html).toContain('properties-library-browser');
    expect(html).toContain('data-library-select="library-arestrix"');
    expect(html).toContain('Biblioteca Arestrix');
    expect(html).toContain('data-library-domain-toggle=');
    expect(html).toContain('Engenharia de Software');
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

  it('transforma a ontologia de 2 níveis em árvore e agrupa assets sem taxonomia', () => {
    const html = renderLibraryBrowser({ registry, scope: 'studio' });

    expect(html).toContain('data-library-domain-toggle=');
    expect(html).toContain('data-library-subdomain-toggle=');
    expect(html).toContain('Engenharia de Software');
    expect(html).toContain('Frontend, Mobile &amp; UI');
    expect(html).toContain('Sem domínio');
    expect(html).toContain('Sem subdomínio');
    expect(html).toContain('data-library-action="reset-builtin"');
    expect(html).toContain('Restaurar padrão de instalação');
  });
});

describe('LibraryTree - edição e restauração', () => {
  it('não oferece restaurar padrão para cópia local ou derivada', () => {
    const copied = {
      bundledInProduct: true,
      libraryId: 'lib-minha-biblioteca',
      derivedFrom: { libraryId: 'library-arestrix', assetId: 'asset-x' },
      version: '1.0.0',
      audit: { hash: 'changed' },
      origin: { installHash: 'factory' }
    } as any;
    expect(isBundledAssetModified(copied, {} as any)).toBe(false);

    const local = {
      bundledInProduct: true,
      libraryId: 'lib-importada',
      version: '1.0.0',
      audit: { hash: 'changed' },
      origin: { installHash: 'factory' }
    } as any;
    expect(isBundledAssetModified(local, {} as any)).toBe(false);
  });

  it('oferece restaurar padrão apenas para ativo de fábrica modificado', () => {
    const factory = {
      bundledInProduct: true,
      libraryId: 'library-arestrix',
      version: '1.0.0',
      audit: { hash: 'factory' },
      origin: { installHash: 'factory' }
    } as any;
    const service = {
      getInstallDefaultAsset: () => ({ ...factory })
    } as any;
    expect(isBundledAssetModified(factory, service)).toBe(false);

    const modified = { ...factory, audit: { hash: 'changed' } } as any;
    expect(isBundledAssetModified(modified, service)).toBe(true);
  });

  it('cria o vértice editável a partir do template do ativo', () => {
    const asset = {
      id: 'asset-local-agent',
      libraryId: 'lib-minha-biblioteca',
      kind: 'agent-template',
      name: 'test-agent',
      version: '1.0.0',
      bundledInProduct: true,
      audit: { hash: 'h', verdict: 'APPROVE', riskScore: 0, policyVersion: 'p', findings: [], warnings: [] },
      origin: { repo: 'example/repo', url: 'https://example.test', license: 'MIT' },
      payload: {
        kind: 'agent-template',
        template: {
          id: 'tpl-test',
          name: 'test-agent',
          taskType: 'agent',
          description: '',
          inputs: [{ name: 'in', type: 'flow' }],
          outputs: [{ name: 'out', type: 'flow' }],
          parameters: {
            tone: { label: 'Tom', type: 'string', defaultValue: 'formal', description: '' }
          },
          lambdaBodyTemplate: '',
          presetLocked: true,
          agentSettings: { inheritModel: false, model: { provider: 'local', model: 'demo' } }
        }
      }
    } as any;
    const graph = {
      getAllNodes: () => [{ position: { x: 100, y: 50, z: 0 } }]
    } as any;

    const node = buildEditableNodeFromAsset(asset, graph, 'test-agent');
    expect(node).not.toBeNull();
    expect(node!.label).toBe('test-agent (editável)');
    expect(node!.type).toBe('agent');
    expect(node!.presetLocked).toBe(false);
    expect(node!.presetId).toBe('tpl-test');
    expect(node!.parameters.tone).toBe('formal');
    expect(node!.inputs[0].id).toBe(`${node!.id}_in_0`);
    expect(node!.outputs[0].id).toBe(`${node!.id}_out_0`);
    expect(node!.position).toEqual({ x: 420, y: 50, z: 0 });
    expect(node!.librarySource?.assetId).toBe('asset-local-agent');
    expect(node!.agentSettings?.inheritModel).toBe(false);
  });
});
