/**
 * Arestrix - Quebra determinística de texto para os vértices SVG.
 *
 * O SVG não quebra texto automaticamente: a quebra é feita aqui, preferindo
 * espaços para não cortar palavras, antes de montar os elementos <text>.
 */

/**
 * Estimativa de largura média de glifo da Inter (em fração do tamanho da fonte).
 * 0.6 dá margem de segurança para maiúsculas e acentos sem estourar a largura do cartão.
 */
const INTER_AVG_CHAR_WIDTH = 0.6;

/**
 * Trunca um texto para caber em `maxLines` linhas de largura `maxWidth` com reticências.
 * Retorna o texto final (com `\n` entre as linhas) e se houve truncamento
 * (para exibir o texto completo em tooltip).
 */
export function fitTextToLines(
  text: string,
  fontSize: number,
  maxWidth: number,
  maxLines: number
): { text: string; truncated: boolean } {
  const clean = (text ?? '').replace(/\s+/g, ' ').trim();
  const charsPerLine = Math.max(4, Math.floor(maxWidth / (fontSize * INTER_AVG_CHAR_WIDTH)));
  const lineLimit = Math.max(1, maxLines);

  const lines: string[] = [];
  let rest = clean;

  while (rest.length > 0 && lines.length < lineLimit) {
    if (rest.length <= charsPerLine) {
      lines.push(rest);
      rest = '';
      break;
    }

    // Prefere o último espaço da faixa; se ele estiver muito no começo, corta no limite.
    const candidate = rest.slice(0, charsPerLine + 1);
    const spaceIndex = candidate.lastIndexOf(' ');
    const cut = spaceIndex > charsPerLine * 0.5 ? spaceIndex : charsPerLine;

    lines.push(rest.slice(0, cut).trimEnd());
    rest = rest.slice(cut).trimStart();
  }

  const truncated = rest.length > 0;
  if (truncated && lines.length > 0) {
    const lastIndex = lines.length - 1;
    lines[lastIndex] = lines[lastIndex].slice(0, Math.max(1, charsPerLine - 1)).trimEnd() + '…';
  }

  return { text: lines.join('\n'), truncated };
}
