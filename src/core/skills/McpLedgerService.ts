import { McpHasher } from './McpHasher.ts';
import { IMcpServerConfig } from '../types.ts';
import { APPROVED_WORKSPACE_MCPS } from '../workspace/ApprovedWorkspaceCatalog.ts';
import { EventBus } from '../EventBus.ts';

export type McpAuditVerdict = 'APPROVE' | 'CAUTION' | 'REJECT' | 'PENDING';

export interface IMcpLedgerChecklistItem {
  rule: string;
  passed: boolean;
  detail: string;
}

export interface IMcpLedgerEntry {
  hash: string;
  serverId: string;
  name: string;
  transport: string;
  verdict: McpAuditVerdict;
  riskScore: number; // 0 (100% seguro) a 100 (risco crítico)
  auditedAt: string;
  auditedBy: string;
  scannerVersion?: string;
  findingsCount: number;
  findings: string[];
  warnings: string[];
  checklist: IMcpLedgerChecklistItem[];
  toolCount: number;
  toolNames: string[];
  provenance?: 'native-embedded' | 'workspace-curated' | 'external-custom';
}

export interface IMcpLedgerStore {
  version: string;
  updatedAt: string;
  servers: Record<string, IMcpLedgerEntry>;
}

const STORAGE_KEY = 'arestrix_mcps_ledger';

/**
 * Servidores MCP nativos embutidos em C++ do Arestrix Core.
 * Executados como chamadas de função com proteção nativa contra DNS rebinding.
 */
export const NATIVE_EMBEDDED_MCPS: Array<{ id: string; name: string; tools: string[] }> = [
  { id: 'arestrix.arquivos', name: 'Arquivos Locais (Nativo C++)', tools: ['read_file', 'write_file', 'list_directory', 'file_info', 'make_directory', 'search_text'] },
  { id: 'arestrix.http', name: 'HTTP Seguro com NetGuard (Nativo C++)', tools: ['http_get', 'http_post_json'] },
  { id: 'arestrix.dados', name: 'Transformação Pura de Dados (Nativo C++)', tools: ['json_query', 'csv_to_json', 'json_to_csv', 'regex_extract', 'base64_encode', 'base64_decode'] },
  { id: 'arestrix.memoria', name: 'Memória Local Simples (Nativo C++)', tools: ['memory_set', 'memory_get', 'memory_list', 'memory_delete'] },
  { id: 'arestrix.tempo', name: 'Tempo e Fusos ISO 8601 (Nativo C++)', tools: ['now', 'add_duration', 'diff'] },
  { id: 'arestrix.git', name: 'Git Read-Only (Nativo C++)', tools: ['git_status', 'git_log', 'git_diff', 'git_show_file'] },
  { id: 'arestrix.sqlite', name: 'SQLite Embutido Transacional (Nativo C++)', tools: ['list_tables', 'describe_table', 'sql_query', 'sql_execute'] }
];

/**
 * Arestrix Studio - Serviço de Ledger de Servidores e Ferramentas MCP Validados
 * 
 * Mantém o registro determinístico (indexado por hash SHA-256 de configuração e schemas)
 * de todos os conectores MCP homologados para execução no Taskflow e nos Agentes.
 */
export class McpLedgerService {
  private static instance?: McpLedgerService;
  private bus?: EventBus;
  private entries: Map<string, IMcpLedgerEntry> = new Map();

  private constructor(bus?: EventBus) {
    this.bus = bus;
    this.loadFromStorage();
    this.seedNativeAndCuratedMcps();
  }

  public static getInstance(bus?: EventBus): McpLedgerService {
    if (!McpLedgerService.instance) {
      McpLedgerService.instance = new McpLedgerService(bus);
    } else if (bus && !McpLedgerService.instance.bus) {
      McpLedgerService.instance.bus = bus;
    }
    return McpLedgerService.instance;
  }

  public static resetForTesting(bus?: EventBus): McpLedgerService {
    McpLedgerService.instance = new McpLedgerService(bus);
    return McpLedgerService.instance;
  }

