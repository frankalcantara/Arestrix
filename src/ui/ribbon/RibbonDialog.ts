/**
 * Diálogo leve da Ribbon.
 *
 * A ribbon contextual não usa window.prompt/confirm: em WebView/ambiente desktop
 * esses diálogos nativos podem estar desabilitados. Este componente oferece
 * entrada de texto/número/seleção e confirmação com a mesma linguagem visual dos
 * modais do Studio, sem depender de APIs bloqueadas pelo host.
 */

export interface IRibbonDialogOption {
  value: string;
  label: string;
}

export interface IRibbonDialogField {
  id: string;
  label: string;
  type: 'text' | 'number' | 'select' | 'textarea';
  value: string | number;
  min?: number;
  max?: number;
  step?: number;
  options?: IRibbonDialogOption[];
  placeholder?: string;
  help?: string;
}

export interface IRibbonInputDialogOptions {
  title: string;
  message?: string;
  fields: IRibbonDialogField[];
  confirmLabel?: string;
  cancelLabel?: string;
  validate?: (values: Record<string, string>) => string | null;
  onConfirm: (values: Record<string, string>) => void;
}

export interface IRibbonConfirmDialogOptions {
  title: string;
  message: string;
  confirmLabel?: string;
  cancelLabel?: string;
  danger?: boolean;
  onConfirm: () => void;
}

export class RibbonDialog {
  private overlay: HTMLElement;
  private isOpen = false;

  constructor() {
    this.overlay = document.createElement('div');
    this.overlay.className = 'modal-overlay ribbon-dialog-overlay hidden';
    this.overlay.id = 'ribbon-dialog-overlay';
    this.overlay.addEventListener('click', (event: Event) => {
      if (event.target === this.overlay) this.close();
    });

    if (typeof window !== 'undefined' && typeof window.addEventListener === 'function') {
      window.addEventListener('keydown', (event: KeyboardEvent) => {
        if (event.key === 'Escape' && this.isOpen) {
          event.preventDefault();
          this.close();
        }
      });
    }

    document.body.appendChild(this.overlay);
  }

  public openInput(options: IRibbonInputDialogOptions): void {
    const fields = options.fields.map(field => this.renderField(field)).join('');
    this.overlay.innerHTML = `
      <div class="modal-dialog ribbon-dialog" role="dialog" aria-modal="true" aria-labelledby="ribbon-dialog-title">
        <div class="office-modal-titlebar">
          <div class="office-modal-titlebar-left">
            <span class="office-modal-titlebar-icon">⚙️</span>
            <span class="office-modal-titlebar-text" id="ribbon-dialog-title">${this.escapeHtml(options.title)}</span>
          </div>
          <button type="button" class="office-modal-close-btn" id="btn-ribbon-dialog-close" title="Fechar">✕</button>
        </div>
        <div class="modal-body-scrollable ribbon-dialog-body">
          ${options.message ? `<p class="ribbon-dialog-message">${this.escapeHtml(options.message)}</p>` : ''}
          ${fields}
          <div class="ribbon-dialog-error hidden" id="ribbon-dialog-error" role="alert"></div>
        </div>
        <div class="modal-footer office-modal-footer">
          <div class="modal-footer-right" style="display:flex; gap:8px;">
            <button type="button" class="btn btn-secondary-office" id="btn-ribbon-dialog-cancel">${this.escapeHtml(options.cancelLabel || 'Cancelar')}</button>
            <button type="button" class="btn btn-primary-office" id="btn-ribbon-dialog-confirm">${this.escapeHtml(options.confirmLabel || 'Aplicar')}</button>
          </div>
        </div>
      </div>
    `;

    this.bindCommonButtons();
    this.overlay.querySelector('#btn-ribbon-dialog-confirm')?.addEventListener('click', () => {
      const values: Record<string, string> = {};
      for (const field of options.fields) {
        const element = this.overlay.querySelector(`#ribbon-dialog-field-${field.id}`) as HTMLInputElement | HTMLSelectElement | HTMLTextAreaElement | null;
        values[field.id] = element ? element.value.trim() : String(field.value);
      }
      const error = options.validate ? options.validate(values) : null;
      if (error) {
        this.showError(error);
        return;
      }
      this.close();
      options.onConfirm(values);
    });

    this.open();
  }

