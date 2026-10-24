import { sha256 } from '../hash.ts';
import { INodeSkill } from '../types.ts';

/**
 * Arestrix Studio - Motor de Hashing Criptográfico e Canônico para Skills
 * 
 * Gera um digest SHA-256 determinístico sobre o conteúdo integral de uma skill.
 * Normaliza quebras de linha (CRLF -> LF), sanitiza espaçamentos e ordena
 * ferramentas para garantir idêntico resultado em qualquer sistema operacional.
 */
export class SkillHasher {
  /**
   * Normaliza texto para representação canônica:
   * - Substitui CRLF (\r\n) e CR (\r) por LF (\n)
   * - Remove espaços em branco ao fim de cada linha
   * - Remove linhas vazias redundantes ao fim do texto
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
   * Normaliza a lista de ferramentas solicitadas:
   * - Remove duplicatas
   * - Aplica trim
   * - Ordena alfabeticamente
   */
  public static normalizeTools(tools: string[] | undefined | null): string[] {
    if (!Array.isArray(tools)) return [];
    const set = new Set<string>();
    for (const t of tools) {
      const clean = (t || '').trim().toLowerCase();
      if (clean) set.add(clean);
    }
    return Array.from(set).sort();
  }

  /**
   * Constrói o payload JSON canônico para cálculo do hash.
   */
  public static buildCanonicalPayload(skill: Partial<INodeSkill>): string {
    const canonical = {
      name: (skill.name || '').trim(),
      tools: this.normalizeTools(skill.tools),
      markdownContent: this.normalizeText(skill.markdownContent),
      systemPrompt: this.normalizeText(skill.systemPrompt),
      license: (skill.license || '').trim().toUpperCase()
    };

    return JSON.stringify(canonical);
  }

  /**
   * Computa o hash SHA-256 canônico (64 caracteres hexadecimais em minúsculas) de uma skill.
   */
  public static computeHash(skill: Partial<INodeSkill>): string {
    const payload = this.buildCanonicalPayload(skill);
    return sha256(payload);
  }

  /**
   * Computa o hash diretamente a partir de um arquivo/conteúdo Markdown bruto.
   */
  public static computeFromMarkdown(
    markdownContent: string,
    tools: string[] = [],
    name: string = '',
    systemPrompt: string = '',
    license: string = ''
  ): string {
    return this.computeHash({
      name,
      tools,
      markdownContent,
      systemPrompt,
      license
    });
  }
}
