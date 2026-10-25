import { INodeSkill } from '../types.ts';

export interface ICloudflareSkillFinding {
  ruleId: string;
  severity: 'CRITICAL' | 'HIGH' | 'MEDIUM' | 'LOW' | 'INFO';
  category: string;
  message: string;
}

export interface ICloudflareSkillAuditResult {
  success: boolean;
  riskScore: number;
  severity: 'CRITICAL' | 'HIGH' | 'MEDIUM' | 'LOW' | 'NONE';
  findings: ICloudflareSkillFinding[];
  recommendation: 'APPROVE' | 'CAUTION' | 'REJECT';
  source: 'cloudflare-security-audit';
  scannedAt: string;
}

/**
 * Segunda linha determinística do Skill Guard.
 *
 * As regras abaixo são uma destilação conservadora das classes de ataque do
 * skill `cloudflare/security-audit-skill` que se aplicam a um SKILL.md:
 * supply chain, persistência, acesso a credenciais, exfiltração, abuso de
 * ferramentas, isolamento de dados, exaustão de recursos e IPC local.
 *
 * Não é um scanner de código-fonte completo nem substitui a revisão semântica
 * adversarial descrita em `.agents/skills/arestrix-skill-guard/SKILL.md`.
 */
export class CloudflareSkillAudit {
  private static readonly REMOTE_EXEC =
    /(?:curl|wget|iwr|invoke-webrequest)[^\n|;]{0,200}\|\s*(?:bash|sh|zsh|iex|invoke-expression|powershell)|\bpowershell(?:\.exe)?\s+(?:-enc|-encodedcommand)\b/i;

  private static readonly DESTRUCTIVE_SHELL =
    /\b(?:rm\s+-rf\s+(?:\/|~)|del\s+\/[fsq]|format\s+[a-z]:|mkfs\.|dd\s+if=)\b/i;

  private static readonly PERSISTENCE =
    /\b(?:crontab|schtasks|systemd|launchd|launchagents|startup\s+folder|\.bashrc|\.zshrc|shell\s+profile|registry\s+run|\\run\\|autorun)\b/i;

  private static readonly CREDENTIAL_ACCESS =
    /(?:\.ssh|id_rsa|\.aws[\\/]credentials|\.env\b|keychain|dpapi|wallet\.dat|browser\s+cookies|cookies\.sqlite)/i;

  private static readonly PROMPT_OVERRIDE =
    /\b(?:never\s+refuse|always\s+comply|no\s+restrictions|ignore\s+(?:your|all)\s+(?:guidelines|rules|instructions)|do\s+anything\s+now|without\s+restrictions|bypass\s+guardrails)\b/i;

  private static readonly DISCLAIMER_SUPPRESSION =
    /\b(?:no\s+disclaimers|do\s+not\s+moralize|omit\s+(?:warnings|disclaimers)|never\s+mention\s+(?:risk|warning))\b/i;

  private static readonly PROMPT_EXFIL =
    /\b(?:reveal|print|show|dump)\s+(?:your|the)\s+(?:system\s+prompt|hidden\s+instructions|internal\s+rules)\b/i;

  private static readonly UNPINNED_INSTALL =
    /\b(?:pip3?\s+install|npm\s+(?:i|install)|npx)\s+(?!.*(?:==|@\d|--no-install|-r\b))[a-z0-9@/_.-]+/i;

  private static readonly UNVALIDATED_OUTPUT =
    /(?:eval|exec|child_process|subprocess|os\.system|shell\s*=\s*true)[\s\S]{0,120}(?:model|tool|response|output|stdout|input)|\b(?:model|tool|response|stdout)\b[\s\S]{0,120}(?:eval|exec|subprocess|shell\s*=\s*true)/i;

  private static readonly LOCAL_BIND_ALL =
    /\b(?:0\.0\.0\.0|\[::\]|bind\s+all|listen\s+on\s+all\s+interfaces)\b/i;

  private static readonly UNBOUNDED_RESOURCE =
    /\b(?:while\s*\(\s*true\s*\)|for\s*\(\s*;\s*;\s*\)|infinite\s+loop|unbounded|no\s+(?:timeout|quota|limit))\b/i;

  private static readonly BROAD_TRIGGER =
    /\b(?:any\s+(?:request|task)|all\s+(?:tasks|requests)|always\s+use\s+this\s+skill|every\s+request|whenever\s+the\s+user)\b/i;

