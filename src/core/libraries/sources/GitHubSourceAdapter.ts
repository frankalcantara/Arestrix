/**
 * Arestrix Studio - Adaptador da fonte GitHub
 *
 * Fala exclusivamente com o endpoint C++ `fetch_library_source`. O frontend
 * nunca acessa api.github.com/raw.githubusercontent.com diretamente.
 *
 * Dois modos:
 *  - manifest: metadados + árvore, sem baixar blobs;
 *  - asset: baixa um único blob sob demanda para a quarentena.
 */

export type GitHubAssetKind = 'skill' | 'mcp';

export interface IGitHubAssetCandidate {
  path: string;
  sha: string;
  size: number;
  kind: GitHubAssetKind;
  name: string;
}

export interface IGitHubManifestResponse {
  ok: boolean;
  repo?: string;
  ref?: string;
  commit?: string;
  license?: string;
  defaultBranch?: string;
  private?: boolean;
  archived?: boolean;
  truncated?: boolean;
  assets?: IGitHubAssetCandidate[];
  error?: string;
  errorCode?: string;
}

export interface IGitHubAssetResponse {
  ok: boolean;
  repo?: string;
  ref?: string;
  commit?: string;
  path?: string;
  kind?: GitHubAssetKind;
  content?: string;
  size?: number;
  error?: string;
  errorCode?: string;
}

export class GitHubSourceAdapter {
  private static instance: GitHubSourceAdapter;

  public static getInstance(): GitHubSourceAdapter {
    if (!GitHubSourceAdapter.instance) {
      GitHubSourceAdapter.instance = new GitHubSourceAdapter();
    }
    return GitHubSourceAdapter.instance;
  }

  public isNativeAvailable(): boolean {
    if (typeof window === 'undefined') return false;
    const webui = (window as any).webui;
    return Boolean(webui && typeof webui.call === 'function');
  }

  public async fetchManifest(repo: string, ref = 'main'): Promise<IGitHubManifestResponse> {
    return this.call({
      mode: 'manifest',
      repo,
      ref
    }) as Promise<IGitHubManifestResponse>;
  }

  public async fetchAsset(
    repo: string,
    ref: string,
    path: string,
    commit: string
  ): Promise<IGitHubAssetResponse> {
    return this.call({
      mode: 'asset',
      repo,
      ref,
      path,
      commit
    }) as Promise<IGitHubAssetResponse>;
  }

  private async call(payload: Record<string, unknown>): Promise<any> {
    if (!this.isNativeAvailable()) {
      return {
        ok: false,
        errorCode: 'github_native_required',
        error: 'A fonte GitHub exige o aplicativo nativo do Arestrix.'
      };
    }

    try {
      const raw = await (window as any).webui.call('fetch_library_source', JSON.stringify(payload));
      if (!raw) {
        return {
          ok: false,
          errorCode: 'github_empty_response',
          error: 'O backend não retornou dados do GitHub.'
        };
      }
      return typeof raw === 'string' ? JSON.parse(raw) : raw;
    } catch (error) {
      return {
        ok: false,
        errorCode: 'github_request_failed',
        error: String(error)
      };
    }
  }
}
