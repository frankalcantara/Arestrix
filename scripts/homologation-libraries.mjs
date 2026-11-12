#!/usr/bin/env node
/**
 * Homologação automatizada de Bibliotecas + GitHub.
 *
 * Executa o fluxo determinístico (Vitest + C++ com mock GitHub), opcionalmente
 * o smoke live, e grava evidências em release/homologation/<runId>/.
 *
 * Uso:
 *   node scripts/homologation-libraries.mjs
 *   node scripts/homologation-libraries.mjs --live
 *   node scripts/homologation-libraries.mjs --no-native
 */
import { spawn, spawnSync } from 'node:child_process';
import { existsSync, mkdirSync, writeFileSync } from 'node:fs';
import { release, platform, arch } from 'node:os';
import path from 'node:path';
import { startMockGitHubServer } from './mock-github-server.mjs';

const root = process.cwd();
const args = new Set(process.argv.slice(2));
const live = args.has('--live');
const skipNative = args.has('--no-native');
const ui = !args.has('--no-ui');
const startedAt = new Date();
const startedMs = Date.now();

function commandOutput(command, commandArgs) {
  const result = spawnSync(command, commandArgs, { cwd: root, encoding: 'utf8' });
  return result.status === 0 ? (result.stdout || '').trim() : '';
}

const commit = commandOutput('git', ['rev-parse', 'HEAD']) || 'unknown';
const dirty = Boolean(commandOutput('git', ['status', '--porcelain']));
const runId = `${startedAt.toISOString().replace(/[:.]/g, '-')}-${commit.slice(0, 8)}`;
const evidenceDir = path.join(root, 'release', 'homologation', runId);
mkdirSync(evidenceDir, { recursive: true });

const stages = [];
const matrix = [];

function recordStage(name, result) {
  stages.push({
    name,
    status: result.status === 0 ? 'passed' : 'failed',
    exitCode: result.status,
    durationMs: result.durationMs,
    log: `${name.replace(/[^a-z0-9]+/gi, '-').toLowerCase()}.log`
  });
  return result.status === 0;
}

function runStage(name, command, commandArgs, extraEnv = {}) {
  console.log(`\n[homologation] ${name}`);
  const started = Date.now();
  return new Promise(resolve => {
    const child = spawn(command, commandArgs, {
      cwd: root,
      env: { ...process.env, ...extraEnv }
    });
    let stdout = '';
    let stderr = '';
    child.stdout.on('data', chunk => {
      stdout += chunk;
      process.stdout.write(chunk);
    });
    child.stderr.on('data', chunk => {
      stderr += chunk;
      process.stderr.write(chunk);
    });
    child.on('error', error => {
      stderr += `\n${error.message}\n`;
    });
    child.on('close', code => {
      const entry = {
        status: code ?? 1,
        stdout,
        stderr,
        durationMs: Date.now() - started
      };
      const logName = `${name.replace(/[^a-z0-9]+/gi, '-').toLowerCase()}.log`;
      writeFileSync(path.join(evidenceDir, logName), `${stdout}\n${stderr}`.trim() + '\n', 'utf8');
      recordStage(name, entry);
      resolve(entry.status === 0);
    });
  });
}

function addCase(id, area, description, type, status, evidence) {
  matrix.push({ id, area, description, type, status, evidence });
}

const vitestPath = path.join(root, 'node_modules', 'vitest', 'vitest.mjs');
const vitestArgs = [
  'run',
  'tests/library-e2e.spec.ts',
  'tests/library-github.spec.ts',
  'tests/library-audit-gate.spec.ts',
  'tests/library-registry.spec.ts',
  'tests/library-service.spec.ts',
  'tests/library-tree.spec.ts',
  'tests/libraries-properties.spec.ts',
  'tests/project-library-store.spec.ts',
  'tests/license-entitlement.spec.ts',
  'tests/asset-delta.spec.ts',
  'tests/i18n.spec.ts'
];

const FRONTEND_STAGE = 'Frontend e integracao Vitest';
const FRONTEND_LOG = 'frontend-e-integracao-vitest.log';
const NATIVE_STAGE = 'C++ mock GitHub';
const NATIVE_LOG = 'c-mock-github.log';

const frontendOk = await runStage(FRONTEND_STAGE, process.execPath, [vitestPath, ...vitestArgs]);

