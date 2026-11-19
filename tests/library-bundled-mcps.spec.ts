import { describe, expect, it } from 'vitest';
import { BUNDLED_MCP_SERVERS } from '../src/core/libraries/BuiltinLibraryCatalog.ts';
import { LibraryRegistry } from '../src/core/libraries/LibraryRegistry.ts';
import { McpLedgerService } from '../src/core/skills/McpLedgerService.ts';
import { buildEditableNodeFromAsset } from '../src/ui/libraries/LibraryTree.ts';

const ENV_REF_RE = /^\$\{[A-Za-z_][A-Za-z0-9_]*\}$/;

describe('Bundled MCP Servers — C++20/23 Compatibility & Interaction Vertices', () => {
  it('hidrata todos os MCPs embutidos em library-arestrix com compatibilidade C++ e zero segredos literais', () => {
    expect(BUNDLED_MCP_SERVERS.length).toBeGreaterThanOrEqual(500);

    const registry = new LibraryRegistry();
    const mcpAssets = registry.listAssets('library-arestrix').filter(a => a.kind === 'mcp');
    expect(mcpAssets).toHaveLength(BUNDLED_MCP_SERVERS.length);

    for (const asset of mcpAssets) {
      expect(asset.id.length).toBeLessThanOrEqual(64);
      expect(asset.id).not.toContain(':');
      expect(asset.bundledInProduct).toBe(true);
      expect(asset.domain).toBeTruthy();
      expect(asset.subdomain).toBeTruthy();
      expect(asset.audit.verdict).not.toBe('REJECT');
      expect(asset.audit.hash).toMatch(/^[a-f0-9]{64}$/);
      expect(asset.audit.hash).toBe(asset.origin?.installHash);

      expect(asset.payload.kind).toBe('mcp');
      if (asset.payload.kind === 'mcp') {
        expect(['stdio', 'sse']).toContain(asset.payload.transport);
        if (asset.payload.transport === 'sse') {
          expect(asset.payload.url?.startsWith('https://')).toBe(true);
        } else {
          expect(asset.payload.command).toBeTruthy();
        }
        for (const val of Object.values(asset.payload.env || {})) {
          if (val) expect(val).toMatch(ENV_REF_RE);
        }
        for (const val of Object.values(asset.payload.headers || {})) {
          if (val) expect(val).toMatch(ENV_REF_RE);
        }
      }
    }
  });

  it('pré-homologa os MCPs embutidos no McpLedgerService e cria vértices de interação type=mcp', () => {
    const ledger = McpLedgerService.resetForTesting();
    const registry = new LibraryRegistry();
    const mcpAssets = registry.listAssets('library-arestrix').filter(a => a.kind === 'mcp');

    const sample = mcpAssets.find(a => a.payload.kind === 'mcp' && (a.payload.cachedTools?.length || 0) > 0)!;
    expect(sample).toBeTruthy();

    const status = ledger.getStatusByServerId(sample.id);
    expect(status).not.toBe('PENDING');
    expect(status).not.toBe('REJECT');

    const node = buildEditableNodeFromAsset(sample, { getAllNodes: () => [] }, sample.name);
    expect(node).not.toBeNull();
    expect(node?.type).toBe('mcp');
    expect(node?.mcpSettings?.server).toBe(sample.id);
    expect(node?.mcpSettings?.tool).toBeTruthy();
    expect(node?.inputs).toHaveLength(1);
    expect(node?.outputs).toHaveLength(1);
    expect(node?.origin?.installHash).toBe(sample.audit.hash);
    expect(node?.librarySource?.assetId).toBe(sample.id);
  });
});
