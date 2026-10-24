import { t } from '../../i18n/index.ts';
import type { IFormField } from '../../core/types.ts';
import { validateInteractionAnswers } from './interactionForm.ts';

export interface IInteractionRequest {
  requestId: string;
  nodeId?: string;
  label?: string;
  form?: {
    title?: string;
    description?: string;
    context?: { markdown?: string; artifacts?: Array<{ path: string; type: string; label?: string }> };
    fields?: IFormField[];
    submitLabel?: string;
    cancelLabel?: string;
  };
  inputContext?: string;
  timeoutSeconds?: number;
  onTimeout?: string;
  sensitive?: boolean;
  defaultAnswers?: Record<string, unknown>;
}

type Control = HTMLInputElement | HTMLTextAreaElement | HTMLSelectElement;

interface FieldControl {
  field: IFormField;
  input?: Control;
  choices?: HTMLInputElement[];
}

/**
 * Modal de interação humana do Studio.
 *
 * O formulário é construído a partir do schema seguro do vértice (tipos declarados),
 * sempre com DOM/textContent — nunca com HTML ou JavaScript gerados pelo modelo.
 */
export class InteractionModal {
  private overlay: HTMLElement;
  private request?: IInteractionRequest;
  private controls: FieldControl[] = [];
  private timer?: ReturnType<typeof setInterval>;
  private busy = false;

  constructor() {
    this.overlay = document.createElement('div');
    this.overlay.className = 'modal-overlay interaction-modal-overlay hidden';
    document.body.appendChild(this.overlay);
  }

  public isOpen(): boolean {
    return !this.overlay.classList.contains('hidden');
  }

