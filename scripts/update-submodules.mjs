#!/usr/bin/env node
import { spawnSync } from 'child_process';
import path from 'path';
import fs from 'fs';
import { fileURLToPath } from 'url';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const ROOT_DIR = path.resolve(__dirname, '..');

const SUBMODULES = [
  {
    name: 'i18next',
    path: 'third_party/i18next',
    remoteBranch: 'master',
    url: 'https://github.com/i18next/i18next'
  }
];

function runCmd(cmd, args, cwd = ROOT_DIR) {
  const res = spawnSync(cmd, args, { cwd, encoding: 'utf8' });
  if (res.error) throw res.error;
  return {
    code: res.status,
    stdout: (res.stdout || '').trim(),
    stderr: (res.stderr || '').trim()
  };
}

async function checkSubmodule(sub) {
  const subDir = path.resolve(ROOT_DIR, sub.path);
  if (!fs.existsSync(subDir)) {
    console.log(`❌ Submódulo ${sub.name} não encontrado em ${sub.path}. Execute git submodule update --init.`);
    return { name: sub.name, status: 'missing' };
  }

  console.log(`🔍 [${sub.name}] Verificando atualizações remotas (${sub.url})...`);
  
  // 1. Fetch do remoto
  runCmd('git', ['fetch', 'origin'], subDir);

  // 2. Commit local atual
  const localRev = runCmd('git', ['rev-parse', 'HEAD'], subDir).stdout;
  const localShort = localRev.slice(0, 7);

  // 3. Commit remoto mais recente na branch alvo
  const remoteRev = runCmd('git', ['rev-parse', `origin/${sub.remoteBranch}`], subDir).stdout;
  const remoteShort = remoteRev.slice(0, 7);

  // 4. Quantidade de commits de diferença
  const behindCount = parseInt(runCmd('git', ['rev-list', '--count', `HEAD..origin/${sub.remoteBranch}`], subDir).stdout, 10) || 0;
  const aheadCount = parseInt(runCmd('git', ['rev-list', '--count', `origin/${sub.remoteBranch}..HEAD`], subDir).stdout, 10) || 0;

  // 5. Versão do package.json do submódulo
  let subVersion = 'desconhecida';
  try {
    const pkg = JSON.parse(fs.readFileSync(path.join(subDir, 'package.json'), 'utf8'));
    subVersion = pkg.version || 'desconhecida';
  } catch (_) {}

  if (behindCount === 0) {
    console.log(`✅ [${sub.name}] Atualizado! Versão v${subVersion} (commit ${localShort} alinhado com origin/${sub.remoteBranch}).\n`);
    return { name: sub.name, status: 'up-to-date', localRev, remoteRev, version: subVersion };
  } else {
    console.log(`⚠️  [${sub.name}] Nova atualização disponível!`);
    console.log(`   - Versão local: commit ${localShort} (v${subVersion})`);
    console.log(`   - Versão remota: commit ${remoteShort} (${behindCount} novo(s) commit(s) em origin/${sub.remoteBranch})`);
    
    // Lista os últimos commits novos
    const log = runCmd('git', ['log', '--oneline', '-n', '5', `HEAD..origin/${sub.remoteBranch}`], subDir).stdout;
    if (log) {
      console.log('   - Últimas alterações:');
      log.split('\n').forEach(l => console.log(`     * ${l}`));
    }
    console.log('');
    return { name: sub.name, status: 'outdated', localRev, remoteRev, behindCount, version: subVersion };
  }
}

async function updateSubmodule(sub) {
  const subDir = path.resolve(ROOT_DIR, sub.path);
  console.log(`🚀 [${sub.name}] Atualizando submódulo para a versão remota mais recente...`);

  // 1. Atualizar submódulo via git
  const updateRes = runCmd('git', ['submodule', 'update', '--remote', '--merge', sub.path]);
  if (updateRes.code !== 0) {
    console.error(`❌ Falha ao atualizar ${sub.name}: ${updateRes.stderr}`);
    return false;
  }

  // 2. Novo commit e versão
  const newRev = runCmd('git', ['rev-parse', 'HEAD'], subDir).stdout.slice(0, 7);
  let newVersion = 'desconhecida';
  try {
    const pkg = JSON.parse(fs.readFileSync(path.join(subDir, 'package.json'), 'utf8'));
    newVersion = pkg.version || 'desconhecida';
  } catch (_) {}

  console.log(`✨ [${sub.name}] Atualizado com sucesso para v${newVersion} (commit ${newRev}).`);

  // 3. Execução automática de testes de regressão
  console.log(`🧪 [${sub.name}] Executando testes automatizados para verificar compatibilidade...`);
  const testRes = runCmd('npm', ['test']);
  if (testRes.code !== 0) {
    console.error(`❌ Atenção: Testes falharam após a atualização de ${sub.name}!`);
    console.error(testRes.stdout || testRes.stderr);
    return false;
  }
  console.log(`✅ Todos os testes passaram após a atualização!`);

  // 4. Teste de build
  console.log(`📦 [${sub.name}] Verificando build de produção...`);
  const buildRes = runCmd('npm', ['run', 'build']);
  if (buildRes.code !== 0) {
    console.error(`❌ Atenção: Build falhou após a atualização de ${sub.name}!`);
    return false;
  }
  console.log(`✅ Build de produção concluído com sucesso!\n`);
  return true;
}

async function main() {
  const args = process.argv.slice(2);
  const shouldUpdate = args.includes('--update') || args.includes('-u');

  console.log(`=======================================================`);
  console.log(`  Arestrix Studio - Gerenciador de Submódulos Git`);
  console.log(`=======================================================\n`);

  for (const sub of SUBMODULES) {
    const result = await checkSubmodule(sub);
    if (shouldUpdate && result.status === 'outdated') {
      await updateSubmodule(sub);
    }
  }

  if (!shouldUpdate) {
    console.log(`💡 Para atualizar automaticamente submódulos desatualizados, execute:`);
    console.log(`   npm run submodule:update\n`);
  }
}

main().catch(err => {
  console.error('Erro na execução:', err);
  process.exit(1);
});