  public openConfirm(options: IRibbonConfirmDialogOptions): void {
    this.overlay.innerHTML = `
      <div class="modal-dialog ribbon-dialog ribbon-dialog-confirm" role="dialog" aria-modal="true" aria-labelledby="ribbon-dialog-title">
        <div class="office-modal-titlebar">
          <div class="office-modal-titlebar-left">
            <span class="office-modal-titlebar-icon">${options.danger ? '⚠️' : '❓'}</span>
            <span class="office-modal-titlebar-text" id="ribbon-dialog-title">${this.escapeHtml(options.title)}</span>
          </div>
          <button type="button" class="office-modal-close-btn" id="btn-ribbon-dialog-close" title="Fechar">✕</button>
        </div>
        <div class="modal-body-scrollable ribbon-dialog-body">
          <p class="ribbon-dialog-message">${this.escapeHtml(options.message)}</p>
        </div>
        <div class="modal-footer office-modal-footer">
          <div class="modal-footer-right" style="display:flex; gap:8px;">
            <button type="button" class="btn btn-secondary-office" id="btn-ribbon-dialog-cancel">${this.escapeHtml(options.cancelLabel || 'Cancelar')}</button>
            <button type="button" class="btn ${options.danger ? 'btn-danger-office' : 'btn-primary-office'}" id="btn-ribbon-dialog-confirm">${this.escapeHtml(options.confirmLabel || 'Confirmar')}</button>
          </div>
        </div>
      </div>
    `;

    this.bindCommonButtons();
    this.overlay.querySelector('#btn-ribbon-dialog-confirm')?.addEventListener('click', () => {
      this.close();
      options.onConfirm();
    });

    this.open();
  }

  public close(): void {
    this.isOpen = false;
    this.overlay.classList.add('hidden');
    this.overlay.innerHTML = '';
  }

  public isVisible(): boolean {
    return this.isOpen;
  }

  private open(): void {
    this.isOpen = true;
    this.overlay.classList.remove('hidden');
    const first = this.overlay.querySelector<HTMLElement>(
      '.ribbon-dialog-body input, .ribbon-dialog-body select, .ribbon-dialog-body textarea'
    );
    first?.focus?.();
  }

  private bindCommonButtons(): void {
    this.overlay.querySelector('#btn-ribbon-dialog-close')?.addEventListener('click', () => this.close());
    this.overlay.querySelector('#btn-ribbon-dialog-cancel')?.addEventListener('click', () => this.close());
  }

  private showError(message: string): void {
    const box = this.overlay.querySelector('#ribbon-dialog-error') as HTMLElement | null;
    if (!box) return;
    box.textContent = message;
    box.classList.remove('hidden');
  }

  private renderField(field: IRibbonDialogField): string {
    const id = `ribbon-dialog-field-${field.id}`;
    const help = field.help ? `<div class="form-hint">${this.escapeHtml(field.help)}</div>` : '';
    if (field.type === 'select') {
      const options = (field.options || [])
        .map(option => `<option value="${this.escapeHtml(option.value)}" ${String(field.value) === option.value ? 'selected' : ''}>${this.escapeHtml(option.label)}</option>`)
        .join('');
      return `
        <div class="form-group">
          <label class="form-label" for="${id}">${this.escapeHtml(field.label)}</label>
          <select class="form-select" id="${id}">${options}</select>
          ${help}
        </div>
      `;
    }
    if (field.type === 'textarea') {
      return `
        <div class="form-group">
          <label class="form-label" for="${id}">${this.escapeHtml(field.label)}</label>
          <textarea class="form-textarea" id="${id}" rows="4" placeholder="${this.escapeHtml(field.placeholder || '')}">${this.escapeHtml(String(field.value ?? ''))}</textarea>
          ${help}
        </div>
      `;
    }
    const attrs = [
      field.min !== undefined ? `min="${field.min}"` : '',
      field.max !== undefined ? `max="${field.max}"` : '',
      field.step !== undefined ? `step="${field.step}"` : '',
      field.placeholder ? `placeholder="${this.escapeHtml(field.placeholder)}"` : ''
    ].filter(Boolean).join(' ');
    return `
      <div class="form-group">
        <label class="form-label" for="${id}">${this.escapeHtml(field.label)}</label>
        <input class="form-input" type="${field.type}" id="${id}" value="${this.escapeHtml(String(field.value ?? ''))}" ${attrs} />
        ${help}
      </div>
    `;
  }

  private escapeHtml(value: string): string {
    return (value || '')
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;')
      .replace(/'/g, '&#039;');
  }
}
