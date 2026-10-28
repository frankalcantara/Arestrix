import type { IMcpServerConfig, IMcpToolDefinition, INodeSkill } from '../types.ts';
import type { ISerializedTemplatePlugin } from '../templates/AgentTemplateTypes.ts';

/**
 * Arestrix Studio - Modelo de Bibliotecas de Assets
 *
 * Contrato compartilhado entre o núcleo, as telas de Propriedades e o seletor
 * usado por agentes/grafos. Um asset de biblioteca é imutável; edições geram
 * um derivado com `derivedFrom`.
 */

export type LibraryScope = 'studio' | 'workspace' | 'project';
export type LibraryKind = 'builtin' | 'local' | 'repository';
export type LibrarySourceKind = 'builtin' | 'github' | 'local-folder' | 'workspace';
export type LibraryAssetKind = 'skill' | 'mcp' | 'agent-template' | 'graph-template';
export type LibraryTrust = 'official' | 'verified' | 'community' | 'local';
export type LibraryAssetStatus = 'available' | 'installed' | 'pinned' | 'disabled';
export type LibraryAuditVerdict = 'APPROVE' | 'CAUTION' | 'REJECT' | 'PENDING';

export interface ILibrarySource {
  id: string;
  kind: LibrarySourceKind;
  scope: LibraryScope;
  name: string;
  enabled: boolean;
  trust: LibraryTrust;
  github?: {
    repo: string;
    ref: string;
    commit?: string;
    subPath?: string;
    licenseSpdx?: string;
    private?: boolean;
  };
  licenseAllowlist: string[];
  lastSyncAt?: string;
  lastSyncError?: string;
}

export interface ILibraryEntry {
  id: string;
  name: string;
  description: string;
  kind: LibraryKind;
  scope: LibraryScope;
  /** Fonte de repositório associada, quando kind === 'repository'. */
  sourceId?: string;
  trust?: LibraryTrust;
  enabled?: boolean;
  createdAt: string;
  isBuiltin: boolean;
}

export interface ILibraryAssetProvenance {
  sourceKind: LibrarySourceKind;
  sourceId: string;
  repo?: string;
  path?: string;
  commit?: string;
  url?: string;
  authority?: string;
  importedAt: string;
}

export interface ILibraryAssetAudit {
  verdict: LibraryAuditVerdict;
  riskScore: number;
  policyVersion: string;
  auditedAt?: string;
  hash: string;
  findings: string[];
  warnings: string[];
}

export interface ILibraryDerivedFrom {
  libraryId: string;
  assetId: string;
  hash: string;
  policyVersion: string;
  originName: string;
}

export interface ISkillAssetPayload {
  kind: 'skill';
  markdownContent: string;
  systemPrompt: string;
  tools: string[];
}

export interface IMcpAssetPayload {
  kind: 'mcp';
  transport: 'stdio' | 'sse';
  command?: string;
  args?: string[];
  env?: Record<string, string>;
  url?: string;
  headers?: Record<string, string>;
  cachedTools?: IMcpToolDefinition[];
}

export interface IAgentTemplatePayload {
  kind: 'agent-template';
  template: ISerializedTemplatePlugin;
}

export type ILibraryAssetPayload = ISkillAssetPayload | IMcpAssetPayload | IAgentTemplatePayload;

export interface ILibraryAsset {
  id: string;
  libraryId: string;
  kind: LibraryAssetKind;
  name: string;
  description: string;
  category: string;
  version: string;
  license: string;
  status: LibraryAssetStatus;
  provenance: ILibraryAssetProvenance;
  audit: ILibraryAssetAudit;
  payload: ILibraryAssetPayload;
  /** Metadados do blob remoto; o conteúdo só é baixado sob demanda. */
  remote?: {
    path: string;
    blobSha: string;
    size: number;
  };
  derivedFrom?: ILibraryDerivedFrom;
  createdAt: string;
  updatedAt: string;
}

/** Candidato remoto: apenas metadados; o conteúdo só é baixado sob demanda. */
export interface ILibraryAssetCandidate {
  id: string;
  libraryId: string;
  kind: LibraryAssetKind;
  name: string;
  description: string;
  path: string;
  blobSha: string;
  size: number;
  license: string;
  commit: string;
  repo: string;
}

