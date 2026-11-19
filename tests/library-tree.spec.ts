import { describe, it, expect, beforeEach } from 'vitest';
import { EventBus } from '../src/core/EventBus.ts';
import { LibraryRegistry } from '../src/core/libraries/LibraryRegistry.ts';
import { LibraryService } from '../src/core/libraries/LibraryService.ts';
import { LibraryAuditGate } from '../src/core/libraries/LibraryAuditGate.ts';
import { WorkspaceManager } from '../src/core/workspace/WorkspaceManager.ts';
import { SkillLedgerService } from '../src/core/skills/SkillLedgerService.ts';
import { McpLedgerService } from '../src/core/skills/McpLedgerService.ts';
import {
  applyTreeNodeContextAction,
  buildEditableNodeFromAsset,
  buildNodeFromLibraryAsset,
  ensureCopyInOwnLibrary,
  getTreeNodeContextActions,
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

  it('constrói vértice livre para qualquer tipo de asset (agent-template, skill e mcp) sem sufixo editável', () => {
    const graph = {
      getAllNodes: () => [{ position: { x: 100, y: 50, z: 0 } }]
    } as any;

    const skillAsset = {
      id: 'asset-skill-demo',
      libraryId: 'library-arestrix',
      kind: 'skill',
      name: 'Skill Demo',
      description: 'Desc',
      license: 'MIT',
      version: '1.0.0',
      updatedAt: '2026-01-01T00:00:00.000Z',
      audit: { hash: 'a'.repeat(64), verdict: 'APPROVE', riskScore: 0, findings: [], warnings: [] },
      provenance: { sourceKind: 'builtin', sourceId: 'library-arestrix', importedAt: '2026-01-01T00:00:00.000Z' },
      payload: { kind: 'skill', markdownContent: '# Demo', systemPrompt: 'Prompt', tools: ['read'] }
    } as any;

    const skillNode = buildNodeFromLibraryAsset(skillAsset, graph);
    expect(skillNode).not.toBeNull();
    expect(skillNode!.type).toBe('agent');
    expect(skillNode!.label).toBe('Skill Demo');
    expect(skillNode!.agentSettings?.activeSkillId).toBe('asset-skill-demo');
    expect(skillNode!.agentSettings?.skills).toHaveLength(1);
    expect(skillNode!.position).toEqual({ x: 420, y: 50, z: 0 });

    const mcpAsset = {
      id: 'asset-mcp-demo',
      libraryId: 'library-arestrix',
      kind: 'mcp',
      name: 'Demo MCP',
      description: 'Desc',
      license: 'MIT',
      version: '1.0.0',
      updatedAt: '2026-01-01T00:00:00.000Z',
      audit: { hash: 'b'.repeat(64), verdict: 'APPROVE', riskScore: 0, findings: [], warnings: [] },
      provenance: { sourceKind: 'builtin', sourceId: 'library-arestrix', importedAt: '2026-01-01T00:00:00.000Z' },
      payload: {
        kind: 'mcp',
        transport: 'stdio',
        command: 'npx',
        args: ['-y', 'demo@1.0.0'],
        cachedTools: [{ name: 'run_demo', description: 'Run', serverId: 'mcp-demo' }]
      }
    } as any;

    const mcpNode = buildNodeFromLibraryAsset(mcpAsset, graph);
    expect(mcpNode).not.toBeNull();
    expect(mcpNode!.type).toBe('mcp');
    expect(mcpNode!.label).toBe('Demo MCP');
    expect(mcpNode!.mcpSettings?.server).toBe('asset-mcp-demo');
    expect(mcpNode!.mcpSettings?.tool).toBe('run_demo');
  });

  it('oferece Expandir todos / Recolher todos no menu de contexto conforme estado do nó e presença de filhos', () => {
    const groups = [
      {
        name: 'Dados & Persistência',
        total: 2,
        subdomains: [
          { name: 'SQL', assets: [{ id: 'a1' } as any] },
          { name: 'NoSQL', assets: [{ id: 'a2' } as any] }
        ]
      }
    ];

    const state = {
      expandedLibraries: new Set<string>(),
      seededLibraries: new Set<string>(),
      expandedGroups: new Set<string>()
    };

    // Biblioteca sem filhos: não exibe menu
    expect(getTreeNodeContextActions({ kind: 'library', libraryId: 'lib-empty' }, [], state)).toEqual([]);

    // Biblioteca fechada com filhos: apenas Expandir todos
    const libTarget = { kind: 'library' as const, libraryId: 'library-arestrix' };
    expect(getTreeNodeContextActions(libTarget, groups, state)).toEqual(['expand-all']);

    // Expandir todos abre biblioteca, domínio e todos os subdomínios
    applyTreeNodeContextAction(libTarget, 'expand-all', groups, state);
    expect(state.expandedLibraries.has('library-arestrix')).toBe(true);
    expect(getTreeNodeContextActions(libTarget, groups, state)).toEqual(['collapse-all']);

    // Recolher um subdomínio deixa o nó pai parcialmente aberto -> oferece ambas as ações
    const subTarget = { kind: 'subdomain' as const, libraryId: 'library-arestrix', domain: 'Dados & Persistência', subdomain: 'SQL' };
    expect(getTreeNodeContextActions(subTarget, groups, state)).toEqual(['collapse-all']);
    applyTreeNodeContextAction(subTarget, 'collapse-all', groups, state);
    expect(getTreeNodeContextActions(subTarget, groups, state)).toEqual(['expand-all']);
    expect(getTreeNodeContextActions(libTarget, groups, state)).toEqual(['expand-all', 'collapse-all']);

    const domTarget = { kind: 'domain' as const, libraryId: 'library-arestrix', domain: 'Dados & Persistência' };
    expect(getTreeNodeContextActions(domTarget, groups, state)).toEqual(['expand-all', 'collapse-all']);

    // Recolher todos na biblioteca fecha tudo
    applyTreeNodeContextAction(libTarget, 'collapse-all', groups, state);
    expect(state.expandedLibraries.has('library-arestrix')).toBe(false);
    expect(state.expandedGroups.size).toBe(0);
    expect(getTreeNodeContextActions(libTarget, groups, state)).toEqual(['expand-all']);
  });

  it('copia ativos da Biblioteca Arestrix para Minha biblioteca sem jamais alterar o original', async () => {
    const bus = new EventBus();
    const reg = LibraryRegistry.getInstance(bus);
    reg.resetForTesting(bus);
    const ws = WorkspaceManager.getInstance(bus);
    ws.resetForTesting(bus);
    SkillLedgerService.resetForTesting(bus);
    McpLedgerService.resetForTesting(bus);
    const svc = new LibraryService(reg, ws, LibraryAuditGate.getInstance(), fakeStore);

    const originalMcp = reg.listAssets('library-arestrix').find(a => a.kind === 'mcp')!;
    expect(originalMcp).toBeTruthy();

    const state = {
      selectedLibraryId: 'library-arestrix',
      selectedAssetId: originalMcp.id,
      search: '',
      kind: 'all' as const,
      expandedLibraries: new Set<string>(['library-arestrix']),
      seededLibraries: new Set<string>(['library-arestrix']),
      expandedGroups: new Set<string>()
    };

    const copy1 = await ensureCopyInOwnLibrary(svc, { registry: reg }, state, originalMcp, () => {});
    expect(copy1).not.toBeNull();
    expect(copy1!.id).not.toBe(originalMcp.id);
    expect(copy1!.id.length).toBeLessThanOrEqual(64);
    expect(copy1!.libraryId).not.toBe('library-arestrix');
    expect(reg.get(copy1!.libraryId)?.name).toBe('Minha biblioteca');
    expect(copy1!.derivedFrom?.assetId).toBe(originalMcp.id);

    // Original na Biblioteca Arestrix permanece intacto e somente leitura
    const originalAfter = reg.getAsset(originalMcp.id)!;
    expect(originalAfter.status).toBe('available');
    expect(originalAfter.libraryId).toBe('library-arestrix');
    const editAttempt = await svc.updateDerivedAsset(originalMcp.id, {});
    expect(editAttempt.ok).toBe(false);

    // Segunda chamada reaproveita a cópia em Minha biblioteca sem duplicar
    const copy2 = await ensureCopyInOwnLibrary(svc, { registry: reg }, state, originalMcp, () => {});
    expect(copy2?.id).toBe(copy1!.id);
  });
});
