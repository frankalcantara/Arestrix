import type {
  ICodeModeConfig,
  IModelConfig,
  INodeAgentSettings,
  INodeMemoryConfig,
  IProjectManifest,
  ISandboxPolicy
} from '../types.ts';

export type SettingsScope = 'studio' | 'workspace' | 'project' | 'vertex';
export type SettingsOrigin = SettingsScope | 'default';

export interface IPipelineDefaultsPatch {
  model?: Partial<IModelConfig>;
  sandbox?: Partial<ISandboxPolicy>;
  codeMode?: Partial<ICodeModeConfig>;
  memory?: Partial<INodeMemoryConfig>;
}

export interface IStudioSettings {
  version: number;
  language: 'pt-BR' | 'en-US';
  appearance: {
    theme: 'light' | 'dark' | 'system';
    canvasBackground: 'light' | 'dark';
  };
  defaults: IPipelineDefaultsPatch;
}

export interface IWorkspaceSettings {
  version: number;
  rootPath: string;
  directories: {
    root: string;
    skills: string;
    mcps: string;
    agents: string;
    graphs: string;
  };
  defaults: IPipelineDefaultsPatch;
  security: {
    autoVerifySkills: boolean;
    strictSecurityMode: boolean;
  };
  lastSyncTimestamp?: string;
}

export interface IStudioModelSettings {
  provider: string;
  model: string;
  baseUrl: string;
}

export interface IStudioProviderCredential {
  id: string;
  label: string;
  keyName: string;
  configured: boolean;
}

export interface IStudioGatewaySettings {
  url: string;
  keyConfigured: boolean;
  requireGateway: boolean;
  locked: boolean;
}

export interface IStudioCredentialsSnapshot {
  providers: IStudioProviderCredential[];
  gateway: IStudioGatewaySettings;
}

export interface IProjectSettings {
  version: number;
  manifest: IProjectManifest;
  /** Somente overrides explícitos do projeto; vazio usa Studio/Workspace/default. */
  defaults: IPipelineDefaultsPatch;
  hasExplicitDefaults: boolean;
}

export interface IResolvedSetting<T> {
  value: T;
  origin: SettingsOrigin;
  source: string;
  inheritedFrom?: SettingsScope;
  readOnly?: boolean;
}

export interface IResolvedSettings {
  model: IResolvedSetting<IModelConfig>;
  sandbox: IResolvedSetting<ISandboxPolicy>;
  codeMode: IResolvedSetting<ICodeModeConfig>;
  memory: IResolvedSetting<INodeMemoryConfig>;
}

export interface ISettingsContext {
  studio: IStudioSettings;
  workspace: IWorkspaceSettings;
  project: IProjectSettings;
  vertex?: INodeAgentSettings | null;
}
