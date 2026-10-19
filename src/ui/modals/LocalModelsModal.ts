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

interface HardwareState {
  ramTotalBytes?: number;
  ramFreeBytes?: number;
  cpuThreads?: number;
  discreteVramFreeBytes?: number;
  modelsMax?: number;
  devices?: Array<{
    backend?: string;
    name?: string;
    vramTotalBytes?: number;
    vramFreeBytes?: number;
  }>;
}

interface RuntimeState {
  installed?: boolean;
  running?: boolean;
  ready?: boolean;
  crashed?: boolean;
  version?: string;
  binaryId?: string;
  backend?: string;
  installedBackends?: string[];
  path?: string;
  baseUrl?: string;
  port?: number;
  models?: string[];
  presetModels?: string[];
  modelsMax?: number;
  recommendedModelId?: string;
  hardware?: HardwareState;
  download?: DownloadState;
  log?: string[];
  error?: string;
  suggestedBackend?: string;
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
  runtime?: RuntimeState;
  error?: string;
}

interface LocalModelsOptions {
  onUse?: (model: CatalogModel) => void;
  onRuntimeChanged?: (running: boolean, baseUrl?: string) => void;
}

const ACTIVE_STATUSES = new Set(['queued', 'downloading', 'verifying', 'extracting', 'cancelling']);

export class LocalModelsModal {
  private overlay: HTMLElement;
  private options: LocalModelsOptions;
  private windowController?: IModalWindowController;
  private state?: LocalModelsState;
  private pollTimer: any = null;
  private busy = false;
  private logsVisible = false;

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

    const runtime = this.state?.runtime;
    const manifest = this.state?.manifest?.components?.['llama.cpp'];
    const version = runtime?.version || (manifest?.version ? String(manifest.version) : '—');
    const backend = runtime?.backend || '—';
    const license = manifest?.license ? String(manifest.license) : 'MIT';
    const modelsDir = this.state?.modelsDir || '—';
    const free = this.state?.diskFreeBytes ? this.formatBytes(this.state.diskFreeBytes) : '—';
    const installed = Boolean(runtime?.installed);
    const running = Boolean(runtime?.running);
    const ready = Boolean(runtime?.ready);
    const crashed = Boolean(runtime?.crashed);
    const download = runtime?.download;
    const downloading = Boolean(download && ACTIVE_STATUSES.has(download.status));
    const received = download?.received || 0;
    const total = download?.total || 0;
    const percent = total > 0 ? Math.min(100, Math.max(0, (received / total) * 100)) : 0;
    const status = running
      ? (ready ? t('local_models.runtime_running') : t('local_models.runtime_starting'))
      : crashed
        ? t('local_models.runtime_crashed')
        : installed
          ? t('local_models.runtime_stopped')
          : t('local_models.runtime_not_installed');
    const statusColor = running
      ? (ready ? '#0e700e' : '#0f6cbd')
      : crashed ? '#a80000' : installed ? '#616161' : '#a80000';
    const logLines = (runtime?.log || []).slice(-40).join('\n');
    const hardware = runtime?.hardware;
    const ram = hardware?.ramTotalBytes ? this.formatBytes(hardware.ramTotalBytes) : '—';
    const cpus = hardware?.cpuThreads ? String(hardware.cpuThreads) : '—';
    const gpuNames = (hardware?.devices || [])
      .map(device => device.name)
      .filter((name): name is string => Boolean(name))
      .join(', ') || (runtime?.installed ? t('local_models.device_cpu') : t('local_models.device_unknown'));
    const vram = hardware?.discreteVramFreeBytes ? this.formatBytes(hardware.discreteVramFreeBytes) : '—';
    const recommendedId = runtime?.recommendedModelId || '';
    const recommendedName = (this.state?.models || []).find(model => model.id === recommendedId)?.name || recommendedId;

    const actions: string[] = [];
    const buttonStyle = 'font-size:11.5px;height:28px;padding:0 10px;';
    if (!installed && !downloading) {
      actions.push(`<button type="button" class="btn btn-primary" data-runtime-action="install" style="${buttonStyle}">${t('local_models.runtime_install')}</button>`);
    }
    if (installed && !running && !downloading) {
      actions.push(`<button type="button" class="btn btn-primary" data-runtime-action="start" style="${buttonStyle}">${t('local_models.runtime_start')}</button>`);
    }
    if (running) {
      actions.push(`<button type="button" class="btn btn-secondary-pill" data-runtime-action="stop" style="${buttonStyle}">${t('local_models.runtime_stop')}</button>`);
    }
    if (logLines) {
      actions.push(`<button type="button" class="btn btn-secondary-pill" data-runtime-action="logs" style="${buttonStyle}">${t('local_models.runtime_logs')}</button>`);
    }

