/**
 * Homologacao automatizada do caso LIB-M-01 (Bibliotecas + GitHub).
 *
 * Abre o Studio web (dist atual) em modo headless, injeta uma biblioteca de
 * teste com tres assets (PENDING, APPROVE e REJECT), abre Propriedades ->
 * Workspace -> Catalogos e captura a arvore em tema claro e escuro nas
 * larguras 1024 e 1800 px. Tambem verifica overflow horizontal, presenca dos
 * tres veredictos e o fundo do canvas no tema escuro.
 *
 * Evidencias: release/homologation/<runId>/screenshots/lib-m-01-*.webp
 * Relatorio:  release/homologation/<runId>/lib-m-01.json
 *
 * Uso:
 *   node scripts/capture-libraries-homologation.mjs
 *   node scripts/capture-libraries-homologation.mjs --port 4176 --keep-server
 */
import puppeteer from 'puppeteer-core';
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { resolve } from 'node:path';

const PORT = Number(process.argv.find(arg => arg.startsWith('--port='))?.split('=')[1] || 4175);
const BASE_URL = `http://localhost:${PORT}`;
const RUN_ID = new Date().toISOString().replace(/[:.]/g, '-');
const OUT_DIR = resolve('release', 'homologation', RUN_ID);
const SHOTS_DIR = resolve(OUT_DIR, 'screenshots');
mkdirSync(SHOTS_DIR, { recursive: true });

const EDGE = 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const CHROME = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
const executablePath = existsSync(EDGE) ? EDGE : CHROME;
const examplePipeline = JSON.parse(readFileSync(resolve('examples/01-svg-agente/pipelines/main.json'), 'utf8'));

const NOW = '2026-10-27T00:00:00.000Z';
const SEED = {
  libraries: [{
    id: 'library-homolog',
    name: 'Biblioteca de Homologacao',
    description: 'Assets de teste para a matriz manual LIB-M-01.',
    kind: 'local',
    scope: 'workspace',
    trust: 'local',
    enabled: true,
    createdAt: NOW,
    isBuiltin: false
  }],
  sources: [],
  assets: [
    ['asset-pending', 'Skill pendente', 'PENDING', 'neutral'],
    ['asset-approved', 'Skill aprovada', 'APPROVE', 'success'],
    ['asset-rejected', 'Skill rejeitada', 'REJECT', 'danger']
  ].map(([id, name, verdict, tone]) => ({
    id,
    libraryId: 'library-homolog',
    kind: 'skill',
    name,
    description: `Asset de homologacao com veredicto ${verdict}.`,
    category: 'Homologacao',
    version: '1.0.0',
    license: 'MIT',
    status: 'available',
    provenance: {
      sourceKind: 'workspace',
      sourceId: 'source-homolog',
      importedAt: NOW
    },
    audit: {
      verdict,
      riskScore: verdict === 'APPROVE' ? 5 : verdict === 'PENDING' ? 40 : 90,
      policyVersion: 'homologation',
      auditedAt: NOW,
      hash: `sha256:${id}`,
      findings: verdict === 'REJECT' ? ['policy_violation'] : [],
      warnings: verdict === 'PENDING' ? ['manual_review_required'] : []
    },
    payload: {
      kind: 'skill',
      markdownContent: `# ${name}\n\nAsset de homologacao ${tone}.`,
      systemPrompt: `Voce e o asset ${name}.`,
      tools: []
    }
  }))
};

const wait = ms => new Promise(resolveLater => setTimeout(resolveLater, ms));

console.log(`[LIB-M-01] Vite preview na porta ${PORT}...`);
const server = spawn('npx.cmd', ['vite', 'preview', '--port', String(PORT), '--strictPort'], {
  shell: true,
  stdio: 'pipe'
});

let ready = false;
for (let attempt = 0; attempt < 60; attempt += 1) {
  try {
    const response = await fetch(BASE_URL);
    if (response.status) { ready = true; break; }
  } catch { /* servidor ainda subindo */ }
  await wait(500);
}
if (!ready) {
  console.error('[LIB-M-01] servidor nao respondeu');
  server.kill();
  process.exit(2);
}

