#!/usr/bin/env node
/**
 * Report (and optionally apply) upstream updates for the git submodules.
 *
 * Usage:
 *   node scripts/check-submodule-updates.mjs
 *   node scripts/check-submodule-updates.mjs --report submodule-updates.md \
 *       --json submodule-updates.json --apply
 *
 * Options:
 *   --root <dir>      repository root (default: the parent of scripts/)
 *   --report <file>   write the Markdown report
 *   --json <file>     write the JSON summary
 *   --apply           fast-forward every updatable submodule to the upstream
 *                     tip and stage the new gitlinks (never creates merge
 *                     commits, never touches diverged submodules)
 *   --only <list>     comma-separated submodule names or paths to consider
 *   --no-fetch        use the refs already in the submodule clones
 *   --quiet           only print the summary
 *
 * Exit code: 0 when the check ran (updates found or not), 1 when a submodule
 * is missing, unreachable or diverged. `--apply` never exits 0 after a failed
 * checkout.
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  STATUS,
  applySubmoduleUpdate,
  buildJsonReport,
  hasAlerts,
  inspectSubmodule,
  readGitmodules,
  renderReport
} from './lib/submodules.mjs';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const DEFAULT_ROOT = path.resolve(__dirname, '..');

function parseArgs(argv) {
  const options = {
    root: DEFAULT_ROOT,
    report: '',
    json: '',
    apply: false,
    only: [],
    fetch: true,
    quiet: false
  };

  for (let i = 0; i < argv.length; i += 1) {
    const arg = argv[i];
    const next = () => {
      const value = argv[i + 1];
      if (value === undefined || value.startsWith('--')) {
        throw new Error(`faltou valor para ${arg}`);
      }
      i += 1;
      return value;
    };

    if (arg === '--root') options.root = path.resolve(next());
    else if (arg === '--report') options.report = path.resolve(next());
    else if (arg === '--json') options.json = path.resolve(next());
    else if (arg === '--only') {
      options.only = next()
        .split(',')
        .map((value) => value.trim())
        .filter(Boolean);
    } else if (arg === '--apply') options.apply = true;
    else if (arg === '--no-fetch') options.fetch = false;
    else if (arg === '--quiet') options.quiet = true;
    else if (arg === '--help' || arg === '-h') options.help = true;
    else throw new Error(`opção desconhecida: ${arg}`);
  }

  return options;
}

const HELP = `Verifica atualizações de submódulos e gera um relatório.

  node scripts/check-submodule-updates.mjs [--root <dir>] [--report <file>]
      [--json <file>] [--apply] [--only <nomes>] [--no-fetch] [--quiet]

Sem --apply o repositório não é modificado. Com --apply, apenas submódulos
que podem avançar em fast-forward são movidos para o commit do upstream.`;

function selectSubmodules(all, only) {
  if (only.length === 0) return all;
  const wanted = new Set(only.map((value) => value.replace(/\\/g, '/').replace(/\/+$/, '')));
  return all.filter((sub) => wanted.has(sub.name) || wanted.has(sub.path));
}

function appendStepSummary(markdown) {
  const file = process.env.GITHUB_STEP_SUMMARY;
  if (!file) return;
  fs.appendFileSync(file, markdown, 'utf8');
}

function writeOutputs(values) {
  const file = process.env.GITHUB_OUTPUT;
  if (!file) return;
  const lines = Object.entries(values).map(([key, value]) => `${key}=${value}`);
  fs.appendFileSync(file, `${lines.join('\n')}\n`, 'utf8');
}

function appendEnv(values) {
  const file = process.env.GITHUB_ENV;
  if (!file) return;
  const lines = Object.entries(values).map(([key, value]) => `${key}=${value}`);
  fs.appendFileSync(file, `${lines.join('\n')}\n`, 'utf8');
}

function formatResult(item) {
  const pinned = item.localDescribe || item.localShort || '—';
  if (item.status === STATUS.UP_TO_DATE) {
    return `✅ ${item.path} — ${pinned} (up to date)`;
  }
  if (item.status === STATUS.UPDATE_AVAILABLE) {
    const upstream = item.remoteDescribe || item.remoteShort;
    return (
      `⬆️  ${item.path} — ${pinned} → ${upstream} ` +
      `(${item.behind} commit(s) on origin/${item.branch})`
    );
  }
  if (item.status === STATUS.DIVERGED) {
    return (
      `⚠️  ${item.path} — diverged: ${item.ahead} local commit(s), ` +
      `${item.behind} upstream commit(s)`
    );
  }
  return `❌ ${item.path} — ${item.error || item.status}`;
}

function main() {
  let options;
  try {
    options = parseArgs(process.argv.slice(2));
  } catch (error) {
    console.error(`Erro: ${error.message}\n\n${HELP}`);
    return 2;
  }

  if (options.help) {
    console.log(HELP);
    return 0;
  }

  const all = readGitmodules(options.root);
  if (all.length === 0) {
    console.error(`Nenhum submódulo encontrado em ${path.join(options.root, '.gitmodules')}.`);
    return 1;
  }

  const submodules = selectSubmodules(all, options.only);
  if (submodules.length === 0) {
    console.error(`Nenhum submódulo corresponde a --only ${options.only.join(',')}.`);
    return 1;
  }

  if (!options.quiet) {
    console.log(`Verificando ${submodules.length} submódulo(s) em ${options.root}...\n`);
  }

  const results = submodules.map((sub) => inspectSubmodule({ root: options.root, sub, fetch: options.fetch }));

  if (options.apply) {
    for (const item of results) {
      if (item.status !== STATUS.UPDATE_AVAILABLE) continue;
      const sub = submodules.find((candidate) => candidate.path === item.path);
      const applied = applySubmoduleUpdate({
        root: options.root,
        sub,
        remoteRev: item.remoteRev
      });
      item.applied = applied.ok;
      if (!applied.ok) {
        item.status = STATUS.ERROR;
        item.error = `falha ao mover o submódulo: ${applied.error}`;
      }
    }
  }

  if (!options.quiet) {
    for (const item of results) console.log(formatResult(item));
    console.log('');
  }

  const updates = results.filter((item) => item.status === STATUS.UPDATE_AVAILABLE);
  const alerts = hasAlerts(results);
  const now = new Date();
  const markdown = renderReport(results, {
    now,
    branch: process.env.GITHUB_REF_NAME || '',
    repository: process.env.GITHUB_REPOSITORY || ''
  });
  const json = buildJsonReport(results, {
    now,
    branch: process.env.GITHUB_REF_NAME || '',
    repository: process.env.GITHUB_REPOSITORY || ''
  });

  if (options.report) {
    fs.writeFileSync(options.report, markdown, 'utf8');
    if (!options.quiet) console.log(`Relatório Markdown: ${options.report}`);
  }
  if (options.json) {
    fs.writeFileSync(options.json, `${JSON.stringify(json, null, 2)}\n`, 'utf8');
    if (!options.quiet) console.log(`Resumo JSON: ${options.json}`);
  }

  appendStepSummary(markdown);

  const updatesValue = updates.map((item) => item.path).join(',');
  writeOutputs({
    has_updates: updates.length > 0 ? 'true' : 'false',
    has_alerts: alerts ? 'true' : 'false',
    updates: updatesValue,
    submodules: results.map((item) => item.path).join(',')
  });
  appendEnv({
    HAS_UPDATES: updates.length > 0 ? 'true' : 'false',
    SUBMODULE_UPDATES: updatesValue,
    HAS_ALERTS: alerts ? 'true' : 'false'
  });

  if (updates.length > 0 && options.apply) {
    console.log(`\n${updates.length} submódulo(s) atualizado(s); gitlinks marcados para commit.`);
  } else if (updates.length > 0) {
    console.log(`\n${updates.length} atualização(ões) disponível(is); nada foi alterado.`);
  } else if (alerts) {
    console.log('\nNenhum submódulo pode ser atualizado automaticamente (veja os alertas acima).');
  } else {
    console.log('\nNenhuma atualização de submódulo disponível.');
  }
  if (alerts) {
    console.error('\nAtenção: há submódulos ausentes, inacessíveis ou divergentes.');
    return 1;
  }
  return 0;
}

try {
  process.exitCode = main();
} catch (error) {
  console.error(`Erro na verificação de submódulos: ${error instanceof Error ? error.message : error}`);
  process.exitCode = 1;
}
