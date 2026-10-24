import { SkillHasher } from './SkillHasher.ts';
import { INodeSkill } from '../types.ts';
import { APPROVED_WORKSPACE_SKILLS } from '../workspace/ApprovedWorkspaceCatalog.ts';
import { EventBus } from '../EventBus.ts';

export type SkillAuditVerdict = 'APPROVE' | 'CAUTION' | 'REJECT' | 'PENDING';

export interface ISkillLedgerChecklistItem {
  rule: string;
  passed: boolean;
  detail: string;
}

export interface ISkillLedgerEntry {
  hash: string;
  skillId: string;
  name: string;
  version?: string;
  verdict: SkillAuditVerdict;
  riskScore: number; // 0 (100% seguro) a 100 (risco crítico)
  auditedAt: string; // ISO 8601
  auditedBy: string; // Ex: 'StudioSkillAuditor' ou 'SkillSpector-2.12.0'
  scannerVersion?: string;
  findingsCount: number;
  findings: string[];
  warnings: string[];
  checklist: ISkillLedgerChecklistItem[];
  tools: string[];
  provenance?: 'workspace-curated' | 'community' | 'custom';
}

export interface ISkillLedgerStore {
  version: string;
  updatedAt: string;
  skills: Record<string, ISkillLedgerEntry>;
}

const STORAGE_KEY = 'arestrix_skills_ledger';

/**
 * Arestrix Studio - Serviço de Ledger de Habilidades Validadas
 * 
 * Gerencia o registro determinístico (endereçado por conteúdo via SHA-256)
 * de todas as skills aprovadas, auditadas ou rejeitadas pelo Agente Auditor do Studio.
 */
export class SkillLedgerService {
  private static instance?: SkillLedgerService;
  private bus?: EventBus;
  private entries: Map<string, ISkillLedgerEntry> = new Map();

  private constructor(bus?: EventBus) {
    this.bus = bus;
    this.loadFromStorage();
    this.seedCuratedWorkspaceSkills();
  }

  public static getInstance(bus?: EventBus): SkillLedgerService {
    if (!SkillLedgerService.instance) {
      SkillLedgerService.instance = new SkillLedgerService(bus);
    } else if (bus && !SkillLedgerService.instance.bus) {
      SkillLedgerService.instance.bus = bus;
    }
    return SkillLedgerService.instance;
  }

  /**
   * Reinicia a instância (útil para suíte de testes isolados).
   */
  public static resetForTesting(bus?: EventBus): SkillLedgerService {
    SkillLedgerService.instance = new SkillLedgerService(bus);
    return SkillLedgerService.instance;
  }

  /**
   * Pré-carrega o ledger com as skills homologadas no catálogo oficial do workspace.
   */
  private seedCuratedWorkspaceSkills(): void {
    if (!Array.isArray(APPROVED_WORKSPACE_SKILLS)) return;

    for (const skill of APPROVED_WORKSPACE_SKILLS) {
      const hash = SkillHasher.computeHash(skill);
      if (!this.entries.has(hash)) {
        this.entries.set(hash, {
          hash,
          skillId: skill.id,
          name: skill.name,
          version: skill.version || '1.0.0',
          verdict: 'APPROVE',
          riskScore: 0,
          auditedAt: new Date().toISOString(),
          auditedBy: 'WorkspaceApprovedCatalog',
          scannerVersion: 'ArestrixCurated-1.5.0',
          findingsCount: 0,
          findings: ['Habilidade homologada e certificada no catálogo oficial do Arestrix.'],
          warnings: [],
          checklist: [
            { rule: 'Proveniência Oficial', passed: true, detail: 'Empacotada no catálogo padrão do Studio.' },
            { rule: 'Conformidade de Estilo', passed: true, detail: 'Respeito estrito às regras visuais e tipográficas.' }
          ],
          tools: SkillHasher.normalizeTools(skill.tools),
          provenance: 'workspace-curated'
        });
      }
    }
  }

  /**
   * Carrega dados persistidos do localStorage se disponível.
   */
  private loadFromStorage(): void {
    if (typeof localStorage === 'undefined') return;

    try {
      const raw = localStorage.getItem(STORAGE_KEY);
      if (!raw) return;

      const store: ISkillLedgerStore = JSON.parse(raw);
      if (store && store.skills && typeof store.skills === 'object') {
        for (const [hash, entry] of Object.entries(store.skills)) {
          if (hash && entry && entry.hash === hash) {
            this.entries.set(hash, entry);
          }
        }
      }
    } catch (e) {
      console.warn('[SkillLedgerService] Falha ao ler ledger do armazenamento:', e);
    }
  }

  /**
   * Salva os registros atuais no armazenamento local.
   */
  private saveToStorage(): void {
    if (typeof localStorage === 'undefined') return;

    try {
      const skillsObj: Record<string, ISkillLedgerEntry> = {};
      for (const [hash, entry] of this.entries.entries()) {
        skillsObj[hash] = entry;
      }

      const store: ISkillLedgerStore = {
        version: '1.0.0',
        updatedAt: new Date().toISOString(),
        skills: skillsObj
      };

      localStorage.setItem(STORAGE_KEY, JSON.stringify(store));
    } catch (e) {
      console.warn('[SkillLedgerService] Falha ao persistir ledger:', e);
    }
  }