const functionalCases = [
  ['LIB-F-01', 'Catálogo embutido e biblioteca Arestrix'],
  ['LIB-F-02', 'Criar fonte GitHub e biblioteca associada'],
  ['LIB-F-03', 'Sincronizar sem baixar blobs'],
  ['LIB-F-04', 'Assets remotos ficam PENDING'],
  ['LIB-F-05', 'Baixar, auditar e instalar skill'],
  ['LIB-F-06', 'Baixar, auditar e instalar MCP'],
  ['LIB-F-07', 'Derivar, editar e detectar atualização'],
  ['LIB-F-08', 'Template de vértice como asset'],
  ['LIB-F-09', 'Pins e habilitação no Projeto'],
  ['LIB-F-10', 'Migração de fontes legadas']
];

for (const [id, description] of functionalCases) {
  addCase(id, 'Funcional', description, 'automated', frontendOk ? 'passed' : 'failed', FRONTEND_LOG);
}

let nativeOk = false;
if (!skipNative) {
  const natives = platform() === 'win32' ? ['build/arestrix_tests.exe'] : ['build/arestrix_tests'];
  const native = natives.map(candidate => path.join(root, candidate)).find(existsSync);
  if (!native) {
    writeFileSync(
      path.join(evidenceDir, NATIVE_LOG),
      'Binário C++ não encontrado. Compile build/arestrix_tests.\n',
      'utf8'
    );
    recordStage(NATIVE_STAGE, { status: 1, durationMs: 0 });
    console.error('[homologation] Binário C++ não encontrado. Compile build/arestrix_tests.');
  } else {
    const mock = await startMockGitHubServer();
    try {
      nativeOk = await runStage(NATIVE_STAGE, native, ['--filter=github_source'], {
        ARESTRIX_TEST_MODE: '1',
        ARESTRIX_TEST_GITHUB_API_BASE: mock.url,
        ARESTRIX_TEST_GITHUB_RAW_BASE: `${mock.url}/raw`
      });
      writeFileSync(
        path.join(evidenceDir, 'mock-github-requests.json'),
        JSON.stringify({ commit: mock.commit, requestCount: mock.requests.length, requests: mock.requests }, null, 2),
        'utf8'
      );
    } finally {
      await mock.close();
    }
  }
}

const securityCases = [
  ['LIB-S-01', 'Skill hostil rejeitada e conteúdo descartado', 'frontend'],
  ['LIB-S-02', 'CAUTION exige confirmação', 'frontend'],
  ['LIB-S-03', 'Licença fora da allowlist bloqueada', 'frontend'],
  ['LIB-S-04', 'Path traversal e payload inválido recusados', 'native'],
  ['LIB-S-05', 'Asset acima do limite recusado', 'native'],
  ['LIB-S-06', 'Segredos MCP somente por ${VAR}', 'frontend']
];
for (const [id, description, source] of securityCases) {
  const ok = source === 'native' ? nativeOk : frontendOk;
  addCase(
    id,
    'Segurança',
    description,
    'automated',
    ok ? 'passed' : (source === 'native' && skipNative ? 'skipped' : 'failed'),
    source === 'native' ? NATIVE_LOG : FRONTEND_LOG
  );
}

const resilienceCases = [
  ['LIB-R-01', 'Manifesto válido com commit e licença SPDX'],
  ['LIB-R-02', 'Repo inexistente retorna erro estável'],
  ['LIB-R-03', 'Rate limit retorna erro estável'],
  ['LIB-R-04', 'Commit pin e árvore estáveis']
];
for (const [id, description] of resilienceCases) {
  addCase(id, 'Resiliência', description, 'automated', nativeOk ? 'passed' : (skipNative ? 'skipped' : 'failed'), NATIVE_LOG);
}

let liveOk = false;
if (live && !skipNative && nativeOk) {
  const natives = platform() === 'win32' ? ['build/arestrix_tests.exe'] : ['build/arestrix_tests'];
  const native = natives.map(candidate => path.join(root, candidate)).find(existsSync);
  if (native) {
    liveOk = await runStage('C++ smoke GitHub live', native, ['--filter=github_source_live'], {
      ARESTRIX_GITHUB_LIVE: '1'
    });
  }
}
addCase(
  'LIB-R-05',
  'Resiliência',
  'Smoke live do manifesto GitHub',
  'automated',
  live && !skipNative ? (liveOk ? 'passed' : 'failed') : 'skipped',
  'cpp-smoke-github-live.log'
);
if (!live || skipNative) {
  writeFileSync(
    path.join(evidenceDir, 'cpp-smoke-github-live.log'),
    `Smoke live não executado (${!live ? '--live ausente' : '--no-native'}).\n`,
    'utf8'
  );
}

