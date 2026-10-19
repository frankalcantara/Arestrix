import { ModalWindowManager, IModalWindowController } from './ModalWindowManager.ts';
import { t } from '../../i18n/index.ts';

interface CatalogModel {
  id: string;
  apiModel?: string;
  name: string;
  family?: string;
  parameters?: string;
  quantization?: string;
  description?: string;
  fileName?: string;
  sizeBytes?: number;
  license?: string;
  licenseUrl?: string;
  contextLength?: number;
  supportsTools?: boolean;
  verified?: boolean;
  curated?: boolean;
  minRamGB?: number;
  minVramGB?: number;
  installed?: boolean;
  bytes?: number;
  partialBytes?: number;
  path?: string;
  download?: DownloadState;
}

interface DownloadState {
  jobId: string;
  targetId?: string;
  status: string;
  received?: number;
  total?: number;
  error?: string;
}

interface LocalModelsState {
  dataRoot?: string;
  modelsDir?: string;
  diskFreeBytes?: number;
  diskTotalBytes?: number;
  catalog?: { models?: CatalogModel[] };
  manifest?: { components?: Record<string, any> };
  models?: CatalogModel[];
  downloads?: DownloadState[];
  error?: string;
}

interface LocalModelsOptions {
  onUse?: (model: CatalogModel) => void;
}

const ACTIVE_STATUSES = new Set(['queued', 'downloading', 'verifying', 'cancelling']);

export class LocalModelsModal {
  private overlay: HTMLElement;
  private options: LocalModelsOptions;
  private windowController?: IModalWindowController;
  private state?: LocalModelsState;
  private pollTimer: any = null;

  constructor(options: LocalModelsOptions = {}) {
    this.options = options;
    this.overlay = document.createElement('div');
    this.overlay.className = 'modal-overlay hidden';
    document.body.appendChild(this.overlay);
    this.overlay.addEventListener('click', (e) => {
      if (e.target === this.overlay) this.hide();
    });
  }

  public show(): void {
    this.overlay.classList.remove('hidden');
    this.renderShell();
    this.windowController?.bringToFront();
    void this.refresh();
  }

  public hide(): void {
    this.stopPolling();
    this.overlay.classList.add('hidden');
  }

  private webui(): any | null {
    const w = typeof window !== 'undefined' ? (window as any) : null;
    return w && w.webui && typeof w.webui.call === 'function' ? w.webui : null;
  }

  private async call(name: string, payload?: string): Promise<any> {
    const webui = this.webui();
    if (!webui) throw new Error('native_only');
    const raw = await webui.call(name, payload || '');
    return typeof raw === 'string' ? JSON.parse(raw || '{}') : raw;
  }

  private renderShell(): void {
    const titleBar = ModalWindowManager.createTitleBarHTML({
      title: t('local_models.title'),
      subtitle: t('local_models.subtitle'),
      icon: '🧩',
      closeBtnId: 'btn-close-local-models'
    });

    this.overlay.innerHTML = `
      <div class="modal-dialog" style="max-width: 980px; width: 94%; max-height: 90vh; display: flex; flex-direction: column;">
        ${titleBar}
        <div class="modal-body-scrollable" style="flex: 1; overflow-y: auto; padding: 16px;">
          <div id="local-models-summary" style="margin-bottom: 14px;"></div>
          <div id="local-models-feedback" style="display: none; margin-bottom: 12px; padding: 8px 12px; border-radius: 4px; font-size: 12px;"></div>
          <div id="local-models-grid" style="display: grid; grid-template-columns: repeat(auto-fill, minmax(290px, 1fr)); gap: 12px;"></div>
        </div>
        <div class="modal-footer office-modal-footer">
          <button type="button" class="btn btn-secondary-pill btn-secondary-office" id="btn-local-models-open-folder">${t('local_models.open_folder')}</button>
          <div style="flex: 1;"></div>
          <button type="button" class="btn btn-secondary-pill btn-secondary-office" id="btn-local-models-close">${t('global_defaults.btn_cancel')}</button>
        </div>
      </div>
    `;

    const dialog = this.overlay.querySelector('.modal-dialog') as HTMLElement | null;
    if (dialog) {
      this.windowController = ModalWindowManager.attach({ dialog, minWidth: 720, minHeight: 480, onClose: () => this.hide() });
    }
    this.overlay.querySelector('#btn-close-local-models')?.addEventListener('click', () => this.hide());
    this.overlay.querySelector('#btn-local-models-close')?.addEventListener('click', () => this.hide());
    this.overlay.querySelector('#btn-local-models-open-folder')?.addEventListener('click', () => {
      void this.call('local_models_open_folder').catch(() => this.showMessage(false, t('local_models.needs_native')));
    });
  }

