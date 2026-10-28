import type { ISerializedTemplatePlugin } from '../../templates/AgentTemplateTypes.ts';
import type {
  IAgentTemplatePayload,
  ILibraryAsset,
  ILibraryAssetAudit,
  ILibraryAssetProvenance,
  LibraryAssetStatus
} from '../types.ts';

export interface ITemplateAssetInput {
  id: string;
  libraryId: string;
  name: string;
  description: string;
  category: string;
  version: string;
  license: string;
  status?: LibraryAssetStatus;
  payload: IAgentTemplatePayload;
  audit: ILibraryAssetAudit;
  provenance: ILibraryAssetProvenance;
  derivedFrom?: ILibraryAsset['derivedFrom'];
}

export function templateAssetId(libraryId: string, templateId: string): string {
  return `asset-${libraryId}-template-${templateId}`;
}

export function serializedTemplateToAssetPayload(template: ISerializedTemplatePlugin): IAgentTemplatePayload {
  return {
    kind: 'agent-template',
    template: JSON.parse(JSON.stringify(template))
  };
}

export function createTemplateAsset(input: ITemplateAssetInput): ILibraryAsset {
  const now = new Date().toISOString();
  return {
    id: input.id,
    libraryId: input.libraryId,
    kind: 'agent-template',
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

export function assetToSerializedTemplate(asset: ILibraryAsset): ISerializedTemplatePlugin | null {
  if (asset.payload.kind !== 'agent-template') return null;
  return asset.payload.template;
}