export interface ILibraryState {
  version: 2;
  libraries: ILibraryEntry[];
  assets: ILibraryAsset[];
  sources?: ILibrarySource[];
  updatedAt: string;
}

export interface ILibraryImportResult {
  imported: ILibraryAsset[];
  rejected: Array<{ id: string; name: string; verdict: LibraryAuditVerdict; reasons: string[] }>;
}

export interface ILibraryCloneResult extends ILibraryImportResult {
  library: ILibraryEntry;
}

export interface ILibrarySyncResult extends ILibraryImportResult {
  ok: boolean;
  error?: string;
  errorCode?: string;
  commit: string;
  license: string;
  added: number;
  updated: number;
  removed: number;
  truncated: boolean;
}

export interface ILibraryDownloadResult {
  ok: boolean;
  asset?: ILibraryAsset;
  reason?: string;
  errorCode?: string;
  needsConfirmation?: boolean;
  audit?: ILibraryAssetAudit;
}

export interface ILibrarySnapshot {
  format: 'arestrix-library';
  version: 1;
  exportedAt: string;
  library: ILibraryEntry;
  assets: ILibraryAsset[];
}

export interface ILibraryInstallResult {
  ok: boolean;
  reason?: string;
  path?: string;
}

export const MCP_AUDIT_POLICY_VERSION = 'mcp-scanner-v1';

export function nodeSkillToPayload(skill: Partial<INodeSkill>): ISkillAssetPayload {
  return {
    kind: 'skill',
    markdownContent: skill.markdownContent || '',
    systemPrompt: skill.systemPrompt || '',
    tools: [...(skill.tools || [])]
  };
}

export function assetPayloadToNodeSkill(
  asset: Pick<ILibraryAsset, 'id' | 'libraryId' | 'name' | 'description' | 'license' | 'payload' | 'audit' | 'provenance'>
): INodeSkill {
  const payload = asset.payload.kind === 'skill' ? asset.payload : { markdownContent: '', systemPrompt: '', tools: [] };
  return {
    id: asset.id,
    name: asset.name,
    description: asset.description,
    markdownContent: payload.markdownContent,
    systemPrompt: payload.systemPrompt,
    tools: [...payload.tools],
    license: asset.license,
    sourceUrl: asset.provenance.url,
    updatedAt: new Date().toISOString(),
    // O snapshot aponta para o asset de biblioteca que ele representa. A cadeia
    // de derivação original continua no próprio asset (`asset.derivedFrom`).
    derivedFrom: {
      libraryId: asset.libraryId,
      assetId: asset.id,
      hash: asset.audit.hash,
      policyVersion: asset.audit.policyVersion,
      originName: asset.name
    },
    audit: {
      verdict: asset.audit.verdict,
      riskScore: asset.audit.riskScore,
      hash: asset.audit.hash,
      policyVersion: asset.audit.policyVersion,
      auditedAt: asset.audit.auditedAt
    }
  };
}

export function mcpConfigToAssetPayload(server: Partial<IMcpServerConfig>): IMcpAssetPayload {
  return {
    kind: 'mcp',
    transport: (server.transport === 'sse' ? 'sse' : 'stdio'),
    command: server.command || '',
    args: [...(server.args || [])],
    env: server.env ? { ...server.env } : undefined,
    url: server.url || '',
    headers: server.headers ? { ...server.headers } : undefined,
    cachedTools: server.cachedTools ? [...server.cachedTools] : undefined
  };
}

export function assetPayloadToMcpConfig(
  asset: Pick<ILibraryAsset, 'id' | 'name' | 'description' | 'payload'>
): IMcpServerConfig {
  const payload: IMcpAssetPayload = asset.payload.kind === 'mcp'
    ? asset.payload
    : { kind: 'mcp', transport: 'stdio', command: '', args: [] };
  return {
    id: asset.id,
    name: asset.name,
    transport: payload.transport,
    command: payload.command,
    args: [...(payload.args || [])],
    env: payload.env ? { ...payload.env } : undefined,
    url: payload.url,
    headers: payload.headers ? { ...payload.headers } : undefined,
    isEnabled: true,
    cachedTools: payload.cachedTools ? [...payload.cachedTools] : []
  };
}
