#!/usr/bin/env node
/**
 * Local submodule helper.
 *
 *   npm run submodule:check    report upstream commits without touching the tree
 *   npm run submodule:update   fast-forward every updatable submodule
 *
 * The same inspection and safety rules run in CI through
 * `scripts/check-submodule-updates.mjs` and
 * `.github/workflows/check-submodules.yml`.
 */
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import {
  STATUS,
  applySubmoduleUpdate,
  inspectSubmodule,
  readGitmodules
} from './lib/submodules.mjs';

const __dirname = path.dirname(fileURLToPath(import.meta.url));
const ROOT_DIR = path.resolve(__dirname, '..');

function label(item) {
  const pinned = item.localDescribe || item.localShort || '—';
  const upstream = item.remoteDescribe || item.remoteShort || '—';
  const version = item.localVersion ? `v${item.localVersion}` : '';
  return `${pinned}${version ? ` (${version})` : ''} → ${upstream}`;
}

function main() {
  const args = process.argv.slice(2);
  const shouldUpdate = args.includes('--update') || args.includes('-u');

  console.log('=======================================================');
  console.log('  Arestrix Studio - Gerenciador de Submódulos Git');
  console.log('=======================================================\n');

  const submodules = readGitmodules(ROOT_DIR);
  if (submodules.length === 0) {
    console.error(`Nenhum submódulo declarado em ${path.join(ROOT_DIR, '.gitmodules')}.`);
    return 1;
  }

  const results = submodules.map((sub) => inspectSubmodule({ root: ROOT_DIR, sub }));
  let failed = false;

  for (const item of results) {
    const sub = submodules.find((candidate) => candidate.path === item.path);

    if (item.status === STATUS.UP_TO_DATE) {
      console.log(`✅ [${item.name}] Atualizado — ${label(item)}\n`);
      continue;
    }

    if (item.status === STATUS.UPDATE_AVAILABLE) {
      console.log(`⚠️  [${item.name}] Nova atualização disponível — ${label(item)}`);
      console.log(`   ${item.behind} commit(s) em origin/${item.branch}:`);
      for (const commit of item.commits) console.log(`     * ${commit}`);

      if (shouldUpdate) {
        const applied = applySubmoduleUpdate({
          root: ROOT_DIR,
          sub,
          remoteRev: item.remoteRev
        });
        if (applied.ok) {
          console.log(`✨ [${item.name}] Atualizado para ${item.remoteShort}.`);
        } else {
          failed = true;
          console.error(`❌ [${item.name}] Falha ao atualizar: ${applied.error}`);
        }
      }
      console.log('');
      continue;
    }

    failed = true;
    if (item.status === STATUS.DIVERGED) {
      console.error(
        `⚠️  [${item.name}] Divergente: ${item.ahead} commit(s) locais fora de ` +
          `origin/${item.branch} e ${item.behind} commit(s) remotos ausentes. ` +
          'Resolva manualmente.\n'
      );
    } else {
      console.error(`❌ [${item.name}] ${item.error || item.status}\n`);
    }
  }

  if (shouldUpdate) {
    console.log('Próximos passos antes de abrir o pull request:');
    console.log('  python scripts/check_third_party_licenses.py');
    console.log('  npm test');
    console.log('  npx tsc --noEmit');
    console.log('  python CppCore/dev.py --test --no-frontend');
    console.log('  npm run check:skill-references   # se SkillSpector/security-audit-skill mudou');
    console.log('');
    console.log('Os submódulos de skill exigem a revisão de docs/quality/skill-reference-review.md.');
  } else {
    console.log('💡 Para atualizar automaticamente submódulos desatualizados, execute:');
    console.log('   npm run submodule:update\n');
  }

  return failed ? 1 : 0;
}

try {
  process.exitCode = main();
} catch (error) {
  console.error('Erro na execução:', error instanceof Error ? error.message : error);
  process.exitCode = 1;
}