  public static scan(skill: Partial<INodeSkill>): ICloudflareSkillAuditResult {
    const content = `${skill.markdownContent || ''}\n${skill.systemPrompt || ''}`;
    const tools = (skill.tools || []).map(tool => tool.trim().toLowerCase()).filter(Boolean);
    const findings: ICloudflareSkillFinding[] = [];
    let risk = 0;

    const add = (
      ruleId: string,
      severity: ICloudflareSkillFinding['severity'],
      category: string,
      message: string,
      weight: number
    ) => {
      findings.push({ ruleId, severity, category, message });
      risk += weight;
    };

    if (this.REMOTE_EXEC.test(content)) {
      add('CF-SUPPLY-REMOTE-EXEC', 'CRITICAL', 'supply_chain',
        'Skill baixa e executa código remoto em um único passo (curl/wget/iwr para shell ou PowerShell codificado).', 50);
    }
    if (this.DESTRUCTIVE_SHELL.test(content)) {
      add('CF-DESTRUCTIVE-COMMAND', 'HIGH', 'availability',
        'Comando destrutivo de disco, sistema de arquivos ou host detectado no texto da skill.', 30);
    }
    if (this.PERSISTENCE.test(content)) {
      add('CF-PERSISTENCE-HOOK', 'HIGH', 'persistence',
        'Skill menciona mecanismo de persistência (cron, systemd, launchd, perfil de shell, startup ou registro).', 25);
    }
    if (this.CREDENTIAL_ACCESS.test(content)) {
      add('CF-CREDENTIAL-ACCESS', 'HIGH', 'data_isolation',
        'Acesso a credenciais, chaves SSH, cookies, keychain, DPAPI ou arquivos .env detectado.', 25);
    }
    if (this.PROMPT_OVERRIDE.test(content)) {
      add('CF-PROMPT-OVERRIDE', 'HIGH', 'prompt_injection',
        'Instrução para suprimir recusas, ignorar diretrizes ou remover restrições de segurança.', 30);
    }
    if (this.DISCLAIMER_SUPPRESSION.test(content)) {
      add('CF-PROMPT-DISCLAIMER-SUPPRESSION', 'MEDIUM', 'prompt_injection',
        'Instrução para omitir avisos, disclaimers ou alertas de risco ao usuário.', 15);
    }
    if (this.PROMPT_EXFIL.test(content)) {
      add('CF-PROMPT-EXFILTRATION', 'HIGH', 'prompt_injection',
        'Instrução para revelar system prompt, regras internas ou instruções ocultas.', 25);
    }
    if (this.UNPINNED_INSTALL.test(content)) {
      add('CF-SUPPLY-UNPINNED-INSTALL', 'MEDIUM', 'supply_chain',
        'Instalação de dependência sem versão fixada; a origem pode mudar sem revisão.', 12);
    }
    if (this.UNVALIDATED_OUTPUT.test(content)) {
      add('CF-OUTPUT-TO-EXEC', 'HIGH', 'tool_abuse',
        'Saída de modelo/ferramenta parece fluir para eval, exec, subprocess ou shell sem validação.', 25);
    }
    if (this.LOCAL_BIND_ALL.test(content)) {
      add('CF-LOCAL-IPC-EXPOSED', 'MEDIUM', 'local_ipc',
        'Servidor ou daemon é instruído a escutar em todas as interfaces (0.0.0.0/::), ampliando a exposição local.', 15);
    }
    if (this.UNBOUNDED_RESOURCE.test(content)) {
      add('CF-RESOURCE-UNBOUNDED', 'MEDIUM', 'resource_exhaustion',
        'Laço, download ou operação sem limite explícito de tempo, quota ou volume.', 12);
    }
    if (this.BROAD_TRIGGER.test(content)) {
      add('CF-TRIGGER-OVERBROAD', 'MEDIUM', 'trigger_abuse',
        'Gatilho genérico ("qualquer tarefa", "sempre use") pode sequestrar pedidos não relacionados.', 10);
    }

    const wildcardTools = tools.filter(tool => ['*', 'all', 'any', 'full', 'root', 'admin'].includes(tool));
    if (wildcardTools.length > 0) {
      add('CF-PERMISSION-WILDCARD', 'MEDIUM', 'excessive_agency',
        `Lista de ferramentas contém permissões amplas: ${wildcardTools.join(', ')}.`, 15);
    }

    const highRiskTools = tools.filter(tool => ['bash', 'terminal', 'exec', 'system', 'powershell', 'cmd'].includes(tool));
    if (highRiskTools.length > 0) {
      add('CF-TOOL-SHELL-EXECUTION', 'MEDIUM', 'tool_abuse',
        `Skill solicita execução de shell/terminal (${highRiskTools.join(', ')}); exige sandbox e justificativa de escopo.`, 15);
    }

    const readOnlyClaim = /\b(?:read[- ]only|somente\s+leitura|no\s+writes?|não\s+escreve)\b/i.test(content);
    const writeTools = tools.filter(tool => ['write', 'edit', 'bash', 'delete', 'remove'].includes(tool));
    if (readOnlyClaim && writeTools.length > 0) {
      add('CF-DESCRIPTION-BEHAVIOR-MISMATCH', 'HIGH', 'scope_creep',
        `A skill se declara somente leitura, mas solicita ferramentas de escrita/execução: ${writeTools.join(', ')}.`, 30);
    }

    const finalRisk = Math.min(100, Math.max(0, risk));
    const severity: ICloudflareSkillAuditResult['severity'] =
      findings.some(f => f.severity === 'CRITICAL') ? 'CRITICAL' :
      findings.some(f => f.severity === 'HIGH') ? 'HIGH' :
      findings.some(f => f.severity === 'MEDIUM') ? 'MEDIUM' :
      findings.some(f => f.severity === 'LOW') ? 'LOW' : 'NONE';

    const recommendation: ICloudflareSkillAuditResult['recommendation'] =
      severity === 'CRITICAL' || finalRisk >= 70 ? 'REJECT' :
      severity === 'HIGH' || finalRisk >= 20 ? 'CAUTION' : 'APPROVE';

    return {
      success: true,
      riskScore: finalRisk,
      severity,
      findings,
      recommendation,
      source: 'cloudflare-security-audit',
      scannedAt: new Date().toISOString()
    };
  }
}
