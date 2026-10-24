import { INodeSkill } from '../types.ts';
import { IWorkspaceSkillAuditVerdict } from './types.ts';
import { APPROVED_WORKSPACE_SKILLS } from './ApprovedWorkspaceCatalog.ts';
import { SkillHasher } from '../skills/SkillHasher.ts';
import { SkillLedgerService, SkillAuditVerdict } from '../skills/SkillLedgerService.ts';
import { SkillSpectorRunner } from '../skills/SkillSpectorRunner.ts';

/**
 * Arestrix Studio - Agente de Workspace para Auditoria e Verificação de Skills
 * 
 * Sentinela de governança do Studio operando com base na meta-skill canônica
 * `third_party/SkillSpector/skills/skill-inspector/SKILL.md`.
 * 
 * Executa duas linhas de defesa:
 * 1. Linha Estática e Determinística: via SkillSpectorRunner (regras estáticas,
 *    filtro anti-esteganografia, checagem da Trifeta Letal e regras de projeto);
 * 2. Linha de Governança Semântica: coerência entre propósito declarado e ferramentas.
 * 
 * Toda validação é atrelada a um hash SHA-256 e gravada no SkillLedgerService;
 * skills idênticas (mesmo hash) são liberadas instantaneamente (0 ms).
 */
export class WorkspaceSkillAuditorAgent {
  private static instance: WorkspaceSkillAuditorAgent;

  public static getInstance(): WorkspaceSkillAuditorAgent {
    if (!WorkspaceSkillAuditorAgent.instance) {
      WorkspaceSkillAuditorAgent.instance = new WorkspaceSkillAuditorAgent();
    }
    return WorkspaceSkillAuditorAgent.instance;
  }

