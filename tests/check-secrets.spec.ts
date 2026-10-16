import { describe, it, expect } from 'vitest';
// @ts-expect-error - modulo .mjs sem tipos
import { scanText, checkPath, collectFiles, scanFiles } from '../scripts/check-secrets.mjs';

// Os "segredos" abaixo sao montados em tempo de execucao para que este proprio arquivo de teste
// nunca contenha um valor que o verificador consideraria um segredo.
const rnd = (n: number) => 'aZ3kQ9xT7mP2vL8cR5wN1bY6dH4jF0sG'.repeat(3).slice(0, n);
const rules = (text: string, file = 'examples/x/a.json') => scanText(text, file).map((f: any) => f.rule);

describe('verificador de segredos', () => {
  it('detecta os formatos reais de chave', () => {
    expect(rules(`key ${'sk-' + rnd(32)}`)).toContain('chave sk-... (OpenAI/DeepSeek/Anthropic)');
    expect(rules(`key ${'sk-ant-' + rnd(40)}`).length).toBeGreaterThan(0);
    expect(rules(`k ${'AIza' + rnd(35)}`)).toContain('chave Google (AIza...)');
    expect(rules(`k ${'gsk_' + rnd(40)}`)).toContain('chave Groq (gsk_...)');
    expect(rules(`t ${'ghp_' + rnd(36)}`)).toContain('token GitHub');
    expect(rules(`k ${'AKIA' + 'ABCDEFGHIJKLMNOP'}`)).toContain('chave de acesso AWS');
    expect(rules('-----BEGIN ' + 'PRIVATE KEY-----')).toContain('chave privada');
    expect(rules(`Authorization: ${'Bearer ' + rnd(40)}`)).toContain('cabecalho Bearer com token');
    expect(rules(`jwt ${'eyJ' + rnd(20)}.${'eyJ' + rnd(20)}.${rnd(20)}`)).toContain('JWT');
  });

  it('detecta segredo atribuido a um campo, mas aceita nomes de variavel e referencias', () => {
    expect(rules(`{"apiKey": "${rnd(30)}"}`).some((r: string) => r.startsWith('atribuicao'))).toBe(true);
    expect(rules(`password = '${rnd(20)}'`).some((r: string) => r.startsWith('atribuicao'))).toBe(true);
    // nome da variavel de ambiente, referencia e valores obviamente falsos: ok
    expect(rules('{"keyEnv": "DEEPSEEK_API_KEY"}')).toEqual([]);
    expect(rules('{"token": "${TICKETS_TOKEN}"}')).toEqual([]);
    expect(rules('{"apiKey": "test-key-deepseek"}')).toEqual([]);
    expect(rules('{"apiKey": "your-key-here"}')).toEqual([]);
    expect(rules('const k = "sk-test-secret-12345";')).toEqual([]);
  });

  it('detecta caminho de usuario e e-mail pessoal, e ignora enderecos de exemplo', () => {
    expect(rules('C:\\' + 'Users\\' + 'maria\\Documentos').length).toBeGreaterThan(0);
    expect(rules('/ho' + 'me/' + 'joao/projetos').length).toBeGreaterThan(0);
    expect(rules('contato: ' + 'maria.silva' + '@gmail.com')).toContain('e-mail');
    expect(rules('ana@example.com cliente@exemplo.com noreply@anthropic.com')).toEqual([]);
    expect(rules('C:\\Users\\Public\\x')).toEqual([]);
  });

  it('manifesto de projeto: rootPath deve ser "." e environment nao pode ter valor literal', () => {
    const ok = JSON.stringify({ rootPath: '.', environment: { TICKETS_TOKEN: '${TICKETS_TOKEN}' } });
    expect(rules(ok, 'examples/p/arestrix.project.json')).toEqual([]);
    const abs = JSON.stringify({ rootPath: 'D:/meus/projetos/p', environment: {} });
    expect(rules(abs, 'examples/p/arestrix.project.json').join()).toContain('rootPath');
    const lit = JSON.stringify({ rootPath: '.', environment: { MY_TOKEN: rnd(24) } });
    expect(rules(lit, 'examples/p/arestrix.project.json').join()).toContain('environment.MY_TOKEN');
    // Fora de examples/: rootPath absoluto tambem e recusado; relativo ("." ou subpasta) passa.
    expect(rules(JSON.stringify({ rootPath: 'C:' + '/Users/' + 'maria/p', environment: {} }), 'arestrix.project.json').join()).toContain('rootPath');
    expect(rules(JSON.stringify({ rootPath: '.', environment: {} }), 'arestrix.project.json')).toEqual([]);
    expect(rules(JSON.stringify({ rootPath: 'testeTutoria', environment: {} }), 'arestrix.project.json')).toEqual([]);
    // E o environment literal e recusado em qualquer manifesto, nao so em examples/.
    expect(rules(JSON.stringify({ rootPath: '.', environment: { MY_TOKEN: rnd(24) } }), 'arestrix.project.json').join()).toContain('environment.MY_TOKEN');
  });

  it('configuracao MCP: env so por referencia', () => {
    const good = JSON.stringify({ id: 's', command: 'python', env: { API_TOKEN: '${API_TOKEN}' } });
    expect(rules(good, 'examples/p/.workspace/mcps/s.json')).toEqual([]);
    const bad = JSON.stringify({ id: 's', command: 'python', env: { API_TOKEN: rnd(24) } });
    expect(rules(bad, 'examples/p/.workspace/mcps/s.json').join()).toContain('env.API_TOKEN');
  });

  it('bloqueia arquivos de credencial e artefatos gerados dentro de exemplos', () => {
    const bad = (p: string) => checkPath(p).length > 0;
    expect(bad('examples/p/.env')).toBe(true);
    expect(bad('examples/p/.env.local')).toBe(true);
    expect(bad('examples/p/chave.pem')).toBe(true);
    expect(bad('examples/p/storage/database.sqlite')).toBe(true);
    expect(bad('examples/p/output/relatorio.txt')).toBe(true);
    expect(bad('examples/p/.arestrix/out/a.txt')).toBe(true);
    expect(bad('examples/p/pipelines/main.json')).toBe(false);
    expect(bad('examples/p/.workspace/mcps/s.json')).toBe(false);
  });

  it('o relatorio nunca reproduz o valor encontrado por inteiro', () => {
    const secret = 'sk-' + rnd(40);
    const [finding] = scanText(`k=${secret}`, 'a.txt');
    expect(JSON.stringify(finding)).not.toContain(secret);
    expect(finding.sample.length).toBeLessThan(secret.length);
  });

  it('GARANTIA: nada em examples/ contem segredo, dado pessoal, credencial ou artefato gerado', () => {
    const files = collectFiles('default', []);
    const findings = scanFiles(files);
    expect(findings, JSON.stringify(findings, null, 2)).toEqual([]);
  });
});
