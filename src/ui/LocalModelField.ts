/**
 * Suporte a modelos locais nas telas de configuração.
 *
 * O Arestrix NÃO instala, baixa nem inicia servidores de inferência. Ele apenas conversa com um
 * endpoint OpenAI-compatível que já esteja rodando na máquina ou na rede local (Ollama, llama.cpp
 * server, vLLM, LM Studio, Jan, KoboldCpp...). A descoberta de modelos usa o backend nativo para
 * não depender de CORS e funcionar também no aplicativo empacotado.
 */

import { EnvironmentManager } from '../core/env/EnvironmentManager.ts';

export const LOCAL_LLM_ENV = 'LOCAL_LLM_BASE_URL';
export const LOCAL_LLM_DEFAULT_URL = 'http://localhost:11434/v1';

export interface LocalModelDiscovery {
  ok: boolean;
  baseUrl: string;
  models: string[];
  errorCode?: string;
  error?: string;
}

/**
 * Espelha a normalização do backend: remove espaços, barras finais e sufixos de endpoint; se o
 * usuário informou apenas host:porta, completa com /v1 (padrão de Ollama, llama.cpp, vLLM e LM Studio).
 */
export function normalizeLocalBaseUrl(input: string): string {
  let url = (input || '').trim();
  while (url.endsWith('/')) url = url.slice(0, -1);
  for (const suffix of ['/chat/completions', '/models']) {
    if (url.length > suffix.length && url.endsWith(suffix)) {
      url = url.slice(0, -suffix.length);
      while (url.endsWith('/')) url = url.slice(0, -1);
      break;
    }
  }
  if (!url) return '';
  const scheme = url.indexOf('://');
  if (scheme < 0) return url;
  const pathStart = url.indexOf('/', scheme + 3);
  return pathStart < 0 ? `${url}/v1` : url;
}

/** Lê o endpoint local configurado nesta máquina (ou vazio). */
export async function loadLocalBaseUrl(): Promise<string> {
  const value = await EnvironmentManager.getInstance().getEnvVar(LOCAL_LLM_ENV);
  return value || '';
}

/** Grava o endpoint local nas variáveis de ambiente do usuário. */
export async function saveLocalBaseUrl(baseUrl: string): Promise<boolean> {
  const normalized = normalizeLocalBaseUrl(baseUrl);
  if (!normalized) return false;
  return EnvironmentManager.getInstance().setEnvVar(LOCAL_LLM_ENV, normalized);
}

/**
 * Lista os modelos de um servidor local. Usa o backend nativo (sem CORS); em ambiente de
 * desenvolvimento/teste sem bridge, tenta o endpoint /models diretamente.
 */
export async function discoverLocalModels(baseUrl: string): Promise<LocalModelDiscovery> {
  const base = normalizeLocalBaseUrl(baseUrl || LOCAL_LLM_DEFAULT_URL);
  if (!/^https?:\/\//i.test(base)) {
    return { ok: false, baseUrl: base, models: [], errorCode: 'invalid_url' };
  }

  // Alguns servidores locais exigem um token; o runtime já usa LOCAL_LLM_API_KEY quando presente.
  let apiKey = '';
  try {
    apiKey = (await EnvironmentManager.getInstance().getEnvVar('LOCAL_LLM_API_KEY')) || '';
  } catch { /* sem ambiente: tenta sem chave */ }

  const webui = typeof window !== 'undefined' ? (window as any).webui : undefined;
  if (webui && typeof webui.call === 'function') {
    try {
      const raw = await webui.call('list_local_models', JSON.stringify(apiKey ? { baseUrl: base, apiKey } : { baseUrl: base }));
      const parsed = typeof raw === 'string' ? JSON.parse(raw || '{}') : raw;
      if (parsed && typeof parsed === 'object') {
        return {
          ok: Boolean(parsed.ok),
          baseUrl: typeof parsed.baseUrl === 'string' && parsed.baseUrl ? parsed.baseUrl : base,
          models: Array.isArray(parsed.models) ? parsed.models.map((m: unknown) => String(m)) : [],
          errorCode: parsed.errorCode ? String(parsed.errorCode) : undefined,
          error: parsed.error ? String(parsed.error) : undefined
        };
      }
    } catch (err) {
      return { ok: false, baseUrl: base, models: [], errorCode: 'bridge_error', error: String(err) };
    }
  }

  try {
    const headers: Record<string, string> = {};
    if (apiKey) headers['Authorization'] = `Bearer ${apiKey}`;
    const resp = await fetch(`${base}/models`, { headers });
    if (!resp.ok) {
      return { ok: false, baseUrl: base, models: [], errorCode: 'http_error', error: `HTTP ${resp.status}` };
    }
    const data: any = await resp.json();
    const models = Array.isArray(data?.data)
      ? data.data.map((m: any) => m?.id).filter((id: unknown) => typeof id === 'string' && id.length > 0)
      : [];
    return { ok: true, baseUrl: base, models };
  } catch (err) {
    return { ok: false, baseUrl: base, models: [], errorCode: 'unreachable', error: String(err) };
  }
}

/** Renderiza os modelos descobertos como botões; clicar preenche o campo de modelo. */
export function renderModelChips(
  host: HTMLElement,
  models: string[],
  onPick: (model: string) => void
): void {
  host.innerHTML = '';
  if (models.length === 0) return;
  for (const model of models) {
    const btn = document.createElement('button');
    btn.type = 'button';
    btn.className = 'btn btn-secondary-pill';
    btn.style.fontSize = '11.5px';
    btn.style.padding = '4px 10px';
    btn.style.fontFamily = 'monospace';
    btn.textContent = model;
    btn.addEventListener('click', () => onPick(model));
    host.appendChild(btn);
  }
}
