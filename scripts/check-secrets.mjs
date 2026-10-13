#!/usr/bin/env node
/**
 * Arestrix - verificador de segredos e dados pessoais.
 *
 * Existe para que NENHUMA chave de API, token, senha, caminho de usuario ou e-mail pessoal chegue ao Git.
 * Segredos do Arestrix vivem somente em variaveis de ambiente do usuario; arquivos de projeto so podem
 * citar o NOME da variavel (ex.: "DEEPSEEK_API_KEY") ou a referencia "${NOME}".
 *
 * Uso:
 *   node scripts/check-secrets.mjs               examina examples/ (arquivos rastreados e nao ignorados)
 *   node scripts/check-secrets.mjs --staged      examina o que esta no indice do Git (usado pelo pre-commit)
 *   node scripts/check-secrets.mjs --all         examina todo o repositorio rastreado
 *   node scripts/check-secrets.mjs caminho...    examina arquivos ou pastas especificos
 *
 * Saida com codigo 1 se algo suspeito for encontrado. O valor encontrado NUNCA e impresso por inteiro.
 */
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync, readdirSync, statSync } from 'node:fs';
import { dirname, join, relative, resolve, sep } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const ROOT = resolve(dirname(fileURLToPath(import.meta.url)), '..');

// --- Padroes de alta confianca (formatos reais de chaves) ---------------------------------------
const TOKEN_PATTERNS = [
  { name: 'chave sk-... (OpenAI/DeepSeek/Anthropic)', re: /\bsk-[A-Za-z0-9][A-Za-z0-9_-]{19,}/g },
  { name: 'chave Google (AIza...)', re: /\bAIza[0-9A-Za-z_-]{30,}/g },
  { name: 'chave Groq (gsk_...)', re: /\bgsk_[A-Za-z0-9]{30,}/g },
  { name: 'token GitHub', re: /\b(?:gh[pousr]_[A-Za-z0-9]{30,}|github_pat_[A-Za-z0-9_]{30,})/g },
  { name: 'token Slack', re: /\bxox[baprs]-[A-Za-z0-9-]{10,}/g },
  { name: 'chave de acesso AWS', re: /\b(?:AKIA|ASIA)[0-9A-Z]{16}\b/g },
  { name: 'chave privada', re: /-----BEGIN [A-Z ]*PRIVATE KEY-----/g },
  { name: 'JWT', re: /\beyJ[A-Za-z0-9_-]{15,}\.eyJ[A-Za-z0-9_-]{10,}\.[A-Za-z0-9_-]{10,}/g },
  { name: 'cabecalho Bearer com token', re: /\bBearer\s+[A-Za-z0-9._~+/=-]{24,}/g }
];

