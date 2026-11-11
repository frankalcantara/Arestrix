import { BaseModal } from './BaseModal.ts';
import { Graph } from '../../core/Graph.ts';
import { EventBus } from '../../core/EventBus.ts';
import {
  ISkillRevalidationReport,
  SkillRevalidationService
} from '../../core/skills/SkillRevalidationService.ts';
import { ISkillLedgerEntry } from '../../core/skills/SkillLedgerService.ts';
import { t } from '../../i18n/index.ts';

/**
 * Arestrix Studio - Aviso de reauditoria após mudança da política de skills.
 *
 * Aberto na inicialização quando o ledger contém aprovações emitidas sob uma
 * `policyVersion` anterior. A ação principal reaudita todas as skills que o
 * Studio consegue localizar no escopo atual; as demais ficam listadas como
 * não encontradas para reauditoria manual no projeto de origem.
 */
export class SkillRevalidationModal extends BaseModal {
  private readonly graph?: Graph;
  private readonly service: SkillRevalidationService;
  private readonly bus?: EventBus;
  private stale: ISkillLedgerEntry[] = [];
  private report?: ISkillRevalidationReport;
  private state: 'idle' | 'running' | 'done' = 'idle';

  constructor(graph?: Graph, bus?: EventBus, service?: SkillRevalidationService) {
    super({
      id: 'skill-revalidation-modal',
      dialogClass: 'skill-revalidation-modal-dialog',
      title: t('skill_revalidation.title', 'Política de validação de skills atualizada'),
      subtitle: t('skill_revalidation.subtitle', 'Reauditoria necessária após mudança da política'),
      icon: '🛡️',
      minWidth: 680,
      minHeight: 420,
      resizable: true,
      draggable: true
    });
    this.graph = graph;
    this.bus = bus;
    this.service = service || new SkillRevalidationService();
  }

  /**
   * Abre o modal somente quando há entradas antigas de skills importadas.
   * Retorna `false` quando não há nada para revalidar.
   */
  public openIfNeeded(): boolean {
    if (this.isOpen) return true;
    this.stale = this.service.listStale();
    if (this.stale.length === 0) return false;
    this.report = undefined;
    this.state = 'idle';
    this.open();
    return true;
  }

  protected renderBody(): string {
    if (this.stale.length === 0) {
      return `<div style="padding: 24px;">${this.escapeHtml(t(
        'skill_revalidation.empty',
        'Nenhuma skill importada precisa de reauditoria nesta política.'
      ))}</div>`;
    }

    const rows = this.stale.map(entry => {
      const item = this.report?.items.find(candidate => candidate.entry.hash === entry.hash);
      const verdict = item?.audited ? item.verdict : (item ? 'PENDING' : undefined);
      const badge = item && !item.audited
        ? `<span style="font-size: 10px; font-weight: 700; color: #92400e; background: #fffbeb; border: 1px solid #fde68a; border-radius: 3px; padding: 2px 6px;">${this.escapeHtml(t('skill_revalidation.unresolved_badge', 'NÃO ENCONTRADA'))}</span>`
        : verdict
          ? `<span style="font-size: 10px; font-weight: 700; color: ${verdict === 'APPROVE' ? '#166534' : verdict === 'CAUTION' ? '#92400e' : '#991b1b'}; background: ${verdict === 'APPROVE' ? '#f0fdf4' : verdict === 'CAUTION' ? '#fffbeb' : '#fef2f2'}; border: 1px solid ${verdict === 'APPROVE' ? '#bbf7d0' : verdict === 'CAUTION' ? '#fde68a' : '#fecaca'}; border-radius: 3px; padding: 2px 6px;">${this.escapeHtml(verdict)}</span>`
          : `<span style="font-size: 10px; font-weight: 700; color: #475569; background: #f8fafc; border: 1px solid #e2e8f0; border-radius: 3px; padding: 2px 6px;">${this.escapeHtml(t('skill_revalidation.pending_badge', 'PENDENTE'))}</span>`;

      return `
        <div style="display: flex; align-items: center; justify-content: space-between; gap: 12px; padding: 10px 12px; border: 1px solid #e2e8f0; border-radius: 5px; background: #ffffff; margin-bottom: 8px;">
          <div style="min-width: 0;">
            <div style="font-size: 13px; font-weight: 700; color: #0f172a; overflow: hidden; text-overflow: ellipsis; white-space: nowrap;">${this.escapeHtml(entry.name || entry.skillId)}</div>
            <div style="font-size: 10px; color: #64748b; font-family: monospace; margin-top: 2px;">${this.escapeHtml(entry.skillId)} · ${this.escapeHtml((entry.policyVersion || '').slice(0, 12))}</div>
          </div>
          ${badge}
        </div>
      `;
    }).join('');

    const summary = this.report
      ? `<div class="office-callout" style="margin-top: 14px; padding: 12px 14px; border-radius: 5px; background: ${this.report.rejected > 0 || this.report.unresolved > 0 ? '#fffbeb' : '#f0fdf4'}; border: 1px solid ${this.report.rejected > 0 || this.report.unresolved > 0 ? '#fde68a' : '#bbf7d0'}; font-size: 12px; color: #334155; line-height: 1.5;">
          ${this.escapeHtml(t('skill_revalidation.result', {
            revalidated: this.report.revalidated,
            total: this.report.total,
            approved: this.report.approved,
            caution: this.report.caution,
            rejected: this.report.rejected,
            unresolved: this.report.unresolved
          }, '{{revalidated}} de {{total}} revalidadas: {{approved}} aprovadas, {{caution}} com atenção, {{rejected}} rejeitadas, {{unresolved}} não encontradas.'))}
          ${this.report.unresolved > 0 ? `<div style="margin-top: 6px;">${this.escapeHtml(t(
            'skill_revalidation.unresolved_hint',
            'As skills não encontradas precisam ser abertas no projeto de origem e auditadas pelo editor.'
          ))}</div>` : ''}
        </div>`
      : '';

    return `
      <div style="padding: 16px 20px;">
        <div class="office-callout" style="padding: 12px 14px; border-radius: 5px; background: #eff6fc; border: 1px solid #c7e0f4; font-size: 12px; color: #0f4c81; line-height: 1.5;">
          <strong>${this.escapeHtml(t('skill_revalidation.intro_title', 'A política de segurança foi atualizada.'))}</strong>
          ${this.escapeHtml(t('skill_revalidation.intro', 'As skills importadas abaixo foram aprovadas com uma política antiga. Reaudite todas para voltar a usá-las nos grafos.'))}
        </div>
        <div style="margin-top: 14px; font-size: 11px; font-weight: 700; color: #475569; text-transform: uppercase; letter-spacing: 0.04em;">
          ${this.escapeHtml(t('skill_revalidation.list_title', 'Skills importadas com política antiga ({{count}})', { count: this.stale.length }))}
        </div>
        <div style="margin-top: 8px;">${rows}</div>
        ${summary}
      </div>
    `;
  }