    host.innerHTML = `
      <div class="settings-section-card" style="padding: 12px 14px;">
        <div style="display:flex; justify-content:space-between; gap:12px; align-items:flex-start; flex-wrap:wrap;">
          <div style="display:flex; flex-direction:column; gap:6px; min-width:260px;">
            <div style="font-weight:700; font-size:13px; color:#242424;">${t('local_models.runtime_title')}</div>
            <div style="display:flex; flex-wrap:wrap; gap:18px; font-size:12px; color:#424242;">
              <div><strong>${t('local_models.runtime_label')}:</strong> llama.cpp ${this.esc(version)} · ${this.esc(backend)}</div>
              <div><strong>${t('local_models.manifest_label')}:</strong> ${this.esc(version)} · ${this.esc(license)}</div>
              <div><strong>${t('local_models.storage_label')}:</strong> <span style="font-family:monospace;">${this.esc(modelsDir)}</span></div>
              <div><strong>${t('local_models.disk_free')}:</strong> ${this.esc(free)}</div>
            </div>
            <div style="font-size:12px; color:${statusColor};">
              <strong>${t('local_models.runtime_status')}:</strong> ${this.esc(status)}
              ${running && runtime?.baseUrl ? ` · <span style="font-family:monospace;">${this.esc(runtime.baseUrl)}</span>` : ''}
            </div>
            <div style="font-size:11.5px; color:#616161;">${t('local_models.runtime_models_count', { count: (runtime?.models || []).length })}</div>
            <div style="display:flex; flex-wrap:wrap; gap:14px; font-size:11.5px; color:#616161;">
              <span><strong>${t('local_models.hardware_ram')}:</strong> ${this.esc(ram)}</span>
              <span><strong>${t('local_models.hardware_cpu')}:</strong> ${this.esc(cpus)}</span>
              <span><strong>${t('local_models.hardware_gpu')}:</strong> ${this.esc(gpuNames)}</span>
              <span><strong>${t('local_models.hardware_vram')}:</strong> ${this.esc(vram)}</span>
              <span><strong>${t('local_models.hardware_models_max')}:</strong> ${runtime?.modelsMax || 1}</span>
            </div>
            ${recommendedName ? `<div style="font-size:11.5px; color:#0f6cbd;">${t('local_models.recommended_for_machine', { name: recommendedName })}</div>` : ''}
          </div>
          <div style="display:flex; flex-wrap:wrap; gap:6px;">${actions.join('')}</div>
        </div>
        ${downloading ? `
          <div style="margin-top:10px;">
            <div style="height:6px; background:#edebe9; border-radius:3px; overflow:hidden;">
              <div style="height:100%; width:${percent.toFixed(1)}%; background:#0f6cbd;"></div>
            </div>
            <div style="font-size:10.5px; color:#616161; margin-top:4px;">
              ${this.statusLabel(download!.status)} · ${this.formatBytes(received)} / ${this.formatBytes(total)}
            </div>
          </div>
        ` : ''}
        ${download?.error ? `<div style="font-size:11.5px; color:#a80000; margin-top:6px;">${this.esc(download.error)}</div>` : ''}
        <div id="local-runtime-log" style="display:${this.logsVisible ? 'block' : 'none'}; margin-top:10px; background:#1e1e1e; color:#d4d4d4; border-radius:4px; padding:8px; max-height:180px; overflow:auto; font-family:monospace; font-size:10.5px; white-space:pre-wrap;">${this.esc(logLines)}</div>
        ${this.state?.error ? `<div class="form-hint" style="color: #a80000; margin-top:6px;">${this.esc(this.state.error)}</div>` : ''}
      </div>
    `;