  private async refresh(): Promise<void> {
    try {
      this.state = await this.call('local_models_state');
    } catch {
      this.showMessage(false, t('local_models.needs_native'));
      return;
    }
    this.renderSummary();
    this.renderModels();
    this.updatePolling();
  }

  private renderSummary(): void {
    const host = this.overlay.querySelector('#local-models-summary') as HTMLElement | null;
    if (!host) return;
    const manifest = this.state?.manifest?.components?.['llama.cpp'];
    const version = manifest?.version ? String(manifest.version) : '—';
    const modelsDir = this.state?.modelsDir || '—';
    const free = this.state?.diskFreeBytes ? this.formatBytes(this.state.diskFreeBytes) : '—';
    host.innerHTML = `
      <div class="settings-section-card" style="padding: 12px 14px;">
        <div style="display: flex; flex-wrap: wrap; gap: 18px; align-items: center; font-size: 12px; color: #424242;">
          <div><strong>${t('local_models.runtime_label')}:</strong> ${t('local_models.runtime_not_installed')}</div>
          <div><strong>${t('local_models.manifest_label')}:</strong> llama.cpp ${this.esc(version)}</div>
          <div><strong>${t('local_models.storage_label')}:</strong> <span style="font-family: monospace;">${this.esc(modelsDir)}</span></div>
          <div><strong>${t('local_models.disk_free')}:</strong> ${this.esc(free)}</div>
        </div>
        <div class="form-hint" style="margin-top: 8px;">${t('local_models.phase_hint')}</div>
        ${this.state?.error ? `<div class="form-hint" style="color: #a80000;">${this.esc(this.state.error)}</div>` : ''}
      </div>
    `;
  }

  private renderModels(): void {
    const grid = this.overlay.querySelector('#local-models-grid') as HTMLElement | null;
    if (!grid) return;
    const models = this.state?.models || this.state?.catalog?.models || [];
    if (models.length === 0) {
      grid.innerHTML = `<div class="form-hint">${t('local_models.no_models')}</div>`;
      return;
    }
    grid.innerHTML = '';
    for (const model of models) {
      grid.appendChild(this.renderModelCard(model));
    }
  }

