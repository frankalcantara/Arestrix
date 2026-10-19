#!/usr/bin/env node
/**
 * Valida os ativos locais embutidos no Arestrix sem baixar nada:
 *   - CppCore/runtime/models.catalog.json
 *   - CppCore/runtime/llama.manifest.json
 *
 * Verifica esquema, IDs únicos, URLs HTTPS, SHA-256, tamanhos, licenças e cobertura
 * mínima de plataformas/backends. Roda em CI ou antes de um release.
 */

import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';

const root = process.cwd();
const catalogPath = path.join(root, 'CppCore', 'runtime', 'models.catalog.json');
const manifestPath = path.join(root, 'CppCore', 'runtime', 'llama.manifest.json');

const errors = [];
const warnings = [];

function fail(message) {
  errors.push(message);
}

function readJson(file) {
  try {
    return JSON.parse(fs.readFileSync(file, 'utf8'));
  } catch (error) {
    fail(`${path.relative(root, file)}: JSON inválido (${error.message})`);
    return null;
  }
}

function isHttps(value) {
  return typeof value === 'string' && value.startsWith('https://');
}

function isSha256(value) {
  return typeof value === 'string' && /^[a-f0-9]{64}$/.test(value);
}

function isSafeName(value) {
  return typeof value === 'string' && value.length > 0 && value.length <= 255 &&
    !value.includes('/') && !value.includes('\\') && value !== '.' && value !== '..';
}

function isId(value) {
  return typeof value === 'string' && /^[a-z0-9][a-z0-9._-]*$/.test(value);
}

const catalog = readJson(catalogPath);
if (catalog) {
  if (catalog.schemaVersion !== 1) fail('catálogo: schemaVersion deve ser 1');
  if (!Array.isArray(catalog.models) || catalog.models.length === 0) {
    fail('catálogo: models deve ser um array não vazio');
  } else {
    const seen = new Set();
    for (const [index, model] of catalog.models.entries()) {
      const prefix = `catálogo.models[${index}]`;
      if (!isId(model.id)) fail(`${prefix}: id inválido`);
      if (seen.has(model.id)) fail(`${prefix}: id duplicado (${model.id})`);
      seen.add(model.id);
      if (!model.name) fail(`${prefix}: name ausente`);
      if (!isSafeName(model.fileName)) fail(`${prefix}: fileName inválido`);
      if (!isHttps(model.downloadUrl)) fail(`${prefix}: downloadUrl deve ser HTTPS`);
      if (!isHttps(model.sourceUrl)) fail(`${prefix}: sourceUrl deve ser HTTPS`);
      if (!model.license) fail(`${prefix}: license ausente`);
      if (!isHttps(model.licenseUrl)) fail(`${prefix}: licenseUrl deve ser HTTPS`);
      if (!isSha256(model.sha256)) fail(`${prefix}: sha256 inválido`);
      if (!Number.isInteger(model.sizeBytes) || model.sizeBytes <= 0) fail(`${prefix}: sizeBytes inválido`);
      if (!Number.isInteger(model.contextLength) || model.contextLength <= 0) fail(`${prefix}: contextLength inválido`);
      if (!Array.isArray(model.recommendedBackends) || model.recommendedBackends.length === 0) {
        fail(`${prefix}: recommendedBackends vazio`);
      }
      if (!Array.isArray(model.languages) || model.languages.length === 0) {
        fail(`${prefix}: languages vazio`);
      }
      if (model.verified) {
        if (!model.verifiedAt) fail(`${prefix}: modelo verificado sem verifiedAt`);
        if (!model.verifiedWith) fail(`${prefix}: modelo verificado sem verifiedWith`);
        if (!model.supportsTools) warnings.push(`${prefix}: verificado sem supportsTools=true`);
      }
    }
    const verified = catalog.models.filter((model) => model.verified).length;
    console.log(`catálogo: ${catalog.models.length} modelos, ${verified} verificados`);
  }
}

const manifest = readJson(manifestPath);
if (manifest) {
  if (manifest.schemaVersion !== 1) fail('manifesto: schemaVersion deve ser 1');
  const llama = manifest.components?.['llama.cpp'];
  if (!llama) {
    fail('manifesto: componente llama.cpp ausente');
  } else {
    if (!llama.version) fail('manifesto: llama.cpp sem version');
    if (!isHttps(llama.sourceUrl)) fail('manifesto: llama.cpp.sourceUrl deve ser HTTPS');
    if (!llama.license) fail('manifesto: llama.cpp.license ausente');
    if (!Array.isArray(llama.binaries) || llama.binaries.length === 0) {
      fail('manifesto: llama.cpp.binaries vazio');
    } else {
      const seen = new Set();
      const coverage = new Map();
      for (const [index, binary] of llama.binaries.entries()) {
        const prefix = `manifesto.llama.cpp.binaries[${index}]`;
        if (!isId(binary.id)) fail(`${prefix}: id inválido`);
        if (seen.has(binary.id)) fail(`${prefix}: id duplicado (${binary.id})`);
        seen.add(binary.id);
        if (!binary.platform) fail(`${prefix}: platform ausente`);
        if (!binary.backend) fail(`${prefix}: backend ausente`);
        if (!['zip', 'tar.gz'].includes(binary.archive)) fail(`${prefix}: archive inválido`);
        if (!isHttps(binary.url)) fail(`${prefix}: url deve ser HTTPS`);
        if (!isSha256(binary.sha256)) fail(`${prefix}: sha256 inválido`);
        if (!Number.isInteger(binary.sizeBytes) || binary.sizeBytes <= 0) fail(`${prefix}: sizeBytes inválido`);
        if (!binary.entryPoint) fail(`${prefix}: entryPoint ausente`);
        if (!coverage.has(binary.platform)) coverage.set(binary.platform, new Set());
        coverage.get(binary.platform).add(binary.backend);
      }
      for (const platform of ['windows-x64', 'linux-x64']) {
        const backends = coverage.get(platform);
        if (!backends) fail(`manifesto: sem binários para ${platform}`);
        else {
          if (!backends.has('cpu')) fail(`manifesto: ${platform} sem fallback CPU`);
          if (!backends.has('vulkan')) warnings.push(`manifesto: ${platform} sem backend Vulkan`);
        }
      }
      console.log(`manifesto: llama.cpp ${llama.version}, ${llama.binaries.length} binários`);
    }
  }
}

for (const warning of warnings) console.warn(`aviso: ${warning}`);
if (errors.length > 0) {
  for (const error of errors) console.error(`erro: ${error}`);
  console.error(`check-local-assets: ${errors.length} erro(s)`);
  process.exit(1);
}
console.log('check-local-assets: ativos locais válidos');
