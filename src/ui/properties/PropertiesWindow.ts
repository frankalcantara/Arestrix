import { BaseModal, type IBaseModalConfig } from '../modals/BaseModal.ts';
import { ribbonIcon } from '../ribbon/RibbonIcons.ts';
import { t } from '../../i18n/index.ts';
import type {
  IPropertiesScopeConfig,
  IPropertySection,
  PropertiesScope,
  PropertyActionHandler
} from './types.ts';

/**
 * Shell compartilhado das telas de propriedades.
 *
 * A mesma janela é usada pelos escopos Studio, Workspace e Projeto. O conteúdo
 * das seções vem de `PropertiesData` para que a casca visual e o comportamento
 * de navegação não sejam duplicados.
 */
export class PropertiesWindow extends BaseModal {
  private scopeConfig: IPropertiesScopeConfig;
  private activeSectionId: string;
  private readonly onAction?: PropertyActionHandler;
  private readonly onAfterSave?: (sectionId: string) => Promise<void> | void;
  private readonly sectionDrafts = new Map<string, Record<string, string>>();
  private dirty = false;
  private saving = false;

  constructor(
    scopeConfig: IPropertiesScopeConfig,
    onAction?: PropertyActionHandler,
    onAfterSave?: (sectionId: string) => Promise<void> | void
  ) {
    const config: IBaseModalConfig = {
      id: `properties-${scopeConfig.scope}`,
      dialogClass: 'properties-dialog',
      title: scopeConfig.title,
      subtitle: scopeConfig.subtitle,
      icon: ribbonIcon(scopeConfig.scope === 'studio' ? 'settings' : scopeConfig.scope === 'workspace' ? 'folder' : 'file', 17),
      minWidth: 760,
      minHeight: 520,
      resizable: true,
      draggable: true,
      closeOnBackdropClick: false
    };
    super(config);
    this.scopeConfig = scopeConfig;
    this.activeSectionId = scopeConfig.sections[0]?.id || '';
    this.onAction = onAction;
    this.onAfterSave = onAfterSave;
    this.dialog.classList.add('properties-dialog-shell');
  }

  public getScope(): PropertiesScope {
    return this.scopeConfig.scope;
  }

  public open(sectionId?: string): void {
    if (sectionId && this.scopeConfig.sections.some(section => section.id === sectionId)) {
      this.activeSectionId = sectionId;
    }
    super.open();
  }

  public setScopeConfig(scopeConfig: IPropertiesScopeConfig): void {
    this.scopeConfig = scopeConfig;
    this.sectionDrafts.clear();
    this.dirty = false;
    this.saving = false;
    this.config.title = scopeConfig.title;
    this.config.subtitle = scopeConfig.subtitle;
    this.config.icon = ribbonIcon(
      scopeConfig.scope === 'studio' ? 'settings' : scopeConfig.scope === 'workspace' ? 'folder' : 'file',
      17
    );
    if (!scopeConfig.sections.some(section => section.id === this.activeSectionId)) {
      this.activeSectionId = scopeConfig.sections[0]?.id || '';
    }
    if (this.isOpen) {
      this.render();
    }
  }

  protected renderBody(): string {
    const sections = this.scopeConfig.sections;
    if (sections.length === 0) {
      return `<div class="properties-empty">${this.escapeHtml(this.scopeConfig.readOnlyNote)}</div>`;
    }
    const active = this.getActiveSection();
    return `
      <div class="properties-shell" data-properties-scope="${this.scopeConfig.scope}">
        <aside class="properties-nav" aria-label="${this.escapeHtml(this.scopeConfig.title)}">
          <div class="properties-nav-heading">${this.escapeHtml(this.scopeConfig.scopeLabel)}</div>
          <div class="properties-nav-list" role="tablist">
            ${sections.map(section => `
              <button
                type="button"
                role="tab"
                class="properties-nav-item ${section.id === active.id ? 'active' : ''}"
                data-property-section="${section.id}"
                aria-selected="${section.id === active.id ? 'true' : 'false'}"
              >
                <span class="properties-nav-icon">${ribbonIcon(section.icon, 17)}</span>
                <span class="properties-nav-label">${this.escapeHtml(section.title)}</span>
              </button>
            `).join('')}
          </div>
        </aside>
        <section class="properties-content" role="tabpanel">
          ${this.renderSectionHtml(active)}
        </section>
      </div>
    `;
  }

  public hide(): void {
    if (this.dirty && !this.saving && typeof window !== 'undefined' && typeof window.confirm === 'function') {
      if (!window.confirm(t('properties_window.note.discard', 'Descartar as alterações não salvas?'))) {
        return;
      }
    }
    this.dirty = false;
    this.sectionDrafts.clear();
    super.hide();
  }