  private renderModelCard(model: CatalogModel): HTMLElement {
    const card = document.createElement('div');
    card.className = 'settings-section-card';
    card.style.padding = '12px';
    card.style.display = 'flex';
    card.style.flexDirection = 'column';
    card.style.gap = '8px';

    const download = model.download;
    const active = Boolean(download && ACTIVE_STATUSES.has(download.status));
    const installed = Boolean(model.installed);
    const received = download?.received ?? model.bytes ?? 0;
    const total = download?.total || model.sizeBytes || 0;
    const percent = total > 0 ? Math.min(100, Math.max(0, (received / total) * 100)) : 0;
    const statusText = installed
      ? t('local_models.installed')
      : active
        ? this.statusLabel(download!.status)
        : download?.status === 'failed'
          ? t('local_models.status_failed')
          : download?.status === 'cancelled'
            ? t('local_models.status_cancelled')
            : t('local_models.not_installed');

    const badges: string[] = [];
    if (model.curated) badges.push(`<span class="badge-inherited">${t('local_models.curated')}</span>`);
    if (model.verified) badges.push(`<span class="badge-inherited">${t('local_models.verified')}</span>`);
    badges.push(model.supportsTools
      ? `<span class="badge-inherited" style="background:#dff6dd;color:#0e700e;">${t('local_models.tools_yes')}</span>`
      : `<span class="badge-inherited" style="background:#f3f2f1;color:#616161;">${t('local_models.tools_no')}</span>`);

    card.innerHTML = `
      <div style="display:flex; justify-content:space-between; gap:8px; align-items:flex-start;">
        <div>
          <div style="font-weight:700; font-size:14px; color:#242424;">${this.esc(model.name || model.id)}</div>
          <div style="font-size:11.5px; color:#616161; margin-top:2px;">
            ${this.esc([model.family, model.parameters, model.quantization].filter(Boolean).join(' · '))}
          </div>
        </div>
        <div style="display:flex; flex-direction:column; gap:4px; align-items:flex-end;">${badges.join('')}</div>
      </div>
      <div style="font-size:12px; color:#424242; line-height:1.45;">${this.esc(model.description || '')}</div>
      <div style="display:flex; flex-wrap:wrap; gap:12px; font-size:11.5px; color:#616161;">
        <span><strong>${this.formatBytes(model.sizeBytes || 0)}</strong></span>
        <span>RAM ${model.minRamGB || '?'} GB</span>
        <span>VRAM ${model.minVramGB || '?'} GB</span>
        <span>${this.esc(model.license || '')}</span>
      </div>
      <div style="font-size:11.5px; color:${installed ? '#0e700e' : active ? '#0f6cbd' : download?.status === 'failed' ? '#a80000' : '#616161'};">
        ${statusText}
      </div>
      ${(active || (model.partialBytes || 0) > 0) ? `
        <div style="height:6px; background:#edebe9; border-radius:3px; overflow:hidden;">
          <div style="height:100%; width:${percent.toFixed(1)}%; background:${active ? '#0f6cbd' : '#c8c6c4'};"></div>
        </div>
        <div style="font-size:10.5px; color:#616161;">${this.formatBytes(received)} / ${this.formatBytes(total)}</div>
      ` : ''}
      <div style="display:flex; flex-wrap:wrap; gap:6px; margin-top:auto;">
        ${this.actionButtons(active, installed)}
      </div>
      ${download?.error ? `<div style="font-size:11px; color:#a80000;">${this.esc(download.error)}</div>` : ''}
    `;

    card.querySelector('[data-action="download"]')?.addEventListener('click', () => void this.download(model));
    card.querySelector('[data-action="cancel"]')?.addEventListener('click', () => void this.cancel(model));
    card.querySelector('[data-action="delete"]')?.addEventListener('click', () => void this.remove(model));
    card.querySelector('[data-action="verify"]')?.addEventListener('click', () => void this.verify(model));
    card.querySelector('[data-action="use"]')?.addEventListener('click', () => {
      this.options.onUse?.(model);
      this.hide();
    });
    return card;
  }

  private actionButtons(active: boolean, installed: boolean): string {
    const buttons: string[] = [];
    if (active) {
      buttons.push(`<button type="button" class="btn btn-secondary-pill" data-action="cancel" style="font-size:11.5px;height:28px;padding:0 10px;">${t('local_models.cancel')}</button>`);
    } else if (!installed) {
      buttons.push(`<button type="button" class="btn btn-primary" data-action="download" style="font-size:11.5px;height:28px;padding:0 10px;">${t('local_models.download')}</button>`);
    }
    if (installed) {
      buttons.push(`<button type="button" class="btn btn-primary" data-action="use" style="font-size:11.5px;height:28px;padding:0 10px;">${t('local_models.use')}</button>`);
      buttons.push(`<button type="button" class="btn btn-secondary-pill" data-action="verify" style="font-size:11.5px;height:28px;padding:0 10px;">${t('local_models.verify')}</button>`);
      buttons.push(`<button type="button" class="btn btn-secondary-pill" data-action="delete" style="font-size:11.5px;height:28px;padding:0 10px;">${t('local_models.delete')}</button>`);
    }
    return buttons.join('');
  }

