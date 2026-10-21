#!/usr/bin/env node
/**
 * Smoke test ponta a ponta do arestrix-gateway.
 *
 * Sobe um upstream OpenAI falso, inicia o gateway em uma porta livre, cria uma chave
 * virtual com budget, valida a primeira chamada (200) e o bloqueio seguinte (402), e
 * confere /metrics e /v1/usage. Não usa rede externa nem chaves reais.
 */

import { spawn, execFileSync } from 'node:child_process';
import fs from 'node:fs';
import http from 'node:http';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';

const root = process.cwd();
const isWindows = process.platform === 'win32';
const gateway = path.join(root, 'build', isWindows ? 'arestrix-gateway.exe' : 'arestrix-gateway');

if (!fs.existsSync(gateway)) {
  console.error(`gateway-smoke: binário não encontrado: ${gateway}`);
  console.error('gateway-smoke: rode o build antes (python CppCore/dev.py --no-run).');
  process.exit(1);
}

const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'arestrix-gateway-smoke-'));
const db = path.join(temp, 'gateway.db');
const configPath = path.join(temp, 'gateway.json');
let gatewayProcess = null;
let upstream = null;
let finished = false;
let output = '';

function fail(message, detail = '') {
  throw new Error(detail ? `${message}\n${detail.trim()}` : message);
}

function wait(ms) {
  return new Promise(resolve => setTimeout(resolve, ms));
}

function freePort() {
  return new Promise((resolve, reject) => {
    const server = net.createServer();
    server.once('error', reject);
    server.listen(0, '127.0.0.1', () => {
      const port = server.address().port;
      server.close(() => resolve(port));
    });
  });
}

async function waitHealth(url) {
  for (let attempt = 0; attempt < 80; ++attempt) {
    try {
      const response = await fetch(`${url}/health`, { signal: AbortSignal.timeout(2000) });
      if (response.ok) return;
    } catch {}
    await wait(100);
  }
  throw new Error('gateway não respondeu em /health');
}

async function cleanup() {
  if (finished) return;
  finished = true;

  if (gatewayProcess && gatewayProcess.exitCode === null) {
    const exited = new Promise(resolve => gatewayProcess.once('exit', resolve));
    try { gatewayProcess.kill(); } catch {}
    await Promise.race([exited, wait(2000)]);
  }
  if (upstream) {
    try { upstream.closeAllConnections?.(); } catch {}
    await new Promise(resolve => upstream.close(resolve));
  }
  try { fs.rmSync(temp, { recursive: true, force: true }); } catch {}
}