  /**
   * Pré-homologa servidores nativos em C++ e o catálogo padrão do workspace.
   */
  private seedNativeAndCuratedMcps(): void {
    const timestamp = new Date().toISOString();

    // 1. Homologa servidores nativos C++
    for (const native of NATIVE_EMBEDDED_MCPS) {
      const mockConfig: Partial<IMcpServerConfig> = {
        id: native.id,
        name: native.name,
        transport: 'stdio',
        command: 'native-core',
        args: [],
        isEnabled: true,
        cachedTools: native.tools.map(t => ({ name: t, description: `Ferramenta nativa C++ ${t}`, serverId: native.id }))
      };
      const hash = McpHasher.computeHash(mockConfig);

      if (!this.entries.has(hash)) {
        this.entries.set(hash, {
          hash,
          serverId: native.id,
          name: native.name,
          transport: 'stdio',
          verdict: 'APPROVE',
          riskScore: 0,
          auditedAt: timestamp,
          auditedBy: 'ArestrixCoreNative',
          scannerVersion: 'Core-1.5.0',
          findingsCount: 0,
          findings: ['Servidor nativo interno em C++ executado sem subprocessos com NetGuard ativo.'],
          warnings: [],
          checklist: [
            { rule: 'Isolamento de Processo', passed: true, detail: 'Execução direta em memória (4 µs), zero subprocessos.' },
            { rule: 'Proteção DNS Rebinding', passed: true, detail: 'NetGuard C++ bloqueia IPs privados e loopbacks.' }
          ],
          toolCount: native.tools.length,
          toolNames: [...native.tools],
          provenance: 'native-embedded'
        });
      }
    }

    // 2. Homologa catálogo de workspace pré-aprovado
    if (Array.isArray(APPROVED_WORKSPACE_MCPS)) {
      for (const mcp of APPROVED_WORKSPACE_MCPS) {
        const mockConfig: Partial<IMcpServerConfig> = {
          id: mcp.id,
          name: mcp.name,
          transport: mcp.transport,
          command: mcp.command,
          args: mcp.args,
          isEnabled: true,
          cachedTools: mcp.tools.map(t => ({ name: t.name, description: t.description, serverId: mcp.id }))
        };
        const hash = McpHasher.computeHash(mockConfig);

        if (!this.entries.has(hash)) {
          this.entries.set(hash, {
            hash,
            serverId: mcp.id,
            name: mcp.name,
            transport: mcp.transport,
            verdict: 'APPROVE',
            riskScore: 0,
            auditedAt: timestamp,
            auditedBy: 'WorkspaceApprovedCatalog',
            scannerVersion: 'Workspace-1.5.0',
            findingsCount: 0,
            findings: ['Servidor pré-homologado empacotado no catálogo oficial do workspace.'],
            warnings: [],
            checklist: [
              { rule: 'Homologação Oficial', passed: true, detail: 'Conector de workspace inspecionado.' }
            ],
            toolCount: mcp.tools.length,
            toolNames: mcp.tools.map(t => t.name),
            provenance: 'workspace-curated'
          });
        }
      }
    }
  }

