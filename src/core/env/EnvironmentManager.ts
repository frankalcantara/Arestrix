/**
 * Arestrix Studio - Environment & Secrets Manager
 * 
 * DIRETRIZ ABSOLUTA DE SEGURANÇA:
 * NUNCA armazenar qualquer segredo, chave de API ou credencial em arquivos,
 * código-fonte, localStorage, cookies ou banco de dados.
 * 
 * Todas as chaves devem ser lidas estritamente de variáveis de ambiente do sistema.
 * Quando o usuário solicitar, podemos gravar variáveis de ambiente de usuário no SO.
 * Se não tivermos acesso, alertamos e ensinamos o usuário a gravar manualmente.
 */

declare const process: any;

export interface IEnvKeyGuidance {
  keyName: string;
  providerName: string;
  description: string;
  powershellCmd: string;
  cmdCmd: string;
  guiInstructions: string[];
}

/**
 * Padrão aceito para valores de environment em manifestos e configs MCP: apenas referências a
 * variáveis de ambiente (${NOME}, %NOME% ou $NOME). Valor literal (um segredo digitado) nunca é
 * gravado em arquivo nem em localStorage — espelha a regra do scripts/check-secrets.mjs.
 */
const ENV_REF_RE = /^\$\{[A-Za-z_][A-Za-z0-9_]*\}$|^%[A-Za-z_][A-Za-z0-9_]*%$|^\$[A-Za-z_][A-Za-z0-9_]*$/;

/**
 * Mantém apenas as entradas que são referências a variáveis de ambiente (ou vazias). Entradas com
 * valor literal são descartadas para nunca persistirem segredos em disco ou localStorage.
 */
export function sanitizeEnvReferences(env: Record<string, string> | null | undefined): Record<string, string> {
  if (!env || typeof env !== 'object') return {};
  const out: Record<string, string> = {};
  for (const [key, value] of Object.entries(env)) {
    if (typeof value !== 'string') continue;
    const trimmed = value.trim();
    if (trimmed.length === 0 || ENV_REF_RE.test(trimmed)) {
      out[key] = trimmed;
    } else {
      console.warn(`[EnvironmentManager] "${key}" descartada da persistencia: somente referencias de variaveis de ambiente (\${NOME}) sao gravadas.`);
    }
  }
  return out;
}

export class EnvironmentManager {
  private static instance: EnvironmentManager;

  public static getInstance(): EnvironmentManager {
    if (!EnvironmentManager.instance) {
      EnvironmentManager.instance = new EnvironmentManager();
    }
    return EnvironmentManager.instance;
  }

  /**
   * Lê uma variável de ambiente de forma segura
   * Tenta: 1) Bridge C++ WebUI do sistema; 2) process.env (Node/Electron); 3) Objeto global
   */
  public async getEnvVar(keyName: string): Promise<string | null> {
    if (!keyName) return null;

    // 1. Tenta via Bridge WebUI nativo C++
    try {
      if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
        const val = await (window as any).webui.call('get_env_var', keyName);
        if (typeof val === 'string' && val.trim().length > 0) {
          return val.trim();
        }
      }
    } catch {
      // Continua para alternativas
    }

    // 2. Tenta via process.env (Node.js ou Vitest)
    try {
      if (typeof process !== 'undefined' && process.env && process.env[keyName]) {
        const val = process.env[keyName];
        if (typeof val === 'string' && val.trim().length > 0) {
          return val.trim();
        }
      }
    } catch {
      // Ignora erro
    }

    // 3. Tenta via globalThis / window.__ARESTRIX_ENV__ seguro apenas em memória de sessão temporária
    try {
      const g = (typeof window !== 'undefined' ? window : globalThis) as any;
      if (g && g.__ARESTRIX_RUNTIME_ENV__ && g.__ARESTRIX_RUNTIME_ENV__[keyName]) {
        return g.__ARESTRIX_RUNTIME_ENV__[keyName];
      }
    } catch {
      // Ignora erro
    }

