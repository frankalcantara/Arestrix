import { IExcelReviewResult } from '../ai/DefaultIntelligenceService.ts';
import { WorkbookModel } from './WorkbookModel.ts';

/**
 * Aplica ao modelo tudo que a revisão do agente trouxe (papéis e tabelas), sem intervenção humana.
 * As dúvidas ficam em `model.review.questions` para o modal de decisões. Devolve o que foi aplicado.
 */
export function applyReview(model: WorkbookModel, review: IExcelReviewResult): { roles: number; tables: number } {
  const regions = new Map(model.sheets.flatMap(s => s.regions).map(r => [r.id, r]));
  let roles = 0;
  let tables = 0;

  for (const c of review.regionCorrections) {
    const r = regions.get(c.regionId);
    if (!r || r.role === c.role) continue;
    r.detectedRole ??= r.role;
    r.role = c.role;
    roles++;
  }
  for (const t of review.tableSuggestions) {
    const r = regions.get(t.regionId);
    if (!r) continue;
    r.table = { name: t.tableName, primaryKey: t.primaryKey, ...(t.notes ? { notes: t.notes } : {}) };
    tables++;
  }

  model.review = {
    summary: review.summary,
    confidenceScore: review.confidenceScore,
    findings: review.findings,
    appliedRoles: roles,
    appliedTables: tables,
    questions: review.questionsForUser.map(text => ({ text }))
  };
  return { roles, tables };
}
