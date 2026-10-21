#!/usr/bin/env node
/**
 * Garante que a versao do produto tenha uma unica fonte de verdade.
 *
 * Fontes verificadas:
 *   - version.txt
 *   - package.json
 *   - package-lock.json (raiz)
 *   - primeira entrada de versao do CHANGELOG.md
 *
 * Uso: npm run check:version
 */
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

function read(relativePath) {
  return fs.readFileSync(path.join(root, relativePath), 'utf8');
}

function fail(message) {
  console.error(`[versao] ${message}`);
  process.exitCode = 1;
}

const versionFile = read('version.txt').trim();
if (!/^\d+\.\d+\.\d+(?:-[0-9A-Za-z.-]+)?$/.test(versionFile)) {
  fail(`version.txt nao segue SemVer: "${versionFile}"`);
}

const pkg = JSON.parse(read('package.json'));
if (pkg.version !== versionFile) {
  fail(`package.json=${pkg.version} difere de version.txt=${versionFile}`);
}

const lock = JSON.parse(read('package-lock.json'));
const lockRoot = lock.packages?.['']?.version;
if (lock.version !== versionFile || lockRoot !== versionFile) {
  fail(
    `package-lock.json=${lock.version} (raiz=${lockRoot}) difere de version.txt=${versionFile}. ` +
      'Rode: npm install --package-lock-only --ignore-scripts',
  );
}

const changelog = read('CHANGELOG.md');
const changelogVersions = [...changelog.matchAll(/^##\s*\[([^\]]+)\]/gm)].map(m => m[1]);
const changelogVersion = changelogVersions.find(v => v.toLowerCase() !== 'unreleased');
if (changelogVersion !== versionFile) {
  fail(`CHANGELOG.md=${changelogVersion ?? 'ausente'} difere de version.txt=${versionFile}`);
}

if (!process.exitCode) {
  console.log(`[versao] OK: ${versionFile} (version.txt, package.json, package-lock.json, CHANGELOG.md)`);
}
