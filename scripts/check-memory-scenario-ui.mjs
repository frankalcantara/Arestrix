import puppeteer from 'puppeteer-core';
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync } from 'node:fs';
import { resolve } from 'node:path';

const PORT = 4178;
const URL = `http://localhost:${PORT}`;
const EDGE = 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
const CHROME = 'C:\\Program Files\\Google\\Chrome\\Application\\chrome.exe';
const executablePath = existsSync(EDGE) ? EDGE : CHROME;
const wait = ms => new Promise(r => setTimeout(r, ms));
const shotDir = resolve('release', 'homologation', 'memory-scenario-ui');
mkdirSync(shotDir, { recursive: true });

const server = spawn('npx.cmd', ['vite', 'preview', '--port', String(PORT), '--strictPort'], {
  shell: true, stdio: 'pipe'
});
for (let i = 0; i < 60; i++) {
  try { if ((await fetch(URL)).status) break; } catch {}
  await wait(500);
}

const browser = await puppeteer.launch({
  executablePath, headless: true,
  defaultViewport: { width: 1440, height: 950, deviceScaleFactor: 1 },
  args: ['--no-sandbox', '--disable-setuid-sandbox']
});
const page = await browser.newPage();
const errors = [];
page.on('pageerror', e => errors.push(e.message));
await page.evaluateOnNewDocument(() => {
  localStorage.setItem('arestrix_locale', 'pt-BR');
  localStorage.setItem('arestrix_studio_settings_v1', JSON.stringify({
    version: 1, language: 'pt-BR',
    appearance: { theme: 'light', canvasBackground: 'light' }, defaults: {}
  }));
});
await page.goto(URL, { waitUntil: 'networkidle2' });
await wait(1500);

async function openProjectMemory() {
  for (let i = 0; i < 8; i++) {
    const active = await page.evaluate(() =>
      document.querySelector('.modal-overlay:not(.hidden) [data-property-section="memory"]')?.classList.contains('active'));
    if (active) return;
    await page.evaluate(() => document.querySelector('#tab-btn-file')?.click());
    await wait(700);
    await page.evaluate(() => document.querySelector('.office-backstage-overlay.open .backstage-nav-item[data-section="properties"]')?.click());
    await wait(500);
    await page.evaluate(() => document.querySelector('.office-backstage-overlay.open [data-open-properties="project"]')?.click());
    await wait(900);
    await page.evaluate(() => document.querySelector('.modal-overlay:not(.hidden) [data-property-section="memory"]')?.click());
    await wait(500);
  }
}

const read = () => page.evaluate(() => {
  const input = name => document.querySelector(`.modal-overlay:not(.hidden) [data-property-input="${name}"]`);
  return {
    scenario: input('memoryScenario')?.value,
    enabled: input('memoryEnabled')?.checked,
    mode: input('memoryMode')?.value,
    gateMode: input('memoryGateMode')?.value,
    provider: input('memoryGateProvider')?.value,
    model: input('memoryGateModel')?.value,
    budget: input('memoryBudget')?.value,
    customOption: Boolean(document.querySelector('.modal-overlay:not(.hidden) option[value="custom"]'))
  };
});

await openProjectMemory();
const initial = await read();
await page.screenshot({ path: resolve(shotDir, 'project-memory-initial.webp'), type: 'webp', quality: 90 });

const setScenario = async value => {
  await page.evaluate(v => {
    const select = document.querySelector('.modal-overlay:not(.hidden) [data-property-input="memoryScenario"]');
    if (select) {
      select.value = v;
      select.dispatchEvent(new Event('change', { bubbles: true }));
    }
  }, value);
  await wait(300);
};

await setScenario('consult');
const consult = await read();
await page.screenshot({ path: resolve(shotDir, 'project-memory-consult.webp'), type: 'webp', quality: 90 });

await setScenario('learn');
const learn = await read();

await page.evaluate(() => {
  const budget = document.querySelector('.modal-overlay:not(.hidden) [data-property-input="memoryBudget"]');
  if (budget) {
    budget.value = '600';
    budget.dispatchEvent(new Event('change', { bubbles: true }));
  }
});
await wait(300);
const custom = await read();
await page.screenshot({ path: resolve(shotDir, 'project-memory-custom.webp'), type: 'webp', quality: 90 });

await page.evaluate(() => document.querySelector('.modal-overlay:not(.hidden) [data-properties-save]')?.click());
await wait(1200);
await openProjectMemory();
const afterSave = await read();

console.log(JSON.stringify({ initial, consult, learn, custom, afterSave, errors }, null, 2));
await browser.close();
server.kill();
process.exit(0);
