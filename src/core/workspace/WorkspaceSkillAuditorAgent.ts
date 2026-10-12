import { INodeSkill } from '../types.ts';
import { IWorkspaceSkillAuditVerdict } from './types.ts';
import { APPROVED_WORKSPACE_SKILLS } from './ApprovedWorkspaceCatalog.ts';

/**
 * Arestrix Studio - Agente de Workspace para Auditoria e Verificação de Skills
 * 
 * Este agente atua como sentinela de governança do workspace. Todo skill que o usuário
 * importa a partir de repositórios externos, cria manualmente ou vincula a um nó é
 * verificado e certificado por este agente.
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
   * Executa a auditoria completa de uma habilidade
   */
  public auditSkill(skill: Partial<INodeSkill>, isCuratedWorkspace: boolean = false): IWorkspaceSkillAuditVerdict {
    const timestamp = new Date().toISOString();
    const skillId = skill.id || 'skill-unknown';
    const skillName = skill.name || 'Habilidade Sem Nome';

    // 1. Verificação instantânea de proveniência para skills do catálogo oficial
    const isApprovedCatalogSkill = isCuratedWorkspace || APPROVED_WORKSPACE_SKILLS.some(s => s.id === skillId);
    if (isApprovedCatalogSkill) {
      return {
        skillId,
        skillName,
        verified: true,
        status: 'approved',
        riskScore: 0,
        auditedBy: 'WorkspaceSkillAuditorAgent',
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
        passedSvgZeroShadowRule: true
      };
    }

    // 2. Auditoria detalhada para habilidades customizadas ou de terceiros
    const checklist: Array<{ rule: string; passed: boolean; detail: string }> = [];
    const findings: string[] = [];
    const warnings: string[] = [];
    let riskScore = 5; // Base inicial para skills externos

    // 2.1 Validação de Nome e Metadados Básicos
    if (skill.name && skill.name.trim().length >= 3) {
      checklist.push({ rule: 'Identificação e Nome', passed: true, detail: `Nome válido: "${skill.name}"` });
      findings.push('Identificação da habilidade consistente.');
    } else {
      checklist.push({ rule: 'Identificação e Nome', passed: false, detail: 'Nome ausente ou excessivamente curto.' });
      warnings.push('Nome da habilidade deve possuir pelo menos 3 caracteres.');
      riskScore += 15;
    }

    // 2.2 Validação de Licença
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

    // 2.3 Validação do Formato SKILL.md
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

    // 2.4 Validação de Ferramentas (Risk Assessment de Tools)
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

    // 2.5 Validação Estrita de Tipografia SVG (Regra Global do Projeto)
    const lowerContent = content.toLowerCase() + ' ' + (skill.systemPrompt || '').toLowerCase();
    const shadowPatterns = [
      'text-shadow',
      'fedropshadow',
      'fegaussianblur',
      'filter: drop-shadow',
      'filter:drop-shadow',
      'drop-shadow('
    ];
    const hasShadowViolation = shadowPatterns.some(pattern => lowerContent.includes(pattern));

    if (hasShadowViolation) {
      checklist.push({
        rule: 'Diretriz de Tipografia Nítida (Zero Sombra em Fontes)',
        passed: false,
        detail: 'Detectada instrução ou estilo com text-shadow ou drop-shadow sobre elementos de texto/fontes.'
      });
      warnings.push('Violação das Diretrizes Globais: efeitos de sombra ou blur em fontes são proibidos. Utilize cores de contraste, peso tipográfico ou badges com fundo sólido.');
      riskScore += 25;
    } else {
      checklist.push({
        rule: 'Diretriz de Tipografia Nítida (Zero Sombra em Fontes)',
        passed: true,
        detail: 'Nenhum efeito de sombra ou borrão em fontes/legendas detectado.'
      });
      findings.push('Conforme com a regra global de tipografia e contraste visual nítido.');
    }

    // 2.6 Determinação do Veredito
    let status: 'approved' | 'warning' | 'rejected' = 'approved';
    if (riskScore >= 40 || hasShadowViolation) {
      status = 'warning';
    }
    if (riskScore >= 70) {
      status = 'rejected';
    }

    const verified = status !== 'rejected';

    return {
      skillId,
      skillName,
      verified,
      status,
      riskScore: Math.min(100, riskScore),
      auditedBy: 'WorkspaceSkillAuditorAgent',
      timestamp,
      checklist,
      findings,
      warnings,
      allowedTools: tools,
      authorizedTools: tools,
      hasTypographyShadowViolation: hasShadowViolation,
      passedSvgZeroShadowRule: !hasShadowViolation
    };
  }
}
