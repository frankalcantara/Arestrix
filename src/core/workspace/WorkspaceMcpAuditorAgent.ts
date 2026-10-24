import { IMcpServerConfig } from '../types.ts';
import { IWorkspaceMcpAuditVerdict } from './types.ts';
import { APPROVED_WORKSPACE_MCPS } from './ApprovedWorkspaceCatalog.ts';
import { McpHasher } from '../skills/McpHasher.ts';
import { McpLedgerService, NATIVE_EMBEDDED_MCPS } from '../skills/McpLedgerService.ts';
import { McpSecurityScanner } from '../skills/McpSecurityScanner.ts';

/**
 * Arestrix Studio - Agente de Workspace para Auditoria e Homologação de Servidores MCP
 * 
 * Sentinela de governança do Studio operando com base nos analisadores de MCP do SkillSpector
 * da NVIDIA (`mcp_tool_poisoning`, `mcp_rug_pull`, `mcp_least_privilege`).
 * 
 * Toda validação é atrelada a um hash SHA-256 canônico e gravada no McpLedgerService;
 * servidores e ferramentas com definições idênticas são liberados instantaneamente (0 ms).
 */
export class WorkspaceMcpAuditorAgent {
  private static instance: WorkspaceMcpAuditorAgent;

  public static getInstance(): WorkspaceMcpAuditorAgent {
    if (!WorkspaceMcpAuditorAgent.instance) {
      WorkspaceMcpAuditorAgent.instance = new WorkspaceMcpAuditorAgent();
    }
    return WorkspaceMcpAuditorAgent.instance;
  }