const UI_STAGE = 'UI web LIB-M-01';
const UI_LOG = 'ui-web-lib-m-01.log';
const browserCandidates = process.platform === 'win32'
  ? [
      'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe',
      'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe'
    ]
  : [];
const hasBrowser = browserCandidates.some(candidate => existsSync(candidate));
const hasDist = existsSync(path.join(root, 'dist', 'index.html'));
let uiAttempted = false;
let uiOk = false;
if (ui && hasBrowser && hasDist) {
  uiAttempted = true;
  uiOk = await runStage(UI_STAGE, process.execPath, [
    path.join(root, 'scripts', 'capture-libraries-homologation.mjs'),
    `--out=${evidenceDir}`,
    `--run-id=${runId}`
  ]);
} else if (ui) {
  console.warn('[homologation] UI web ignorada: navegador ou dist/index.html ausente.');
}

const manualCases = [
  [
    'LIB-M-01',
    'UI web claro/escuro e 1024/1800 px',
    uiOk ? 'passed' : (uiAttempted ? 'failed' : 'pending'),
    uiOk ? 'lib-m-01.json' : (uiAttempted ? UI_LOG : 'checklist manual')
  ],
  ['LIB-M-02', 'Navegação por teclado e foco visível', 'passed', 'manual 2026-10-27'],
  ['LIB-M-03', 'Instalação limpa e upgrade preservando projetos', 'pending', 'checklist manual'],
  ['LIB-M-04', 'Offline após sincronização/cache', 'pending', 'checklist manual']
];
for (const [id, description, manualStatus, evidence] of manualCases) {
  addCase(id, 'Manual', description, 'manual', manualStatus, evidence);
}

const finishedAt = new Date();
const failedAutomated = matrix.some(item => item.status === 'failed');
const optionalSkipped = matrix.filter(item => item.status === 'skipped').map(item => item.id);
const pendingManual = matrix.filter(item => item.status === 'pending').length;
const requiredSkipped = skipNative;
const status = failedAutomated ? 'failed' : requiredSkipped ? 'partial' : 'passed-automated';

const environment = {
  runId,
  commit,
  dirty,
  startedAt: startedAt.toISOString(),
  finishedAt: finishedAt.toISOString(),
  durationSeconds: Number(((Date.now() - startedMs) / 1000).toFixed(1)),
  platform: platform(),
  arch: arch(),
  osRelease: release(),
  node: process.version,
  appBinary: existsSync(path.join(root, platform() === 'win32' ? 'build/arestrix.exe' : 'build/arestrix')),
  testBinary: existsSync(path.join(root, platform() === 'win32' ? 'build/arestrix_tests.exe' : 'build/arestrix_tests')),
  live,
  skipNative,
  ui: uiAttempted,
  uiOk
};

const report = {
  status,
  releaseReady: !failedAutomated && pendingManual === 0 && (live ? liveOk : false),
  scope: 'Bibliotecas + fonte GitHub',
  environment,
  stages,
  matrix,
  pendingManual,
  optionalSkipped
};

writeFileSync(path.join(evidenceDir, 'environment.json'), JSON.stringify(environment, null, 2), 'utf8');
writeFileSync(path.join(evidenceDir, 'homologation-report.json'), JSON.stringify(report, null, 2), 'utf8');
writeFileSync(path.join(evidenceDir, 'matrix.csv'), [
  'id,area,description,type,status,evidence',
  ...matrix.map(item => [item.id, item.area, item.description, item.type, item.status, item.evidence]
    .map(value => `"${String(value).replace(/"/g, '""')}"`)
    .join(','))
].join('\n'), 'utf8');

console.log(`\n[homologation] Status: ${status.toUpperCase()}`);
console.log(`[homologation] Evidências: ${path.relative(root, evidenceDir)}`);
console.log(`[homologation] Casos: ${matrix.length} | manual pendente: ${pendingManual}`);
process.exit(status === 'failed' ? 1 : 0);
