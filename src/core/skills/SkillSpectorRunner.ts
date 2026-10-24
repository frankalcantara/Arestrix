import { INodeSkill } from '../types.ts';

export interface ISkillSpectorFinding {
  ruleId: string;
  severity: 'CRITICAL' | 'HIGH' | 'MEDIUM' | 'LOW' | 'INFO';
  message: string;
  location?: { file?: string; line?: number };
  category?: string;
}

export interface ISkillSpectorScanResult {
  success: boolean;
  riskScore: number; // 0 a 100
  severity: 'CRITICAL' | 'HIGH' | 'MEDIUM' | 'LOW' | 'NONE';
  findings: ISkillSpectorFinding[];
  recommendation: 'APPROVE' | 'CAUTION' | 'REJECT';
  source: 'skillspector-cli' | 'native-builtin-scanner';
  scannedAt: string;
  rawOutput?: string;
  error?: string;
}

/**
 * Arestrix Studio - Runner de Análise de Segurança de Skills (SkillSpector)
 * 
 * Executa a inspeção estática determinística de vulnerabilidades.
 * Integra o scanner SkillSpector da NVIDIA via CLI/subprocesso e fornece
 * um analisador de regras nativo de alta velocidade com proteções contra:
 * 1. Caracteres invisíveis e esteganografia (Zero-Width Unicode / BiDi overrides);
 * 2. Injeções de prompt diretas e indiretas;
 * 3. Tentativas de exfiltração de variáveis de ambiente e segredos;
 * 4. Trifeta Letal (leitura de dados + escrita/execução + rede externa);
 * 5. Violações tipográficas de sombras em fontes SVG.
 */
export class SkillSpectorRunner {
  // Regex para caracteres invisíveis de largura zero e overrides bidirecionais
  private static readonly INVISIBLE_CHARS_REGEX = /[\u200B-\u200D\uFEFF\u202A-\u202E\u2066-\u2069]/;

  // Regex para comentários HTML ocultos contendo termos operacionais
  private static readonly SUSPICIOUS_HTML_COMMENT_REGEX = /<!--[\s\S]*?(?:ignore|system|prompt|secret|token|eval|exec|password|env|credential)[\s\S]*?-->/i;

  // Regex para tentativas de injeção de prompt e jailbreak
  private static readonly PROMPT_INJECTION_REGEX = /\b(?:ignore\s+all\s+(?:previous|prior)\s+instructions|system\s+override|developer\s+mode\s+enabled|unrestricted\s+mode|dan\s+mode|bypass\s+(?:all\s+)?safety)\b/i;

  // Regex para exfiltração de chaves e variáveis sensíveis para endpoints externos
  private static readonly EXFILTRATION_REGEX = /\b(?:send|transmit|post|upload|exfiltrate|fetch)\b[\s\S]*?\b(?:\.env|\.ssh|api[_-]?key|password|secret|token|bearer|id_rsa)\b[\s\S]*?https?:\/\//i;

  // Regex para violações de sombras em fontes (regra de projeto)
  private static readonly SHADOW_ON_FONT_REGEX = /(?:filter:\s*drop-shadow|text-shadow|feGaussianBlur|feDropShadow)[\s\S]*?<text|<text[\s\S]*?(?:filter:\s*drop-shadow|text-shadow|feGaussianBlur|feDropShadow)/i;

