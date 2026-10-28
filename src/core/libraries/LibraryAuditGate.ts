import type { IMcpServerConfig, INodeSkill } from '../types.ts';
import type { ILibraryAssetAudit, LibraryAuditVerdict } from './types.ts';
import { MCP_AUDIT_POLICY_VERSION } from './types.ts';
import { WorkspaceSkillAuditorAgent } from '../workspace/WorkspaceSkillAuditorAgent.ts';
import { WorkspaceMcpAuditorAgent } from '../workspace/WorkspaceMcpAuditorAgent.ts';
import { SkillHasher } from '../skills/SkillHasher.ts';
import { McpHasher } from '../skills/McpHasher.ts';
import { CURRENT_SKILL_POLICY_VERSION } from '../skills/SkillValidationPolicy.ts';
import { sha256 } from '../hash.ts';
import type { ISerializedTemplatePlugin } from '../templates/AgentTemplateTypes.ts';

/**
 * Arestrix Studio - Gate único de auditoria de assets de biblioteca
 *
 * Nenhum asset entra em uma biblioteca ou no Workspace sem passar por aqui.
 * Skills usam SkillSpector + Cloudflare + SkillLedger; MCPs usam o scanner
 * de MCP + McpLedger. O veredito é sempre acompanhado de hash e política.
 */
export class LibraryAuditGate {
  private static instance: LibraryAuditGate;

  public static getInstance(): LibraryAuditGate {
    if (!LibraryAuditGate.instance) {
      LibraryAuditGate.instance = new LibraryAuditGate();
    }
    return LibraryAuditGate.instance;
  }

  public auditSkill(skill: Partial<INodeSkill>, isCurated = false): ILibraryAssetAudit {
    // O auditor do Workspace libera skills apenas pelo ID do catálogo. Para
    // assets de biblioteca não curados, usamos um ID de auditoria próprio para
    // forçar a varredura real do conteúdo.
    const auditInput = isCurated
      ? skill
      : { ...skill, id: `library-audit:${skill.id || 'unknown'}` };
    const verdict = WorkspaceSkillAuditorAgent.getInstance().auditSkill(auditInput, isCurated);
    return {
      verdict: this.mapVerdict(verdict.verdict, verdict.status),
      riskScore: verdict.riskScore,
      policyVersion: verdict.policyVersion || CURRENT_SKILL_POLICY_VERSION,
      auditedAt: verdict.timestamp,
      hash: verdict.hash || SkillHasher.computeHash(skill),
      findings: [...verdict.findings],
      warnings: [...verdict.warnings]
    };
  }

  public auditMcp(server: Partial<IMcpServerConfig>, isCurated = false): ILibraryAssetAudit {
    const auditInput = isCurated
      ? server
      : { ...server, id: `library-audit:${server.id || 'unknown'}` };
    const verdict = WorkspaceMcpAuditorAgent.getInstance().auditMcpServer(auditInput, isCurated);
    return {
      verdict: this.mapVerdict(verdict.verdict, verdict.status),
      riskScore: verdict.riskScore,
      policyVersion: MCP_AUDIT_POLICY_VERSION,
      auditedAt: verdict.timestamp,
      hash: verdict.hash || McpHasher.computeHash(server),
      findings: [...verdict.findings],
      warnings: [...verdict.warnings]
    };
  }

