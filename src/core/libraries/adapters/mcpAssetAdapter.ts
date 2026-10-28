import type { IMcpServerConfig } from '../../types.ts';
import type { IWorkspaceApprovedMcp } from '../../workspace/types.ts';
import type { ILibraryAsset, ILibraryAssetAudit, ILibraryAssetProvenance, LibraryAssetStatus } from '../types.ts';
import { assetPayloadToMcpConfig, mcpConfigToAssetPayload } from '../types.ts';

export interface IMcpAssetInput {
  id: string;
  libraryId: string;
  name: string;
  description: string;
  category: string;
  version: string;
  license: string;
  status?: LibraryAssetStatus;
  payload: ReturnType<typeof mcpConfigToAssetPayload>;
  audit: ILibraryAssetAudit;
  provenance: ILibraryAssetProvenance;
  derivedFrom?: ILibraryAsset['derivedFrom'];
}

export function mcpAssetId(libraryId: string, serverId: string): string {
  return `asset-${libraryId}-mcp-${serverId}`;
}

export function createMcpAsset(input: IMcpAssetInput): ILibraryAsset {
  const now = new Date().toISOString();
  return {
    id: input.id,
    libraryId: input.libraryId,
    kind: 'mcp',
    name: input.name,
    description: input.description,
    category: input.category,
    version: input.version,
    license: input.license,
    status: input.status || 'available',
    provenance: input.provenance,
    audit: input.audit,
    payload: input.payload,
    derivedFrom: input.derivedFrom,
    createdAt: now,
    updatedAt: now
  };
}

export function workspaceMcpToAsset(
  item: IWorkspaceApprovedMcp,
  libraryId: string,
  audit: ILibraryAssetAudit
): ILibraryAsset {
  return createMcpAsset({
    id: mcpAssetId(libraryId, item.id),
    libraryId,
    name: item.name,
    description: item.description,
    category: item.category,
    version: '1.0.0',
    license: 'MIT',
    payload: mcpConfigToAssetPayload({
      id: item.id,
      name: item.name,
      transport: item.transport,
      command: item.command,
      args: item.args,
      isEnabled: item.isPreApproved,
      cachedTools: item.tools.map(tool => ({
        name: tool.name,
        description: tool.description,
        serverId: item.id
      }))
    }),
    audit,
    provenance: {
      sourceKind: 'workspace',
      sourceId: libraryId,
      authority: 'Arestrix Workspace',
      importedAt: new Date().toISOString()
    }
  });
}

export function mcpAssetToServerConfig(asset: ILibraryAsset): IMcpServerConfig {
  return assetPayloadToMcpConfig(asset);
}
