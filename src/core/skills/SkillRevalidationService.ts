import { Graph } from '../Graph.ts';
import { INodeSkill } from '../types.ts';
import { LibraryService } from '../libraries/LibraryService.ts';
import { assetPayloadToNodeSkill } from '../libraries/types.ts';
import { WorkspaceManager } from '../workspace/WorkspaceManager.ts';
import { SkillHasher } from './SkillHasher.ts';
import { ISkillLedgerEntry, SkillLedgerService } from './SkillLedgerService.ts';

/**
 * Arestrix Studio - Reauditoria em lote após mudança da política de skills.
 *
 * A `policyVersion` invalida aprovações antigas de propósito. Este serviço
 * encontra as entradas antigas, localiza o conteúdo atual da skill (biblioteca
 * instalada, catálogo do workspace ou nó do grafo atual) e executa o mesmo
 * auditor usado pelo botão "Auditar com Skill Guard".
 *
 * Entradas cujo conteúdo não está disponível no escopo atual não são aprovadas
 * silenciosamente: voltam como `unresolved` para o usuário abrir o projeto de
 * origem e auditar no editor.
 */

export type SkillRevalidationVerdict = 'APPROVE' | 'CAUTION' | 'REJECT' | 'PENDING';

export interface ISkillRevalidationItem {
  entry: ISkillLedgerEntry;
  audited: boolean;
  verdict?: SkillRevalidationVerdict;
}

export interface ISkillRevalidationReport {
  total: number;
  revalidated: number;
  approved: number;
  caution: number;
  rejected: number;
  unresolved: number;
  items: ISkillRevalidationItem[];
}

type LedgerDependency = Pick<SkillLedgerService, 'getStaleEntries'>;
type WorkspaceDependency = Pick<WorkspaceManager, 'getApprovedSkills' | 'convertToNodeSkill' | 'auditSkill'>;
type LibraryDependency = Pick<LibraryService, 'listAssets' | 'hydrate'>;

function normalizeVerdict(value: { status?: string; verdict?: string }): SkillRevalidationVerdict {
  const explicit = value.verdict;
  if (explicit === 'APPROVE' || explicit === 'CAUTION' || explicit === 'REJECT' || explicit === 'PENDING') {
    return explicit;
  }
  if (value.status === 'approved') return 'APPROVE';
  if (value.status === 'warning') return 'CAUTION';
  if (value.status === 'rejected') return 'REJECT';
  return 'PENDING';
}

export class SkillRevalidationService {
  constructor(
    private readonly ledger: LedgerDependency = SkillLedgerService.getInstance(),
    private readonly workspace: WorkspaceDependency = WorkspaceManager.getInstance(),
    private readonly library: LibraryDependency = LibraryService.getInstance()
  ) {}

  /**
   * Skills importadas/customizadas com aprovação emitida sob outra política.
   * Skills do catálogo oficial são re-semeadas automaticamente e não entram
   * na lista.
   */
  public listStale(): ISkillLedgerEntry[] {
    return this.ledger.getStaleEntries().filter(entry =>
      entry.provenance !== 'workspace-curated' &&
      entry.auditedBy !== 'WorkspaceApprovedCatalog'
    );
  }

  /**
   * Reaudita todas as entradas antigas que ainda têm conteúdo localizável.
   * A hidratação da biblioteca é feita antes da coleta para que skills
   * instaladas em sessões anteriores também sejam encontradas.
   */
  public async revalidateAll(graph?: Graph): Promise<ISkillRevalidationReport> {
    const stale = this.listStale();
    try {
      await this.library.hydrate('workspace');
    } catch {
      // Sem hidratação a reauditoria continua com o que já está em memória.
    }

    const candidates = this.collectCandidates(graph);
    const report: ISkillRevalidationReport = {
      total: stale.length,
      revalidated: 0,
      approved: 0,
      caution: 0,
      rejected: 0,
      unresolved: 0,
      items: []
    };

    for (const entry of stale) {
      const skill = candidates.get(entry.hash.toLowerCase());
      if (!skill) {
        report.unresolved += 1;
        report.items.push({ entry, audited: false });
        continue;
      }

      const verdict = normalizeVerdict(this.workspace.auditSkill(skill));
      report.revalidated += 1;
      if (verdict === 'APPROVE') report.approved += 1;
      else if (verdict === 'CAUTION') report.caution += 1;
      else if (verdict === 'REJECT') report.rejected += 1;
      report.items.push({ entry, audited: true, verdict });
    }

    return report;
  }

  private collectCandidates(graph?: Graph): Map<string, INodeSkill> {
    const candidates = new Map<string, INodeSkill>();
    const add = (skill: Partial<INodeSkill> | undefined) => {
      if (!skill || !skill.id) return;
      const content = `${skill.markdownContent || ''}\n${skill.systemPrompt || ''}`.trim();
      if (!content) return;
      const hash = SkillHasher.computeHash(skill).toLowerCase();
      if (!candidates.has(hash)) {
        candidates.set(hash, skill as INodeSkill);
      }
    };

    for (const skill of this.workspace.getApprovedSkills()) {
      add(this.workspace.convertToNodeSkill(skill));
    }

    for (const asset of this.library.listAssets()) {
      if (asset.payload.kind !== 'skill') continue;
      if (asset.status !== 'installed' && asset.status !== 'pinned') continue;
      add(assetPayloadToNodeSkill(asset));
    }

    if (graph) {
      for (const node of graph.getAllNodes()) {
        for (const skill of node.agentSettings?.skills || []) {
          add(skill);
        }
      }
    }

    return candidates;
  }
}
