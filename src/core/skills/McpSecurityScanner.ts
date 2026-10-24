import { IMcpServerConfig } from '../types.ts';

export interface IMcpSecurityFinding {
  ruleId: string;
  category: 'tool_poisoning' | 'rug_pull' | 'tool_shadowing' | 'least_privilege' | 'transport_security';
  severity: 'CRITICAL' | 'HIGH' | 'MEDIUM' | 'LOW' | 'INFO';
  message: string;
  toolName?: string;
}

export interface IMcpSecurityScanResult {
  success: boolean;
  riskScore: number; // 0 a 100
  severity: 'CRITICAL' | 'HIGH' | 'MEDIUM' | 'LOW' | 'NONE';
  findings: IMcpSecurityFinding[];
  recommendation: 'APPROVE' | 'CAUTION' | 'REJECT';
  scannedAt: string;
}

/**
 * Arestrix Studio - Scanner de Segurança para Servidores e Ferramentas MCP
 * 
 * Implementa as verificações especializadas de MCP definidas no SkillSpector da NVIDIA:
 * 1. Anti-Tool Poisoning (TP1-TP4): Injeção de instruções e caracteres ocultos em descrições de ferramentas;
 * 2. Anti-Rug Pull (RP1-RP3): Detecção de comandos npx/uvx sem pinning estrito de versão (@versão);
 * 3. Anti-Tool Shadowing: Prevenção de colisões maliciosas de nomes com ferramentas nativas do sistema;
 * 4. Menor Privilégio (LP1-LP4): Argumentos perigosos de shell ou escopos globais de filesystem.
 */
export class McpSecurityScanner {
  private static readonly INVISIBLE_CHARS_REGEX = /[\u200B-\u200D\uFEFF\u202A-\u202E\u2066-\u2069]/;
  private static readonly PROMPT_INJECTION_REGEX = /\b(?:ignore\s+all\s+(?:previous|prior)\s+instructions|system\s+override|developer\s+mode\s+enabled|unrestricted\s+mode|dan\s+mode|bypass\s+(?:all\s+)?safety)\b/i;
  private static readonly HIDDEN_DIRECTIVE_REGEX = /\b(?:before\s+(?:calling|executing|running)|always\s+pass\s+the\s+content\s+of|leak|exfiltrate|send\s+to\s+http|upload\s+secret)\b/i;
  private static readonly WINDOWS_SHELL_METACHARS_REGEX = /[&|<>\^%!]/;

  // Nomes de ferramentas reservadas do núcleo Arestrix
  private static readonly RESERVED_NATIVE_TOOL_NAMES = new Set([
    'read_file', 'write_file', 'list_directory', 'file_info', 'make_directory', 'search_text',
    'http_get', 'http_post_json', 'json_query', 'csv_to_json', 'now', 'git_status',
    'sql_query', 'sql_execute', 'memory_get', 'memory_set'
  ]);