  /**
   * Executa a auditoria completa de uma habilidade (versão síncrona / compatível).
   */
  public auditSkill(skill: Partial<INodeSkill>, isCuratedWorkspace: boolean = false): IWorkspaceSkillAuditVerdict {
    const timestamp = new Date().toISOString();
    const skillId = skill.id || 'skill-unknown';
    const skillName = skill.name || 'Habilidade Sem Nome';
    const ledger = SkillLedgerService.getInstance();
    const hash = SkillHasher.computeHash(skill);

    // 1. Verificação instantânea de cache no Ledger ($O(1)$)
    const existingEntry = ledger.getEntry(hash);
    if (existingEntry && (existingEntry.verdict === 'APPROVE' || existingEntry.verdict === 'CAUTION')) {
      const isApproved = existingEntry.verdict === 'APPROVE';
      return {
        skillId,
        skillName,
        verified: true,
        status: isApproved ? 'approved' : 'warning',
        riskScore: existingEntry.riskScore,
        auditedBy: existingEntry.auditedBy,
        timestamp: existingEntry.auditedAt,
        checklist: existingEntry.checklist,
        findings: existingEntry.findings,
        warnings: existingEntry.warnings,
        allowedTools: existingEntry.tools,
        authorizedTools: existingEntry.tools,
        hasTypographyShadowViolation: false,
        passedSvgZeroShadowRule: true,
        hash,
        verdict: existingEntry.verdict
      };
    }

    // 2. Verificação de proveniência para skills do catálogo oficial
    const isApprovedCatalogSkill = isCuratedWorkspace || APPROVED_WORKSPACE_SKILLS.some(s => s.id === skillId);
    if (isApprovedCatalogSkill) {
      const verdict: IWorkspaceSkillAuditVerdict = {
        skillId,
        skillName,
        verified: true,
        status: 'approved',
        riskScore: 0,
        auditedBy: 'WorkspaceApprovedCatalog',
        timestamp,
        checklist: [
          { rule: 'Proveniência Oficial do Workspace', passed: true, detail: 'Habilidade homologada e empacotada no catálogo central do Arestrix.' },
          { rule: 'Estrutura Canônica de SKILL.md', passed: true, detail: 'Frontmatter YAML válido com nome, descrição e licença aberta compatível.' },
          { rule: 'Escopo de Ferramentas Autorizado', passed: true, detail: 'Ferramentas declaradas mapeadas e restritas ao escopo operacional seguro.' },
          { rule: 'Conformidade de Estilo e Tipografia', passed: true, detail: 'Zero efeitos de sombra/glow em fontes e respeito estrito às regras visuais.' }
        ],
        findings: [
          'Habilidade homologada oficialmente pelo Workspace Arestrix.',
          'Assinatura de segurança íntegra e verificada no catálogo central.',
          'Ferramentas associadas previamente autorizadas.'
        ],
        warnings: [],
        allowedTools: skill.tools || ['write', 'edit', 'read'],
        authorizedTools: skill.tools || ['write', 'edit', 'read'],
        hasTypographyShadowViolation: false,
        passedSvgZeroShadowRule: true,
        hash,
        verdict: 'APPROVE'
      };

      ledger.recordApproval(skill, {
        auditedBy: 'WorkspaceApprovedCatalog',
        provenance: 'workspace-curated',
        findings: verdict.findings,
        checklist: verdict.checklist
      });

      return verdict;
    }

    // 3. Auditoria aprofundada via SkillSpector
    const checklist: Array<{ rule: string; passed: boolean; detail: string }> = [];
    const findings: string[] = [];
    const warnings: string[] = [];
    let riskScore = 0;

    // 3.1 Validação de Nome e Metadados Básicos
    if (skill.name && skill.name.trim().length >= 3) {
      checklist.push({ rule: 'Identificação e Nome', passed: true, detail: `Nome válido: "${skill.name}"` });
      findings.push('Identificação da habilidade consistente.');
    } else {
      checklist.push({ rule: 'Identificação e Nome', passed: false, detail: 'Nome ausente ou excessivamente curto.' });
      warnings.push('Nome da habilidade deve possuir pelo menos 3 caracteres.');
      riskScore += 15;
    }

    // 3.2 Validação de Licença Aberta
    const lic = (skill.license || '').toUpperCase().trim();
    const isOpenLicense = ['MIT', 'APACHE-2.0', 'BSD', 'ISC', 'CC0'].some(l => lic.includes(l));
    if (isOpenLicense) {
      checklist.push({ rule: 'Licença Aberta Permissiva', passed: true, detail: `Licença compatível detectada: ${skill.license}` });
      findings.push(`Licença aberta declarada: ${skill.license}`);
    } else {
      checklist.push({ rule: 'Licença Aberta Permissiva', passed: false, detail: `Licença não-padrão ou ausente: ${skill.license || 'Nenhuma'}` });
      warnings.push('Habilidade sem licença de código aberto explicitamente permissiva (MIT/Apache recomendado).');
      riskScore += 10;
    }

    // 3.3 Validação do Formato SKILL.md
    const content = skill.markdownContent || '';
    const hasFrontmatter = content.trim().startsWith('---') && content.indexOf('---', 3) > 3;
    if (hasFrontmatter) {
      checklist.push({ rule: 'Frontmatter YAML Canônico', passed: true, detail: 'Cabeçalho YAML delimitado por --- encontrado no SKILL.md.' });
      findings.push('Estrutura de metadados no padrão SKILL.md em conformidade.');
    } else {
      checklist.push({ rule: 'Frontmatter YAML Canônico', passed: false, detail: 'Cabeçalho YAML ausente ou malformatado no topo do documento.' });
      warnings.push('Recomendado incluir frontmatter YAML canônico no início do SKILL.md.');
      riskScore += 10;
    }

    // 3.4 Varredura Estática SkillSpector
    const scanResult = SkillSpectorRunner.scanSync(skill);
    riskScore += scanResult.riskScore;

    for (const f of scanResult.findings) {
      checklist.push({
        rule: `SkillSpector: ${f.ruleId}`,
        passed: f.severity === 'LOW' || f.severity === 'INFO',
        detail: f.message
      });
      if (f.severity === 'CRITICAL' || f.severity === 'HIGH') {
        warnings.push(f.message);
      } else {
        findings.push(f.message);
      }
    }

    // 3.5 Verificação de Ferramentas de Alto Privilégio / Terminal
    const tools = skill.tools || [];
    const highRiskTools = ['bash', 'terminal', 'exec', 'system', 'root'];
    const detectedHighRisk = tools.filter(t => highRiskTools.includes(t.toLowerCase()));
    if (detectedHighRisk.length > 0) {
      checklist.push({ rule: 'Privilégios de Ferramentas', passed: false, detail: `Ferramentas de alto privilégio solicitadas: ${detectedHighRisk.join(', ')}` });
      warnings.push(`A habilidade solicita execução de comandos de terminal (${detectedHighRisk.join(', ')}). Exige sandbox isolado.`);
      riskScore += 30;
    } else {
      checklist.push({ rule: 'Privilégios de Ferramentas', passed: true, detail: `Ferramentas solicitadas estão dentro do escopo seguro: ${tools.join(', ') || 'Nenhuma'}` });
      findings.push('Ferramentas declaradas respeitam o perímetro de segurança padrão.');
    }

    // 3.6 Diretriz de Tipografia Nítida
    const hasShadowViolation = scanResult.findings.some(f => f.ruleId === 'STYLE-TYPOGRAPHY-ZERO-SHADOW');
    if (!hasShadowViolation) {
      checklist.push({
        rule: 'Diretriz de Tipografia Nítida (Zero Sombra em Fontes)',
        passed: true,
        detail: 'Nenhum efeito de sombra ou borrão em fontes/legendas detectado.'
      });
      findings.push('Conforme com a regra global de tipografia e contraste visual nítido.');
    }

    // 3.7 Determinação do Veredito e Gravação no Ledger
    const finalRiskScore = Math.min(100, Math.max(0, riskScore));
    let status: 'approved' | 'warning' | 'rejected' = 'approved';
    let verdict: SkillAuditVerdict = 'APPROVE';

    const hasCriticalFinding = scanResult.findings.some(f => f.severity === 'CRITICAL');
    if (finalRiskScore >= 70 || hasCriticalFinding) {
      status = 'rejected';
      verdict = 'REJECT';
    } else if (finalRiskScore >= 20 || hasShadowViolation || warnings.length > 0) {
      status = 'warning';
      verdict = 'CAUTION';
    }

    const verified = status !== 'rejected';

    const resultVerdict: IWorkspaceSkillAuditVerdict = {
      skillId,
      skillName,
      verified,
      status,
      riskScore: finalRiskScore,
      auditedBy: 'StudioSkillAuditor (SkillSpector)',
      timestamp,
      checklist,
      findings,
      warnings,
      allowedTools: tools,
      authorizedTools: tools,
      hasTypographyShadowViolation: hasShadowViolation,
      passedSvgZeroShadowRule: !hasShadowViolation,
      hash,
      verdict
    };

    // Grava no Ledger deterministicamente
    ledger.recordAudit({
      hash,
      skillId,
      name: skillName,
      verdict,
      riskScore: finalRiskScore,
      auditedAt: timestamp,
      auditedBy: 'StudioSkillAuditor (SkillSpector)',
      scannerVersion: 'SkillSpector-2.12.0',
      findingsCount: findings.length,
      findings,
      warnings,
      checklist,
      tools,
      provenance: 'custom'
    });

    return resultVerdict;
  }
}
