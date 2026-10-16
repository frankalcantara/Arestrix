import { describe, it, expect, beforeEach } from 'vitest';
import fs from 'fs';
import path from 'path';
import { t, getLocale, setLocale, toggleLocale, SUPPORTED_LOCALES, translateMcpError } from '../src/i18n/index.ts';
import ptBR from '../src/i18n/locales/pt-BR.json';
import enUS from '../src/i18n/locales/en-US.json';
import { globalEventBus } from '../src/core/EventBus.ts';

describe('Arestrix Studio - Motor Poliglota i18n (i18next)', () => {
  beforeEach(async () => {
    await setLocale('pt-BR');
  });

  it('1. Deve suportar os idiomas Português do Brasil (pt-BR) e Inglês dos EUA (en-US)', () => {
    expect(SUPPORTED_LOCALES).toHaveLength(2);
    const codes = SUPPORTED_LOCALES.map(l => l.code);
    expect(codes).toContain('pt-BR');
    expect(codes).toContain('en-US');
  });

  it('2. Deve traduzir chaves básicas em pt-BR por padrão', () => {
    expect(getLocale()).toBe('pt-BR');
    expect(t('common.save')).toBe('Salvar');
    expect(t('ribbon.titlebar.autosave')).toBe('Salvamento Automático');
    expect(t('ribbon.tabs.home')).toBe('Página Inicial');
    expect(t('statusbar.ready')).toBe('Pronto');
    expect(t('console.tab_terminal')).toBe('Terminal MSVC C++20');
  });

  it('3. Deve alternar para en-US e traduzir com precisão', async () => {
    await setLocale('en-US');
    expect(getLocale()).toBe('en-US');

    expect(t('common.save')).toBe('Save');
    expect(t('ribbon.titlebar.autosave')).toBe('AutoSave');
    expect(t('ribbon.tabs.home')).toBe('Home');
    expect(t('statusbar.ready')).toBe('Ready');
    expect(t('console.tab_terminal')).toBe('MSVC C++20 Terminal');
  });

  it('4. Deve alternar entre idiomas via toggleLocale()', async () => {
    expect(getLocale()).toBe('pt-BR');
    const l1 = await toggleLocale();
    expect(l1).toBe('en-US');
    expect(getLocale()).toBe('en-US');
    expect(t('ribbon.tabs.file')).toBe('File');

    const l2 = await toggleLocale();
    expect(l2).toBe('pt-BR');
    expect(getLocale()).toBe('pt-BR');
    expect(t('ribbon.tabs.file')).toBe('Arquivo');
  });

  it('5. Deve suportar interpolação de variáveis em ambos os idiomas', async () => {
    await setLocale('pt-BR');
    expect(t('statusbar.stats', { nodes: 5, edges: 4 })).toBe('5 vértices, 4 conexões');
    expect(t('actions.port_input', { name: 'in_data' })).toBe('Porta de Entrada: in_data');

    await setLocale('en-US');
    expect(t('statusbar.stats', { nodes: 5, edges: 4 })).toBe('5 vertices, 4 connections');
    expect(t('actions.port_input', { name: 'in_data' })).toBe('Input Port: in_data');
  });

  it('6. Deve disparar evento no globalEventBus quando o idioma for alterado', async () => {
    let notifiedLocale = '';
    const unsubscribe = globalEventBus.on('locale:changed', (locale: string) => {
      notifiedLocale = locale;
    });

    await setLocale('en-US');
    expect(notifiedLocale).toBe('en-US');

    await setLocale('pt-BR');
    expect(notifiedLocale).toBe('pt-BR');
  });

  it('7. Deve garantir paridade de chaves estruturais entre pt-BR e en-US', () => {
    function getAllKeys(obj: any, prefix = ''): string[] {
      let keys: string[] = [];
      for (const k of Object.keys(obj)) {
        const fullKey = prefix ? `${prefix}.${k}` : k;
        if (typeof obj[k] === 'object' && obj[k] !== null && !Array.isArray(obj[k])) {
          keys = keys.concat(getAllKeys(obj[k], fullKey));
        } else {
          keys.push(fullKey);
        }
      }
      return keys;
    }

    const ptKeys = getAllKeys(ptBR).sort();
    const enKeys = getAllKeys(enUS).sort();

    expect(ptKeys.length).toBeGreaterThan(50);
    expect(enKeys.length).toBe(ptKeys.length);
    expect(enKeys).toEqual(ptKeys);
  });

  it('8. Deve carregar a biblioteca diretamente do submódulo git em third_party/i18next sem depender de node_modules', () => {
    const submodulePath = path.resolve(process.cwd(), 'third_party/i18next');
    const submoduleEntry = path.join(submodulePath, 'src/index.js');
    const nodeModulesPath = path.resolve(process.cwd(), 'node_modules/i18next');
    const gitmodulesPath = path.resolve(process.cwd(), '.gitmodules');

    expect(fs.existsSync(submodulePath)).toBe(true);
    expect(fs.existsSync(submoduleEntry)).toBe(true);
    expect(fs.existsSync(nodeModulesPath)).toBe(false);

    const gitmodulesContent = fs.readFileSync(gitmodulesPath, 'utf8');
    expect(gitmodulesContent).toContain('submodule "third_party/i18next"');
    expect(gitmodulesContent).toContain('url = https://github.com/i18next/i18next');
  });

  it('9. Não deve disparar eventos redundantes quando o mesmo idioma for solicitado novamente', async () => {
    expect(getLocale()).toBe('pt-BR');
    let callCount = 0;
    const unsub = globalEventBus.on('locale:changed', () => {
      callCount++;
    });

    await setLocale('pt-BR');
    expect(callCount).toBe(0);

    await setLocale('en-US');
    expect(callCount).toBe(1);

    await setLocale('en-US');
    expect(callCount).toBe(1);

    unsub();
  });

  it('10. Deve traduzir erros MCP estruturados (errorCode + error + errorDetail)', async () => {
    await setLocale('pt-BR');
    const pt = translateMcpError('invalid_arg', 'Argumento invalido para o comando do servidor MCP.', 'a" & b');
    expect(pt).toContain('aspas');
    expect(pt).toContain('(a" & b)');

    await setLocale('en-US');
    expect(translateMcpError('url_not_https', 'So URLs https:// sao permitidas...', null)).toContain('https://');
    expect(translateMcpError('url_bad_port', 'So a porta 443...', '8443')).toContain('443');
    expect(translateMcpError('url_bad_port', 'So a porta 443...', '8443')).toContain('(8443)');

    // Codigo desconhecido ou ausente: usa o texto de fallback enviado pelo backend.
    expect(translateMcpError('codigo_futuro', 'Texto bruto do backend', 'x')).toBe('Texto bruto do backend');
    expect(translateMcpError(null, 'Sem codigo')).toBe('Sem codigo');
    expect(translateMcpError(undefined, undefined)).toBe('');
  });

  it('11. Deve manter paridade das chaves mcp_errors entre pt-BR e en-US', () => {
    const ptKeys = Object.keys((ptBR as any).mcp_errors).sort();
    const enKeys = Object.keys((enUS as any).mcp_errors).sort();
    expect(ptKeys.length).toBeGreaterThan(20);
    expect(enKeys).toEqual(ptKeys);
  });
});