  private statusLabel(status: string): string {
    return t(`local_models.status_${status}`);
  }

  private async download(model: CatalogModel): Promise<void> {
    const free = this.state?.diskFreeBytes || 0;
    const needed = model.sizeBytes || 0;
    if (free > 0 && needed > free) {
      this.showMessage(false, t('local_models.not_enough_space', {
        needed: this.formatBytes(needed),
        free: this.formatBytes(free)
      }));
      return;
    }
    const license = model.license || '?';
    if (!confirm(t('local_models.license_confirm', { name: model.name || model.id, license }))) return;
    try {
      const res = await this.call('local_model_download', JSON.stringify({ modelId: model.id }));
      if (!res.ok) {
        this.showMessage(false, t('local_models.download_failed', { error: res.error || '?' }));
      } else {
        this.showMessage(true, t('local_models.download_started', { name: model.name || model.id }));
      }
      await this.refresh();
    } catch {
      this.showMessage(false, t('local_models.needs_native'));
    }
  }

  private async cancel(model: CatalogModel): Promise<void> {
    const jobId = model.download?.jobId;
    if (!jobId) return;
    try {
      await this.call('local_model_download_cancel', JSON.stringify({ jobId }));
      await this.refresh();
    } catch {
      this.showMessage(false, t('local_models.needs_native'));
    }
  }

  private async remove(model: CatalogModel): Promise<void> {
    if (!confirm(t('local_models.delete_confirm', { name: model.name || model.id }))) return;
    try {
      const res = await this.call('local_model_delete', JSON.stringify({ modelId: model.id }));
      this.showMessage(Boolean(res.ok), res.ok ? t('local_models.deleted') : t('local_models.delete_failed', { error: res.error || '?' }));
      await this.refresh();
    } catch {
      this.showMessage(false, t('local_models.needs_native'));
    }
  }

  private async verify(model: CatalogModel): Promise<void> {
    try {
      const res = await this.call('local_model_verify', JSON.stringify({ modelId: model.id }));
      if (res.ok) {
        this.showMessage(true, t('local_models.verify_ok', { name: model.name || model.id }));
      } else {
        this.showMessage(false, t('local_models.verify_failed', { error: res.error || 'hash_mismatch' }));
      }
    } catch {
      this.showMessage(false, t('local_models.needs_native'));
    }
  }

  private updatePolling(): void {
    const active = (this.state?.downloads || []).some(d => ACTIVE_STATUSES.has(d.status)) ||
      (this.state?.models || []).some(m => m.download && ACTIVE_STATUSES.has(m.download.status));
    if (active && !this.pollTimer) {
      this.pollTimer = setInterval(() => void this.refresh(), 800);
    } else if (!active) {
      this.stopPolling();
    }
  }

  private stopPolling(): void {
    if (this.pollTimer) {
      clearInterval(this.pollTimer);
      this.pollTimer = null;
    }
  }

  private showMessage(ok: boolean, text: string): void {
    const host = this.overlay.querySelector('#local-models-feedback') as HTMLElement | null;
    if (!host) return;
    host.style.display = 'block';
    host.style.background = ok ? '#dff6dd' : '#fde7e9';
    host.style.color = ok ? '#0e700e' : '#a80000';
    host.textContent = text;
  }

  private formatBytes(bytes: number): string {
    if (!bytes || bytes <= 0) return '—';
    const units = ['B', 'KB', 'MB', 'GB', 'TB'];
    let value = bytes;
    let unit = 0;
    while (value >= 1024 && unit < units.length - 1) { value /= 1024; unit++; }
    return `${value.toFixed(value >= 10 || unit === 0 ? 0 : 1)} ${units[unit]}`;
  }

  private esc(text: string): string {
    return String(text)
      .replace(/&/g, '&amp;')
      .replace(/</g, '&lt;')
      .replace(/>/g, '&gt;')
      .replace(/"/g, '&quot;');
  }
}