  /**
   * Auditoria de template de vértice. Templates geram código C++ por definição;
   * a auditoria valida o esquema, limites de tamanho e padrões de execução
   * perigosos no corpo gerado. Não executa nada.
   */
  public auditTemplate(template: Partial<ISerializedTemplatePlugin>): ILibraryAssetAudit {
    const findings: string[] = [];
    const warnings: string[] = [];
    let riskScore = 0;

    const invalid = !template
      || typeof template.id !== 'string' || !template.id.trim()
      || typeof template.name !== 'string' || !template.name.trim()
      || typeof template.taskType !== 'string' || !template.taskType.trim()
      || typeof template.lambdaBodyTemplate !== 'string';

    if (invalid) {
      return {
        verdict: 'REJECT',
        riskScore: 100,
        policyVersion: 'template-schema-v1',
        auditedAt: new Date().toISOString(),
        hash: sha256(JSON.stringify(template || {})),
        findings: ['Esquema de template inválido: id, name, taskType e lambdaBodyTemplate são obrigatórios.'],
        warnings: []
      };
    }

    const body = template.lambdaBodyTemplate || '';
    if (body.length > 200_000) {
      return {
        verdict: 'REJECT',
        riskScore: 90,
        policyVersion: 'template-schema-v1',
        auditedAt: new Date().toISOString(),
        hash: sha256(JSON.stringify(template)),
        findings: ['Corpo C++ do template excede o limite de 200 KB.'],
        warnings: []
      };
    }

    findings.push('Esquema básico do template válido.');
    findings.push(`Tipo de tarefa declarado: ${template.taskType}.`);
    findings.push('Corpo C++ tratado como texto; o template não é executado durante a auditoria.');

    if (/\b(?:system|popen|_popen|CreateProcess[AW]?|ShellExecute[AW]?|exec[lv]?p?e?)\s*\(/i.test(body)) {
      warnings.push('Template contém chamada de criação/execução de processo no corpo C++.');
      riskScore += 40;
    }
    if (/\b(?:password|passwd|secret|api[_-]?key|token)\s*=\s*["'][^"']+["']/i.test(body)) {
      warnings.push('Possível segredo literal embutido no corpo do template.');
      riskScore += 30;
    }
    if (/https?:\/\//i.test(body)) {
      warnings.push('Template referencia URL externa; revise o destino antes de compilar.');
      riskScore += 10;
    }

    const verdict: ILibraryAssetAudit['verdict'] = warnings.length > 0 ? 'CAUTION' : 'APPROVE';
    const hash = sha256(JSON.stringify({
      id: template.id,
      name: template.name,
      taskType: template.taskType,
      inputs: template.inputs || [],
      outputs: template.outputs || [],
      parameters: template.parameters || {},
      requiredHeaders: template.requiredHeaders || [],
      lambdaBodyTemplate: body
    }));

    return {
      verdict,
      riskScore: Math.min(100, riskScore),
      policyVersion: 'template-schema-v1',
      auditedAt: new Date().toISOString(),
      hash,
      findings,
      warnings
    };
  }

  /**
   * Regras de promoção:
   * - REJECT nunca instala.
   * - CAUTION instala apenas com confirmação explícita; no modo estrito, bloqueia.
   * - PENDING precisa de auditoria antes de instalar.
   */
  public canInstall(
    audit: ILibraryAssetAudit | undefined,
    options: { allowCaution?: boolean; strict?: boolean } = {}
  ): { ok: boolean; reason?: string } {
    if (!audit || audit.verdict === 'PENDING') {
      return { ok: false, reason: 'Asset ainda não auditado.' };
    }
    if (audit.verdict === 'REJECT') {
      return { ok: false, reason: 'Asset rejeitado pelo auditor de segurança.' };
    }
    if (audit.verdict === 'CAUTION') {
      if (options.strict) {
        return { ok: false, reason: 'Modo estrito bloqueia assets com alerta de auditoria.' };
      }
      if (!options.allowCaution) {
        return { ok: false, reason: 'Asset com alerta exige confirmação explícita.' };
      }
    }
    return { ok: true };
  }

  private mapVerdict(
    verdict: 'APPROVE' | 'CAUTION' | 'REJECT' | 'PENDING' | undefined,
    status: 'approved' | 'warning' | 'rejected'
  ): LibraryAuditVerdict {
    if (verdict) return verdict;
    if (status === 'approved') return 'APPROVE';
    if (status === 'rejected') return 'REJECT';
    return 'CAUTION';
  }
}