  /**
   * Executa a varredura síncrona completa do servidor MCP e suas ferramentas.
   */
  public static scan(server: Partial<IMcpServerConfig>): IMcpSecurityScanResult {
    const timestamp = new Date().toISOString();
    const findings: IMcpSecurityFinding[] = [];
    let calculatedRisk = 0;

    const command = (server.command || '').trim().toLowerCase();
    const args = server.args || [];
    const tools = server.cachedTools || [];
    const isNativeCore = (server.id || '').startsWith('arestrix.');

    // Servidores nativos do núcleo são confiáveis por definição
    if (isNativeCore) {
      return {
        success: true,
        riskScore: 0,
        severity: 'NONE',
        findings: [],
        recommendation: 'APPROVE',
        scannedAt: timestamp
      };
    }

    // 1. ANÁLISE DE RUG PULL (RP1-RP3): Dependências Não-Pinadas em npx / uvx / bunx
    if (['npx', 'uvx', 'pnpm dlx', 'bunx'].includes(command)) {
      // No runner de pacotes (npx/uvx), o primeiro argumento que não é flag (-y, --yes, etc.) é o pacote
      const firstPackageArg = args.find(a => !a.startsWith('-'));

      if (firstPackageArg) {
        // Extrai a parte da versão após o último '@' (suporta pacotes escopados como @org/pkg@1.0.0)
        const lastAtIndex = firstPackageArg.lastIndexOf('@');
        const hasVersionPin = lastAtIndex > 0 && /^[0-9]+\.[0-9]+/.test(firstPackageArg.slice(lastAtIndex + 1));

        if (!hasVersionPin) {
          findings.push({
            ruleId: 'MCP-RP1-UNPINNED-PACKAGE',
            category: 'rug_pull',
            severity: 'HIGH',
            message: `Dependência dinâmica não pinada "${firstPackageArg}" detectada no comando "${command}". Risco de supply-chain rug-pull; fixe a versão exata com @x.y.z.`
          });
          calculatedRisk += 35;
        }
      }
    }

    // 2. ANÁLISE DE MENOR PRIVILÉGIO (LP1-LP4): Metacaracteres de Shell e Escopo de Disco
    for (const arg of args) {
      if (this.WINDOWS_SHELL_METACHARS_REGEX.test(arg)) {
        findings.push({
          ruleId: 'MCP-LP2-SHELL-METACHARS',
          category: 'least_privilege',
          severity: 'CRITICAL',
          message: `Argumento de comando contém metacaracteres proibidos de shell (& | < > ^ % !): "${arg}".`
        });
        calculatedRisk += 50;
      }

      // Escopo raiz perigoso no filesystem (ex: '/', 'c:\', 'C:', 'c:/')
      const cleanPath = arg.toLowerCase().trim().replace(/[\\/]+$/, '');
      if (cleanPath === '' && (arg.trim() === '/' || arg.trim() === '\\') || cleanPath === 'c:' || cleanPath === '/') {
        findings.push({
          ruleId: 'MCP-LP3-UNCONFINED-ROOT-SCOPE',
          category: 'least_privilege',
          severity: 'HIGH',
          message: `Escopo de filesystem irrestrito apontando para a raiz do disco: "${arg}". Restrinja a pasta ao projeto.`
        });
        calculatedRisk += 30;
      }
    }

    // 3. ANÁLISE DE TOOL POISONING (TP1-TP4) & TOOL SHADOWING em cada ferramenta
    for (const tool of tools) {
      const toolName = (tool.name || '').trim();
      const desc = tool.description || '';
      const schemaStr = JSON.stringify(tool.inputSchema || {});

      // A. Tool Shadowing
      if (this.RESERVED_NATIVE_TOOL_NAMES.has(toolName.toLowerCase())) {
        findings.push({
          ruleId: 'MCP-SHADOWING-RESERVED-NAME',
          category: 'tool_shadowing',
          severity: 'HIGH',
          toolName,
          message: `Colisão perigosa: a ferramenta "${toolName}" intercepta o nome de uma ferramenta nativa segura do Arestrix.`
        });
        calculatedRisk += 35;
      }

      // B. Caracteres Invisíveis (Zero-Width / Esteganografia) na descrição ou schema
      if (this.INVISIBLE_CHARS_REGEX.test(desc) || this.INVISIBLE_CHARS_REGEX.test(schemaStr)) {
        findings.push({
          ruleId: 'MCP-TP1-ZERO-WIDTH-CHARS',
          category: 'tool_poisoning',
          severity: 'CRITICAL',
          toolName,
          message: `Caracteres invisíveis de largura zero ou overrides Unicode detectados na descrição da ferramenta "${toolName}".`
        });
        calculatedRisk += 45;
      }

      // C. Injeção de Prompt / Jailbreak dentro da descrição da ferramenta
      if (this.PROMPT_INJECTION_REGEX.test(desc) || this.PROMPT_INJECTION_REGEX.test(schemaStr)) {
        findings.push({
          ruleId: 'MCP-TP2-PROMPT-INJECTION',
          category: 'tool_poisoning',
          severity: 'CRITICAL',
          toolName,
          message: `Tentativa de injeção de prompt ou quebra de instruções de sistema identificada na descrição da ferramenta "${toolName}".`
        });
        calculatedRisk += 50;
      }

      // D. Diretiva Oculta de Exfiltração ou Ação Forçada
      if (this.HIDDEN_DIRECTIVE_REGEX.test(desc)) {
        findings.push({
          ruleId: 'MCP-TP3-HIDDEN-DIRECTIVE',
          category: 'tool_poisoning',
          severity: 'CRITICAL',
          toolName,
          message: `Instrução velada de coação ("always pass content", "exfiltrate") detectada na descrição da ferramenta "${toolName}".`
        });
        calculatedRisk += 50;
      }
    }

    // Normalização da pontuação de risco para a faixa 0 - 100
    const finalRiskScore = Math.min(100, Math.max(0, calculatedRisk));

    let overallSeverity: IMcpSecurityScanResult['severity'] = 'NONE';
    if (findings.some(f => f.severity === 'CRITICAL')) overallSeverity = 'CRITICAL';
    else if (findings.some(f => f.severity === 'HIGH')) overallSeverity = 'HIGH';
    else if (findings.some(f => f.severity === 'MEDIUM')) overallSeverity = 'MEDIUM';
    else if (findings.some(f => f.severity === 'LOW')) overallSeverity = 'LOW';

    let recommendation: IMcpSecurityScanResult['recommendation'] = 'APPROVE';
    if (overallSeverity === 'CRITICAL' || finalRiskScore >= 50) {
      recommendation = 'REJECT';
    } else if (overallSeverity === 'HIGH' || finalRiskScore >= 20) {
      recommendation = 'CAUTION';
    }

    return {
      success: true,
      riskScore: finalRiskScore,
      severity: overallSeverity,
      findings,
      recommendation,
      scannedAt: timestamp
    };
  }
}