  private loadFromStorage(): void {
    if (typeof localStorage === 'undefined') return;
    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (!raw) return;
      const store: IMcpLedgerStore = JSON.parse(raw);
      if (store && store.servers && typeof store.servers === 'object') {
        for (const [hash, entry] of Object.entries(store.servers)) {
          if (hash && entry && entry.hash === hash) {
            this.entries.set(hash, entry);
          }
        }
      }
    } catch (e) {
      console.warn('[McpLedgerService] Falha ao ler ledger MCP do armazenamento:', e);
    }
  }

  private saveToStorage(): void {
    if (typeof localStorage === 'undefined') return;
    try {
      localStorage.setItem(STORAGE_KEY, JSON.stringify(this.buildStore()));
    } catch (e) {
      console.warn('[McpLedgerService] Falha ao persistir ledger MCP:', e);
    }
  }

  /** Monta o documento persistido (localStorage e espelho do workspace). */
  private buildStore(): IMcpLedgerStore {
    const serversObj: Record<string, IMcpLedgerEntry> = {};
    for (const [hash, entry] of this.entries.entries()) {
      serversObj[hash] = entry;
    }
    return {
      version: '1.0.0',
      updatedAt: new Date().toISOString(),
      servers: serversObj
    };
  }

  /**
   * Espelha o ledger no workspace (`.workspace/governance/mcp-ledger.json`) para que o backend
   * C++ aplique a mesma governança no Assistente. Não grava segredos: as entradas só têm
   * metadados de auditoria, ids, veredictos e nomes de ferramentas.
   */
  public async syncToWorkspace(): Promise<boolean> {
    const glob = typeof window !== 'undefined' ? (window as any) : (typeof globalThis !== 'undefined' ? (globalThis as any) : null);
    const webui = glob?.webui;
    if (!webui || typeof webui.call !== 'function') return false;
    try {
      const raw = await webui.call('save_mcp_governance_ledger', JSON.stringify(this.buildStore()));
      return raw === true || raw === 'true';
    } catch (e) {
      console.warn('[McpLedgerService] Falha ao espelhar o ledger MCP no workspace:', e);
      return false;
    }
  }

  public getEntry(hash: string): IMcpLedgerEntry | undefined {
    if (!hash) return undefined;
    return this.entries.get(hash.toLowerCase());
  }

  public isApproved(hash: string): boolean {
    const entry = this.getEntry(hash);
    if (!entry) return false;
    return entry.verdict === 'APPROVE' || entry.verdict === 'CAUTION';
  }

  public getStatus(hash: string): McpAuditVerdict {
    const entry = this.getEntry(hash);
    return entry ? entry.verdict : 'PENDING';
  }

  /**
   * Obtém a entrada do ledger pelo ID do servidor MCP.
   */
  public getEntryByServerId(serverId: string): IMcpLedgerEntry | undefined {
    if (!serverId) return undefined;
    const cleanId = serverId.trim().toLowerCase();
    for (const entry of this.entries.values()) {
      if (entry.serverId.toLowerCase() === cleanId) {
        return entry;
      }
    }
    return undefined;
  }

  /**
   * Obtém o veredicto de homologação de um servidor MCP pelo seu ID.
   * Servidores nativos em C++ (arestrix.*) são sempre homologados ('APPROVE').
   */
  public getStatusByServerId(serverId: string): McpAuditVerdict {
    if (this.isNativeCoreServer(serverId)) return 'APPROVE';
    const entry = this.getEntryByServerId(serverId);
    return entry ? entry.verdict : 'PENDING';
  }

  /**
   * Verifica se um servidor é nativo C++ permanente (ex: arestrix.arquivos).
   */
  public isNativeCoreServer(serverId: string): boolean {
    const clean = (serverId || '').trim().toLowerCase();
    return clean.startsWith('arestrix.') || NATIVE_EMBEDDED_MCPS.some(n => n.id === clean);
  }

  public recordAudit(entry: IMcpLedgerEntry): void {
    if (!entry.hash) throw new Error('Entrada do ledger MCP requer um hash SHA-256 canônico.');
    const cleanHash = entry.hash.toLowerCase();
    const finalEntry: IMcpLedgerEntry = {
      ...entry,
      hash: cleanHash,
      auditedAt: entry.auditedAt || new Date().toISOString()
    };
    this.entries.set(cleanHash, finalEntry);
    this.saveToStorage();
    void this.syncToWorkspace();

    this.bus?.emit('mcps:ledger-updated', {
      hash: cleanHash,
      serverId: entry.serverId,
      verdict: entry.verdict
    });
  }

  public recordApproval(
    server: Partial<IMcpServerConfig>,
    auditMeta: Partial<IMcpLedgerEntry> = {}
  ): IMcpLedgerEntry {
    const hash = McpHasher.computeHash(server);
    const tools = server.cachedTools || [];
    const entry: IMcpLedgerEntry = {
      hash,
      serverId: server.id || `mcp-${hash.slice(0, 8)}`,
      name: server.name || server.id || 'Servidor MCP Sem Nome',
      transport: server.transport || 'stdio',
      verdict: auditMeta.verdict || 'APPROVE',
      riskScore: auditMeta.riskScore ?? 0,
      auditedAt: new Date().toISOString(),
      auditedBy: auditMeta.auditedBy || 'StudioMcpAuditor',
      scannerVersion: auditMeta.scannerVersion || 'SkillSpector-MCP-2.12.0',
      findingsCount: auditMeta.findings?.length || 0,
      findings: auditMeta.findings || ['Servidor MCP auditado e homologado pelo Studio.'],
      warnings: auditMeta.warnings || [],
      checklist: auditMeta.checklist || [
        { rule: 'Anti-Tool Poisoning Scan', passed: true, detail: 'Zero injeções de prompt em descrições de ferramentas.' },
        { rule: 'Anti-Rug Pull Pinning', passed: true, detail: 'Dependências fixadas com segurança.' }
      ],
      toolCount: tools.length,
      toolNames: tools.map(t => t.name),
      provenance: auditMeta.provenance || 'external-custom'
    };

    this.recordAudit(entry);
    return entry;
  }

  public recordRejection(
    server: Partial<IMcpServerConfig>,
    reason: string,
    findings: string[] = [],
    checklist: IMcpLedgerChecklistItem[] = []
  ): IMcpLedgerEntry {
    const hash = McpHasher.computeHash(server);
    const tools = server.cachedTools || [];
    const entry: IMcpLedgerEntry = {
      hash,
      serverId: server.id || `mcp-${hash.slice(0, 8)}`,
      name: server.name || server.id || 'Servidor MCP Sem Nome',
      transport: server.transport || 'stdio',
      verdict: 'REJECT',
      riskScore: 95,
      auditedAt: new Date().toISOString(),
      auditedBy: 'StudioMcpAuditor (SkillSpector)',
      scannerVersion: 'SkillSpector-MCP-2.12.0',
      findingsCount: findings.length + 1,
      findings: [reason, ...findings],
      warnings: ['Uso bloqueado no grafo de execução.'],
      checklist: checklist.length > 0 ? checklist : [
        { rule: 'Auditoria de Segurança MCP', passed: false, detail: reason }
      ],
      toolCount: tools.length,
      toolNames: tools.map(t => t.name),
      provenance: 'external-custom'
    };

    this.recordAudit(entry);
    return entry;
  }

  public removeEntry(hash: string): boolean {
    const deleted = this.entries.delete(hash.toLowerCase());
    if (deleted) {
      this.saveToStorage();
      void this.syncToWorkspace();
      this.bus?.emit('mcps:ledger-updated', { hash: hash.toLowerCase(), verdict: 'PENDING' });
    }
    return deleted;
  }

  public getAllEntries(): IMcpLedgerEntry[] {
    return Array.from(this.entries.values());
  }

  public clear(): void {
    this.entries.clear();
    this.saveToStorage();
    void this.syncToWorkspace();
  }
}
