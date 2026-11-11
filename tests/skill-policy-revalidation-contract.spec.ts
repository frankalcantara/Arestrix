import { readFileSync } from 'node:fs';
import path from 'node:path';
import { describe, expect, it } from 'vitest';

/**
 * Contrato de release para a revalidação de skills após mudança de política.
 *
 * O instalador não executa auditorias; ele detecta que a policyVersion da
 * instalação anterior difere da nova e mostra o aviso para o usuário abrir o
 * Studio. O Studio faz a reauditoria em lote no modal de inicialização.
 */
describe('Contrato de revalidação de skills no instalador', () => {
  const root = process.cwd();
  const iss = readFileSync(path.join(root, 'installer', 'arestrix.iss'), 'utf8');
  const packageRelease = readFileSync(path.join(root, 'scripts', 'package_release.py'), 'utf8');

  it('embute a policyVersion no instalador e compara com a versão instalada', () => {
    expect(iss).toContain('#ifndef SkillPolicyVersion');
    expect(iss).toContain('skill-policy-version.txt');
    expect(iss).toContain('SkillPolicyChanged');
    expect(iss).toContain('procedure CurStepChanged');
    expect(iss).toContain("OldPolicy <> NewPolicy");
  });

  it('grava skill-policy-version.txt no staging e passa o define ao ISCC', () => {
    expect(packageRelease).toContain('skill-policy-version.txt');
    expect(packageRelease).toContain('/DSkillPolicyVersion=');
    expect(packageRelease).toContain('skill_policy_version()');
  });

  it('o lock de skills tem uma policyVersion de 64 hex', () => {
    const policy = JSON.parse(
      readFileSync(path.join(root, 'src', 'core', 'skills', 'skill-validation-policy.json'), 'utf8')
    );
    expect(policy.policyVersion).toMatch(/^[0-9a-f]{64}$/);
  });
});
