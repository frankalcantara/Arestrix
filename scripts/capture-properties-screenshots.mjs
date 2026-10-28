import puppeteer from 'puppeteer-core';
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const SCREENSHOTS_DIR = resolve('docs/assets/screenshots');
if (!existsSync(SCREENSHOTS_DIR)) {
  mkdirSync(SCREENSHOTS_DIR, { recursive: true });
}

const PORT = 4174;
const BASE_URL = `http://localhost:${PORT}`;
const EDGE = 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const CHROME = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
const executablePath = existsSync(EDGE) ? EDGE : CHROME;
const examplePipeline = JSON.parse(readFileSync(resolve('examples/01-svg-agente/pipelines/main.json'), 'utf8'));

const wait = ms => new Promise(resolve => setTimeout(resolve, ms));

console.log(`[Screenshot] Iniciando Vite preview na porta ${PORT}...`);
const preview = spawn('npx.cmd', ['vite', 'preview', '--port', String(PORT), '--strictPort'], {
  shell: true,
  stdio: 'pipe'
});

let ready = false;
for (let i = 0; i < 40; i++) {
  try {
    const res = await fetch(BASE_URL);
    if (res.status) {
      ready = true;
      console.log('[Screenshot] Servidor pronto.');
      break;
    }
  } catch {
    await wait(500);
  }
}
if (!ready) {
  console.warn('[Screenshot] Servidor demorou para responder; prosseguindo.');
}

async function capture(page, name, label) {
  await page.screenshot({
    type: 'webp',
    quality: 88,
    path: resolve(SCREENSHOTS_DIR, name),
    fullPage: false
  });
  console.log(`[OK] ${name} (${label})`);
}

async function openBackstage(page, section) {
  await page.evaluate(() => document.querySelector('#tab-btn-file')?.click());
  await wait(1200);
  await page.evaluate(sec => {
    document.querySelector(`.backstage-nav-item[data-section="${sec}"]`)?.click();
  }, section);
  await wait(700);
}

async function openProperties(page, scope, section) {
  await page.evaluate(() => {
    document.querySelectorAll('.modal-overlay:not(.hidden) [id^="btn-close-properties-"]').forEach(button => button.click());
  });
  await wait(350);
  await page.evaluate(() => document.querySelector('#tab-btn-file')?.click());
  await wait(1200);
  await page.evaluate(() => document.querySelector('.backstage-nav-item[data-section="properties"]')?.click());
  await wait(500);
  await page.evaluate(s => document.querySelector(`[data-open-properties="${s}"]`)?.click(), scope);
  await wait(1000);
  if (section) {
    await page.evaluate(id => {
      document.querySelector(`[data-property-section="${id}"]`)?.click();
    }, section);
    await wait(400);
  }
}

async function closeProperties(page, scope) {
  await page.evaluate(s => {
    document.querySelector(`.modal-overlay:not(.hidden) #btn-close-properties-${s}`)?.click();
  }, scope);
  await wait(350);
}

async function setTheme(page, theme) {
  await openProperties(page, 'studio', 'general');
  await page.evaluate(value => {
    const select = document.querySelector('.modal-overlay:not(.hidden) [data-property-input="theme"]');
    if (select) {
      select.value = value;
      select.dispatchEvent(new Event('input', { bubbles: true }));
      select.dispatchEvent(new Event('change', { bubbles: true }));
    }
  }, theme);
  await page.evaluate(() => {
    document.querySelector('.modal-overlay:not(.hidden) [data-properties-save]')?.click();
  });
  await wait(1200);
}

