import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { EventBus } from '../src/core/EventBus.ts';
import { LibraryRegistry } from '../src/core/libraries/LibraryRegistry.ts';
import { LibraryService } from '../src/core/libraries/LibraryService.ts';
import { LibraryAuditGate } from '../src/core/libraries/LibraryAuditGate.ts';
import { LibraryStore } from '../src/core/libraries/LibraryStore.ts';
import { WorkspaceManager } from '../src/core/workspace/WorkspaceManager.ts';
import { SkillLedgerService } from '../src/core/skills/SkillLedgerService.ts';
import { McpLedgerService } from '../src/core/skills/McpLedgerService.ts';
import { createSkillAsset } from '../src/core/libraries/adapters/skillAssetAdapter.ts';
import { assetPayloadToNodeSkill } from '../src/core/libraries/types.ts';
import type { ILibraryAssetAudit } from '../src/core/libraries/types.ts';

const fakeStore = {
  load: async () => null,
  save: async () => true
} as any;

function approvedAudit(): ILibraryAssetAudit {
  return {
    verdict: 'APPROVE',
    riskScore: 0,
    policyVersion: 'test',
    auditedAt: new Date().toISOString(),
    hash: 'a'.repeat(64),
    findings: [],
    warnings: []
  };
}

describe('LibraryService - importação, derivação e instalação', () => {
  let bus: EventBus;
  let registry: LibraryRegistry;
  let workspace: WorkspaceManager;
  let service: LibraryService;

  beforeEach(() => {
    bus = new EventBus();
    registry = LibraryRegistry.getInstance(bus);
    registry.resetForTesting(bus);
    workspace = WorkspaceManager.getInstance(bus);
    workspace.resetForTesting(bus);
    SkillLedgerService.resetForTesting(bus);
    McpLedgerService.resetForTesting(bus);
    service = new LibraryService(
      registry,
      workspace,
      LibraryAuditGate.getInstance(),
      fakeStore
    );
  });

  afterEach(() => {
    delete (globalThis as any).window;
  });

  it('importa as skills da biblioteca embutida somente após auditoria', async () => {
    const result = await service.importBuiltinSkills();

    expect(result.imported.length).toBeGreaterThan(0);
    expect(result.rejected).toHaveLength(0);
    for (const asset of result.imported) {
      expect(asset.kind).toBe('skill');
      expect(asset.audit.verdict).not.toBe('REJECT');
      expect(asset.audit.hash).toMatch(/^[a-f0-9]{64}$/);
      expect(registry.getAsset(asset.id)).toBeDefined();
    }
    expect(registry.listAssets('library-arestrix').length).toBeGreaterThan(0);
  });

  it('importa skills e MCPs do catálogo do Workspace para uma biblioteca', async () => {
    const result = await service.importWorkspaceCatalog();

    expect(result.imported.some(asset => asset.kind === 'skill')).toBe(true);
    expect(result.imported.some(asset => asset.kind === 'mcp')).toBe(true);
    expect(result.imported.every(asset => !asset.id.includes(':'))).toBe(true);
    expect(registry.list('workspace').some(library => library.name === 'Arestrix Workspace')).toBe(true);
  });

  it('cria derivado em Minha biblioteca e mantém o asset original imutável', async () => {
    const imported = await service.importBuiltinSkills();
    const source = imported.imported[0];

    const derived = await service.deriveAsset(source.id);
    expect(derived).not.toBeNull();
    expect(derived?.derivedFrom?.assetId).toBe(source.id);
    expect(derived?.libraryId).not.toBe(source.libraryId);
    expect(registry.getAsset(source.id)?.name).toBe(source.name);

    const originalEdit = await service.updateDerivedAsset(source.id, {
      kind: 'skill',
      markdownContent: '# alterado'
    });
    expect(originalEdit.ok).toBe(false);

    const updated = await service.updateDerivedAsset(derived!.id, {
      kind: 'skill',
      markdownContent: '# derivado atualizado\n\nNovo conteúdo auditado.'
    });
    expect(updated.ok).toBe(true);
    expect(updated.asset?.audit.hash).not.toBe(derived!.audit.hash);
  });

  it('instala uma skill aprovada em .workspace/skills via backend', async () => {
    const writes = new Map<string, string>();
    (globalThis as any).window = {
      webui: {
        call: async (name: string, payload: string) => {
          if (name !== 'write_workspace_asset') return null;
          const data = JSON.parse(payload);
          writes.set(data.filePath, data.content);
          return true;
        }
      }
    };

    const imported = await service.importBuiltinSkills();
    const asset = imported.imported[0];
    const result = await service.installAsset(asset.id);

    expect(result.ok).toBe(true);
    expect(result.path).toContain('.workspace/skills/');
    const paths = Array.from(writes.keys());
    expect(paths.some(path => path.endsWith('/SKILL.md'))).toBe(true);
    expect(paths.some(path => path.endsWith('/skill.json'))).toBe(true);
    expect(registry.getAsset(asset.id)?.status).toBe('installed');
  });

  it('não instala asset REJECT', async () => {
    const asset = createSkillAsset({
      id: 'asset-hostil',
      libraryId: 'library-arestrix',
      name: 'Hostil',
      description: 'Teste',
      category: 'Teste',
      version: '1.0.0',
      license: 'MIT',
      payload: { kind: 'skill', markdownContent: '# Hostil', systemPrompt: '', tools: [] },
      audit: { ...approvedAudit(), verdict: 'REJECT', riskScore: 100 },
      provenance: { sourceKind: 'builtin', sourceId: 'library-arestrix', importedAt: new Date().toISOString() }
    });
    registry.upsertAsset(asset);

    const result = await service.installAsset(asset.id);
    expect(result.ok).toBe(false);
  });

  it('reaudita um asset existente', async () => {
    const imported = await service.importBuiltinSkills();
    const result = await service.reauditAsset(imported.imported[0].id);

    expect(result.ok).toBe(true);
    expect(result.asset?.audit.hash).toMatch(/^[a-f0-9]{64}$/);
    expect(result.asset?.audit.verdict).not.toBe('PENDING');
  });

  it('cria uma skill em branco já auditada em Minha biblioteca', async () => {
    const asset = await service.createBlankSkill({ name: 'Nova Skill de Teste' });

    expect(asset).not.toBeNull();
    expect(asset?.kind).toBe('skill');
    expect(asset?.audit.verdict).not.toBe('PENDING');
    expect(asset?.payload.kind).toBe('skill');
    expect(registry.get(asset!.libraryId)?.name).toContain('Minha biblioteca');
  });

  it('detecta quando o asset da biblioteca mudou depois do snapshot', async () => {
    const imported = await service.importBuiltinSkills();
    const derived = await service.deriveAsset(imported.imported[0].id);
    expect(derived).not.toBeNull();

    const snapshot = assetPayloadToNodeSkill(derived!);
    expect(service.getAssetForSkill(snapshot)?.id).toBe(derived!.id);
    expect(service.hasLibraryUpdate(snapshot)).toBe(false);

    const updated = await service.updateDerivedAsset(derived!.id, {
      kind: 'skill',
      markdownContent: '# Conteúdo novo da biblioteca'
    });
    expect(updated.ok).toBe(true);
    expect(service.hasLibraryUpdate(snapshot)).toBe(true);
  });

  it('salva templates de vértice na biblioteca com auditoria', async () => {
    const template = {
      id: 'custom_template_test',
      name: 'Template de Teste',
      taskType: 'agent' as const,
      description: 'Template criado no teste',
      inputs: [{ name: 'in', type: 'flow' as const }],
      outputs: [{ name: 'out', type: 'flow' as const }],
      lambdaBodyTemplate: '// corpo do template'
    };

    const asset = await service.saveTemplateToLibrary(template, { scope: 'workspace' });
    expect(asset).not.toBeNull();
    expect(asset?.kind).toBe('agent-template');
    expect(asset?.payload.kind).toBe('agent-template');
    expect(asset?.audit.verdict).not.toBe('REJECT');
    expect(asset?.id).not.toContain(':');

    const cautious = await service.saveTemplateToLibrary({
      ...template,
      id: 'custom_template_process',
      name: 'Template com Processo',
      lambdaBodyTemplate: 'system("calc");'
    }, { scope: 'workspace' });
    expect(cautious?.audit.verdict).toBe('CAUTION');

    const invalid = await service.saveTemplateToLibrary({
      ...template,
      id: '',
      name: ''
    }, { scope: 'workspace' });
    expect(invalid).toBeNull();
  });

  it('clona uma biblioteca e mantém vínculo com a origem', async () => {
    const imported = await service.importBuiltinSkills();
    const clone = await service.cloneLibrary('library-arestrix', 'Biblioteca Clonada', 'workspace');

    expect(clone).not.toBeNull();
    expect(clone?.imported.length).toBe(imported.imported.length);
    expect(registry.list('workspace').some(library => library.name === 'Biblioteca Clonada')).toBe(true);
    expect(clone?.imported[0].derivedFrom?.libraryId).toBe('library-arestrix');
  });

  it('exporta e reimporta uma biblioteca com auditoria', async () => {
    await service.importBuiltinSkills();
    const exported = service.exportLibrary('library-arestrix');
    expect(exported).toBeTruthy();
    expect(exported).not.toContain('"env"');

    const imported = await service.importLibrary(exported!, { scope: 'workspace', name: 'Biblioteca Importada' });
    expect(imported).not.toBeNull();
    expect(imported?.imported.length).toBeGreaterThan(0);
    expect(registry.list('workspace').some(library => library.name === 'Biblioteca Importada')).toBe(true);
    expect(imported?.imported.every(asset => asset.audit.hash.length === 64)).toBe(true);
  });

  it('LibraryStore grava e lê o snapshot no arquivo do Workspace', async () => {
    const files = new Map<string, string>();
    (globalThis as any).window = {
      webui: {
        call: async (name: string, payload: string) => {
          const data = JSON.parse(payload);
          if (name === 'write_workspace_asset') {
            files.set(data.filePath, data.content);
            return true;
          }
          if (name === 'read_workspace_asset') {
            const content = files.get(data.filePath);
            return content ? JSON.stringify({ ok: true, content }) : null;
          }
          return null;
        }
      }
    };

    const store = new LibraryStore(workspace);
    const state = registry.snapshot();
    expect(await store.save('workspace', state)).toBe(true);

    const loaded = await store.load('workspace');
    expect(loaded?.version).toBe(2);
    expect(loaded?.libraries.some(library => library.id === 'library-arestrix')).toBe(true);
    expect(files.size).toBe(1);
    expect(Array.from(files.keys())[0]).toContain('.workspace/libraries.json');
  });
});
