import puppeteer from 'puppeteer-core';
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync } from 'node:fs';
import { resolve } from 'node:path';

const SCREENSHOTS_DIR = resolve('docs/assets/screenshots');
if (!existsSync(SCREENSHOTS_DIR)) {
  mkdirSync(SCREENSHOTS_DIR, { recursive: true });
}

console.log('[Screenshot] Iniciando servidor de visualizacao Vite na porta 4173...');
const preview = spawn('npx.cmd', ['vite', 'preview', '--port', '4173', '--strictPort'], {
  shell: true,
  stdio: 'pipe'
});

await new Promise(r => setTimeout(r, 2500));

try {
  console.log('[Screenshot] Lancando Microsoft Edge em modo headless...');
  const browser = await puppeteer.launch({
    executablePath: 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe',
    headless: true,
    defaultViewport: {
      width: 1440,
      height: 900,
      deviceScaleFactor: 2
    },
    args: ['--no-sandbox', '--disable-setuid-sandbox']
  });

  const page = await browser.newPage();
  console.log('[Screenshot] Carregando interface...');
  await page.goto('http://localhost:4173', { waitUntil: 'networkidle0', timeout: 30000 });
  await new Promise(r => setTimeout(r, 1500));

  // 1. Fecha Backstage inicial para revelar o canvas do Studio
  await page.evaluate(() => {
    const closeBtn = document.querySelector('#btn-backstage-close');
    if (closeBtn) (closeBtn).click();
  });
  await new Promise(r => setTimeout(r, 800));

  // 2. Carrega o Grafo Demo Multi-Agente Concorrente
  await page.evaluate(() => {
    // Alterna para aba Executar no Ribbon
    const runTab = Array.from(document.querySelectorAll('.office-tab-btn')).find(b => b.textContent && b.textContent.includes('Executar'));
    if (runTab) (runTab).click();
  });
  await new Promise(r => setTimeout(r, 400));

  await page.evaluate(() => {
    const demoBtn = document.querySelector('#ribbon-btn-load-demo-tab');
    if (demoBtn) (demoBtn).click();
  });
  await new Promise(r => setTimeout(r, 1000));

  // Volta para a aba Página Inicial do Ribbon para uma visualização equilibrada
  await page.evaluate(() => {
    const homeTab = Array.from(document.querySelectorAll('.office-tab-btn')).find(b => b.textContent && b.textContent.includes('Página Inicial') || b.textContent.includes('Início'));
    if (homeTab) (homeTab).click();
  });
  await new Promise(r => setTimeout(r, 600));

  // Ajusta visão do canvas
  await page.evaluate(() => {
    const fitBtn = document.querySelector('#ribbon-btn-zoom-fit') || document.querySelector('.canvas-control-btn[title*="Ajustar"]');
    if (fitBtn) (fitBtn).click();
  });
  await new Promise(r => setTimeout(r, 800));

  // Screenshot 1: Tela Principal do Arestrix Studio com Grafo Ativo
  await page.screenshot({
    path: resolve(SCREENSHOTS_DIR, 'studio-main.png'),
    fullPage: false
  });
  console.log('[OK] studio-main.png capturado.');

  // Screenshot 2: Modal C++20 Taskflow Code Exporter
  await page.evaluate(() => {
    // Abre aba Executar
    const runTab = Array.from(document.querySelectorAll('.office-tab-btn')).find(b => b.textContent && b.textContent.includes('Executar'));
    if (runTab) (runTab).click();
  });
  await new Promise(r => setTimeout(r, 400));

  await page.evaluate(() => {
    const codeBtn = document.querySelector('#ribbon-btn-export-code-tab');
    if (codeBtn) (codeBtn).click();
  });
  await new Promise(r => setTimeout(r, 800));

  await page.screenshot({
    path: resolve(SCREENSHOTS_DIR, 'cpp20-taskflow-generator.png'),
    fullPage: false
  });
  console.log('[OK] cpp20-taskflow-generator.png capturado.');

  // Fecha modal de código
  await page.evaluate(() => {
    const closeBtn = document.querySelector('#modal-code-exporter .office-modal-close-btn, #modal-code-exporter button[title="Fechar"], #btn-close-code-modal');
    if (closeBtn) (closeBtn).click();
    else {
      const esc = new KeyboardEvent('keydown', { key: 'Escape' });
      document.dispatchEvent(esc);
    }
  });
  await new Promise(r => setTimeout(r, 600));

  // Screenshot 3: Modal do Runner Standalone - Aba 1: Progresso
  await page.evaluate(() => {
    const runnerBtn = document.querySelector('#ribbon-btn-standalone-runner');
    if (runnerBtn) (runnerBtn).click();
  });
  await new Promise(r => setTimeout(r, 800));

  await page.screenshot({
    path: resolve(SCREENSHOTS_DIR, 'standalone-runner.png'),
    fullPage: false
  });
  console.log('[OK] standalone-runner.png capturado.');

  // Screenshot 4: Runner Standalone - Aba 2: Diagnóstico
  await page.evaluate(() => {
    const modal = document.querySelector('#modal-standalone-runner');
    if (modal) {
      const tabs = Array.from(modal.querySelectorAll('.office-modal-tab-btn'));
      const diag = tabs.find(t => t.dataset.tabId === 'diagnostics' || t.textContent.includes('Diagnóstico'));
      if (diag) (diag).click();
    }
  });
  await new Promise(r => setTimeout(r, 600));

  await page.screenshot({
    path: resolve(SCREENSHOTS_DIR, 'runner-diagnostics.png'),
    fullPage: false
  });
  console.log('[OK] runner-diagnostics.png capturado.');

  // Screenshot 5: Runner Standalone - Aba 4: Exportar Executável & Governança Corporativa
  await page.evaluate(() => {
    const modal = document.querySelector('#modal-standalone-runner');
    if (modal) {
      const tabs = Array.from(modal.querySelectorAll('.office-modal-tab-btn'));
      const exp = tabs.find(t => t.dataset.tabId === 'export' || t.textContent.includes('Exportar'));
      if (exp) (exp).click();
    }
  });
  await new Promise(r => setTimeout(r, 600));

  await page.screenshot({
    path: resolve(SCREENSHOTS_DIR, 'runner-export-governance.png'),
    fullPage: false
  });
  console.log('[OK] runner-export-governance.png capturado.');

  await browser.close();
  console.log('[Screenshot] Todos os prints foram atualizados com perfeição!');
} catch (err) {
  console.error('[Erro]', err);
} finally {
  preview.kill();
  process.exit(0);
}
