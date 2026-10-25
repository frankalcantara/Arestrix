#!/usr/bin/env node
/**
 * Gate de integridade das referências de validação de skills.
 *
 * Verifica se os submodules NVIDIA SkillSpector e Cloudflare security-audit
 * estão exatamente nos commits/trees/hashes aprovados no lock e se a
 * policyVersion foi recalculada depois de qualquer mudança.
 *
 * Uso:
 *   node scripts/check-skill-references.mjs
 *   node scripts/check-skill-references.mjs --json
 *   node scripts/check-skill-references.mjs --suggest
 *
 * --suggest imprime um lock candidato para revisão humana. Ele NÃO grava o
 * arquivo e não aprova a atualização.
 */
import { createHash } from 'node:crypto';
import { execFileSync } from 'node:child_process';
import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const ROOT = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const POLICY_PATH = path.join(ROOT, 'src', 'core', 'skills', 'skill-validation-policy.json');
const args = new Set(process.argv.slice(2));
const asJson = args.has('--json');
const suggest = args.has('--suggest');

const failures = [];
const checks = [];

function sha256(text) {
  return createHash('sha256').update(text, 'utf8').digest('hex');
}

function fileSha256(file) {
  return createHash('sha256').update(readFileSync(file)).digest('hex');
}

function git(cwd, gitArgs) {
  try {
    return execFileSync('git', gitArgs, { cwd, encoding: 'utf8', stdio: ['ignore', 'pipe', 'pipe'] }).trim();
  } catch (error) {
    return { error: String(error.stderr || error.message || error) };
  }
}

function canonicalPolicyVersion(policy) {
  const references = [...policy.references]
    .sort((a, b) => (a.id < b.id ? -1 : a.id > b.id ? 1 : 0))
    .map(reference => [
      reference.id,
      reference.repo,
      reference.path,
      reference.commit,
      reference.tree,
      reference.skillPath,
      reference.skillSha256,
      reference.license,
      reference.role
    ].join('|'));
  const canonical = [
    `schemaVersion=${policy.schemaVersion}`,
    `ruleSetVersion=${policy.ruleSetVersion}`,
    `guardSkill=${policy.guardSkill.path}|${policy.guardSkill.sha256}`,
    ...[...policy.ruleSources]
      .sort((a, b) => (a.path < b.path ? -1 : a.path > b.path ? 1 : 0))
      .map(source => `ruleSource=${source.path}|${source.sha256}`),
    ...references
  ].join('\n');
  return sha256(canonical);
}

function addCheck(label, ok, detail) {
  checks.push({ label, ok, detail });
  if (!ok) failures.push(`${label}: ${detail}`);
}

if (!existsSync(POLICY_PATH)) {
  console.error(`[skill-references] lock ausente: ${POLICY_PATH}`);
  process.exit(1);
}

const policy = JSON.parse(readFileSync(POLICY_PATH, 'utf8'));
addCheck('lock schema', policy.schemaVersion === 1, `schemaVersion=${policy.schemaVersion}`);
addCheck('rule set', typeof policy.ruleSetVersion === 'string' && policy.ruleSetVersion.length > 0,
  `ruleSetVersion=${policy.ruleSetVersion}`);

const modules = existsSync(path.join(ROOT, '.gitmodules'))
  ? readFileSync(path.join(ROOT, '.gitmodules'), 'utf8')
  : '';

const guardFile = path.join(ROOT, policy.guardSkill.path);
const guardHash = existsSync(guardFile) ? fileSha256(guardFile) : '';
addCheck('guard skill', guardHash === policy.guardSkill.sha256,
  `sha256=${guardHash || 'ausente'} esperado=${policy.guardSkill.sha256}`);

const suggested = JSON.parse(JSON.stringify(policy));
for (const source of policy.ruleSources) {
  const sourceFile = path.join(ROOT, source.path);
  const sourceHash = existsSync(sourceFile) ? fileSha256(sourceFile) : '';
  addCheck(`rule source ${source.path}`, sourceHash === source.sha256,
    `sha256=${sourceHash || 'ausente'} esperado=${source.sha256}`);
  const candidate = suggested.ruleSources.find(item => item.path === source.path);
  if (candidate && sourceHash) candidate.sha256 = sourceHash;
}
if (guardHash) suggested.guardSkill.sha256 = guardHash;
for (const reference of policy.references) {
  const target = path.join(ROOT, reference.path);
  addCheck(`${reference.id} path`, existsSync(target), reference.path);
  if (!existsSync(target)) continue;

  const head = git(target, ['rev-parse', 'HEAD']);
  const tree = git(target, ['rev-parse', 'HEAD^{tree}']);
  const status = git(target, ['status', '--porcelain']);
  const describe = git(target, ['describe', '--tags', '--always']);
  const skillFile = path.join(target, reference.skillPath);
  const actualSkillHash = existsSync(skillFile) ? fileSha256(skillFile) : '';

  addCheck(`${reference.id} gitmodules`,
    modules.includes(`path = ${reference.path}`) && modules.includes(`url = ${reference.repo}`),
    `${reference.path} -> ${reference.repo}`);
  addCheck(`${reference.id} commit`,
    typeof head === 'string' && head === reference.commit,
    `HEAD=${typeof head === 'string' ? head : head.error} esperado=${reference.commit}`);
  addCheck(`${reference.id} tree`,
    typeof tree === 'string' && tree === reference.tree,
    `tree=${typeof tree === 'string' ? tree : tree.error} esperado=${reference.tree}`);
  addCheck(`${reference.id} worktree`, status === '', status || 'clean');
  addCheck(`${reference.id} SKILL.md`,
    actualSkillHash === reference.skillSha256,
    `sha256=${actualSkillHash || 'ausente'} esperado=${reference.skillSha256}`);

  const candidate = suggested.references.find(item => item.id === reference.id);
  if (candidate) {
    if (typeof head === 'string') candidate.commit = head;
    if (typeof tree === 'string') candidate.tree = tree;
    if (typeof describe === 'string' && describe) candidate.version = describe;
    if (actualSkillHash) candidate.skillSha256 = actualSkillHash;
  }
}

const computedPolicy = canonicalPolicyVersion(policy);
addCheck('policyVersion', computedPolicy === policy.policyVersion,
  `calculado=${computedPolicy} lock=${policy.policyVersion}`);

const ids = new Set(policy.references.map(reference => reference.id));
addCheck('NVIDIA reference', ids.has('nvidia-skillspector'), 'nvidia-skillspector');
addCheck('Cloudflare reference', ids.has('cloudflare-security-audit'), 'cloudflare-security-audit');

suggested.policyVersion = canonicalPolicyVersion(suggested);
if (suggest) {
  process.stdout.write(JSON.stringify(suggested, null, 2) + '\n');
  console.error('[skill-references] lock candidato impresso; revise o diff antes de gravar.');
  process.exit(0);
}

if (asJson) {
  console.log(JSON.stringify({ ok: failures.length === 0, checks, failures }, null, 2));
} else {
  for (const check of checks) {
    console.log(`  ${check.ok ? 'ok  ' : 'ERRO'}  ${check.label}${check.ok ? '' : ' -> ' + check.detail}`);
  }
}

if (failures.length > 0) {
  console.error(`\n[skill-references] ${failures.length} divergência(s). Atualize o lock somente após revisão.`);
  process.exit(1);
}
console.log('\n[skill-references] referências e política íntegras.');