async function main() {
  upstream = http.createServer((req, res) => {
    req.resume();
    req.on('end', () => {
      res.writeHead(200, { 'Content-Type': 'application/json', Connection: 'close' });
      res.end(JSON.stringify({
        id: 'chatcmpl-smoke',
        object: 'chat.completion',
        model: 'test-model',
        choices: [{ index: 0, message: { role: 'assistant', content: 'ok' }, finish_reason: 'stop' }],
        usage: { prompt_tokens: 10, completion_tokens: 5, total_tokens: 15,
                 prompt_tokens_details: { cached_tokens: 0 } }
      }));
    });
  });
  await new Promise((resolve, reject) => {
    upstream.once('error', reject);
    upstream.listen(0, '127.0.0.1', resolve);
  });
  const upstreamPort = upstream.address().port;
  const gatewayPort = await freePort();

  fs.writeFileSync(configPath, JSON.stringify({
    host: '127.0.0.1',
    port: gatewayPort,
    database: db,
    defaultMaxTokens: 5,
    requestTimeoutSeconds: 30,
    routes: {
      fake: {
        protocol: 'openai',
        baseUrl: `http://127.0.0.1:${upstreamPort}/v1`,
        keyEnv: 'FAKE_KEY'
      }
    },
    pricing: {
      fake: {
        'test-model': { input: 1.0, output: 2.0, cachedInput: 0.5 }
      }
    }
  }, null, 2));

  gatewayProcess = spawn(gateway, ['serve', '--config', configPath], {
    cwd: root,
    env: { ...process.env, FAKE_KEY: 'test' },
    stdio: ['ignore', 'pipe', 'pipe']
  });
  gatewayProcess.stdout.on('data', chunk => { output += chunk; });
  gatewayProcess.stderr.on('data', chunk => { output += chunk; });

  const base = `http://127.0.0.1:${gatewayPort}`;
  await waitHealth(base);

  const keyOutput = execFileSync(gateway, [
    'key', 'create', '--name', 'smoke', '--project', 'demo', '--user', 'smoke',
    '--budget-usd', '0.0001', '--period', 'total', '--db', db
  ], { cwd: root, encoding: 'utf8' });
  const keyData = JSON.parse(keyOutput);
  const key = keyData.key;
  if (!key || !key.startsWith('av_')) fail('a CLI não devolveu uma chave virtual válida', keyOutput);

  const body = JSON.stringify({
    model: 'test-model',
    messages: [{ role: 'user', content: 'hello' }],
    max_tokens: 5
  });
  const headers = {
    'Content-Type': 'application/json',
    Authorization: `Bearer ${key}`,
    Connection: 'close',
    'X-Arestrix-Agent': 'agente-smoke',
    'X-Arestrix-Run-Id': 'run-smoke',
    'X-Arestrix-Memory-Scope': 'project:demo',
    'X-Arestrix-Memory-Enabled': '1',
    'X-Arestrix-Memory-Context-Chars': '640'
  };
  const request = () => fetch(`${base}/fake/chat/completions`, {
    method: 'POST',
    headers,
    body,
    signal: AbortSignal.timeout(10000)
  });

  const first = await request();
  const firstText = await first.text();
  if (first.status !== 200) fail(`primeira chamada retornou HTTP ${first.status}`, firstText);

  let blocked = null;
  let blockedText = '';
  for (let attempt = 0; attempt < 8; ++attempt) {
    blocked = await request();
    blockedText = await blocked.text();
    if (blocked.status === 402) break;
  }
  if (!blocked || blocked.status !== 402) {
    fail(`o budget não bloqueou a chamada (HTTP ${blocked ? blocked.status : '?'})`, blockedText);
  }
  if (!blockedText.includes('budget')) fail('resposta 402 sem error.code de budget', blockedText);

  const metrics = await fetch(`${base}/metrics`, { signal: AbortSignal.timeout(5000) });
  const metricsText = await metrics.text();
  if (!metricsText.includes('budget_rejections_total')) fail('métricas sem budget_rejections_total', metricsText);

  const usage = await fetch(`${base}/v1/usage`, {
    headers: { Authorization: `Bearer ${key}`, Connection: 'close' },
    signal: AbortSignal.timeout(5000)
  });
  const usageText = await usage.text();
  if (!usage.ok || !usageText.includes('budgets')) fail('/v1/usage inválido', usageText);
  const usageJson = JSON.parse(usageText);
  if (!(Number(usageJson.usage?.memoryTokens) > 0)) fail('/v1/usage sem tokens de memória', usageText);
  if (!(Number(usageJson.usage?.memoryContextChars) > 0)) fail('/v1/usage sem contexto de memória', usageText);
  if (!(Number(usageJson.usage?.memoryRequests) > 0)) fail('/v1/usage sem requisições com memória', usageText);

  const report = JSON.parse(execFileSync(gateway, [
    'usage', 'report', '--key', keyData.keyId, '--db', db
  ], { cwd: root, encoding: 'utf8' }));
  if (!(Number(report.totals?.memoryTokens) > 0)) fail('usage report sem tokens de memória', JSON.stringify(report));
  if (!Array.isArray(report.byMemory) || report.byMemory.length === 0) fail('usage report sem comparativo ON/OFF', JSON.stringify(report));
}

main()
  .then(async () => {
    await cleanup();
    console.log('gateway-smoke: OK (200 -> 402, métricas, /v1/usage e impacto da memória)');
  })
  .catch(async error => {
    await cleanup();
    console.error(`gateway-smoke: FALHOU: ${error.message}`);
    if (output.trim()) console.error(output.trim());
    process.exitCode = 1;
  });