const checks = {};
function check(name, value) { checks[name] = Boolean(value); }

async function openProperties(page, scope, section) {
  for (let attempt = 0; attempt < 8; attempt += 1) {
    const isOpen = await page.evaluate(
      s => Boolean(document.querySelector(`.modal-overlay:not(.hidden) [data-property-section="${s}"]`)),
      section);
    if (isOpen) return;
    await page.evaluate(() => {
      document.querySelectorAll('.modal-overlay:not(.hidden) [id^="btn-close-properties-"]')
        .forEach(button => button.click());
    });
    await wait(350);
    await page.evaluate(() => document.querySelector('#tab-btn-file')?.click());
    await wait(800);
    await page.evaluate(() =>
      document.querySelector('.office-backstage-overlay.open .backstage-nav-item[data-section="properties"]')?.click());
    await wait(500);
    await page.evaluate(s => document.querySelector(`.office-backstage-overlay.open [data-open-properties="${s}"]`)?.click(), scope);
    await wait(1000);
    await page.evaluate(s => document.querySelector(`.modal-overlay:not(.hidden) [data-property-section="${s}"]`)?.click(), section);
    await wait(600);
  }
}

async function closeModals(page) {
  await page.evaluate(() => {
    document.querySelectorAll('.modal-overlay:not(.hidden) [id^="btn-close-properties-"]').forEach(button => button.click());
    document.querySelectorAll('.office-backstage-overlay').forEach(el => el.classList.remove('open'));
  });
  await wait(500);
}

async function captureLibraries(page, theme, width) {
  await page.setViewport({ width, height: width === 1024 ? 768 : 1000, deviceScaleFactor: 1 });
  await page.evaluate(value => localStorage.setItem('arx_test_theme', value), theme);
  await page.reload({ waitUntil: 'networkidle2' });
  await wait(1200);
  await page.evaluate(pipeline => {
    window.arestrixGraph?.fromJSON(pipeline);
    window.arestrixViewport?.zoomToFit();
  }, examplePipeline);
  await wait(600);
  await openProperties(page, 'workspace', 'libraries');
  await wait(900);
  const state = await page.evaluate(() => {
    const tree = document.querySelector('.properties-library-tree');
    const text = tree?.textContent || '';
    const treeRect = tree?.getBoundingClientRect();
    return {
      treeVisible: Boolean(tree && treeRect && treeRect.width > 0 && treeRect.height > 0),
      pendingBadge: Boolean(tree?.querySelector('.properties-badge--neutral')),
      approveBadge: Boolean(tree?.querySelector('.properties-badge--success')),
      rejectBadge: Boolean(tree?.querySelector('.properties-badge--danger')),
      overflow: document.documentElement.scrollWidth > window.innerWidth + 1,
      textSample: text.replace(/\s+/g, ' ').slice(0, 160)
    };
  });
  const name = `lib-m-01-${theme}-${width}.webp`;
  await page.evaluate(() => {
    document.querySelector('.properties-library-tree')?.scrollIntoView({ block: 'center' });
  });
  await wait(400);
  await page.screenshot({ path: resolve(SHOTS_DIR, name), type: 'webp', quality: 90 });
  const tree = await page.$('.properties-library-tree');
  if (tree) {
    await tree.screenshot({
      path: resolve(SHOTS_DIR, `lib-m-01-${theme}-${width}-tree.webp`),
      type: 'webp',
      quality: 92
    });
  }
  check(`treeVisible_${theme}_${width}`, state.treeVisible);
  check(`verdictsVisible_${theme}_${width}`, state.pendingBadge && state.approveBadge && state.rejectBadge);
  check(`noOverflow_${theme}_${width}`, !state.overflow);
  return { name, ...state };
}

