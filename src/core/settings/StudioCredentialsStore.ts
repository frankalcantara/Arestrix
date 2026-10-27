import { EnvironmentManager } from '../env/EnvironmentManager.ts';
import type { IStudioCredentialsSnapshot, IStudioGatewaySettings } from './types.ts';

export const AI_GATEWAY_URL_VAR = 'ARESTRIX_AI_GATEWAY_URL';
export const AI_GATEWAY_KEY_VAR = 'ARESTRIX_AI_GATEWAY_KEY';
export const AI_GATEWAY_REQUIRE_VAR = 'ARESTRIX_REQUIRE_AI_GATEWAY';
export const AI_GATEWAY_POLICY_VAR = 'ARESTRIX_AI_GATEWAY_POLICY';

const PROVIDERS = [
  { id: 'deepseek', label: 'DeepSeek', keyName: 'DEEPSEEK_API_KEY' },
  { id: 'openai', label: 'OpenAI', keyName: 'OPENAI_API_KEY' },
  { id: 'anthropic', label: 'Anthropic', keyName: 'ANTHROPIC_API_KEY' },
  { id: 'google', label: 'Google (Gemini)', keyName: 'GEMINI_API_KEY' },
  { id: 'mistral', label: 'Mistral', keyName: 'MISTRAL_API_KEY' },
  { id: 'groq', label: 'Groq', keyName: 'GROQ_API_KEY' },
  { id: 'local', label: 'Local / OpenAI-compatível', keyName: 'LOCAL_LLM_API_KEY' },
  { id: 'jev', label: 'Jev / Engine Visual', keyName: 'JEV_API_KEY' }
] as const;

export interface IStudioCredentialsDraft {
  gatewayUrl: string;
  gatewayKey: string;
  gatewayRequire: boolean;
  apiKeys: Record<string, string>;
}

/**
 * Credenciais e AI Gateway do Studio.
 *
 * Somente status é lido pela UI. Valores de chave são write-only: o usuário
 * informa uma nova chave e o EnvironmentManager grava no SO/cofre. Nenhum valor
 * é colocado no DOM, no localStorage ou em studio.config.json.
 */
export class StudioCredentialsStore {
  private readonly env = EnvironmentManager.getInstance();

  public async load(): Promise<IStudioCredentialsSnapshot> {
    const [providers, url, keyConfigured, requireRaw, policy] = await Promise.all([
      Promise.all(PROVIDERS.map(async provider => ({
        id: provider.id,
        label: provider.label,
        keyName: provider.keyName,
        configured: await this.env.hasEnvVar(provider.keyName)
      }))),
      this.env.getEnvVar(AI_GATEWAY_URL_VAR),
      this.env.hasEnvVar(AI_GATEWAY_KEY_VAR),
      this.env.getEnvVar(AI_GATEWAY_REQUIRE_VAR),
      this.env.getEnvVar(AI_GATEWAY_POLICY_VAR)
    ]);

    const policyValue = (policy || '').toLowerCase();
    const gateway: IStudioGatewaySettings = {
      url: url || '',
      keyConfigured: Boolean(keyConfigured),
      requireGateway: requireRaw === '1' || (requireRaw || '').toLowerCase() === 'true',
      locked: policyValue === 'locked' || policyValue === 'enforced'
    };
    return { providers, gateway };
  }

  public async save(draft: IStudioCredentialsDraft): Promise<boolean> {
    const snapshot = await this.load();
    const results: boolean[] = [];

    for (const provider of snapshot.providers) {
      const value = draft.apiKeys[provider.id]?.trim();
      if (value) {
        results.push(await this.env.setEnvVar(provider.keyName, value));
      }
    }

    if (!snapshot.gateway.locked) {
      const url = draft.gatewayUrl.trim();
      if (url) {
        results.push(await this.env.setEnvVar(AI_GATEWAY_URL_VAR, url));
      } else {
        await this.env.clearEnvVar(AI_GATEWAY_URL_VAR);
      }

      if (draft.gatewayKey.trim()) {
        results.push(await this.env.setEnvVar(AI_GATEWAY_KEY_VAR, draft.gatewayKey.trim()));
      }
      results.push(await this.env.setEnvVar(AI_GATEWAY_REQUIRE_VAR, draft.gatewayRequire ? '1' : '0'));
    }

    return results.every(Boolean);
  }
}