  public open(request: IInteractionRequest): void {
    if (!request || !request.requestId) return;
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = undefined;
    }
    this.busy = false;
    this.request = request;
    this.render();
    this.overlay.classList.remove('hidden');
    const dialog = this.overlay.querySelector<HTMLElement>('.interaction-modal-dialog');
    dialog?.focus();
  }

  public closeIf(requestId?: string): void {
    if (!this.request) return;
    if (requestId && this.request.requestId !== requestId) return;
    this.hide();
  }

  public forceClose(): void {
    this.hide();
  }

  private hide(): void {
    if (this.timer) {
      clearInterval(this.timer);
      this.timer = undefined;
    }
    this.overlay.classList.add('hidden');
    this.overlay.innerHTML = '';
    this.request = undefined;
    this.controls = [];
    this.busy = false;
  }

  private q<T extends Element>(selector: string): T | null {
    return this.overlay.querySelector<T>(selector);
  }

  private render(): void {
    const request = this.request!;
    const form = request.form || {};
    const fields = Array.isArray(form.fields) ? form.fields : [];

    this.overlay.innerHTML = `
      <div class="modal-dialog interaction-modal-dialog" role="dialog" aria-modal="true" tabindex="-1">
        <div class="modal-header interaction-modal-header">
          <div class="modal-title-group">
            <div class="modal-header-icon">💬</div>
            <div>
              <h2 class="modal-title interaction-modal-title"></h2>
              <div class="modal-subtitle interaction-modal-subtitle"></div>
            </div>
          </div>
          <div class="window-controls">
            <button type="button" class="modal-close-btn window-control-btn interaction-modal-x" title="${t('common.close', 'Fechar')}">&times;</button>
          </div>
        </div>
        <div class="modal-body-scrollable interaction-modal-body">
          <div class="interaction-modal-context" hidden></div>
          <div class="interaction-modal-meta" hidden></div>
          <form class="interaction-modal-form" novalidate></form>
          <div class="interaction-modal-error" hidden></div>
        </div>
        <div class="modal-footer interaction-modal-footer">
          <div class="interaction-modal-timer" hidden></div>
          <div class="interaction-modal-actions">
            <button type="button" class="btn-secondary interaction-modal-cancel"></button>
            <button type="submit" class="btn-primary interaction-modal-submit"></button>
          </div>
        </div>
      </div>
    `;

    this.q('.interaction-modal-title')!.textContent = form.title || request.label || t('interaction_modal.title', 'Interação necessária');
    const subtitle = this.q<HTMLElement>('.interaction-modal-subtitle')!;
    subtitle.textContent = form.description || request.label || '';
    if (!subtitle.textContent) subtitle.hidden = true;

    const contextText = form.context?.markdown || request.inputContext || '';
    const artifacts = Array.isArray(form.context?.artifacts) ? form.context!.artifacts! : [];
    if (contextText || artifacts.length > 0) {
      const context = this.q<HTMLElement>('.interaction-modal-context')!;
      context.hidden = false;
      if (contextText) context.textContent = contextText;
      if (artifacts.length > 0) {
        const heading = document.createElement('div');
        heading.className = 'interaction-artifacts-title';
        heading.textContent = t('interaction_modal.artifacts', 'Artefatos');
        context.appendChild(heading);
        const list = document.createElement('div');
        list.className = 'interaction-artifacts';
        for (const artifact of artifacts) {
          if (!artifact || typeof artifact !== 'object') continue;
          const item = document.createElement('div');
          item.className = 'interaction-artifact-item';
          const label = artifact.label || artifact.path || '';
          item.textContent = `[${artifact.type || 'file'}] ${label}${artifact.path && artifact.label ? ` — ${artifact.path}` : ''}`;
          list.appendChild(item);
        }
        context.appendChild(list);
      }
    }

    if (request.sensitive) {
      const meta = this.q<HTMLElement>('.interaction-modal-meta')!;
      meta.hidden = false;
      meta.textContent = t('interaction_modal.sensitive', 'Resposta sensível: evite exibir a tela para terceiros.');
      meta.classList.add('interaction-modal-sensitive');
    }

    const formEl = this.q<HTMLFormElement>('.interaction-modal-form')!;
    this.controls = [];
    for (const field of fields) this.renderField(formEl, field);

    const cancel = this.q<HTMLButtonElement>('.interaction-modal-cancel')!;
    const submit = this.q<HTMLButtonElement>('.interaction-modal-submit')!;
    cancel.textContent = form.cancelLabel || t('common.cancel', 'Cancelar');
    submit.textContent = form.submitLabel || t('common.ok', 'OK');
    cancel.addEventListener('click', () => void this.answer(true));
    submit.addEventListener('click', (ev) => {
      ev.preventDefault();
      void this.answer(false);
    });
    formEl.addEventListener('submit', (ev) => {
      ev.preventDefault();
      void this.answer(false);
    });
    this.q('.interaction-modal-x')!.addEventListener('click', () => void this.answer(true));

    const dialog = this.q<HTMLElement>('.interaction-modal-dialog')!;
    dialog.addEventListener('keydown', (ev: KeyboardEvent) => {
      if (ev.key === 'Escape' && !this.busy) {
        ev.preventDefault();
        void this.answer(true);
      }
    });

    this.startTimer(request.timeoutSeconds || 0);
    const first = this.overlay.querySelector<HTMLElement>('input, textarea, select, button');
    first?.focus();
  }

  private renderField(container: HTMLElement, field: IFormField): void {
    const request = this.request!;
    const wrapper = document.createElement('div');
    wrapper.className = 'form-group interaction-field';

    const label = document.createElement('label');
    label.className = 'form-label';
    label.textContent = field.label || field.id;
    if (field.required) {
      const required = document.createElement('span');
      required.className = 'interaction-required';
      required.textContent = ' *';
      label.appendChild(required);
    }
    wrapper.appendChild(label);

    const initial = request.defaultAnswers && Object.prototype.hasOwnProperty.call(request.defaultAnswers, field.id)
      ? request.defaultAnswers[field.id]
      : field.defaultValue;

    const control: FieldControl = { field };
    if (field.type === 'textarea') {
      const textarea = document.createElement('textarea');
      textarea.className = 'form-textarea';
      textarea.rows = 4;
      textarea.placeholder = field.placeholder || '';
      if (field.maxLength) textarea.maxLength = field.maxLength;
      if (request.sensitive) {
        textarea.classList.add('interaction-masked');
        textarea.autocomplete = 'off';
      }
      if (typeof initial === 'string') textarea.value = initial;
      wrapper.appendChild(textarea);
      control.input = textarea;
    } else if (field.type === 'boolean') {
      const row = document.createElement('label');
      row.className = 'interaction-check-row';
      const checkbox = document.createElement('input');
      checkbox.type = 'checkbox';
      checkbox.checked = initial === true;
      row.appendChild(checkbox);
      const text = document.createElement('span');
      text.textContent = field.help || t('interaction_modal.yes', 'Sim');
      row.appendChild(text);
      wrapper.appendChild(row);
      control.input = checkbox;
    } else if (field.type === 'choice') {
      const select = document.createElement('select');
      select.className = 'form-select';
      const placeholder = document.createElement('option');
      placeholder.value = '';
      placeholder.textContent = t('interaction_modal.select', 'Selecione...');
      select.appendChild(placeholder);
      for (const option of field.options || []) {
        const el = document.createElement('option');
        el.value = option.value;
        el.textContent = option.label || option.value;
        select.appendChild(el);
      }
      if (typeof initial === 'string') select.value = initial;
      wrapper.appendChild(select);
      control.input = select;
    } else if (field.type === 'multi_choice') {
      const group = document.createElement('div');
      group.className = 'interaction-option-list';
      const selected = new Set(Array.isArray(initial) ? initial.map(String) : []);
      const boxes: HTMLInputElement[] = [];
      for (const option of field.options || []) {
        const row = document.createElement('label');
        row.className = 'interaction-check-row';
        const checkbox = document.createElement('input');
        checkbox.type = 'checkbox';
        checkbox.value = option.value;
        checkbox.checked = selected.has(option.value);
        row.appendChild(checkbox);
        const text = document.createElement('span');
        text.textContent = option.label || option.value;
        row.appendChild(text);
        group.appendChild(row);
        boxes.push(checkbox);
      }
      wrapper.appendChild(group);
      control.choices = boxes;
    } else {
      const input = document.createElement('input');
      input.className = 'form-input';
      input.type = field.type === 'number' ? 'number' : field.type === 'date' ? 'date' : 'text';
      input.placeholder = field.placeholder || '';
      if (field.maxLength) input.maxLength = field.maxLength;
      if (request.sensitive && field.type === 'text') {
        input.type = 'password';
        input.autocomplete = 'off';
      }
      if (field.type === 'number') {
        if (typeof field.min === 'number') input.min = String(field.min);
        if (typeof field.max === 'number') input.max = String(field.max);
      }
      if (initial !== undefined && initial !== null) input.value = String(initial);
      wrapper.appendChild(input);
      control.input = input;
    }

    if (field.help && field.type !== 'boolean') {
      const help = document.createElement('div');
      help.className = 'form-hint';
      help.textContent = field.help;
      wrapper.appendChild(help);
    }

    this.controls.push(control);
    container.appendChild(wrapper);
  }

  private rawValues(): Record<string, unknown> {
    const values = Object.create(null) as Record<string, unknown>;
    for (const control of this.controls) {
      if (control.choices) {
        values[control.field.id] = control.choices.filter(c => c.checked).map(c => c.value);
        continue;
      }
      if (!control.input) continue;
      if (control.field.type === 'boolean') {
        values[control.field.id] = (control.input as HTMLInputElement).checked;
      } else {
        values[control.field.id] = control.input.value;
      }
    }
    return values;
  }

  private startTimer(seconds: number): void {
    if (seconds <= 0) return;
    let remaining = Math.max(1, Math.floor(seconds));
    const timer = this.q<HTMLElement>('.interaction-modal-timer')!;
    timer.hidden = false;
    const update = () => {
      timer.textContent = t('interaction_modal.timeout', { seconds: remaining }, `Tempo restante: ${remaining}s`);
      if (remaining <= 0) {
        if (this.timer) clearInterval(this.timer);
        this.timer = undefined;
        timer.textContent = t('interaction_modal.timed_out', 'Tempo esgotado. Aguardando o pipeline...');
        const submit = this.q<HTMLButtonElement>('.interaction-modal-submit');
        if (submit) submit.disabled = true;
        return;
      }
      remaining -= 1;
    };
    update();
    this.timer = setInterval(update, 1000);
  }

  private showError(message: string): void {
    const el = this.q<HTMLElement>('.interaction-modal-error');
    if (!el) return;
    el.hidden = false;
    el.textContent = message;
    el.classList.add('interaction-modal-error-visible');
  }

  private clearError(): void {
    const el = this.q<HTMLElement>('.interaction-modal-error');
    if (!el) return;
    el.hidden = true;
    el.textContent = '';
    el.classList.remove('interaction-modal-error-visible');
  }

  private validationMessage(code: string, fieldLabel: string): string {
    const labels: Record<string, string> = {
      required: t('interaction_modal.required_error', { field: fieldLabel }, `Preencha o campo ${fieldLabel}.`),
      invalid_text: t('interaction_modal.invalid_text', { field: fieldLabel }, `Valor inválido no campo ${fieldLabel}.`),
      invalid_number: t('interaction_modal.invalid_number', { field: fieldLabel }, `Informe um número no campo ${fieldLabel}.`),
      invalid_boolean: t('interaction_modal.invalid_boolean', { field: fieldLabel }, `Valor inválido no campo ${fieldLabel}.`),
      invalid_choice: t('interaction_modal.invalid_choice', { field: fieldLabel }, `Escolha uma opção válida em ${fieldLabel}.`),
      invalid_multi: t('interaction_modal.invalid_multi', { field: fieldLabel }, `Escolha opções válidas em ${fieldLabel}.`)
    };
    return labels[code] || labels.invalid_text;
  }

  private async answer(cancelled: boolean): Promise<void> {
    if (this.busy || !this.request) return;
    this.busy = true;
    const request = this.request;
    this.clearError();

    const submit = this.q<HTMLButtonElement>('.interaction-modal-submit');
    const cancel = this.q<HTMLButtonElement>('.interaction-modal-cancel');
    if (submit) submit.disabled = true;
    if (cancel) cancel.disabled = true;

    let payload: Record<string, unknown>;
    if (cancelled) {
      payload = { requestId: request.requestId, cancelled: true };
    } else {
      const formFields = Array.isArray(request.form?.fields) ? request.form!.fields : [];
      const validation = validateInteractionAnswers(formFields, this.rawValues());
      if (!validation.ok) {
        this.showError(this.validationMessage(validation.code, validation.fieldLabel));
        this.busy = false;
        if (submit) submit.disabled = false;
        if (cancel) cancel.disabled = false;
        return;
      }
      payload = { requestId: request.requestId, answers: validation.answers };
    }

    try {
      const webui = (globalThis as any).webui;
      if (!webui || typeof webui.call !== 'function') {
        throw new Error(t('interaction_modal.no_backend', 'Sem backend C++ para responder à interação.'));
      }
      const raw = await webui.call('answer_interaction', JSON.stringify(payload));
      const res = typeof raw === 'string' && raw ? JSON.parse(raw) : raw;
      if (res && res.ok === false) {
        throw new Error(res.error || t('interaction_modal.submit_failed', 'Não foi possível enviar a resposta.'));
      }
      if (this.request?.requestId !== request.requestId) return;
      this.hide();
    } catch (err: any) {
      if (this.request?.requestId !== request.requestId) return;
      this.showError(err?.message || String(err));
      this.busy = false;
      if (submit) submit.disabled = false;
      if (cancel) cancel.disabled = false;
    }
  }
}