  protected renderFooter(): string {
    if (this.state === 'done') {
      return `
        <div class="modal-footer office-modal-footer">
          <div class="modal-footer-left"></div>
          <div class="modal-footer-right" style="display: flex; gap: 8px;">
            <button type="button" class="btn btn-primary-office" id="btn-skill-revalidation-close">${this.escapeHtml(t('skill_revalidation.btn_close', 'Fechar'))}</button>
          </div>
        </div>
      `;
    }

    if (this.state === 'running') {
      return `
        <div class="modal-footer office-modal-footer">
          <div class="modal-footer-left" style="font-size: 12px; color: #64748b;">${this.escapeHtml(t('skill_revalidation.running', 'Reauditando skills...'))}</div>
          <div class="modal-footer-right" style="display: flex; gap: 8px;">
            <button type="button" class="btn btn-primary-office" disabled>${this.escapeHtml(t('skill_revalidation.running', 'Reauditando skills...'))}</button>
          </div>
        </div>
      `;
    }

    return `
      <div class="modal-footer office-modal-footer">
        <div class="modal-footer-left"></div>
        <div class="modal-footer-right" style="display: flex; gap: 8px;">
          <button type="button" class="btn btn-secondary-office" id="btn-skill-revalidation-later">${this.escapeHtml(t('skill_revalidation.btn_later', 'Depois'))}</button>
          <button type="button" class="btn btn-primary-office" id="btn-skill-revalidation-run">${this.escapeHtml(t('skill_revalidation.btn_revalidate', 'Revalidar todas'))}</button>
        </div>
      </div>
    `;
  }

  protected bindEvents(): void {
    this.dialog.querySelector('#btn-skill-revalidation-later')?.addEventListener('click', () => this.hide());
    this.dialog.querySelector('#btn-skill-revalidation-close')?.addEventListener('click', () => this.hide());
    this.dialog.querySelector('#btn-skill-revalidation-run')?.addEventListener('click', () => {
      void this.runRevalidation();
    });
  }

  private async runRevalidation(): Promise<void> {
    if (this.state === 'running') return;
    this.state = 'running';
    this.render();
    await new Promise(resolve => setTimeout(resolve, 30));
    this.report = await this.service.revalidateAll(this.graph);
    this.state = 'done';
    this.render();
    this.bus?.emit('skills:revalidation-completed', this.report);
  }
}
