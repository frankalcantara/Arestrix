import { EnvironmentManager } from '../env/EnvironmentManager.ts';
import type { IStudioModelSettings } from './types.ts';

export const STUDIO_PROVIDER_VAR = 'ARESTRIX_DEFAULT_PROVIDER';
export const STUDIO_MODEL_VAR = 'ARESTRIX_DEFAULT_MODEL';
export const STUDIO_LOCAL_BASE_URL_VAR = 'LOCAL_LLM_BASE_URL';
export const STUDIO_JEV_BASE_URL_VAR = 'JEV_BASE_URL';

function baseUrlVar(provider: string): string | null {
  if (provider === 'local') return STUDIO_LOCAL_BASE_URL_VAR;
  if (provider === 'jev') return STUDIO_JEV_BASE_URL_VAR;
  return null;
}

/**
 * Modelo padrão do Studio/Assistente.
 *
 * As variáveis de ambiente continuam sendo a fonte de verdade para o backend
 * nativo. Nenhum segredo é lido ou gravado por este store.
 */
export class StudioModelStore {
  private readonly env = EnvironmentManager.getInstance();

  public async load(): Promise<IStudioModelSettings> {
    const provider = (await this.env.getEnvVar(STUDIO_PROVIDER_VAR)) || 'auto';
    const model = (await this.env.getEnvVar(STUDIO_MODEL_VAR)) || '';
    const urlVar = baseUrlVar(provider);
    const baseUrl = urlVar ? ((await this.env.getEnvVar(urlVar)) || '') : '';
    return { provider, model, baseUrl };
  }

  public async save(settings: IStudioModelSettings): Promise<boolean> {
    const provider = settings.provider || 'auto';
    const model = settings.model.trim() || 'auto';
    if ((provider === 'local' || provider === 'jev') && model === 'auto') {
      return false;
    }

    const results = [
      await this.env.setEnvVar(STUDIO_PROVIDER_VAR, provider),
      await this.env.setEnvVar(STUDIO_MODEL_VAR, model)
    ];

    const urlVar = baseUrlVar(provider);
    if (urlVar) {
      const baseUrl = settings.baseUrl.trim();
      if (baseUrl) {
        results.push(await this.env.setEnvVar(urlVar, baseUrl));
      } else {
        await this.env.clearEnvVar(urlVar);
      }
    }

    return results.every(Boolean);
  }
}