let browser;
let exitCode = 0;
try {
  browser = await puppeteer.launch({
    executablePath,
    headless: true,
    defaultViewport: { width: 1440, height: 900, deviceScaleFactor: 1 },
    args: ['--no-sandbox', '--disable-setuid-sandbox']
  });
  const page = await browser.newPage();
  page.on('pageerror', error => console.error('[LIB-M-01] PAGE ERROR:', error.message));
  page.on('console', message => {
    if (message.type() === 'error') console.error('[LIB-M-01] CONSOLE:', message.text());
  });
  await page.evaluateOnNewDocument(seed => {
    const theme = localStorage.getItem('arx_test_theme') === 'dark' ? 'dark' : 'light';
    localStorage.setItem('arestrix_locale', 'pt-BR');
    localStorage.setItem('arestrix_libraries_v2', JSON.stringify(seed));
    localStorage.setItem('arestrix_studio_settings_v1', JSON.stringify({
      version: 1,
      language: 'pt-BR',
      appearance: { theme, canvasBackground: theme },
      defaults: {}
    }));
    localStorage.setItem('arestrix_canvas_background', theme);
  }, SEED);
  await page.goto(BASE_URL, { waitUntil: 'networkidle2' });
  await wait(1500);
  await page.evaluate(pipeline => {
    window.arestrixGraph?.fromJSON(pipeline);
    window.arestrixViewport?.zoomToFit();
  }, examplePipeline);
  await wait(800);

  const light1024 = await captureLibraries(page, 'light', 1024);
  const light1800 = await captureLibraries(page, 'light', 1800);
  const dark1024 = await captureLibraries(page, 'dark', 1024);
  const dark1800 = await captureLibraries(page, 'dark', 1800);

  await closeModals(page);
  const canvas = await page.evaluate(() => {
    const element = document.querySelector('.viewport-container');
    const color = element ? getComputedStyle(element).backgroundColor : '';
    const match = color.match(/rgba?\((\d+),\s*(\d+),\s*(\d+)/);
    const luminance = match
      ? (0.2126 * Number(match[1]) + 0.7152 * Number(match[2]) + 0.0722 * Number(match[3])) / 255
      : 1;
    return {
      color,
      luminance: Number(luminance.toFixed(3)),
      theme: document.documentElement.getAttribute('data-theme')
        || document.body.getAttribute('data-theme')
        || document.documentElement.className
    };
  });
  await page.screenshot({ path: resolve(SHOTS_DIR, 'lib-m-01-canvas-dark.webp'), type: 'webp', quality: 90 });
  check('darkCanvasBelowThreshold', canvas.luminance < 0.35);

  const report = {
    case: 'LIB-M-01',
    description: 'Tema claro/escuro e larguras 1024/1800 px',
    runId: RUN_ID,
    generatedAt: new Date().toISOString(),
    baseUrl: BASE_URL,
    seed: 'library-homolog com assets PENDING/APPROVE/REJECT',
    checks,
    pass: Object.values(checks).every(Boolean),
    canvas,
    screenshots: {
      light1024: `screenshots/${light1024.name}`,
      light1024Tree: `screenshots/lib-m-01-light-1024-tree.webp`,
      light1800: `screenshots/${light1800.name}`,
      light1800Tree: `screenshots/lib-m-01-light-1800-tree.webp`,
      dark1024: `screenshots/${dark1024.name}`,
      dark1024Tree: `screenshots/lib-m-01-dark-1024-tree.webp`,
      dark1800: `screenshots/${dark1800.name}`,
      dark1800Tree: `screenshots/lib-m-01-dark-1800-tree.webp`,
      canvasDark: 'screenshots/lib-m-01-canvas-dark.webp'
    },
    samples: { light1024, light1800, dark1024, dark1800 }
  };
  writeFileSync(resolve(OUT_DIR, 'lib-m-01.json'), `${JSON.stringify(report, null, 2)}\n`, 'utf8');
  console.log(`[LIB-M-01] pass=${report.pass} checks=${JSON.stringify(checks)}`);
  console.log(`[LIB-M-01] evidencias: ${OUT_DIR}`);
  exitCode = report.pass ? 0 : 1;
} catch (error) {
  console.error('[LIB-M-01] falhou:', error);
  exitCode = 1;
} finally {
  if (browser) await browser.close();
  if (!process.argv.includes('--keep-server')) server.kill();
  process.exit(exitCode);
}