// Atribuicao de segredo: "api_key": "valor", token = 'valor', password: valor
const ASSIGNMENT_RE =
  /["']?\b([A-Za-z0-9_.-]*(?:api[_-]?key|secret|passw(?:or)?d|passwd|token|credential|private[_-]?key)[A-Za-z0-9_.-]*)["']?\s*[:=]\s*["']([^"'\r\n]{12,})["']/gi;

// Dados pessoais: caminhos de usuario e e-mails.
const PERSONAL_PATTERNS = [
  { name: 'caminho de usuario do Windows', re: /\b[A-Za-z]:[\\/]+Users[\\/]+(?!Public\b|Default\b|<|\{|%|USERNAME\b|you\b|user\b)[A-Za-z0-9._-]+/gi },
  { name: 'caminho de usuario do Linux/macOS', re: /(?:^|[\s"'=(])\/(?:home|Users)\/(?!<|\{|user\b|you\b|USERNAME\b|runner\b)[A-Za-z0-9._-]{2,}/g },
  { name: 'e-mail', re: /\b[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(?:\.[A-Za-z0-9-]+)*\.[A-Za-z]{2,}\b/g }
];

// Valores que claramente nao sao segredos reais.
const PLACEHOLDER_HINTS = /(test|fake|mock|dummy|example|sample|placeholder|changeme|your[-_]?|xxxx|\*{3,}|\.{3}|<[^>]+>|redacted|todo|demo)/i;
const ENV_NAME_RE = /^[A-Z][A-Z0-9_]{2,}$/;
const ENV_REF_RE = /^\$\{[A-Za-z_][A-Za-z0-9_]*\}$|^%[A-Za-z_][A-Za-z0-9_]*%$|^\$[A-Za-z_][A-Za-z0-9_]*$/;
const SAFE_EMAIL_DOMAINS = /@(example\.(com|org|net)|exemplo\.(com|org|net)|users\.noreply\.github\.com|anthropic\.com|invalid|localhost|test)$/i;
const SAFE_EMAILS = new Set(['noreply@anthropic.com', 'noreply@github.com']);

const SKIP_DIRS = new Set(['node_modules', '.git', 'third_party', 'dist', 'build', 'runtime', '.arestrix']);
const SKIP_FILES = new Set(['package-lock.json']);
const BINARY_EXT = /\.(png|jpe?g|gif|ico|webp|woff2?|ttf|otf|exe|dll|lib|obj|zip|pdf|pptx|docx|xlsx|xls|svgz|wasm|node|sqlite)$/i;

// Arquivos que NUNCA devem ser versionados dentro de exemplos.
const FORBIDDEN_NAME_RE = /(^|\/)(\.env(\..*)?|.*\.(pem|key|p12|pfx|sqlite|sqlite3|db)|id_rsa|id_ed25519|credentials(\.json)?|secrets?\.(json|ya?ml|toml))$/i;
const FORBIDDEN_DIR_RE = /(^|\/)(\.arestrix|output|storage|node_modules)\//;

/** Mostra so o comeco do valor para o relatorio nunca reproduzir um segredo. */
function mask(value) {
  const v = String(value);
  return v.length <= 6 ? '***' : `${v.slice(0, 4)}…(${v.length} caracteres)`;
}

function lineOf(text, index) {
  let line = 1;
  for (let i = 0; i < index; i++) if (text.charCodeAt(i) === 10) line++;
  return line;
}

function isPlaceholder(value) {
  return PLACEHOLDER_HINTS.test(value) || ENV_NAME_RE.test(value) || ENV_REF_RE.test(value);
}

/**
 * Examina um texto. Devolve achados {kind, rule, line, sample}. `file` serve para decidir regras por caminho.
 */
export function scanText(text, file = '') {
  const findings = [];
  const normalized = file.split(sep).join('/');

  for (const { name, re } of TOKEN_PATTERNS) {
    re.lastIndex = 0;
    for (const m of text.matchAll(re)) {
      if (PLACEHOLDER_HINTS.test(m[0])) continue;
      findings.push({ kind: 'segredo', rule: name, line: lineOf(text, m.index), sample: mask(m[0]) });
    }
  }

  ASSIGNMENT_RE.lastIndex = 0;
  for (const m of text.matchAll(ASSIGNMENT_RE)) {
    const value = m[2].trim();
    if (isPlaceholder(value)) continue;
    // segredos reais nao tem espacos: texto corrido (ex.: descricoes de interface) nunca e uma chave
    if (/\s/.test(value)) continue;
    if (/^[a-z][a-z-]*$/.test(value) && value.length < 16) continue; // palavra simples: provavelmente um enum
    findings.push({ kind: 'segredo', rule: `atribuicao de "${m[1]}"`, line: lineOf(text, m.index), sample: mask(value) });
  }

  for (const { name, re } of PERSONAL_PATTERNS) {
    re.lastIndex = 0;
    for (const m of text.matchAll(re)) {
      const value = m[0].trim();
      if (name === 'e-mail' && (SAFE_EMAILS.has(value.toLowerCase()) || SAFE_EMAIL_DOMAINS.test(value))) continue;
      findings.push({ kind: 'dado pessoal', rule: name, line: lineOf(text, m.index), sample: mask(value) });
    }
  }

  // Manifesto de projeto: nada de caminho absoluto nem variaveis de ambiente com valor literal.
  if (normalized.startsWith('examples/') && /(^|\/)arestrix\.project\.json$/.test(normalized)) {
    try {
      const manifest = JSON.parse(text);
      if (manifest.rootPath && manifest.rootPath !== '.') {
        findings.push({ kind: 'portabilidade', rule: 'rootPath deve ser "." em projetos de exemplo', line: 1, sample: mask(manifest.rootPath) });
      }
      const env = manifest.environment ?? {};
      for (const [k, v] of Object.entries(env)) {
        if (typeof v === 'string' && v.length > 0 && !ENV_REF_RE.test(v)) {
          findings.push({ kind: 'segredo', rule: `environment.${k} com valor literal (use \${NOME})`, line: 1, sample: mask(v) });
        }
      }
    } catch {
      findings.push({ kind: 'formato', rule: 'arestrix.project.json invalido', line: 1, sample: '' });
    }
  }

  // Configuracao de servidor MCP: "env" so pode referenciar variaveis (${NOME}), nunca guardar o valor.
  if (/(^|\/)\.workspace\/mcps\/[^/]+\.json$/.test(normalized) || /arestrix-mcp-servers\.json$/.test(normalized)) {
    try {
      const parsed = JSON.parse(text);
      const servers = Array.isArray(parsed) ? parsed : [parsed];
      for (const s of servers) {
        for (const [k, v] of Object.entries(s?.env ?? {})) {
          if (typeof v === 'string' && v.length > 0 && !ENV_REF_RE.test(v)) {
            findings.push({ kind: 'segredo', rule: `MCP "${s.id ?? '?'}": env.${k} com valor literal (use \${NOME})`, line: 1, sample: mask(v) });
          }
        }
      }
    } catch {
      findings.push({ kind: 'formato', rule: 'configuracao MCP invalida', line: 1, sample: '' });
    }
  }

  return findings;
}

/** Regras por nome de arquivo (independentes do conteudo). */
export function checkPath(file) {
  const normalized = file.split(sep).join('/');
  const findings = [];
  if (FORBIDDEN_NAME_RE.test(normalized)) {
    findings.push({ kind: 'arquivo proibido', rule: 'arquivo de credencial ou dado local', line: 0, sample: normalized });
  }
  if (normalized.startsWith('examples/') && FORBIDDEN_DIR_RE.test(normalized)) {
    findings.push({ kind: 'arquivo proibido', rule: 'artefato gerado em exemplo (.arestrix, output, storage)', line: 0, sample: normalized });
  }
  return findings;
}

function git(args) {
  return execFileSync('git', args, { cwd: ROOT, encoding: 'utf8', maxBuffer: 64 * 1024 * 1024 });
}

function walk(dir, out) {
  for (const entry of readdirSync(dir)) {
    if (SKIP_DIRS.has(entry)) continue;
    const full = join(dir, entry);
    const st = statSync(full);
    if (st.isDirectory()) walk(full, out);
    else out.push(full);
  }
}

/** Lista de arquivos a examinar conforme o modo. Caminhos relativos a raiz, com "/". */
export function collectFiles(mode, extra = []) {
  const rel = (p) => relative(ROOT, resolve(ROOT, p)).split(sep).join('/');
  if (mode === 'staged') {
    return git(['diff', '--cached', '--name-only', '--diff-filter=ACMR']).split('\n').filter(Boolean);
  }
  if (mode === 'all') {
    return git(['ls-files']).split('\n').filter(Boolean);
  }
  if (extra.length) {
    const out = [];
    for (const p of extra) {
      const abs = resolve(ROOT, p);
      if (!existsSync(abs)) continue;
      if (statSync(abs).isDirectory()) walk(abs, out);
      else out.push(abs);
    }
    return out.map(rel);
  }
  // padrao: examples/ = rastreados + nao rastreados que o Git nao ignora (o que um "git add ." incluiria)
  const tracked = git(['ls-files', '--cached', '--others', '--exclude-standard', '--', 'examples']).split('\n').filter(Boolean);
  return tracked;
}

export function scanFiles(files, { readStaged = false } = {}) {
  const results = [];
  for (const file of files) {
    const base = file.split('/').pop();
    if (SKIP_FILES.has(base) || file.startsWith('third_party/') || BINARY_EXT.test(file)) continue;

    for (const f of checkPath(file)) results.push({ file, ...f });

    let text;
    try {
      text = readStaged ? git(['show', `:${file}`]) : readFileSync(resolve(ROOT, file), 'utf8');
    } catch {
      continue; // removido ou ilegivel
    }
    if (text.length > 4 * 1024 * 1024) continue;
    for (const f of scanText(text, file)) results.push({ file, ...f });
  }
  return results;
}

function main(argv) {
  const args = argv.slice(2);
  const mode = args.includes('--staged') ? 'staged' : args.includes('--all') ? 'all' : 'default';
  const extra = args.filter(a => !a.startsWith('--'));
  const files = collectFiles(mode, extra);
  const results = scanFiles(files, { readStaged: mode === 'staged' });

  if (results.length === 0) {
    console.log(`check-secrets: ${files.length} arquivo(s) examinado(s), nenhum segredo ou dado pessoal encontrado.`);
    return 0;
  }
  console.error(`\ncheck-secrets: ${results.length} problema(s) encontrado(s):\n`);
  for (const r of results) {
    console.error(`  [${r.kind}] ${r.file}${r.line ? `:${r.line}` : ''}  ${r.rule}${r.sample ? `  (${r.sample})` : ''}`);
  }
  console.error(
    '\nSegredos devem ficar somente em variaveis de ambiente do usuario. Remova o valor, troque por ${NOME_DA_VARIAVEL}' +
    '\n(ou pelo nome da variavel) e, se a chave chegou a ser digitada num arquivo, REVOGUE-A no provedor.\n'
  );
  return 1;
}

if (process.argv[1] && import.meta.url === pathToFileURL(resolve(process.argv[1])).href) {
  process.exit(main(process.argv));
}
