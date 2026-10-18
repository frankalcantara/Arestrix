import puppeteer from 'puppeteer-core';
import { spawn } from 'node:child_process';
import { existsSync, mkdirSync, readFileSync } from 'node:fs';
import { resolve } from 'node:path';

const SCREENSHOTS_DIR = resolve('docs/assets/screenshots');
if (!existsSync(SCREENSHOTS_DIR)) {
  mkdirSync(SCREENSHOTS_DIR, { recursive: true });
}

// Carrega o pipeline de exemplo do tutorial multiagente
const examplePipelinePath = resolve('examples/01-svg-agente/pipelines/main.json');
const examplePipeline = JSON.parse(readFileSync(examplePipelinePath, 'utf8'));

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
  page.on('console', msg => console.log('PAGE LOG:', msg.text()));
  page.on('pageerror', err => console.error('PAGE ERROR:', err));

  console.log('[Screenshot] Carregando interface...');
  await page.goto('http://localhost:4173', { waitUntil: 'networkidle0', timeout: 30000 });
  await new Promise(r => setTimeout(r, 2000));

  // 1. Remove definitivamente o Backstage overlay do DOM para expor o canvas do Studio
  console.log('[Screenshot] Removendo Backstage overlay do DOM...');
  await page.evaluate(() => {
    document.querySelectorAll('.office-backstage-overlay').forEach(el => el.remove());
  });
  await new Promise(r => setTimeout(r, 600));

  // 2. Carrega o Grafo de Exemplo Real (01-svg-agente)
  console.log('[Screenshot] Carregando grafo de exemplo...');
  await page.evaluate((pipelineData) => {
    document.querySelectorAll('.office-backstage-overlay').forEach(el => el.remove());
    console.log('[EVAL] arestrixGraph disponivel:', !!window.arestrixGraph);
    if (window.arestrixGraph) {
      window.arestrixGraph.fromJSON(pipelineData);
      console.log('[EVAL] Grafo carregado com sucesso. Nós:', window.arestrixGraph.getAllNodes().length);
    }
  }, examplePipeline);
  await new Promise(r => setTimeout(r, 800));

  // 3. Centraliza e ajusta o canvas
  await page.evaluate(() => {
    if (window.arestrixViewport) {
      window.arestrixViewport.zoomToFit();
    }
  });
  await new Promise(r => setTimeout(r, 600));

  // 4. Coloca o pipeline no estado ATIVO / RODANDO com telemetria realista
  console.log('[Screenshot] Ativando estado de execucao (rodando) com telemetria...');
  await page.evaluate(() => {
    const bus = window.globalEventBus;
    if (!bus) return;

    // 1. Etapa de compilacao preliminar
    bus.emit('pipeline:compiling', 'Compilando grafo C++20 com Taskflow...');
    bus.emit('pipeline:log', { line: '[BUILD] Compilação MSVC v143 (C++20 /O2 /std:c++20 /GL) concluída com sucesso em 0.38s.' });
    bus.emit('pipeline:log', { line: '[RUN] Taskflow DAG inicializado: 7 vértices alocados em 4 workers heterogêneos concorrentes.' });

    // 2. Coloca o pipeline no estado ativo de execucao
    bus.emit('pipeline:running', 'Executando pipeline heterogêneo Taskflow C++20...');

    // Etapa 1 concluida: Agente Gerador
    bus.emit('pipeline:log', { line: '[NF_TELEMETRY:ENTER] Task=Agente_Gerador_DeepSeek Worker=0' });
    bus.emit('pipeline:log', { line: '[Agente_Gerador_DeepSeek] Inferência concluída com DeepSeek-V3: 248 tokens gerados em 1.14s.' });
    bus.emit('pipeline:log', { line: '[NF_TELEMETRY:EXIT] Task=Agente_Gerador_DeepSeek Worker=0' });

    // Etapa 2 concluida em paralelo: Linter Estatico e Auditor SMIL
    bus.emit('pipeline:log', { line: '[NF_TELEMETRY:ENTER] Task=Linter_Tipografia_Estatico Worker=1' });
    bus.emit('pipeline:log', { line: '[NF_TELEMETRY:ENTER] Task=Auditor_Timeline_SMIL Worker=2' });
    bus.emit('pipeline:log', { line: '[Linter_Tipografia_Estatico] Verificação estática aprovada: 0 violações de regras encontradas.' });
    bus.emit('pipeline:log', { line: '[Auditor_Timeline_SMIL] Linha do tempo SMIL e keyframes auditados com sucesso.' });
    bus.emit('pipeline:log', { line: '[NF_TELEMETRY:EXIT] Task=Linter_Tipografia_Estatico Worker=1' });
    bus.emit('pipeline:log', { line: '[NF_TELEMETRY:EXIT] Task=Auditor_Timeline_SMIL Worker=2' });

    // Etapa 3 concluida: Consolidador de Diagnostico
    bus.emit('pipeline:log', { line: '[NF_TELEMETRY:ENTER] Task=Consolidador_Diagnostico Worker=0' });
    bus.emit('pipeline:log', { line: '[Consolidador_Diagnostico] Métricas combinadas e consolidadas para avaliação.' });
    bus.emit('pipeline:log', { line: '[NF_TELEMETRY:EXIT] Task=Consolidador_Diagnostico Worker=0' });

    // Etapa 4 ATIVA AGORA: Agente Auditor JEV (em execucao concorrente)
    bus.emit('pipeline:log', { line: '[NF_TELEMETRY:ENTER] Task=Agente_Auditor_JEV Worker=1' });
    bus.emit('pipeline:log', { line: '[Agente_Auditor_JEV] Raciocinando (Claude 3.7 Sonnet): auditando semântica e conformidade visual...' });

    // Emite telemetria de thinking para o no jev
    if (window.onArestrixTelemetry) {
      window.onArestrixTelemetry({
        event: {
          nodeId: 'jev',
          type: 'thinking',
          payload: { thinkingChars: 2450 }
        }
      });
    }

    // Seleciona o no ativo para exibir os parametros no inspetor a direita
    if (window.arestrixViewport) {
      window.arestrixViewport.selectNode('jev');
      window.arestrixViewport.zoomToFit();
    }
  });
  await new Promise(r => setTimeout(r, 1200));

  // Screenshot 1: Tela Principal do Arestrix Studio com Grafo Ativo e Rodando
  await page.screenshot({
    path: resolve(SCREENSHOTS_DIR, 'studio-main.png'),
    fullPage: false
  });
  console.log('[OK] studio-main.png capturado (com grafo de exemplo ativamente rodando).');

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
  await new Promise(r => setTimeout(r, 1000));

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
