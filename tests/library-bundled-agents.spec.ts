import { describe, it, expect, beforeEach, afterEach } from 'vitest';
import { EventBus } from '../src/core/EventBus.ts';
import {
  BUNDLED_AGENT_VERTICES,
  BUNDLED_MCP_SERVERS,
  bundledAgentVertexToLibraryAsset,
  getBundledAgentVertexById
} from '../src/core/libraries/BuiltinLibraryCatalog.ts';
import { LibraryRegistry } from '../src/core/libraries/LibraryRegistry.ts';
import { LibraryService } from '../src/core/libraries/LibraryService.ts';
import { LibraryAuditGate } from '../src/core/libraries/LibraryAuditGate.ts';
import { LibraryStore } from '../src/core/libraries/LibraryStore.ts';
import { WorkspaceManager } from '../src/core/workspace/WorkspaceManager.ts';
import { SkillLedgerService } from '../src/core/skills/SkillLedgerService.ts';
import { McpLedgerService } from '../src/core/skills/McpLedgerService.ts';
import { AgentTemplateRegistry } from '../src/core/templates/AgentTemplateRegistry.ts';
import { createSkillAsset } from '../src/core/libraries/adapters/skillAssetAdapter.ts';

function createQuotaGuardedStorage(maxBytesPerItem = 250_000) {
  const store = new Map<string, string>();
  return {
    store,
    api: {
      getItem: (key: string) => store.get(key) ?? null,
      setItem: (key: string, value: string) => {
        if (value.length > maxBytesPerItem) {
          const err = new Error(`QuotaExceededError: ${value.length} bytes exceeds ${maxBytesPerItem}`);
          err.name = 'QuotaExceededError';
          throw err;
        }
        store.set(key, value);
      },
      removeItem: (key: string) => {
        store.delete(key);
      }
    }
  };
}