    return null;
  }

  /**
   * Verifica se uma variável de ambiente está definida e não vazia
   */
  public async hasEnvVar(keyName: string): Promise<boolean> {
    const val = await this.getEnvVar(keyName);
    return val !== null && val.length > 0;
  }

  /**
   * Grava uma variável de ambiente de usuário no sistema operacional Windows.
   * Se executado via C++, grava permanentemente nas variáveis de usuário.
   */
  public async setEnvVar(keyName: string, value: string): Promise<boolean> {
    if (!keyName || !value) return false;
    const cleanValue = value.trim();

    // Mantém na sessão atual em memória
    try {
      const g = (typeof window !== 'undefined' ? window : globalThis) as any;
      if (!g.__ARESTRIX_RUNTIME_ENV__) g.__ARESTRIX_RUNTIME_ENV__ = {};
      g.__ARESTRIX_RUNTIME_ENV__[keyName] = cleanValue;
    } catch {}

    // 1. Tenta gravar via C++ WebUI (Registro do usuário no Windows / ~/.config/arestrix no Linux, sem shell)
    try {
      if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
        const payload = JSON.stringify({ key: keyName, value: cleanValue });
        const res = await (window as any).webui.call('set_env_var', payload);
        if (res === true || res === 'true') {
          return true;
        }
      }
    } catch (e) {
      console.warn('[EnvironmentManager] Falha ao gravar variável no SO via WebUI:', e);
    }

    return false;
  }

  /**
   * Remove uma variável do processo atual e da configuração do usuário.
   */
  public async clearEnvVar(keyName: string): Promise<boolean> {
    if (!keyName) return false;
    try {
      const g = (typeof window !== 'undefined' ? window : globalThis) as any;
      if (g && g.__ARESTRIX_RUNTIME_ENV__) delete g.__ARESTRIX_RUNTIME_ENV__[keyName];
    } catch {}
    try {
      if (typeof window !== 'undefined' && (window as any).webui && (window as any).webui.call) {
        const res = await (window as any).webui.call('clear_env_var', keyName);
        return res === true || res === 'true';
      }
    } catch (e) {
      console.warn('[EnvironmentManager] Falha ao remover variável do SO via WebUI:', e);
    }
    return false;
  }

  /**
   * Retorna guia e comandos detalhados para o usuário gravar a variável no Windows
   */
  public getGuidance(keyName: string): IEnvKeyGuidance {
    const providerMap: Record<string, string> = {
      DEEPSEEK_API_KEY: 'DeepSeek (Modelo Padrão do Arestrix)',
      OPENAI_API_KEY: 'OpenAI (GPT-4o / O1)',
      ANTHROPIC_API_KEY: 'Anthropic (Claude 3.5 Sonnet)',
      GEMINI_API_KEY: 'Google Gemini (Gemini 2.0 Flash)',
      MISTRAL_API_KEY: 'Mistral AI (Large / Codestral)',
      GROQ_API_KEY: 'Groq Cloud LPU'
    };

    const providerName = providerMap[keyName] || 'Provedor de Inteligência Artificial';

    return {
      keyName,
      providerName,
      description: `Chave de acesso à API do ${providerName}. O Arestrix exige que esta chave seja armazenada exclusivamente nas variáveis de ambiente do seu sistema operacional para segurança absoluta de credenciais.`,
      powershellCmd: `[System.Environment]::SetEnvironmentVariable("${keyName}", "sua_chave_aqui", "User")`,
      cmdCmd: `setx ${keyName} "sua_chave_aqui"`,
      guiInstructions: [
        'Pressione as teclas Win + R, digite "sysdm.cpl" e pressione Enter.',
        'Na janela de Propriedades do Sistema, clique na aba "Avançado".',
        'Clique no botão "Variáveis de Ambiente...".',
        'Na seção superior ("Variáveis de usuário para seu_nome"), clique em "Nova...".',
        `No campo "Nome da variável", insira exatamente: ${keyName}`,
        'No campo "Valor da variável", cole a sua chave de API e clique em OK em todas as janelas.',
        'Reinicie o terminal ou aplicativo para carregar a nova variável do sistema.'
      ]
    };
  }
}
