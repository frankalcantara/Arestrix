import { sha256 } from '../hash.ts';
import policyData from './skill-validation-policy.json';

export interface ISkillValidationReference {
  id: string;
  role: 'static-evidence' | 'boundary-methodology';
  repo: string;
  path: string;
  commit: string;
  tree: string;
  version: string;
  license: string;
  skillPath: string;
  skillSha256: string;
  reviewedAt: string;
}

export interface ISkillValidationPolicy {
  schemaVersion: number;
  ruleSetVersion: string;
  policyVersion: string;
  reviewedAt: string;
  reviewedBy: string;
  reviewProcess: string;
  guardSkill: {
    path: string;
    sha256: string;
  };
  ruleSources: Array<{
    path: string;
    sha256: string;
  }>;
  references: ISkillValidationReference[];
}

export const SKILL_VALIDATION_POLICY = policyData as ISkillValidationPolicy;

/**
 * Versão da política efetiva. O ledger usa este valor como parte da chave
 * lógica de cache: qualquer mudança nas referências ou no conjunto de regras
 * invalida aprovações antigas e força uma nova auditoria.
 *
 * A string canônica é replicada em scripts/check-skill-references.mjs. Se você
 * alterar o formato, atualize os dois lados e o teste de política.
 */
export function computeSkillPolicyVersion(policy: ISkillValidationPolicy): string {
  const references = [...policy.references]
    .sort((a, b) => (a.id < b.id ? -1 : a.id > b.id ? 1 : 0))
    .map(reference => [
      reference.id,
      reference.repo,
      reference.path,
      reference.commit,
      reference.tree,
      reference.skillPath,
      reference.skillSha256,
      reference.license,
      reference.role
    ].join('|'));

  const canonical = [
    `schemaVersion=${policy.schemaVersion}`,
    `ruleSetVersion=${policy.ruleSetVersion}`,
    `guardSkill=${policy.guardSkill.path}|${policy.guardSkill.sha256}`,
    ...[...policy.ruleSources]
      .sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0))
      .map(source => `ruleSource=${source.path}|${source.sha256}`),
    ...references
  ].join('\n');

  return sha256(canonical);
}

export const CURRENT_SKILL_POLICY_VERSION = computeSkillPolicyVersion(SKILL_VALIDATION_POLICY);
export const LOCKED_SKILL_POLICY_VERSION = SKILL_VALIDATION_POLICY.policyVersion;

export const CURRENT_SKILL_AUDIT_CYCLES = [
  'integrity',
  'nvidia-skillspector',
  'cloudflare-security-audit'
] as const;

export const SKILL_AUDIT_ENGINE_VERSION =
  `ArestrixSkillGuard-1.0+nvidia-${referenceById('nvidia-skillspector')?.version || 'unknown'}` +
  `+cloudflare-${referenceById('cloudflare-security-audit')?.version || 'unknown'}`;

export function referenceById(id: string): ISkillValidationReference | undefined {
  return SKILL_VALIDATION_POLICY.references.find(reference => reference.id === id);
}

export function referenceVersionLabels(): string[] {
  return SKILL_VALIDATION_POLICY.references.map(
    reference => `${reference.id}@${reference.version}`
  );
}

export function isPolicyCurrent(policyVersion: string | undefined): boolean {
  return policyVersion === CURRENT_SKILL_POLICY_VERSION;
}
