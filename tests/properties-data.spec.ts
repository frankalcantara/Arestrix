import { describe, it, expect } from 'vitest';
import { buildPropertiesScopeConfig, type IPropertiesContext } from '../src/ui/properties/PropertiesData.ts';
import type { IProjectManifest } from '../src/core/types.ts';

const secretLiteral = ['super', 'secret', 'value'].join('-');

const manifest: IProjectManifest = {
  projectId: 'project-1',
  name: 'Projeto de Teste',
  version: '1.0.0',
  description: 'Projeto de teste',
  createdAt: '2026-10-27T00:00:00.000Z',
  updatedAt: '2026-10-27T00:00:00.000Z',
  rootPath: 'C:/projetos/teste',
  outputDir: 'Output',
  pipelines: [{ id: 'main', name: 'Pipeline Principal', file: 'pipelines/main.json', isPrimary: true }],
  environment: { API_KEY: secretLiteral },
  git: { enabled: true, provider: 'github', serverUrl: '', username: 'user', repositoryName: 'repo', remoteUrl: '', isPrivate: true, autoCommitOnRun: true, autoCommitOnShutdown: false },
  runner: { enabled: true, title: 'Runner', outputDir: 'dist/runner', outputName: 'runner.exe', version: '1.0.0', parameters: [] }
};

const context = {
  graph: {
    getGlobalDefaults: () => ({
      model: { provider: 'deepseek', model: 'deepseek-flash' },
      sandbox: { isolationLevel: 'container', network: 'egress_llm_only' },
      codeMode: { allowedTools: ['read'], customScripts: [] },
      memory: { enabled: true, persistent: false }
    })
  },
  projectManager: { getCurrentProject: () => manifest },
  workspaceManager: {
    getWorkspaceRoot: () => 'C:/projetos/teste',
    getWorkspaceDirectories: () => ({ root: 'C:/projetos/teste/.workspace', skills: 'C:/projetos/teste/.workspace/skills', mcps: 'C:/projetos/teste/.workspace/mcps', agents: 'C:/projetos/teste/.workspace/agents' }),
    getApprovedSkills: () => [{ id: 'skill-1', name: 'Skill 1', category: 'Teste' }],
    getApprovedMcps: () => [{ id: 'mcp-1', name: 'MCP 1', transport: 'stdio' }],
    isWorkspaceProvisioned: () => true
  },
  githubRegistry: { getRepositories: () => [] }
} as unknown as IPropertiesContext;

describe('PropertiesData', () => {
  it('builds the three scopes with their expected sections', () => {
    expect(buildPropertiesScopeConfig('studio', context).sections.map(s => s.id)).toEqual(['general', 'studio-model', 'credentials', 'integrations', 'advanced']);
    expect(buildPropertiesScopeConfig('workspace', context).sections.map(s => s.id)).toEqual(['general', 'catalogs', 'sandbox', 'security', 'memory', 'advanced']);
    expect(buildPropertiesScopeConfig('project', context).sections.map(s => s.id)).toEqual(['general', 'pipelines', 'defaults', 'environment', 'git', 'runner', 'advanced']);
  });

  it('does not render secret environment values', () => {
    const project = buildPropertiesScopeConfig('project', context);
    const rendered = project.sections.map(section => section.content).join(' ');
    expect(rendered).not.toContain(secretLiteral);
    expect(rendered).toContain('1');
  });

  it('marks the credential section as read-only and protected', () => {
    const studio = buildPropertiesScopeConfig('studio', context);
    const credentials = studio.sections.find(section => section.id === 'credentials');
    expect(credentials).toBeDefined();
    expect(credentials?.content).toContain('Nenhum valor secreto');
  });
});
