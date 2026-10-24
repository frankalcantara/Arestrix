/**
 * Central de notificações assíncronas do Studio.
 *
 * Recebe eventos `notification` do runtime (chamadas concluídas/falhas) e mostra
 * toasts não bloqueantes. Todo texto é inserido via textContent: o payload do
 * runtime nunca vira HTML.
 */
export interface INotificationPayload {
  nodeId?: string;
  kind?: string;
  severity?: 'info' | 'success' | 'warning' | 'error';
  title?: string;
  message?: string;
  timestamp?: number;
  metadata?: Record<string, unknown>;
}

const SEVERITY_STYLES: Record<string, { border: string; background: string; title: string }> = {
  info: { border: '#0f6cbd', background: '#eff6ff', title: '#0f3f75' },
  success: { border: '#047857', background: '#ecfdf5', title: '#065f46' },
  warning: { border: '#b45309', background: '#fffbeb', title: '#92400e' },
  error: { border: '#b91c1c', background: '#fef2f2', title: '#991b1b' }
};

export class NotificationCenter {
  private container: HTMLElement;
  private readonly maxVisible = 4;

  constructor(parent: HTMLElement = document.body) {
    this.container = document.createElement('div');
    this.container.id = 'arx-notification-center';
    this.container.setAttribute('aria-live', 'polite');
    this.container.setAttribute('aria-atomic', 'false');
    Object.assign(this.container.style, {
      position: 'fixed',
      top: '58px',
      right: '16px',
      width: '360px',
      maxWidth: 'calc(100vw - 32px)',
      display: 'flex',
      flexDirection: 'column',
      gap: '8px',
      zIndex: '12000',
      pointerEvents: 'none'
    });
    parent.appendChild(this.container);
  }

  public show(payload: INotificationPayload): HTMLElement {
    const severity = payload.severity && SEVERITY_STYLES[payload.severity] ? payload.severity : 'info';
    const style = SEVERITY_STYLES[severity];
    const toast = document.createElement('div');
    toast.className = `arx-notification arx-notification-${severity}`;
    toast.setAttribute('role', severity === 'error' ? 'alert' : 'status');
    Object.assign(toast.style, {
      pointerEvents: 'auto',
      background: style.background,
      border: `1px solid ${style.border}44`,
      borderLeft: `4px solid ${style.border}`,
      borderRadius: '6px',
      padding: '10px 12px',
      color: '#1f2937',
      fontFamily: 'Segoe UI, Inter, sans-serif',
      fontSize: '12px',
      lineHeight: '1.45',
      boxShadow: '0 6px 18px rgba(15, 23, 42, 0.12)',
      position: 'relative'
    });

    const title = document.createElement('div');
    title.className = 'arx-notification-title';
    title.textContent = payload.title || 'Notificação';
    Object.assign(title.style, {
      color: style.title,
      fontWeight: '700',
      paddingRight: '22px',
      marginBottom: payload.message ? '3px' : '0'
    });
    toast.appendChild(title);

    if (payload.message) {
      const message = document.createElement('div');
      message.className = 'arx-notification-message';
      message.textContent = payload.message;
      message.style.whiteSpace = 'pre-wrap';
      message.style.overflowWrap = 'anywhere';
      toast.appendChild(message);
    }

    const close = document.createElement('button');
    close.type = 'button';
    close.textContent = '×';
    close.setAttribute('aria-label', 'Fechar notificação');
    Object.assign(close.style, {
      position: 'absolute',
      top: '6px',
      right: '8px',
      border: 'none',
      background: 'transparent',
      color: '#475569',
      cursor: 'pointer',
      fontSize: '16px',
      lineHeight: '1'
    });
    close.addEventListener('click', () => toast.remove());
    toast.appendChild(close);

    this.container.appendChild(toast);
    while (this.container.children.length > this.maxVisible) {
      this.container.firstElementChild?.remove();
    }
    window.setTimeout(() => toast.remove(), severity === 'error' ? 10000 : 6500);
    return toast;
  }

  public clear(): void {
    this.container.replaceChildren();
  }

  public getContainer(): HTMLElement {
    return this.container;
  }
}