  /**
   * Executa a varredura síncrona completa da habilidade.
   */
  public static scanSync(skill: Partial<INodeSkill>): ISkillSpectorScanResult {
    const timestamp = new Date().toISOString();
    const content = (skill.markdownContent || '') + '\n' + (skill.systemPrompt || '');
    const tools = skill.tools || [];

    // 1. Executa o scanner de regras estáticas nativo do Arestrix
    const nativeFindings: ISkillSpectorFinding[] = [];
    let calculatedRisk = 0;

    // A. Verificação de Caracteres Invisíveis (Zero-Width / Esteganografia)
    if (this.INVISIBLE_CHARS_REGEX.test(content)) {
      nativeFindings.push({
        ruleId: 'SEC-ZERO-WIDTH-CHARS',
        category: 'obfuscation',
        severity: 'CRITICAL',
        message: 'Caracteres invisíveis de largura zero ou overrides de texto Unicode detectados no texto da skill.'
      });
      calculatedRisk += 45;
    }

    // B. Verificação de Comentários HTML Ocultos Suspeitos
    if (this.SUSPICIOUS_HTML_COMMENT_REGEX.test(content)) {
      nativeFindings.push({
        ruleId: 'SEC-HIDDEN-HTML-DIRECTIVE',
        category: 'prompt_injection',
        severity: 'HIGH',
        message: 'Comentário HTML oculto contendo palavras-chave de override ou acesso a credenciais.'
      });
      calculatedRisk += 30;
    }

    // C. Padrões de Injeção de Prompt Direta / Jailbreak
    if (this.PROMPT_INJECTION_REGEX.test(content)) {
      nativeFindings.push({
        ruleId: 'SEC-PROMPT-INJECTION-OVERRIDE',
        category: 'prompt_injection',
        severity: 'CRITICAL',
        message: 'Padrão explícito de injeção de prompt ou tentativa de desativação de salvaguardas (ex: "ignore previous instructions").'
      });
      calculatedRisk += 50;
    }

    // D. Tentativas de Exfiltração de Segredos
    if (this.EXFILTRATION_REGEX.test(content)) {
      nativeFindings.push({
        ruleId: 'SEC-DATA-EXFILTRATION-EXTERNAL',
        category: 'data_exfiltration',
        severity: 'CRITICAL',
        message: 'Diretiva potencial de exfiltração de variáveis de ambiente (.env) ou tokens para URLs externas.'
      });
      calculatedRisk += 50;
    }

    // E. Análise de Fronteira - A Trifeta Letal (Lethal Trifecta)
    const hasRead = tools.some(t => ['read', 'read_file'].includes(t.toLowerCase()));
    const hasExecOrWrite = tools.some(t => ['write', 'edit', 'bash', 'write_file'].includes(t.toLowerCase()));
    const hasNetwork = tools.some(t => ['http', 'fetch', 'http_get', 'http_post_json'].includes(t.toLowerCase()));

    if (hasRead && hasExecOrWrite && hasNetwork) {
      nativeFindings.push({
        ruleId: 'SEC-LETHAL-TRIFECTA',
        category: 'excessive_agency',
        severity: 'HIGH',
        message: 'Lethal Trifecta detectada: a skill solicita simultaneamente leitura de arquivos, execução/escrita e acesso à internet sem isolamento.'
      });
      calculatedRisk += 25;
    }

    // F. Violação de Tipografia e Sombras em Fontes (Regra de Estilo do Usuário)
    if (this.SHADOW_ON_FONT_REGEX.test(content)) {
      nativeFindings.push({
        ruleId: 'STYLE-TYPOGRAPHY-ZERO-SHADOW',
        category: 'code_quality',
        severity: 'HIGH',
        message: 'Violação da diretriz estrita do projeto: uso proibido de sombra ou brilho (drop-shadow/text-shadow/feDropShadow) em fontes.'
      });
      calculatedRisk += 20;
    }

    // Normaliza a pontuação de risco para a faixa 0 - 100
    const finalRiskScore = Math.min(100, Math.max(0, calculatedRisk));

    // Determina a severidade geral
    let overallSeverity: ISkillSpectorScanResult['severity'] = 'NONE';
    if (nativeFindings.some(f => f.severity === 'CRITICAL')) overallSeverity = 'CRITICAL';
    else if (nativeFindings.some(f => f.severity === 'HIGH')) overallSeverity = 'HIGH';
    else if (nativeFindings.some(f => f.severity === 'MEDIUM')) overallSeverity = 'MEDIUM';
    else if (nativeFindings.some(f => f.severity === 'LOW')) overallSeverity = 'LOW';

    // Determina a recomendação
    let recommendation: ISkillSpectorScanResult['recommendation'] = 'APPROVE';
    if (overallSeverity === 'CRITICAL' || finalRiskScore >= 50) {
      recommendation = 'REJECT';
    } else if (overallSeverity === 'HIGH' || finalRiskScore >= 20) {
      recommendation = 'CAUTION';
    }

    return {
      success: true,
      riskScore: finalRiskScore,
      severity: overallSeverity,
      findings: nativeFindings,
      recommendation,
      source: 'native-builtin-scanner',
      scannedAt: timestamp
    };
  }

  /**
   * Executa a varredura assíncrona da habilidade.
   */
  public static async scan(skill: Partial<INodeSkill>): Promise<ISkillSpectorScanResult> {
    return Promise.resolve(this.scanSync(skill));
  }
}