describe('Bundled Agent Vertices (322) - Catalog, Quota Protection & Restore Defaults', () => {
  let bus: EventBus;
  let registry: LibraryRegistry;
  let workspace: WorkspaceManager;
  let service: LibraryService;
  let storageData: Map<string, string>;
  let workspaceFiles: Map<string, string>;
  let originalLocalStorage: any;
  let originalWindow: any;

  beforeEach(() => {
    originalLocalStorage = (globalThis as any).localStorage;
    originalWindow = (globalThis as any).window;

    const guarded = createQuotaGuardedStorage(250_000);
    storageData = guarded.store;
    (globalThis as any).localStorage = guarded.api;

    workspaceFiles = new Map();
    (globalThis as any).window = {
      webui: {
        call: async (name: string, payload: string) => {
          const data = JSON.parse(payload);
          if (name === 'write_workspace_asset') {
            workspaceFiles.set(data.filePath, data.content);
            return true;
          }
          if (name === 'read_workspace_asset') {
            const content = workspaceFiles.get(data.filePath);
            return content ? JSON.stringify({ ok: true, content }) : null;
          }
          return null;
        }
      }
    };

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
      new LibraryStore(workspace)
    );
  });

  afterEach(() => {
    (globalThis as any).localStorage = originalLocalStorage;
    (globalThis as any).window = originalWindow;
  });

  it('carrega todos os 322 vértices de agentes embutidos em library-arestrix com configurações completas', () => {
    expect(BUNDLED_AGENT_VERTICES).toHaveLength(322);

    const assets = service.listAssets('library-arestrix');
    const agentAssets = assets.filter(asset => asset.kind === 'agent-template');
    const skillAssets = assets.filter(asset => asset.kind === 'skill');
    const mcpAssets = assets.filter(asset => asset.kind === 'mcp');

    expect(agentAssets).toHaveLength(322);
    expect(skillAssets).toHaveLength(6);
    expect(mcpAssets).toHaveLength(BUNDLED_MCP_SERVERS.length);
    expect(assets).toHaveLength(328 + BUNDLED_MCP_SERVERS.length);

    for (const asset of agentAssets) {
      expect(asset.bundledInProduct).toBe(true);
      expect(asset.status).toBe('available');
      expect(asset.domain).toBeTruthy();
      expect(asset.subdomain).toBeTruthy();
      expect(asset.origin?.bundledInProduct).toBe(true);
      expect(asset.origin?.sourceKind).toBe('builtin');
      expect(asset.origin?.libraryId).toBe('library-arestrix');
      expect(asset.audit.verdict).toBe('APPROVE');
      expect(asset.audit.policyVersion).toBe('bundled-v1');
      expect(asset.audit.hash).toBe(asset.origin?.installHash);

      expect(asset.payload.kind).toBe('agent-template');
      if (asset.payload.kind === 'agent-template') {
        const settings = asset.payload.template.agentSettings;
        expect(settings).toBeDefined();
        expect(settings?.model?.reasoningEffort).toBeTruthy();
        expect(settings?.sandbox?.isolationLevel).toBeTruthy();
        expect(Array.isArray(settings?.codeMode?.allowedTools)).toBe(true);
        expect(settings?.memory).toBeDefined();
        expect(settings?.compression).toBeDefined();
        expect(Array.isArray(settings?.skills)).toBe(true);
        expect(settings?.skills?.length).toBeGreaterThan(0);

        const gateAudit = LibraryAuditGate.getInstance().auditTemplate(asset.payload.template);
        expect(gateAudit.verdict).not.toBe('REJECT');
        expect(LibraryAuditGate.getInstance().canInstall(gateAudit).ok).toBe(true);
      }
    }
  });

  it('resolve lookup por id, origin.assetId e templateId com imutabilidade de catálogo', () => {
    const byTemplate = getBundledAgentVertexById('resolve-pr-feedback');
    const byEntryId = getBundledAgentVertexById('asset-library-arestrix-agent-resolve-pr-feedback');
    const byOriginAsset = getBundledAgentVertexById('everyinc__compound-engineering-plugin__ce-resolve-pr-feedback');

    expect(byTemplate).toBeDefined();
    expect(byEntryId?.id).toBe(byTemplate?.id);
    expect(byOriginAsset?.id).toBe(byTemplate?.id);

    // Entradas que compartilham templateId continuam distinguíveis pelo assetId/entry.id
    const pmSecondary = getBundledAgentVertexById('asset-library-arestrix-agent-product-manager-257a77');
    expect(pmSecondary).toBeDefined();
    expect(pmSecondary?.origin.assetId).toBe('alirezarezvani__claude-skills__product-manager');

    // Mutação no objeto retornado não contamina o catálogo
    byTemplate!.name = 'MUTATED';
    byTemplate!.template.agentSettings!.model!.reasoningEffort = 'none';
    const fresh = getBundledAgentVertexById('resolve-pr-feedback');
    expect(fresh?.name).toBe('ce-resolve-pr-feedback');
    expect(fresh?.template.agentSettings?.model?.reasoningEffort).toBe('high');

    const converted = bundledAgentVertexToLibraryAsset(fresh!);
    expect(converted.provenance.authority).toBe('Arestrix');
    expect(converted.provenance.repo).toBe('EveryInc/compound-engineering-plugin');
  });

  it('protege a quota do localStorage sem serializar payloads completos dos 322 agentes', async () => {
    const snapshot = registry.snapshot();
    const serializedSnapshot = JSON.stringify(snapshot);
    expect(serializedSnapshot.length).toBeLessThan(10_000);
    expect(snapshot.assets).toHaveLength(0);

    // Mesmo alterando status de um asset de fábrica e persistindo em ambos os stores,
    // o conteúdo gravado continua leve e sobrevive a restauração.
    const target = registry.getAsset('asset-library-arestrix-agent-resolve-pr-feedback')!;
    registry.upsertAsset({ ...target, status: 'pinned' });

    const persistedOk = await service.persist('workspace');
    expect(persistedOk).toBe(true);

    const registryRaw = storageData.get('arestrix_libraries_v2') || '';
    const storeRaw = storageData.get('arestrix_library_state_workspace_v1') || '';
    const workspaceRaw = workspaceFiles.get('.workspace/libraries.json') || '';

    expect(registryRaw.length).toBeGreaterThan(0);
    expect(registryRaw.length).toBeLessThan(20_000);
    expect(storeRaw.length).toBeLessThan(20_000);
    expect(workspaceRaw.length).toBeLessThan(20_000);

    // Simula reinicialização limpando memória e restaurando do snapshot persistido
    const parsedStore = JSON.parse(storeRaw);
    expect(parsedStore.assets).toHaveLength(1);
    expect(parsedStore.assets[0].status).toBe('pinned');

    registry.restore(parsedStore, true);
    const hydrated = registry.listAssets('library-arestrix');
    expect(hydrated).toHaveLength(328 + BUNDLED_MCP_SERVERS.length);

    const restoredPinned = registry.getAsset('asset-library-arestrix-agent-resolve-pr-feedback');
    expect(restoredPinned?.status).toBe('pinned');
    expect(restoredPinned?.payload.kind).toBe('agent-template');
    if (restoredPinned?.payload.kind === 'agent-template') {
      expect(restoredPinned.payload.template.agentSettings?.skills?.length).toBeGreaterThan(0);
      expect(restoredPinned.payload.template.agentSettings?.skills?.[0]?.markdownContent).toContain('Resolve PR Review Feedback');
    }
  });

  it('restaura asset individual e reseta Biblioteca Arestrix para os padrões de instalação', async () => {
    const targetId = 'asset-library-arestrix-agent-resolve-pr-feedback';
    const factoryBefore = service.getInstallDefaultAsset(targetId)!;
    expect(factoryBefore).not.toBeNull();
    factoryBefore.name = 'mutated-local-clone';
    expect(service.getInstallDefaultAsset(targetId)?.name).toBe('ce-resolve-pr-feedback');

    // Altera e remove um asset embutido, além de inserir um asset não-embutido em library-arestrix
    const existing = registry.getAsset(targetId)!;
    registry.upsertAsset({
      ...existing,
      name: 'Corrompido',
      status: 'disabled',
      audit: { ...existing.audit, hash: '0'.repeat(64) }
    });
    expect(registry.getAsset(targetId)?.name).toBe('Corrompido');

    const restoredSingle = await service.restoreBuiltinAsset(targetId);
    expect(restoredSingle).not.toBeNull();
    expect(restoredSingle?.name).toBe('ce-resolve-pr-feedback');
    expect(restoredSingle?.status).toBe('available');
    expect(restoredSingle?.audit.hash).toBe(restoredSingle?.origin?.installHash);

    // Remove um embutido e adiciona um asset customizado indevido dentro de library-arestrix
    expect(registry.removeAsset(targetId)).toBe(true);
    expect(registry.getAsset(targetId)).toBeUndefined();

    const customInBuiltin = createSkillAsset({
      id: 'asset-library-arestrix-custom-intruder',
      libraryId: 'library-arestrix',
      name: 'Custom Intruder',
      description: 'Should be removed by resetBuiltinLibrary',
      category: 'Personalizada',
      version: '1.0.0',
      license: 'MIT',
      payload: { kind: 'skill', markdownContent: '# Custom', systemPrompt: 'Custom', tools: ['read'] },
      audit: {
        verdict: 'APPROVE',
        riskScore: 0,
        policyVersion: 'test',
        hash: 'b'.repeat(64),
        findings: [],
        warnings: []
      },
      provenance: { sourceKind: 'workspace', sourceId: 'user', importedAt: new Date().toISOString() }
    });
    registry.upsertAsset(customInBuiltin);

    const resetResult = await service.resetBuiltinLibrary();
    expect(resetResult.removedCount).toBe(1);
    expect(resetResult.restoredCount).toBe(328 + BUNDLED_MCP_SERVERS.length);
    expect(registry.getAsset('asset-library-arestrix-custom-intruder')).toBeUndefined();
    expect(registry.getAsset(targetId)?.status).toBe('available');
    expect(registry.listAssets('library-arestrix')).toHaveLength(328 + BUNDLED_MCP_SERVERS.length);
  });

  it('instancia vértice embutido com origin e librarySource e restaura nó modificado/derivado via buildInstallDefaultNodePatch', async () => {
    const templateRegistry = new AgentTemplateRegistry();
    const node = templateRegistry.instantiateNode('resolve-pr-feedback', 'Custom PR Bot', 120, 240);

    expect(node.type).toBe('agent');
    expect(node.templateId).toBe('resolve-pr-feedback');
    expect(node.presetId).toBe('resolve-pr-feedback');
    expect(node.presetLocked).toBe(true);
    expect(node.origin?.bundledInProduct).toBe(true);
    expect(node.origin?.templateId).toBe('resolve-pr-feedback');
    expect(node.librarySource?.libraryId).toBe('library-arestrix');
    expect(node.librarySource?.assetId).toBe('asset-library-arestrix-agent-resolve-pr-feedback');
    expect(node.librarySource?.bundledInProduct).toBe(true);
    expect(service.getAsset(node.librarySource!.assetId)).toBeDefined();

    // Também garante instância direta pelo assetId do catálogo e ausência de bloat no storage de plugins customizados
    const secondaryNode = templateRegistry.instantiateNode('asset-library-arestrix-agent-product-manager-257a77');
    expect(secondaryNode.origin?.assetId).toBe('alirezarezvani__claude-skills__product-manager');
    expect(storageData.get('arestrix_custom_agent_plugins')).toBeUndefined();

    // Cria derivado na biblioteca do usuário e modifica o nó no grafo
    const derived = await service.deriveAsset(node.librarySource!.assetId, {
      name: 'PR Bot Derivado'
    });
    expect(derived).not.toBeNull();

    const modifiedNode = {
      ...node,
      label: 'Nó Derivado Alterado',
      presetLocked: false,
      inputs: [],
      outputs: [],
      parameters: { customFlag: true },
      librarySource: {
        libraryId: derived!.libraryId,
        assetId: derived!.id,
        hash: derived!.audit.hash,
        version: derived!.version
      }
    };
    modifiedNode.agentSettings = {
      ...modifiedNode.agentSettings!,
      model: {
        ...modifiedNode.agentSettings!.model!,
        reasoningEffort: 'low',
        temperature: 0.9
      },
      skills: []
    };

    const patch = templateRegistry.buildInstallDefaultNodePatch(modifiedNode, service);
    expect(patch).not.toBeNull();
    expect(patch?.label).toBe('ce-resolve-pr-feedback');
    expect(patch?.presetLocked).toBe(true);
    expect(patch?.presetId).toBe('resolve-pr-feedback');
    expect(patch?.templateId).toBe('resolve-pr-feedback');
    expect(patch?.inputs).toHaveLength(1);
    expect(patch?.inputs?.[0].id).toBe(`${modifiedNode.id}_in_0`);
    expect(patch?.outputs).toHaveLength(1);
    expect(patch?.outputs?.[0].id).toBe(`${modifiedNode.id}_out_0`);
    expect(patch?.parameters).toEqual({});
    expect(patch?.agentSettings?.model?.reasoningEffort).toBe('high');
    expect(patch?.agentSettings?.skills?.length).toBeGreaterThan(0);
    expect(patch?.origin?.assetId).toBe('everyinc__compound-engineering-plugin__ce-resolve-pr-feedback');
    expect(patch?.origin?.bundledInProduct).toBe(true);
    expect(patch?.librarySource?.assetId).toBe('asset-library-arestrix-agent-resolve-pr-feedback');
    expect(patch?.librarySource?.libraryId).toBe('library-arestrix');

    const factoryTpl = templateRegistry.getInstallDefaultTemplate('resolve-pr-feedback');
    expect(factoryTpl?.bundledInProduct).toBe(true);
    expect(factoryTpl?.domain).toBe('Engenharia de Software');
    expect(factoryTpl?.subdomain).toBe('Code Review, Debug & Git');
  });
});
