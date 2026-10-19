import { describe, it, expect, afterEach, vi } from 'vitest';
import {
  LOCAL_LLM_DEFAULT_URL,
  discoverLocalModels,
  normalizeLocalBaseUrl
} from '../src/ui/LocalModelField.ts';

describe('LocalModelField - modelos locais sem instalar servidor', () => {
  afterEach(() => {
    delete (globalThis as any).window;
    vi.unstubAllGlobals();
    vi.restoreAllMocks();
  });

  it('normaliza a URL base como o backend (host puro vira /v1)', () => {
    expect(normalizeLocalBaseUrl('http://localhost:11434')).toBe('http://localhost:11434/v1');
    expect(normalizeLocalBaseUrl('http://localhost:11434/')).toBe('http://localhost:11434/v1');
    expect(normalizeLocalBaseUrl('  http://localhost:8080/v1/  ')).toBe('http://localhost:8080/v1');
    expect(normalizeLocalBaseUrl('http://localhost:8080/v1/models')).toBe('http://localhost:8080/v1');
    expect(normalizeLocalBaseUrl('http://localhost:8080/v1/chat/completions')).toBe('http://localhost:8080/v1');
    expect(normalizeLocalBaseUrl('http://localhost:8080/custom')).toBe('http://localhost:8080/custom');
    expect(normalizeLocalBaseUrl('')).toBe('');
  });

  it('usa o backend nativo quando a bridge WebUI existe (sem CORS)', async () => {
    const calls: Array<{ fn: string; arg: string }> = [];
    (globalThis as any).window = {
      webui: {
        call: async (fn: string, arg: string) => {
          calls.push({ fn, arg });
          if (fn === 'get_env_var') return '';
          return JSON.stringify({
            ok: true,
            baseUrl: 'http://localhost:11434/v1',
            models: ['qwen2.5:7b', 'llama3.1:8b']
          });
        }
      }
    };

    const result = await discoverLocalModels('http://localhost:11434');
    expect(result.ok).toBe(true);
    expect(result.models).toEqual(['qwen2.5:7b', 'llama3.1:8b']);
    const listCall = calls.find(c => c.fn === 'list_local_models');
    expect(listCall).toBeDefined();
    expect(JSON.parse(listCall!.arg)).toEqual({ baseUrl: 'http://localhost:11434/v1' });
  });

  it('devolve erro estável quando o servidor local não está no ar', async () => {
    (globalThis as any).window = {
      webui: {
        call: async (fn: string) => fn === 'get_env_var'
          ? ''
          : JSON.stringify({ ok: false, errorCode: 'unreachable', error: 'conexão recusada' })
      }
    };

    const result = await discoverLocalModels('http://localhost:11434');
    expect(result.ok).toBe(false);
    expect(result.errorCode).toBe('unreachable');
    expect(result.baseUrl).toBe('http://localhost:11434/v1');
  });

  it('cai para fetch /models em ambiente de desenvolvimento sem backend', async () => {
    vi.stubGlobal('fetch', vi.fn(async () => ({
      ok: true,
      json: async () => ({ data: [{ id: 'qwen2.5:7b' }] })
    })));

    const result = await discoverLocalModels('');
    expect(result.ok).toBe(true);
    expect(result.baseUrl).toBe(LOCAL_LLM_DEFAULT_URL);
    expect(result.models).toEqual(['qwen2.5:7b']);
  });

  it('recusa endereço sem http(s)', async () => {
    const result = await discoverLocalModels('localhost:11434');
    expect(result.ok).toBe(false);
    expect(result.errorCode).toBe('invalid_url');
  });
});
