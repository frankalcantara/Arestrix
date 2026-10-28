#!/usr/bin/env node
/**
 * Teste automatizado do fluxo completo de Bibliotecas + fonte GitHub.
 *
 * Etapas:
 *  1. Vitest: E2E do ciclo completo + adaptadores, gate, registry, árvore e Propriedades.
 *  2. Testes nativos C++: validação do parser/segurança e, com --live, um manifesto real
 *     do GitHub usando o endpoint fetch_library_source.
 *
 * Uso:
 *   node scripts/test-libraries.mjs
 *   node scripts/test-libraries.mjs --live
 *   node scripts/test-libraries.mjs --no-native
 */
import { spawnSync } from 'node:child_process';
import { existsSync } from 'node:fs';
import path from 'node:path';

const root = process.cwd();
const args = new Set(process.argv.slice(2));
const live = args.has('--live');
const skipNative = args.has('--no-native');
const started = Date.now();

function run(label, command, commandArgs, extraEnv = {}) {
  console.log(`\n[test:libraries] ${label}`);
  const result = spawnSync(command, commandArgs, {
    cwd: root,
    stdio: 'inherit',
    shell: false,
    env: { ...process.env, ...extraEnv }
  });
  if (result.error) {
    console.error(`[test:libraries] Falha ao executar ${command}: ${result.error.message}`);
    process.exit(1);
  }
  if (result.status !== 0) {
    console.error(`[test:libraries] Etapa falhou: ${label} (exit ${result.status})`);
    process.exit(result.status || 1);
  }
  console.log(`[test:libraries] OK: ${label}`);
}

const vitestPath = path.join(root, 'node_modules', 'vitest', 'vitest.mjs');
const vitestArgs = [
  'run',
  'tests/library-e2e.spec.ts',
  'tests/library-github.spec.ts',
  'tests/library-audit-gate.spec.ts',
  'tests/library-registry.spec.ts',
  'tests/library-tree.spec.ts',
  'tests/libraries-properties.spec.ts',
  'tests/project-library-store.spec.ts',
  'tests/i18n.spec.ts'
];

run('Frontend e integração (Vitest)', process.execPath, [vitestPath, ...vitestArgs]);

if (!skipNative) {
  const natives = process.platform === 'win32'
    ? ['build/arestrix_tests.exe']
    : ['build/arestrix_tests'];
  const native = natives.map(candidate => path.join(root, candidate)).find(existsSync);

  if (!native) {
    console.error(
      '[test:libraries] Binário C++ de testes não encontrado. Compile com:\n' +
      '  cmake --build build --target arestrix_tests\n' +
      '  (ou use --no-native para pular esta etapa)'
    );
    process.exit(1);
  }

  run(
    live ? 'C++ + manifesto GitHub real (live)' : 'C++ (segurança e parser)',
    native,
    ['--filter=github_source'],
    live ? { ARESTRIX_GITHUB_LIVE: '1' } : {}
  );

  const app = process.platform === 'win32' ? 'build/arestrix.exe' : 'build/arestrix';
  if (!existsSync(path.join(root, app))) {
    console.warn(`[test:libraries] Aviso: ${app} não encontrado; o endpoint nativo não foi validado no binário final.`);
  }
}

const seconds = ((Date.now() - started) / 1000).toFixed(1);
console.log(`\n[test:libraries] Todos os testes passaram em ${seconds}s${live ? ' (modo live)' : ''}.`);
