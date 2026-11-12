import type { ISerializedTemplatePlugin } from '../../templates/AgentTemplateTypes.ts';
import type {
  ILibraryAsset,
  ILibraryAssetAudit,
  ILibraryAssetProvenance,
  IPipelineAssetPayload,
  LibraryAssetStatus
} from '../types.ts';

export interface IPipelineAssetInput {
  id: string;
  libraryId: string;
  name: string;
  description: string;
  category: string;
  version: string;
  license: string;
  status?: LibraryAssetStatus;
  payload: IPipelineAssetPayload;
  audit: ILibraryAssetAudit;
  provenance: ILibraryAssetProvenance;
  derivedFrom?: ILibraryAsset['derivedFrom'];
}

export function pipelineAssetId(libraryId: string, templateId: string): string {
  return `asset-${libraryId}-pipeline-${templateId}`;
}

export function serializedPipelineToAssetPayload(template: ISerializedTemplatePlugin): IPipelineAssetPayload {
  return {
    kind: 'pipeline',
    template: JSON.parse(JSON.stringify(template))
  };
}

export function createPipelineAsset(input: IPipelineAssetInput): ILibraryAsset {
  const now = new Date().toISOString();
  return {
    id: input.id,
    libraryId: input.libraryId,
    kind: 'pipeline',
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

export function assetToPipelineTemplate(asset: ILibraryAsset): ISerializedTemplatePlugin | null {
  if (asset.payload.kind !== 'pipeline') return null;
  return asset.payload.template;
}