  /**
   * Consulta a entrada de auditoria associada a um determinado hash.
   */
  public getEntry(hash: string): ISkillLedgerEntry | undefined {
    if (!hash) return undefined;
    return this.entries.get(hash.toLowerCase());
  }

  /**
   * Verifica se a skill correspondente ao hash está liberada para uso no grafo.
   * Retorna true apenas para vereditos 'APPROVE' ou 'CAUTION'.
   */
  public isApproved(hash: string): boolean {
    const entry = this.getEntry(hash);
    if (!entry) return false;
    return entry.verdict === 'APPROVE' || entry.verdict === 'CAUTION';
  }

  /**
   * Obtém o status de aprovação de um determinado hash.
   */
  public getStatus(hash: string): SkillAuditVerdict {
    const entry = this.getEntry(hash);
    return entry ? entry.verdict : 'PENDING';
  }

  /**
   * Registra ou atualiza formalmente o veredito de auditoria de uma skill.
   */
  public recordAudit(entry: ISkillLedgerEntry): void {
    if (!entry.hash) throw new Error('Entrada do ledger requer um hash SHA-256 canônico válido.');

    const cleanHash = entry.hash.toLowerCase();
    const finalEntry: ISkillLedgerEntry = {
      ...entry,
      hash: cleanHash,
      tools: SkillHasher.normalizeTools(entry.tools),
      auditedAt: entry.auditedAt || new Date().toISOString()
    };

    this.entries.set(cleanHash, finalEntry);
    this.saveToStorage();

    this.bus?.emit('skills:ledger-updated', {
      hash: cleanHash,
      skillId: entry.skillId,
      verdict: entry.verdict
    });
  }

  /**
   * Atalho para registrar aprovação de uma skill a partir de seus dados.
   */
  public recordApproval(
    skill: Partial<INodeSkill>,
    auditMeta: Partial<ISkillLedgerEntry> = {}
  ): ISkillLedgerEntry {
    const hash = SkillHasher.computeHash(skill);
    const entry: ISkillLedgerEntry = {
      hash,
      skillId: skill.id || `skill-${hash.slice(0, 8)}`,
      name: skill.name || 'Habilidade Sem Nome',
      verdict: auditMeta.verdict || 'APPROVE',
      riskScore: auditMeta.riskScore ?? 0,
      auditedAt: new Date().toISOString(),
      auditedBy: auditMeta.auditedBy || 'StudioSkillAuditor',
      scannerVersion: auditMeta.scannerVersion || 'SkillSpector-2.12.0',
      findingsCount: auditMeta.findings?.length || 0,
      findings: auditMeta.findings || ['Aprovada pelo Agente Auditor do Studio.'],
      warnings: auditMeta.warnings || [],
      checklist: auditMeta.checklist || [
        { rule: 'SkillSpector Static Scan', passed: true, detail: 'Zero vulnerabilidades críticas detectadas.' },
        { rule: 'Filtro Anti-Esteganografia', passed: true, detail: 'Texto limpo sem caracteres ocultos.' }
      ],
      tools: SkillHasher.normalizeTools(skill.tools),
      provenance: auditMeta.provenance || 'custom'
    };

    this.recordAudit(entry);
    return entry;
  }

  /**
   * Atalho para registrar rejeição de uma skill.
   */
  public recordRejection(
    skill: Partial<INodeSkill>,
    reason: string,
    findings: string[] = [],
    checklist: ISkillLedgerChecklistItem[] = []
  ): ISkillLedgerEntry {
    const hash = SkillHasher.computeHash(skill);
    const entry: ISkillLedgerEntry = {
      hash,
      skillId: skill.id || `skill-${hash.slice(0, 8)}`,
      name: skill.name || 'Habilidade Sem Nome',
      verdict: 'REJECT',
      riskScore: 90,
      auditedAt: new Date().toISOString(),
      auditedBy: 'StudioSkillAuditor',
      scannerVersion: 'SkillSpector-2.12.0',
      findingsCount: findings.length + 1,
      findings: [reason, ...findings],
      warnings: ['Uso bloqueado no grafo de execução.'],
      checklist: checklist.length > 0 ? checklist : [
        { rule: 'Auditoria de Segurança', passed: false, detail: reason }
      ],
      tools: SkillHasher.normalizeTools(skill.tools),
      provenance: 'custom'
    };

    this.recordAudit(entry);
    return entry;
  }

  /**
   * Remove uma entrada específica do ledger.
   */
  public removeEntry(hash: string): boolean {
    const deleted = this.entries.delete(hash.toLowerCase());
    if (deleted) {
      this.saveToStorage();
      this.bus?.emit('skills:ledger-updated', { hash: hash.toLowerCase(), verdict: 'PENDING' });
    }
    return deleted;
  }

  /**
   * Retorna todas as entradas ativas no ledger.
   */
  public getAllEntries(): ISkillLedgerEntry[] {
    return Array.from(this.entries.values());
  }

  /**
   * Limpa todo o ledger (útil para testes unitários).
   */
  public clear(): void {
    this.entries.clear();
    this.saveToStorage();
  }
}