try {
  const browser = await puppeteer.launch({
    executablePath,
    headless: true,
    defaultViewport: { width: 1440, height: 900, deviceScaleFactor: 1.5 },
    args: ['--no-sandbox', '--disable-setuid-sandbox']
  });

  const page = await browser.newPage();
  page.on('pageerror', error => console.error('PAGE ERROR:', error));
  await page.evaluateOnNewDocument(() => {
    const now = Date.now();
    localStorage.setItem('arestrix_locale', 'en-US');
    localStorage.setItem('arestrix_studio_settings_v1', JSON.stringify({
      version: 1,
      language: 'en-US',
      appearance: { theme: 'light', canvasBackground: 'light' },
      defaults: {}
    }));
    localStorage.setItem('arestrix_canvas_background', 'light');
    localStorage.setItem('arestrix_recent_projects', JSON.stringify([
      {
        projectId: 'demo-svg',
        name: 'Corporate SVG Agent',
        rootPath: 'C:/Projects/CorporateSVG',
        manifestPath: 'C:/Projects/CorporateSVG/arestrix.project.json',
        lastOpened: new Date(now - 25 * 60 * 1000).toISOString()
      },
      {
        projectId: 'demo-web',
        name: 'Web Research Swarm',
        rootPath: 'C:/Projects/WebResearch',
        manifestPath: 'C:/Projects/WebResearch/arestrix.project.json',
        lastOpened: new Date(now - 5 * 60 * 60 * 1000).toISOString()
      },
      {
        projectId: 'demo-audit',
        name: 'Compliance Audit Pipeline',
        rootPath: 'C:/Projects/ComplianceAudit',
        manifestPath: 'C:/Projects/ComplianceAudit/arestrix.project.json',
        lastOpened: new Date(now - 28 * 60 * 60 * 1000).toISOString()
      }
    ]));
  });

  await page.goto(BASE_URL, { waitUntil: 'networkidle0', timeout: 30000 });
  await wait(1800);

  // 1. Properties hub
  await openBackstage(page, 'properties');
  await capture(page, 'properties-hub.webp', 'Backstage Properties hub');

  // 2. Office-style Open screen
  await openBackstage(page, 'open');
  for (let attempt = 0; attempt < 5; attempt++) {
    const active = await page.evaluate(() =>
      document.querySelector('.office-backstage-overlay.open .backstage-nav-item.active')?.getAttribute('data-section')
    );
    if (active === 'open') break;
    await page.evaluate(() =>
      document.querySelector('.office-backstage-overlay.open .backstage-nav-item[data-section="open"]')?.click()
    );
    await wait(500);
  }
  await capture(page, 'open-screen.webp', 'Office-style Open screen');

  // 3. Studio Properties
  await openProperties(page, 'studio', 'general');
  await capture(page, 'studio-properties.webp', 'Studio Properties');

  // 3. Workspace Properties
  await openProperties(page, 'workspace', 'sandbox');
  await capture(page, 'workspace-properties.webp', 'Workspace sandbox properties');

  // 4. Project Properties
  await openProperties(page, 'project', 'general');
  await capture(page, 'project-properties.webp', 'Project properties');

  // 5. Project defaults
  await openProperties(page, 'project', 'defaults');
  await capture(page, 'project-defaults.webp', 'Project model and tools defaults');
  await closeProperties(page, 'project');

  // 6. Dark theme on the main canvas
  await setTheme(page, 'dark');
  await page.evaluate(() => {
    document.querySelectorAll('.office-backstage-overlay').forEach(el => el.classList.remove('open'));
  });
  await page.evaluate(pipeline => {
    window.arestrixGraph?.fromJSON(pipeline);
    window.arestrixViewport?.zoomToFit();
    window.arestrixViewport?.selectNode?.('jev');
  }, examplePipeline);
  await wait(900);
  await capture(page, 'studio-dark.webp', 'Studio dark theme');

  // 7. Dark Studio Properties
  await openProperties(page, 'studio', 'general');
  await capture(page, 'studio-properties-dark.webp', 'Studio Properties dark');
  await closeProperties(page, 'studio');

  // 8. Vertex sandbox modal
  await page.evaluate(() => document.querySelector('#ribbon-btn-quick-agent')?.click());
  await wait(900);
  await page.evaluate(() => document.querySelector('#btn-open-node-sandbox')?.click());
  await wait(700);
  await capture(page, 'vertex-sandbox.webp', 'Vertex sandbox modal');
  await page.evaluate(() => {
    document.querySelector('.office-modal-overlay:not(.hidden) #btn-cancel-sandbox-modal')?.click();
  });
  await wait(350);

  // 9. Vertex model modal
  await page.evaluate(() => document.querySelector('#btn-open-node-model')?.click());
  await wait(700);
  await capture(page, 'vertex-model.webp', 'Vertex model modal');

  await browser.close();
  console.log('[Screenshot] Novas capturas concluídas.');
} catch (error) {
  console.error('[Erro]', error);
  process.exitCode = 1;
} finally {
  preview.kill();
  process.exit(process.exitCode || 0);
}
