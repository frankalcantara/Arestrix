import { sha256 } from '../hash.ts';
import { IMcpServerConfig, IMcpToolDefinition } from '../types.ts';

/**
 * Arestrix Studio - Motor de Hashing Criptográfico Canônico para Servidores e Ferramentas MCP
 * 
 * Gera um digest SHA-256 determinístico sobre a configuração integral do servidor MCP,
 * incluindo transporte, comandos, argumentos, chaves de ambiente e a lista ordenada de
 * ferramentas com suas descrições e esquemas JSON normalizados.
 * 
 * Qualquer alteração em um único caractere da descrição de uma ferramenta (ex: tentativa
 * de MCP Tool Poisoning) ou modificação de argumentos/schemas altera o hash imediatamente.
 */
export class McpHasher {
  /**
   * Normaliza textos de descrição e metadados:
   * - Substitui CRLF (\r\n) por LF (\n)
   * - Remove espaços em branco ao fim de linhas
   * - Aplica trim geral
   */
  public static normalizeText(text: string | undefined | null): string {
    if (!text) return '';
    return text
      .replace(/\r\n/g, '\n')
      .replace(/\r/g, '\n')
      .split('\n')
      .map(line => line.trimEnd())
      .join('\n')
      .trim();
  }

  /**
   * Serializa deterministamente um objeto JSON com chaves ordenadas alfabeticamente.
   */
  public static canonicalizeJson(obj: any): string {
    if (obj === null || obj === undefined) return '{}';
    if (typeof obj !== 'object') return JSON.stringify(obj);

    if (Array.isArray(obj)) {
      return '[' + obj.map(item => this.canonicalizeJson(item)).join(',') + ']';
    }

    const sortedKeys = Object.keys(obj).sort();
    const parts = sortedKeys.map(k => `${JSON.stringify(k)}:${this.canonicalizeJson(obj[k])}`);
    return '{' + parts.join(',') + '}';
  }

  /**
   * Normaliza a lista de ferramentas declaradas pelo servidor MCP.
   */
  public static normalizeTools(tools: IMcpToolDefinition[] | undefined | null): Array<{
    name: string;
    description: string;
    inputSchema: string;
  }> {
    if (!Array.isArray(tools)) return [];

    return tools
      .map(t => ({
        name: (t.name || '').trim().toLowerCase(),
        description: this.normalizeText(t.description),
        inputSchema: this.canonicalizeJson(t.inputSchema || {})
      }))
      .sort((a, b) => a.name.localeCompare(b.name));
  }

  /**
   * Constrói o payload JSON canônico do servidor MCP.
   */
  public static buildCanonicalPayload(server: Partial<IMcpServerConfig>): string {
    const canonical = {
      id: (server.id || '').trim().toLowerCase(),
      transport: (server.transport || 'stdio').toLowerCase(),
      command: (server.command || '').trim(),
      args: (server.args || []).map(a => a.trim()),
      url: (server.url || '').trim(),
      envKeys: Object.keys(server.env || {}).map(k => k.trim()).sort(),
      headerKeys: Object.keys(server.headers || {}).map(k => k.trim().toLowerCase()).sort(),
      tools: this.normalizeTools(server.cachedTools)
    };

    return JSON.stringify(canonical);
  }

  /**
   * Computa o hash SHA-256 canônico de um servidor MCP e suas ferramentas.
   */
  public static computeHash(server: Partial<IMcpServerConfig>): string {
    const payload = this.buildCanonicalPayload(server);
    return sha256(payload);
  }
}