  protected renderFooter(): string {
    const editable = this.hasEditableSections();
    const note = editable
      ? t('properties_window.note.edit_hint', 'Alterações são aplicadas ao salvar.')
      : this.scopeConfig.readOnlyNote;
    const actions = editable
      ? `
          <button type="button" class="btn btn-secondary-office" data-properties-cancel>
            ${t('properties_window.cancel', 'Cancelar')}
          </button>
          <button type="button" class="btn btn-primary-office" data-properties-save disabled>
            ${t('properties_window.save', 'Salvar')}
          </button>
        `
      : `
          <button type="button" class="btn btn-secondary-office" id="btn-properties-close-${this.scopeConfig.scope}" data-properties-close>
            ${t('properties_window.close', 'Fechar')}
          </button>
        `;
    return `
      <div class="modal-footer office-modal-footer properties-footer">
        <div class="modal-footer-left properties-footer-note" data-properties-footer-note>
          ${this.escapeHtml(note)}
        </div>
        <div class="modal-footer-right">
          ${actions}
        </div>
      </div>
    `;
  }

  protected bindEvents(): void {
    super.bindEvents();
    this.dialog.querySelector('[data-properties-close]')?.addEventListener('click', () => this.hide());
    this.dialog.querySelector('[data-properties-cancel]')?.addEventListener('click', () => this.hide());
    this.dialog.querySelector('[data-properties-save]')?.addEventListener('click', () => {
      void this.saveDraft();
    });
    this.dialog.querySelectorAll<HTMLElement>('[data-property-section]').forEach(item => {
      item.addEventListener('click', () => {
        const sectionId = item.getAttribute('data-property-section');
        if (!sectionId || sectionId === this.activeSectionId) return;
        this.captureSectionDraft(this.activeSectionId);
        this.activeSectionId = sectionId;
        this.renderActiveSection();
      });
    });
    this.bindSectionActions();
    this.bindSectionInputs();
    this.bindSectionExtras(this.activeSectionId);
    this.applySectionDraft(this.activeSectionId);
    this.updateSaveButton();
  }

  private getActiveSection(): IPropertySection {
    return this.scopeConfig.sections.find(section => section.id === this.activeSectionId)
      || this.scopeConfig.sections[0];
  }

  private renderActiveSection(): void {
    const active = this.getActiveSection();
    this.dialog.querySelectorAll<HTMLElement>('[data-property-section]').forEach(item => {
      const isActive = item.getAttribute('data-property-section') === active.id;
      item.classList.toggle('active', isActive);
      item.setAttribute('aria-selected', isActive ? 'true' : 'false');
    });
    const content = this.dialog.querySelector('.properties-content');
    if (content) {
      content.innerHTML = this.renderSectionHtml(active);
      this.bindSectionActions();
      this.bindSectionInputs();
      this.bindSectionExtras(active.id);
      this.applySectionDraft(active.id);
      this.updateSaveButton();
    }
  }

  private renderSectionHtml(section: IPropertySection): string {
    return `
      <header class="properties-section-header">
        <span class="properties-section-icon">${ribbonIcon(section.icon, 20)}</span>
        <div class="properties-section-titles">
          <div class="properties-section-title-line">
            <h3 class="properties-section-title">${this.escapeHtml(section.title)}</h3>
            ${section.badge ? `<span class="badge-inherited properties-section-badge">${this.escapeHtml(section.badge)}</span>` : ''}
          </div>
          <p class="properties-section-description">${this.escapeHtml(section.description)}</p>
        </div>
      </header>
      <div class="properties-section-body" data-property-section-body="${this.escapeHtml(section.id)}">
        ${section.content}
      </div>
      ${section.actions && section.actions.length > 0 ? `
        <footer class="properties-section-actions">
          ${section.actions.map(action => `
            <button
              type="button"
              class="btn ${action.kind === 'primary' ? 'btn-primary-office' : 'btn-secondary-office'}"
              data-property-action="${this.escapeHtml(action.id)}"
            >${this.escapeHtml(action.label)}</button>
          `).join('')}
        </footer>
      ` : ''}
    `;
  }

  private bindSectionActions(): void {
    this.dialog.querySelectorAll<HTMLElement>('[data-property-action]').forEach(button => {
      button.addEventListener('click', () => {
        const actionId = button.getAttribute('data-property-action');
        if (actionId) this.onAction?.(actionId);
      });
    });
  }

  private hasEditableSections(): boolean {
    return this.scopeConfig.sections.some(section => typeof section.save === 'function');
  }

  private bindSectionInputs(): void {
    this.dialog.querySelectorAll<HTMLInputElement | HTMLSelectElement | HTMLTextAreaElement>('[data-property-input]')
      .forEach(input => {
        const handler = () => {
          const sectionId = input.closest('[data-property-section-body]')?.getAttribute('data-property-section-body');
          if (!sectionId) return;
          this.captureSectionDraft(sectionId);
          this.setDirty(true);
        };
        input.addEventListener('input', handler);
        input.addEventListener('change', handler);
      });
  }

