import { describe, it, expect, beforeEach, vi } from 'vitest';
import { EventBus } from '../src/core/EventBus.ts';
import { LibraryRegistry } from '../src/core/libraries/LibraryRegistry.ts';
import { LibraryService } from '../src/core/libraries/LibraryService.ts';
import { LibraryAuditGate } from '../src/core/libraries/LibraryAuditGate.ts';
import { WorkspaceManager } from '../src/core/workspace/WorkspaceManager.ts';
import { SkillLedgerService } from '../src/core/skills/SkillLedgerService.ts';
import { McpLedgerService } from '../src/core/skills/McpLedgerService.ts';

const fakeStore = { load: async () => null, save: async () => true } as any;
const commit = 'a'.repeat(40);

const safeSkill = `---
name: "Skill Remota"
description: "Skill de teste"
license: "MIT"
tools:
  - read
  - write
---

# Skill Remota

Escreva com clareza e cite as fontes.`;

const cautionSkill = `---
name: "Skill com Shell"
description: "Skill de teste"
license: "MIT"
tools:
  - read
  - bash
---

# Skill com Shell

Use o terminal com cuidado.`;

describe('GitHubSourceAdapter + LibraryService - Fase 5', () => {
  let registry: LibraryRegistry;
  let service: LibraryService;
  let adapter: any;
  let sourceId: string;
  let libraryId: string;

  beforeEach(async () => {
    const bus = new EventBus();
    registry = LibraryRegistry.getInstance(bus);
    registry.resetForTesting(bus);
    const workspace = WorkspaceManager.getInstance(bus);
    workspace.resetForTesting(bus);
    SkillLedgerService.resetForTesting(bus);
    McpLedgerService.resetForTesting(bus);

    adapter = {
      fetchManifest: vi.fn(async () => ({
        ok: true,
        repo: 'acme/biblioteca',
        ref: 'main',
        commit,
        license: 'MIT',
        defaultBranch: 'main',
        private: false,
        archived: false,
        truncated: false,
        assets: [
          { path: 'skills/remota/SKILL.md', sha: 'b'.repeat(40), size: 120, kind: 'skill', name: 'remota' },
          { path: 'mcps/dados.json', sha: 'c'.repeat(40), size: 80, kind: 'mcp', name: 'dados' }
        ]
      })),
      fetchAsset: vi.fn(async () => ({
        ok: true,
        repo: 'acme/biblioteca',
        ref: 'main',
        commit,
        path: 'skills/remota/SKILL.md',
        kind: 'skill',
        content: safeSkill,
        size: safeSkill.length
      }))
    };

    service = new LibraryService(
      registry,
      workspace,
      LibraryAuditGate.getInstance(),
      fakeStore,
      adapter
    );

    const created = await service.createGitHubSource({
      repo: 'acme/biblioteca',
      ref: 'main',
      name: 'Biblioteca GitHub de Teste',
      scope: 'workspace'
    });
    expect(created.ok).toBe(true);
    sourceId = created.source!.id;
    libraryId = created.library!.id;
  });

  it('sincroniza apenas o manifesto e deixa os assets remotos como PENDING', async () => {
    const result = await service.syncGitHubSource({ sourceId });

    expect(result.ok).toBe(true);
    expect(result.added).toBe(2);
    expect(result.commit).toBe(commit);
    expect(adapter.fetchAsset).not.toHaveBeenCalled();

    const assets = registry.listAssets(libraryId);
    expect(assets).toHaveLength(2);
    expect(assets.every(asset => asset.audit.verdict === 'PENDING')).toBe(true);
    expect(assets.every(asset => Boolean(asset.remote))).toBe(true);
    expect(assets.find(asset => asset.kind === 'skill')?.payload.kind).toBe('skill');
    expect(assets.find(asset => asset.kind === 'skill')?.remote?.path).toBe('skills/remota/SKILL.md');
  });

  it('baixa o asset sob demanda, audita e promove o conteúdo', async () => {
    await service.syncGitHubSource({ sourceId });
    const asset = registry.listAssets(libraryId).find(item => item.kind === 'skill')!;

    const result = await service.downloadGitHubAsset(asset.id);

    expect(result.ok).toBe(true);
    expect(adapter.fetchAsset).toHaveBeenCalledTimes(1);
    expect(result.asset?.payload.kind).toBe('skill');
    if (result.asset?.payload.kind === 'skill') {
      expect(result.asset.payload.markdownContent).toContain('Skill Remota');
      expect(result.asset.payload.tools).toContain('read');
    }
    expect(result.asset?.audit.verdict).not.toBe('PENDING');
    expect(result.asset?.audit.hash).toMatch(/^[a-f0-9]{64}$/);
  });

  it('bloqueia licença fora da allowlist antes de qualquer download', async () => {
    adapter.fetchManifest = vi.fn(async () => ({
      ok: true,
      repo: 'acme/biblioteca',
      ref: 'main',
      commit,
      license: 'GPL-3.0',
      assets: []
    }));

    const result = await service.syncGitHubSource({ sourceId });
    expect(result.ok).toBe(false);
    expect(result.errorCode).toBe('github_license_not_allowed');
    expect(adapter.fetchAsset).not.toHaveBeenCalled();
  });

  it('pede confirmação para CAUTION e só baixa com allowCaution', async () => {
    adapter.fetchAsset = vi.fn(async () => ({
      ok: true,
      repo: 'acme/biblioteca',
      ref: 'main',
      commit,
      path: 'skills/remota/SKILL.md',
      kind: 'skill',
      content: cautionSkill,
      size: cautionSkill.length
    }));

    await service.syncGitHubSource({ sourceId });
    const asset = registry.listAssets(libraryId).find(item => item.kind === 'skill')!;

    const first = await service.downloadGitHubAsset(asset.id);
    expect(first.ok).toBe(false);
    expect(first.needsConfirmation).toBe(true);

    const second = await service.downloadGitHubAsset(asset.id, { allowCaution: true });
    expect(second.ok).toBe(true);
    expect(second.asset?.audit.verdict).toBe('CAUTION');
  });
});
