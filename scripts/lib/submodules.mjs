/**
 * Shared helpers for inspecting and updating the git submodules declared in
 * `.gitmodules`.
 *
 * The CI workflow (`.github/workflows/check-submodules.yml`) and the local
 * helpers (`npm run submodule:check` / `submodule:update`) both use this module
 * so the report, the safety rules and the update mechanism cannot drift apart.
 *
 * Design rules:
 * - never force-push or merge inside a submodule: a bot-created merge commit
 *   would be recorded as a gitlink that does not exist upstream;
 * - only fast-forward updates (`behind > 0 && ahead === 0`) are applied
 *   automatically; diverged submodules are reported for a human decision;
 * - reference skills (`third_party/SkillSpector` and
 *   `third_party/security-audit-skill`) always need the review described in
 *   `docs/quality/skill-reference-review.md`, so the report calls them out.
 */
import fs from 'node:fs';
import path from 'node:path';
import { spawnSync } from 'node:child_process';

/** Submodules backed by the Arestrix Skill Guard policy. */
export const REFERENCE_SKILL_PATHS = [
  'third_party/SkillSpector',
  'third_party/security-audit-skill'
];

/** How many upstream commits are listed per submodule in the report. */
export const DEFAULT_LOG_LIMIT = 10;

export const STATUS = {
  UP_TO_DATE: 'up-to-date',
  UPDATE_AVAILABLE: 'update-available',
  DIVERGED: 'diverged',
  MISSING: 'missing',
  ERROR: 'error'
};

function stripComment(line) {
  let inQuotes = false;
  for (let i = 0; i < line.length; i += 1) {
    const ch = line[i];
    if (ch === '"') inQuotes = !inQuotes;
    else if (!inQuotes && (ch === '#' || ch === ';')) return line.slice(0, i);
  }
  return line;
}