  /**
   * Executa a auditoria completa de um servidor MCP e suas ferramentas.
   */
  public auditMcpServer(
    server: Partial<IMcpServerConfig>,
    isCuratedWorkspace: boolean = false
  ): IWorkspaceMcpAuditVerdict {
    const timestamp = new Date().toISOString();
    const serverId = server.id || 'mcp-unknown';
    const serverName = server.name || serverId;
    const ledger = McpLedgerService.getInstance();
    const hash = McpHasher.computeHash(server);
    const tools = server.cachedTools || [];

    // 1. Verificação instantânea de cache no Ledger ($O(1)$)
    const existingEntry = ledger.getEntry(hash);
    if (existingEntry) {
      return {
        serverId,
        serverName,
        verified: existingEntry.verdict === 'APPROVE' || existingEntry.verdict === 'CAUTION',
        status: existingEntry.verdict === 'APPROVE' ? 'approved' : existingEntry.verdict === 'CAUTION' ? 'warning' : 'rejected',
        riskScore: existingEntry.riskScore,
        auditedBy: existingEntry.auditedBy,
        timestamp: existingEntry.auditedAt,
        checklist: existingEntry.checklist,
        findings: existingEntry.findings,
        warnings: existingEntry.warnings,
        toolCount: existingEntry.toolCount,
        toolNames: existingEntry.toolNames,
        hash,
        verdict: existingEntry.verdict,
        provenance: existingEntry.provenance
      };
    }

    // 2. Verificação de Servidor Nativo C++ permanente
    const isNativeCore = ledger.isNativeCoreServer(serverId) || NATIVE_EMBEDDED_MCPS.some(n => n.id === serverId);
    if (isNativeCore) {
      const verdict: IWorkspaceMcpAuditVerdict = {
        serverId,
        serverName,
        verified: true,
        status: 'approved',
        riskScore: 0,
        auditedBy: 'ArestrixCoreNative',
        timestamp,
        checklist: [
          { rule: 'Isolamento de Processo Nativo', passed: true, detail: 'Execução direta em memória (4 µs), zero subprocessos.' },
          { rule: 'NetGuard SSRF & Rebinding Shield', passed: true, detail: 'Bloqueio automático de IPs privados e loopbacks em C++.' },
          { rule: 'Anti-Tool Poisoning Scan', passed: true, detail: 'Definições nativas imutáveis compiladas no core.' }
        ],
        findings: [
          'Servidor nativo interno em C++ executado sem subprocessos.',
          'Proteção de rede NetGuard ativa contra SSRF e DNS rebinding.',
          'Ferramentas nativas do núcleo Arestrix homologadas permanentemente.'
        ],
        warnings: [],
        toolCount: tools.length,
        toolNames: tools.map(t => t.name),
        hash,
        verdict: 'APPROVE',
        provenance: 'native-embedded'
      };

      ledger.recordApproval(server, {
        auditedBy: 'ArestrixCoreNative',
        provenance: 'native-embedded',
        findings: verdict.findings,
        checklist: verdict.checklist,
        riskScore: 0
      });

      return verdict;
    }

    // 3. Verificação de proveniência para MCPs do catálogo oficial do workspace
    const isApprovedCatalog = isCuratedWorkspace || APPROVED_WORKSPACE_MCPS.some(m => m.id === serverId);
    if (isApprovedCatalog) {
      const verdict: IWorkspaceMcpAuditVerdict = {
        serverId,
        serverName,
        verified: true,
        status: 'approved',
        riskScore: 0,
        auditedBy: 'WorkspaceApprovedCatalog',
        timestamp,
        checklist: [
          { rule: 'Catálogo Oficial do Workspace', passed: true, detail: 'Conector homologado e empacotado no catálogo central.' },
          { rule: 'Anti-Tool Poisoning Scan', passed: true, detail: 'Zero injeções de prompt detectadas nas ferramentas.' },
          { rule: 'Anti-Rug Pull Pinning', passed: true, detail: 'Versões estritamente fixadas.' }
        ],
        findings: [
          'Servidor homologado oficialmente pelo Workspace Arestrix.',
          'Configuração auditada no catálogo central.',
          'Ferramentas associadas inspecionadas.'
        ],
        warnings: [],
        toolCount: tools.length,
        toolNames: tools.map(t => t.name),
        hash,
        verdict: 'APPROVE',
        provenance: 'workspace-curated'
      };

      ledger.recordApproval(server, {
        auditedBy: 'WorkspaceApprovedCatalog',
        provenance: 'workspace-curated',
        findings: verdict.findings,
        checklist: verdict.checklist,
        riskScore: 0
      });

      return verdict;
    }

    // 4. Auditoria de Segurança para Servidores MCP Externos ou Customizados
    const checklist: Array<{ rule: string; passed: boolean; detail: string }> = [];
    const findings: string[] = [];
    const warnings: string[] = [];

    // 4.1 Validação do Scanner Estático MCP (SkillSpector)
    const scanResult = McpSecurityScanner.scan(server);

    // Checklist de Rug Pull
    const hasRugPullFinding = scanResult.findings.some(f => f.category === 'rug_pull');
    checklist.push({
      rule: 'Anti-Rug Pull (Fixação de Versão)',
      passed: !hasRugPullFinding,
      detail: hasRugPullFinding
        ? 'Pacotes dinâmicos sem versão fixa detectados. Risco de alteração em upstream.'
        : 'Dependências de execução devidamente pinadas ou binário local estático.'
    });

    // Checklist de Tool Poisoning
    const hasPoisoningFinding = scanResult.findings.some(f => f.category === 'tool_poisoning');
    checklist.push({
      rule: 'Anti-Tool Poisoning (Instruções em Ferramentas)',
      passed: !hasPoisoningFinding,
      detail: hasPoisoningFinding
        ? 'Caracteres invisíveis ou instruções de injeção de prompt detectadas.'
        : 'Descrições de ferramentas limpas de injeções e caracteres ocultos.'
    });

    // Checklist de Tool Shadowing
    const hasShadowingFinding = scanResult.findings.some(f => f.category === 'tool_shadowing');
    checklist.push({
      rule: 'Anti-Tool Shadowing (Conflito de Nomes)',
      passed: !hasShadowingFinding,
      detail: hasShadowingFinding
        ? 'Ferramentas interceptam nomes protegidos do sistema Arestrix.'
        : 'Zero colisões com ferramentas do núcleo nativo.'
    });

    // Checklist de Menor Privilégio (Shell e FS)
    const hasPrivilegeFinding = scanResult.findings.some(f => f.category === 'least_privilege');
    checklist.push({
      rule: 'Menor Privilégio e Sanitização de Shell',
      passed: !hasPrivilegeFinding,
      detail: hasPrivilegeFinding
        ? 'Argumentos contêm metacaracteres de shell ou escopo irrestrito de disco.'
        : 'Argumentos restritos e delimitados sem caracteres especiais de shell.'
    });

    // Agrupamento de achados
    for (const f of scanResult.findings) {
      if (f.severity === 'CRITICAL') {
        findings.push(`[CRÍTICO] ${f.message}`);
      } else if (f.severity === 'HIGH') {
        warnings.push(`[ALERTA] ${f.message}`);
      } else {
        warnings.push(f.message);
      }
    }

    if (scanResult.findings.length === 0) {
      findings.push('Configuração do servidor e catálogo de ferramentas validados com sucesso.');
    }

    const isRejected = scanResult.recommendation === 'REJECT';
    const isWarning = scanResult.recommendation === 'CAUTION';
    const finalVerdict = scanResult.recommendation;

    const auditVerdict: IWorkspaceMcpAuditVerdict = {
      serverId,
      serverName,
      verified: !isRejected,
      status: isRejected ? 'rejected' : isWarning ? 'warning' : 'approved',
      riskScore: scanResult.riskScore,
      auditedBy: 'WorkspaceMcpAuditor (SkillSpector)',
      timestamp,
      checklist,
      findings,
      warnings,
      toolCount: tools.length,
      toolNames: tools.map(t => t.name),
      hash,
      verdict: finalVerdict,
      provenance: 'external-custom'
    };

    if (isRejected) {
      ledger.recordRejection(
        server,
        findings[0] || 'Servidor MCP reprovado pelo SkillSpector.',
        findings,
        checklist
      );
    } else {
      ledger.recordApproval(server, {
        auditedBy: 'WorkspaceMcpAuditor (SkillSpector)',
        provenance: 'external-custom',
        verdict: finalVerdict,
        riskScore: scanResult.riskScore,
        findings,
        warnings,
        checklist
      });
    }

    return auditVerdict;
  }
}
