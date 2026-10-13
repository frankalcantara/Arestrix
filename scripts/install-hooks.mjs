#!/usr/bin/env node
// Ativa os hooks versionados em .githooks/ para este clone (git config core.hooksPath).
import { execFileSync } from 'node:child_process';
import { chmodSync, existsSync } from 'node:fs';
import { dirname, join, resolve } from 'node:path';
import { fileURLToPath } from 'node:url';

const root = resolve(dirname(fileURLToPath(import.meta.url)), '..');
const hook = join(root, '.githooks', 'pre-commit');
if (!existsSync(hook)) {
  console.error('hook nao encontrado: .githooks/pre-commit');
  process.exit(1);
}
try { chmodSync(hook, 0o755); } catch { /* Windows: o Git executa via sh de qualquer forma */ }
execFileSync('git', ['config', 'core.hooksPath', '.githooks'], { cwd: root, stdio: 'inherit' });
console.log('Hooks ativados: o pre-commit agora roda scripts/check-secrets.mjs --staged.');