  private bindSectionExtras(sectionId: string): void {
    const section = this.scopeConfig.sections.find(item => item.id === sectionId);
    if (!section?.bind) return;
    const root = this.dialog.querySelector<HTMLElement>(`[data-property-section-body="${sectionId}"]`);
    if (root) section.bind(root);
  }

  private readInputValue(input: HTMLInputElement | HTMLSelectElement | HTMLTextAreaElement): string {
    if (input instanceof HTMLInputElement && (input.type === 'checkbox' || input.type === 'radio')) {
      return input.checked ? 'true' : 'false';
    }
    return input.value;
  }

  private captureSectionDraft(sectionId: string): void {
    const values = this.collectValues(sectionId);
    if (Object.keys(values).length > 0) {
      this.sectionDrafts.set(sectionId, values);
    }
  }

  private applySectionDraft(sectionId: string): void {
    const draft = this.sectionDrafts.get(sectionId);
    if (!draft) return;
    const root = this.dialog.querySelector(`[data-property-section-body="${sectionId}"]`);
    root?.querySelectorAll<HTMLInputElement | HTMLSelectElement | HTMLTextAreaElement>('[data-property-input]')
      .forEach(input => {
        const key = input.getAttribute('data-property-input');
        if (!key || !(key in draft)) return;
        const value = draft[key] || '';
        if (input instanceof HTMLInputElement && (input.type === 'checkbox' || input.type === 'radio')) {
          input.checked = value === 'true';
        } else {
          input.value = value;
        }
      });
  }

  private setDirty(dirty: boolean): void {
    this.dirty = dirty;
    if (dirty) {
      this.setFooterNote(t('properties_window.note.unsaved', 'Alterações não salvas.'));
    } else {
      this.setFooterNote(t('properties_window.note.edit_hint', 'Alterações são aplicadas ao salvar.'));
    }
    this.updateSaveButton();
  }

  private updateSaveButton(): void {
    const save = this.dialog.querySelector<HTMLButtonElement>('[data-properties-save]');
    if (save) {
      save.disabled = !this.dirty || this.saving;
      save.textContent = this.saving
        ? t('properties_window.saving', 'Salvando...')
        : t('properties_window.save', 'Salvar');
    }
  }

  private setFooterNote(text: string, variant: 'neutral' | 'ok' | 'error' = 'neutral'): void {
    const note = this.dialog.querySelector<HTMLElement>('[data-properties-footer-note]');
    if (!note) return;
    note.textContent = text;
    note.classList.toggle('properties-footer-note--error', variant === 'error');
    note.classList.toggle('properties-footer-note--ok', variant === 'ok');
  }

  private collectValues(sectionId: string): Record<string, string> {
    const values: Record<string, string> = {};
    const root = this.dialog.querySelector(`[data-property-section-body="${sectionId}"]`);
    root?.querySelectorAll<HTMLInputElement | HTMLSelectElement | HTMLTextAreaElement>('[data-property-input]')
      .forEach(input => {
        const key = input.getAttribute('data-property-input');
        if (key) values[key] = this.readInputValue(input);
      });
    return values;
  }

  private async saveDraft(): Promise<void> {
    if (this.saving || !this.dirty) return;

    this.captureSectionDraft(this.activeSectionId);
    const sections = this.scopeConfig.sections.filter(section =>
      typeof section.save === 'function' && this.sectionDrafts.has(section.id)
    );
    if (sections.length === 0) return;

    this.saving = true;
    this.updateSaveButton();
    try {
      const keepOpenSection = sections.find(section => section.keepOpenOnSave === true);
      for (const section of sections) {
        const values = this.sectionDrafts.get(section.id) || {};
        const ok = await section.save?.(values);
        if (ok === false) throw new Error('save failed');
      }
      if (keepOpenSection) {
        this.sectionDrafts.clear();
        await this.onAfterSave?.(keepOpenSection.id);
      }
      const keepOpen = Boolean(keepOpenSection);
      this.dirty = false;
      this.saving = false;
      this.sectionDrafts.clear();
      this.setFooterNote(t('properties_window.note.saved', 'Alterações salvas.'), 'ok');
      this.updateSaveButton();
      if (!keepOpen && typeof window !== 'undefined') {
        window.setTimeout(() => {
          if (this.isOpen && !this.dirty) this.hide();
        }, 350);
      }
    } catch {
      this.saving = false;
      this.setFooterNote(t('properties_window.note.save_failed', 'Não foi possível salvar as alterações.'), 'error');
      this.updateSaveButton();
    }
  }
}
