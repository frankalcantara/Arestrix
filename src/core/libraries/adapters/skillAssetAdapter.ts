import type { IGitHubSkillItem, INodeSkill } from '../../types.ts';
import type { IWorkspaceApprovedSkill } from '../../workspace/types.ts';
import type { ILibraryAsset, ILibraryAssetAudit, ILibraryAssetProvenance, LibraryAssetStatus } from '../types.ts';
import { assetPayloadToNodeSkill, nodeSkillToPayload } from '../types.ts';

export interface ISkillAssetInput {
  id: string;
  libraryId: string;
  name: string;
  description: string;
  category: string;
  version: string;
  license: string;
  status?: LibraryAssetStatus;
  payload: ReturnType<typeof nodeSkillToPayload>;
  audit: ILibraryAssetAudit;
  provenance: ILibraryAssetProvenance;
  derivedFrom?: ILibraryAsset['derivedFrom'];
}

export function skillAssetId(libraryId: string, skillId: string): string {
  return `asset-${libraryId}-skill-${skillId}`;
}

export function createSkillAsset(input: ISkillAssetInput): ILibraryAsset {
  const now = new Date().toISOString();
  return {
    id: input.id,
    libraryId: input.libraryId,
    kind: 'skill',
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

export function builtinSkillToAsset(
  item: IGitHubSkillItem,
  libraryId: string,
  audit: ILibraryAssetAudit
): ILibraryAsset {
  return createSkillAsset({
    id: skillAssetId(libraryId, item.id),
    libraryId,
    name: item.name,
    description: item.description,
    category: item.category,
    version: '1.0.0',
    license: item.license,
    payload: nodeSkillToPayload({
      markdownContent: item.markdownContent,
      systemPrompt: item.systemPrompt,
      tools: item.tools
    }),
    audit,
    provenance: {
      sourceKind: 'builtin',
      sourceId: libraryId,
      authority: 'Arestrix',
      importedAt: new Date().toISOString()
    }
  });
}

export function workspaceSkillToAsset(
  item: IWorkspaceApprovedSkill,
  libraryId: string,
  audit: ILibraryAssetAudit
): ILibraryAsset {
  return createSkillAsset({
    id: skillAssetId(libraryId, item.id),
    libraryId,
    name: item.name,
    description: item.description,
    category: item.category,
    version: item.version,
    license: item.license,
    payload: nodeSkillToPayload({
      markdownContent: item.markdownContent,
      systemPrompt: item.systemPrompt,
      tools: item.tools
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

export function skillAssetToNodeSkill(asset: ILibraryAsset): INodeSkill {
  return assetPayloadToNodeSkill(asset);
}