function unquote(value) {
  if (value.length >= 2 && value.startsWith('"') && value.endsWith('"')) {
    return value.slice(1, -1).replace(/\\"/g, '"').replace(/\\\\/g, '\\');
  }
  return value;
}

/**
 * Parse a `.gitmodules` file. Handles quoted values, comments and CRLF.
 * Returns one entry per `[submodule "..."]` section that declares `path`+`url`.
 */
export function parseGitmodules(text) {
  const entries = [];
  let current = null;

  for (const rawLine of String(text).split(/\r?\n/)) {
    const line = stripComment(rawLine).trim();
    if (!line) continue;

    const section = line.match(/^\[submodule\s+"(.+)"\]$/i);
    if (section) {
      current = { name: section[1], path: '', url: '', branch: '' };
      entries.push(current);
      continue;
    }

    if (line.startsWith('[')) {
      current = null;
      continue;
    }

    if (!current) continue;

    const assignment = line.match(/^([A-Za-z0-9._-]+)\s*=\s*(.*)$/);
    if (!assignment) continue;

    const key = assignment[1].toLowerCase();
    const value = unquote(assignment[2].trim());
    if (key === 'path') current.path = value;
    else if (key === 'url') current.url = value;
    else if (key === 'branch') current.branch = value;
  }

  return entries
    .filter((entry) => entry.path && entry.url)
    .map((entry) => ({
      ...entry,
      path: entry.path.replace(/\\/g, '/').replace(/\/+$/, ''),
      trackedBranch: entry.branch || '',
      referenceSkill: REFERENCE_SKILL_PATHS.includes(
        entry.path.replace(/\\/g, '/').replace(/\/+$/, '')
      )
    }));
}

/** Read and parse `<root>/.gitmodules`. Returns `[]` when the file is absent. */
export function readGitmodules(root) {
  const file = path.join(root, '.gitmodules');
  if (!fs.existsSync(file)) return [];
  return parseGitmodules(fs.readFileSync(file, 'utf8'));
}

function runGit(root, args, { allowFailure = false } = {}) {
  const result = spawnSync('git', args, {
    cwd: root,
    encoding: 'utf8',
    maxBuffer: 20 * 1024 * 1024
  });
  if (result.error) {
    if (allowFailure) return { code: 1, stdout: '', stderr: String(result.error) };
    throw result.error;
  }
  const output = {
    code: typeof result.status === 'number' ? result.status : 1,
    stdout: (result.stdout || '').trim(),
    stderr: (result.stderr || '').trim()
  };
  if (output.code !== 0 && !allowFailure) {
    throw new Error(
      `git ${args.join(' ')} falhou (${output.code}): ${output.stderr || output.stdout}`
    );
  }
  return output;
}

/** Resolve the upstream branch a submodule should track. */
export function resolveRemoteBranch(root, sub) {
  if (sub.trackedBranch) return sub.trackedBranch;

  const head = runGit(root, ['symbolic-ref', '--short', 'refs/remotes/origin/HEAD'], {
    allowFailure: true
  });
  if (head.code === 0 && head.stdout) {
    return head.stdout.replace(/^origin\//, '');
  }

  const remote = runGit(root, ['ls-remote', '--symref', 'origin', 'HEAD'], {
    allowFailure: true
  });
  if (remote.code === 0) {
    const match = remote.stdout.match(/^ref:\s+refs\/heads\/(\S+)\s+HEAD$/m);
    if (match) return match[1];
  }

  for (const candidate of ['main', 'master']) {
    const ref = runGit(root, ['rev-parse', '--verify', '--quiet', `refs/remotes/origin/${candidate}`], {
      allowFailure: true
    });
    if (ref.code === 0) return candidate;
  }

  return '';
}

function readManifestVersion(root, rev) {
  for (const file of ['package.json']) {
    const shown = runGit(root, ['show', `${rev}:${file}`], { allowFailure: true });
    if (shown.code !== 0) continue;
    try {
      const parsed = JSON.parse(shown.stdout);
      if (parsed && typeof parsed.version === 'string' && parsed.version) return parsed.version;
    } catch {
      /* not JSON: ignore */
    }
  }
  return '';
}

function describe(root, rev) {
  const described = runGit(root, ['describe', '--tags', '--always', rev], {
    allowFailure: true
  });
  return described.code === 0 ? described.stdout : '';
}

/**
 * Inspect a single submodule and compare its pinned commit with the tip of the
 * upstream branch. Never mutates the worktree.
 */
export function inspectSubmodule({
  root,
  sub,
  fetch = true,
  logLimit = DEFAULT_LOG_LIMIT
}) {
  const dir = path.join(root, sub.path);
  const result = {
    name: sub.name,
    path: sub.path,
    url: sub.url,
    trackedBranch: sub.trackedBranch || '',
    branch: '',
    referenceSkill: Boolean(sub.referenceSkill),
    status: STATUS.UP_TO_DATE,
    localRev: '',
    localShort: '',
    localDescribe: '',
    localVersion: '',
    remoteRev: '',
    remoteShort: '',
    remoteDescribe: '',
    remoteVersion: '',
    behind: 0,
    ahead: 0,
    commits: [],
    applied: false,
    error: ''
  };

  if (!fs.existsSync(dir)) {
    result.status = STATUS.MISSING;
    result.error = `submódulo ausente em ${sub.path} (rode git submodule update --init)`;
    return result;
  }

  if (fetch) {
    const fetched = runGit(dir, ['fetch', '--prune', 'origin'], {
      allowFailure: true
    });
    if (fetched.code !== 0) {
      result.status = STATUS.ERROR;
      result.error = `git fetch falhou: ${fetched.stderr || fetched.stdout}`;
      return result;
    }
  }

  result.branch = resolveRemoteBranch(dir, sub);
  if (!result.branch) {
    result.status = STATUS.ERROR;
    result.error = 'não foi possível resolver o branch remoto (defina branch = ... em .gitmodules)';
    return result;
  }

  const localRev = runGit(dir, ['rev-parse', 'HEAD'], { allowFailure: true });
  const remoteRev = runGit(dir, ['rev-parse', `origin/${result.branch}`], {
    allowFailure: true
  });
  if (localRev.code !== 0 || remoteRev.code !== 0) {
    result.status = STATUS.ERROR;
    result.error = `não foi possível resolver os commits locais/remotos (${
      remoteRev.stderr || localRev.stderr
    })`;
    return result;
  }

  result.localRev = localRev.stdout;
  result.remoteRev = remoteRev.stdout;
  result.localShort = result.localRev.slice(0, 7);
  result.remoteShort = result.remoteRev.slice(0, 7);
  result.localDescribe = describe(dir, result.localRev);
  result.remoteDescribe = describe(dir, result.remoteRev);
  result.localVersion = readManifestVersion(dir, result.localRev);
  result.remoteVersion = readManifestVersion(dir, result.remoteRev);

  if (result.localRev === result.remoteRev) {
    return result;
  }

  const behind = runGit(
    dir,
    ['rev-list', '--count', `HEAD..origin/${result.branch}`],
    { allowFailure: true }
  );
  const ahead = runGit(
    dir,
    ['rev-list', '--count', `origin/${result.branch}..HEAD`],
    { allowFailure: true }
  );
  result.behind = Number.parseInt(behind.stdout, 10) || 0;
  result.ahead = Number.parseInt(ahead.stdout, 10) || 0;

  const log = runGit(
    dir,
    [
      'log',
      `--max-count=${logLimit}`,
      '--no-decorate',
      '--format=%h %s',
      `HEAD..origin/${result.branch}`
    ],
    { allowFailure: true }
  );
  result.commits = log.code === 0 && log.stdout ? log.stdout.split('\n') : [];

  result.status =
    result.ahead > 0 ? STATUS.DIVERGED : STATUS.UPDATE_AVAILABLE;
  return result;
}

/**
 * Move a submodule to the exact upstream commit (detached HEAD, no merge
 * commits) and stage the new gitlink in the superproject.
 */
export function applySubmoduleUpdate({ root, sub, remoteRev }) {
  const dir = path.join(root, sub.path);
  const checkout = runGit(dir, ['checkout', '--quiet', '--detach', remoteRev], {
    allowFailure: true
  });
  if (checkout.code !== 0) {
    return {
      ok: false,
      commit: '',
      error: checkout.stderr || checkout.stdout
    };
  }

  const staged = runGit(root, ['add', '--', sub.path], { allowFailure: true });
  if (staged.code !== 0) {
    return { ok: false, commit: '', error: staged.stderr || staged.stdout };
  }

  const newRev = runGit(dir, ['rev-parse', 'HEAD'], { allowFailure: true });
  return { ok: true, commit: newRev.stdout, error: '' };
}

export function hasUpdates(results) {
  return results.some((item) => item.status === STATUS.UPDATE_AVAILABLE);
}

export function hasAlerts(results) {
  return results.some(
    (item) =>
      item.status === STATUS.DIVERGED ||
      item.status === STATUS.MISSING ||
      item.status === STATUS.ERROR
  );
}

function cell(value) {
  return String(value ?? '')
    .replace(/\|/g, '\\|')
    .replace(/\r?\n/g, ' ');
}

function describePair(item) {
  const local = item.localDescribe || item.localShort;
  const remote = item.remoteDescribe || item.remoteShort;
  return { local, remote };
}

function shortSha(rev) {
  return rev ? rev.slice(0, 7) : '—';
}

/**
 * Render the Markdown report used as the pull-request body, the workflow step
 * summary and the uploaded artifact.
 */
export function renderReport(results, options = {}) {
  const now = options.now instanceof Date ? options.now : new Date();
  const generatedAt = now.toISOString().replace('T', ' ').replace(/\.\d+Z$/, ' UTC');
  const updates = results.filter((item) => item.status === STATUS.UPDATE_AVAILABLE);
  const upToDate = results.filter((item) => item.status === STATUS.UP_TO_DATE);
  const diverged = results.filter((item) => item.status === STATUS.DIVERGED);
  const problems = results.filter(
    (item) => item.status === STATUS.MISSING || item.status === STATUS.ERROR
  );
  const skills = updates.filter((item) => item.referenceSkill);

  const lines = [];
  lines.push('<!-- arestrix:submodule-check -->');
  lines.push('# Submodule update check');
  lines.push('');
  lines.push(`Generated at **${generatedAt}** by \`.github/workflows/check-submodules.yml\`.`);
  lines.push(
    'The Arestrix build keeps using the commits pinned in `main` until this pull request is merged.'
  );
  lines.push('');

  if (results.length === 0) {
    lines.push('No submodules were found in `.gitmodules`.');
    return `${lines.join('\n')}\n`;
  }

  lines.push('## Summary');
  lines.push('');
  lines.push('| Submodule | Pinned | Upstream | Status |');
  lines.push('| :--- | :--- | :--- | :--- |');
  for (const item of results) {
    const pair = describePair(item);
    const pinned = item.localRev ? `${pair.local} (\`${shortSha(item.localRev)}\`)` : '—';
    const upstream = item.remoteRev ? `${pair.remote} (\`${shortSha(item.remoteRev)}\`)` : '—';
    const status =
      item.status === STATUS.UPDATE_AVAILABLE
        ? `⬆️ ${item.behind} new commit(s)`
        : item.status === STATUS.DIVERGED
          ? `⚠️ diverged (${item.ahead} local commit(s))`
          : item.status === STATUS.UP_TO_DATE
            ? '✅ up to date'
            : item.status === STATUS.MISSING
              ? '❌ missing checkout'
              : `❌ ${item.error || 'error'}`;
    lines.push(
      `| \`${cell(item.path)}\` | ${cell(pinned)} | ${cell(upstream)} | ${cell(status)} |`
    );
  }
  lines.push('');

  if (updates.length === 0) {
    lines.push('All submodules already point at the tip of their upstream branch. Nothing to merge.');
    lines.push('');
  } else {
    lines.push(
      `${updates.length} submodule(s) have upstream commits that are not pinned in \`main\`.`
    );
    lines.push('');
  }

  if (skills.length > 0) {
    lines.push('> [!IMPORTANT]');
    lines.push(
      '> `SkillSpector` and/or `security-audit-skill` changed. Merging the new gitlink is not enough: ' +
        'follow `docs/quality/skill-reference-review.md`, update the lock file and run ' +
        '`npm run check:skill-references`. An automated submodule PR never approves the skill policy.'
    );
    lines.push('');
  }

  if (diverged.length > 0) {
    lines.push('> [!WARNING]');
    lines.push(
      '> Diverged submodules are **not** updated automatically. The pinned commit contains work that ' +
        'is not on the upstream branch, so compare both sides and update the pointer by hand.'
    );
    lines.push('');
    for (const item of diverged) {
      lines.push(
        `> - \`${item.path}\`: ${item.ahead} local commit(s) not in \`origin/${item.branch}\`, ` +
          `${item.behind} upstream commit(s) missing.`
      );
    }
    lines.push('');
  }

  if (problems.length > 0) {
    lines.push('> [!CAUTION]');
    for (const item of problems) {
      lines.push(`> - \`${item.path}\`: ${item.error || item.status}`);
    }
    lines.push('');
  }

  if (updates.length > 0) {
    lines.push('## Upstream commits');
    lines.push('');
    for (const item of updates) {
      lines.push(`### \`${item.path}\``);
      lines.push('');
      lines.push(
        `- pinned: \`${item.localRev}\`${item.localVersion ? ` (v${item.localVersion})` : ''}`
      );
      lines.push(
        `- upstream: \`${item.remoteRev}\` on \`origin/${item.branch}\`` +
          `${item.remoteVersion ? ` (v${item.remoteVersion})` : ''}`
      );
      lines.push(`- difference: ${item.behind} new commit(s), ${item.ahead} local commit(s)`);
      lines.push('');
      if (item.commits.length > 0) {
        lines.push('```text');
        lines.push(...item.commits);
        lines.push('```');
        lines.push('');
      }
    }
  }

  if (upToDate.length > 0 && updates.length > 0) {
    lines.push('Already up to date: ' + upToDate.map((item) => `\`${item.path}\``).join(', ') + '.');
    lines.push('');
  }

  if (updates.length > 0) {
    lines.push('## Before merging');
    lines.push('');
    lines.push('```bash');
    lines.push('python scripts/check_third_party_licenses.py');
    lines.push('npm test');
    lines.push('npx tsc --noEmit');
    lines.push('python CppCore/dev.py --test --no-frontend');
    if (skills.length > 0) {
      lines.push('npm run check:skill-references');
    }
    lines.push('```');
    lines.push('');
    lines.push(
      'Check the upstream changelog and license of every updated project, then merge only if the ' +
        'four required CI checks are green. Closing this pull request keeps the pinned versions.'
    );
    lines.push('');
  }

  return `${lines.join('\n')}\n`;
}

/** Serialisable summary written next to the Markdown report. */
export function buildJsonReport(results, options = {}) {
  const now = options.now instanceof Date ? options.now : new Date();
  return {
    generatedAt: now.toISOString(),
    branch: options.branch || '',
    repository: options.repository || '',
    hasUpdates: hasUpdates(results),
    hasAlerts: hasAlerts(results),
    submodules: results.map((item) => ({
      name: item.name,
      path: item.path,
      url: item.url,
      branch: item.branch,
      status: item.status,
      pinned: item.localRev,
      pinnedDescribe: item.localDescribe,
      pinnedVersion: item.localVersion,
      upstream: item.remoteRev,
      upstreamDescribe: item.remoteDescribe,
      upstreamVersion: item.remoteVersion,
      behind: item.behind,
      ahead: item.ahead,
      applied: item.applied,
      referenceSkill: item.referenceSkill,
      commits: item.commits,
      error: item.error
    }))
  };
}
