import { beforeEach, describe, expect, it } from 'vitest';
import { EventBus } from '../src/core/EventBus.ts';
import { INodeSkill } from '../src/core/types.ts';
import { SkillHasher } from '../src/core/skills/SkillHasher.ts';
import { SkillLedgerService } from '../src/core/skills/SkillLedgerService.ts';
import {
  CURRENT_SKILL_POLICY_VERSION
} from '../src/core/skills/SkillValidationPolicy.ts';
import { SkillRevalidationService } from '../src/core/skills/SkillRevalidationService.ts';

const OLD_POLICY = '0000000000000000000000000000000000000000000000000000000000000000';

function makeSkill(id: string, name: string, rule: string): INodeSkill {
  return {
    id,
    name,
    description: `Skill de teste ${name}`,
    license: 'MIT',
    tools: ['read', 'write'],
    systemPrompt: `Você executa ${name}.`,
    markdownContent: `---\nname: "${name}"\nlicense: MIT\n---\n\n# ${name}\n\n${rule}\n`,
    updatedAt: '2026-11-10T00:00:00.000Z'
  };
}

function fakeWorkspace(audit: (skill: Partial<INodeSkill>) => any) {
  return {
    getApprovedSkills: () => [],
    convertToNodeSkill: (skill: any) => skill,
    auditSkill: audit
  };
}

function fakeLibrary() {
  return {
    listAssets: () => [],
    hydrate: async () => false
  };
}

describe('Reauditoria após mudança de policyVersion', () => {
  let ledger: SkillLedgerService;

  beforeEach(() => {
    ledger = SkillLedgerService.resetForTesting(new EventBus());
  });

  it('lista entradas antigas e mantém o veredito antigo fora da política atual', () => {
    const skill = makeSkill('skill-old', 'Skill Antiga', 'Regra antiga.');
    const entry = ledger.recordApproval(skill, {
      policyVersion: OLD_POLICY,
      provenance: 'custom'
    });

    expect(ledger.getStaleEntries()).toHaveLength(1);
    expect(ledger.getStaleEntries()[0].hash).toBe(entry.hash);
    expect(ledger.getEntry(entry.hash)).toBeUndefined();
    expect(ledger.getStatus(entry.hash)).toBe('PENDING');
    expect(ledger.getStaleEntries(CURRENT_SKILL_POLICY_VERSION)).toHaveLength(1);
  });

  it('não lista entradas do catálogo oficial como skills importadas', () => {
    const curated = makeSkill('skill-curated', 'Skill Oficial', 'Regra oficial.');
    ledger.recordApproval(curated, {
      policyVersion: OLD_POLICY,
      provenance: 'workspace-curated',
      auditedBy: 'WorkspaceApprovedCatalog'
    });
    const custom = makeSkill('skill-custom', 'Skill Custom', 'Regra custom.');
    ledger.recordApproval(custom, {
      policyVersion: OLD_POLICY,
      provenance: 'custom'
    });

    const service = new SkillRevalidationService(
      { getStaleEntries: () => ledger.getStaleEntries() } as any,
      fakeWorkspace(() => ({ status: 'approved', verdict: 'APPROVE', riskScore: 0 })) as any,
      fakeLibrary() as any
    );

    const stale = service.listStale();
    expect(stale).toHaveLength(1);
    expect(stale[0].skillId).toBe('skill-custom');
  });

  it('reaudita uma skill encontrada no grafo e contabiliza o novo veredito', async () => {
    const skill = makeSkill('skill-graph', 'Skill do Grafo', 'Regra do grafo.');
    ledger.recordApproval(skill, {
      policyVersion: OLD_POLICY,
      provenance: 'custom'
    });
    const audited: string[] = [];

    const service = new SkillRevalidationService(
      { getStaleEntries: () => ledger.getStaleEntries() } as any,
      fakeWorkspace((candidate) => {
        audited.push(candidate.id || '');
        return {
          status: 'approved',
          verdict: 'APPROVE',
          riskScore: 0,
          hash: SkillHasher.computeHash(candidate)
        };
      }) as any,
      fakeLibrary() as any
    );
    const graph = {
      getAllNodes: () => [{ agentSettings: { skills: [skill] } }]
    } as any;

    const report = await service.revalidateAll(graph);

    expect(audited).toEqual(['skill-graph']);
    expect(report.total).toBe(1);
    expect(report.revalidated).toBe(1);
    expect(report.approved).toBe(1);
    expect(report.unresolved).toBe(0);
    expect(report.items[0].verdict).toBe('APPROVE');
  });

  it('deixa como não encontrada a entrada sem conteúdo localizável', async () => {
    const skill = makeSkill('skill-missing', 'Skill Ausente', 'Regra ausente.');
    ledger.recordApproval(skill, {
      policyVersion: OLD_POLICY,
      provenance: 'custom'
    });

    const service = new SkillRevalidationService(
      { getStaleEntries: () => ledger.getStaleEntries() } as any,
      fakeWorkspace(() => ({ status: 'approved', verdict: 'APPROVE', riskScore: 0 })) as any,
      fakeLibrary() as any
    );

    const report = await service.revalidateAll();

    expect(report.total).toBe(1);
    expect(report.revalidated).toBe(0);
    expect(report.unresolved).toBe(1);
    expect(report.items[0].audited).toBe(false);
  });
});