    host.querySelector('[data-runtime-action="install"]')?.addEventListener('click', () => void this.installRuntime());
    host.querySelector('[data-runtime-action="start"]')?.addEventListener('click', () => void this.startServer());
    host.querySelector('[data-runtime-action="stop"]')?.addEventListener('click', () => void this.stopServer());
    host.querySelector('[data-runtime-action="logs"]')?.addEventListener('click', () => {
      this.logsVisible = !this.logsVisible;
      const panel = this.overlay.querySelector('#local-runtime-log') as HTMLElement | null;
      if (panel) panel.style.display = this.logsVisible ? 'block' : 'none';
    });
  }

  private async installRuntime(): Promise<void> {
    try {
      const res = await this.call('local_runtime_install');
      if (res.installed) {
        this.showMessage(true, t('local_models.runtime_installed'));
        await this.refresh();
        return;
      }
      if (!res.ok) {
        this.showMessage(false, t('local_models.runtime_install_failed', { error: res.error || '?' }));
        return;
      }
      this.showMessage(true, t('local_models.runtime_downloading'));
      await this.refresh();
    } catch {
      this.showMessage(false, t('local_models.needs_native'));
    }
  }

  private async waitForJob(jobId: string): Promise<boolean> {
    for (let attempt = 0; attempt < 900; ++attempt) {
      try {
        const status = await this.call('local_model_download_status', JSON.stringify({ jobId }));
        const state = String(status.status || '');
        if (state === 'done') return true;
        if (state === 'failed' || state === 'cancelled') {
          this.showMessage(false, t('local_models.download_failed', { error: status.error || state }));
          return false;
        }
      } catch {
        this.showMessage(false, t('local_models.needs_native'));
        return false;
      }
      await new Promise(resolve => setTimeout(resolve, 800));
    }
    this.showMessage(false, t('local_models.download_failed', { error: 'timeout' }));
    return false;
  }

  private async ensureRuntime(): Promise<boolean> {
    if (this.state?.runtime?.installed) return true;
    try {
      const res = await this.call('local_runtime_install');
      if (res.installed) {
        await this.refresh();
        return true;
      }
      if (!res.ok) {
        this.showMessage(false, t('local_models.runtime_install_failed', { error: res.error || '?' }));
        return false;
      }
      this.showMessage(true, t('local_models.runtime_downloading'));
      await this.refresh();
      const ok = await this.waitForJob(res.jobId);
      await this.refresh();
      return ok;
    } catch {
      this.showMessage(false, t('local_models.needs_native'));
      return false;
    }
  }

  private async startServer(backend?: string): Promise<boolean> {
    this.showMessage(true, t('local_models.runtime_starting'));
    try {
      let res = await this.call('local_runtime_start', backend ? JSON.stringify({ backend }) : '');

      if (!res.ok && !backend && res.suggestedBackend) {
        const fallback = String(res.suggestedBackend);
        this.showMessage(true, t('local_models.runtime_fallback', { backend: fallback }));
        const install = await this.call('local_runtime_install', JSON.stringify({ backend: fallback }));
        if (!install.ok) {
          this.showMessage(false, t('local_models.runtime_install_failed', { error: install.error || '?' }));
          return false;
        }
        if (!install.installed && install.jobId) {
          const installed = await this.waitForJob(install.jobId);
          if (!installed) return false;
        }
        await this.refresh();
        res = await this.call('local_runtime_start', JSON.stringify({ backend: fallback }));
      }

      await this.refresh();
      if (!res.ok) {
        const detail = res.detail ? ` (${res.detail})` : '';
        this.showMessage(false, t('local_models.runtime_start_failed', { error: res.error || '?' }) + detail);
        return false;
      }
      this.options.onRuntimeChanged?.(true, res.baseUrl);
      this.showMessage(true, t('local_models.runtime_started', { url: res.baseUrl || '' }));
      return true;
    } catch {
      this.showMessage(false, t('local_models.needs_native'));
      return false;
    }
  }

  private async stopServer(): Promise<void> {
    try {
      await this.call('local_runtime_stop');
      this.options.onRuntimeChanged?.(false);
      await this.refresh();
      this.showMessage(true, t('local_models.runtime_stopped'));
    } catch {
      this.showMessage(false, t('local_models.needs_native'));
    }
  }

  private async useModel(model: CatalogModel): Promise<void> {
    if (this.busy) return;
    this.busy = true;
    try {
      if (!(await this.ensureRuntime())) return;

      const modelName = model.apiModel || model.id;
      let runtime = this.state?.runtime;
      // Um modelo baixado depois do start não está no preset atual: reinicia para incluí-lo.
      if (runtime?.running && !(runtime.presetModels || []).includes(modelName)) {
        await this.stopServer();
        runtime = this.state?.runtime;
      }
      if (!runtime?.running) {
        if (!(await this.startServer())) return;
      }

      this.options.onUse?.(model);
      this.hide();
    } finally {
      this.busy = false;
    }
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
    if (this.state?.runtime?.recommendedModelId === model.id) {
      badges.push(`<span class="badge-inherited" style="background:#fff4ce;color:#797673;">${t('local_models.recommended')}</span>`);
    }
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
      void this.useModel(model);
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
      this.options.onRuntimeChanged?.(false);
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
    const runtimeDownload = this.state?.runtime?.download;
    const active = (this.state?.downloads || []).some(d => ACTIVE_STATUSES.has(d.status)) ||
      (this.state?.models || []).some(m => m.download && ACTIVE_STATUSES.has(m.download.status)) ||
      Boolean(runtimeDownload && ACTIVE_STATUSES.has(runtimeDownload.status));
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
