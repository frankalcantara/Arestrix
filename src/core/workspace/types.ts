/**
 * Arestrix Studio - Tipos de Governança e Configuração do Workspace
 */

export interface IWorkspaceConfig {
  workspaceRoot: string;
  skillsDir: string;
  mcpsDir: string;
  agentsDir: string;
  graphsDir: string;
  autoVerifySkills: boolean;
  strictSecurityMode: boolean;
  installedAssetsVersion: string;
  lastSyncTimestamp?: string;
}

export interface IWorkspaceApprovedSkill {
  id: string;
  name: string;
  category: string;
  description: string;
  license: string;
  tools: string[];
  systemPrompt: string;
  markdownContent: string;
  version: string;
  provenance: 'workspace-curated' | 'community' | 'custom';
  isPreApproved: boolean;
}

export interface IWorkspaceApprovedMcp {
  id: string;
  name: string;
  description: string;
  category: string;
  transport: 'stdio' | 'sse';
  command: string;
  args: string[];
  isPreApproved: boolean;
  tools: Array<{
    name: string;
    description: string;
    riskLevel: 'safe' | 'moderate' | 'high';
  }>;
}

export interface IWorkspaceSkillAuditVerdict {
  skillId: string;
  skillName: string;
  verified: boolean;
  status: 'approved' | 'warning' | 'rejected';
  riskScore: number; // 0 (100% seguro) a 100 (risco crítico)
  auditedBy: string;
  timestamp: string;
  checklist: Array<{
    rule: string;
    passed: boolean;
    detail: string;
  }>;
  findings: string[];
  warnings: string[];
  allowedTools: string[];
  authorizedTools: string[];
  hasTypographyShadowViolation: boolean;
  passedSvgZeroShadowRule: boolean;
  hash?: string;
  verdict?: 'APPROVE' | 'CAUTION' | 'REJECT';
}

export interface IWorkspaceAgent {
  id: string;
  name: string;
  role: string;
  description: string;
  category: 'governance' | 'security' | 'linter' | 'automation';
  version: string;
  systemPrompt: string;
  enabled: boolean;
}

export interface IWorkspaceExecutionResult {
  executionId: string;
  targetType: 'agent' | 'graph' | 'project';
  targetId: string;
  status: 'success' | 'warning' | 'error';
  summary: string;
  timestamp: string;
  details?: Record<string, any>;
}
