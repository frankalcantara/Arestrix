# Revisão das referências de validação de skills

> **Aplica-se a:** Arestrix Beta 2.3 (`2.3.0-beta.2`) · Última revisão: 2026-10-28
>
> Este documento define o processo obrigatório sempre que uma das referências de segurança de skills
> for atualizada. O lock em `src/core/skills/skill-validation-policy.json` é a fonte de verdade
> operacional; esta página explica como revisá-lo.

## 1. Referências fixadas

| ID | Repositório | Papel | Licença |
| :--- | :--- | :--- | :--- |
| `nvidia-skillspector` | [NVIDIA/SkillSpector](https://github.com/NVIDIA/SkillSpector) | evidência estática e catálogo de regras | Apache-2.0 |
| `cloudflare-security-audit` | [cloudflare/security-audit-skill](https://github.com/cloudflare/security-audit-skill) | metodologia de fronteira, validação adversarial e relatório | MIT |

O lock guarda `commit`, `tree`, `skillPath`, `skillSha256`, versão, licença e data de revisão de cada
referência, além do hash do nosso próprio `arestrix-skill-guard/SKILL.md` e dos arquivos de regra
`SkillSpectorRunner.ts`/`CloudflareSkillAudit.ts`. A `policyVersion` é o SHA-256 de uma string canônica
com `schemaVersion`, `ruleSetVersion`, hashes do guard skill e das regras locais, e os campos das
referências. O mesmo algoritmo existe em:

- `src/core/skills/SkillValidationPolicy.ts` (runtime);
- `scripts/check-skill-references.mjs` (gate de CI/dev).

Se os dois divergirem, o teste `tests/skill-validation-policy.spec.ts` falha.

## 2. Quando revisar

A revisão é obrigatória quando:

1. o commit de `third_party/SkillSpector` ou `third_party/security-audit-skill` mudar;
2. o `SKILL.md` de qualquer referência mudar (mesmo sem mudar o commit);
3. uma regra local de `SkillSpectorRunner` ou `CloudflareSkillAudit` for adicionada, removida ou
   alterada;
4. o skill `arestrix-skill-guard` mudar de contrato, rubrica ou classes de ataque;
5. houver CVE/advisory, incidente de supply chain ou nova técnica relevante;
6. a revisão trimestral vencer, mesmo sem mudança.

Atualização de submodule feita por Dependabot/bot **não** aprova a política. O PR pode ser aberto
automaticamente, mas precisa da revisão humana descrita aqui.

## 3. Procedimento

### 3.1 Detectar a divergência

```bash
git submodule update --init --recursive third_party/SkillSpector third_party/security-audit-skill
npm run check:skill-references
```

O gate falha se commit, tree, worktree, hash do `SKILL.md` ou `policyVersion` não baterem com o lock.

### 3.2 Ler o upstream

```bash
git -C third_party/SkillSpector fetch origin
git -C third_party/security-audit-skill fetch origin
git -C third_party/SkillSpector log --oneline <commit-antigo>..origin/main
git -C third_party/security-audit-skill log --oneline <commit-antigo>..origin/main
```

Leia, no mínimo:

- `third_party/SkillSpector/README.md`, `CHANGELOG.md` e
  `skills/skill-inspector/SKILL.md`;
- `third_party/security-audit-skill/README.md`,
  `skills/security-audit/SKILL.md` e os companions afetados
  (`AI-AND-LLM.md`, `SUPPLY-CHAIN-AND-RELEASE.md`,
  `DATA-ISOLATION-AND-LIFECYCLE.md`, etc.);
- os validadores `validate-findings.cjs` e `validate-coverage-ledger.cjs` quando o schema/relatório
  mudar.

Procure especificamente por:

- regras novas, removidas ou com severidade alterada;
- mudança de rubrica de veredito ou de política de evidência;
- novas classes de ataque aplicáveis a skills;
- mudança de schema de relatório/findings;
- mudança de requisitos de sandbox ou de execução;
- qualquer instrução que conflite com a política local do Arestrix.

### 3.3 Mapear impacto local

Preencha internamente (PR/revisão):

| Pergunta | Onde responder |
| :--- | :--- |
| A regra nova cabe em `SkillSpectorRunner`? | regras estáticas TS |
| A regra nova cabe em `CloudflareSkillAudit`? | fronteira/persistência/supply chain |
| A regra exige revisão semântica? | `arestrix-skill-guard/SKILL.md` |
| O schema de relatório mudou? | skill guard, docs, validadores |
| A severidade/veredito mudou? | rubrica, testes, UI |
| É necessário reauditar skills já aprovadas? | `policyVersion` (automático) |

Não copie texto upstream sem necessidade. O Arestrix mantém regras próprias, pequenas e testáveis;
as referências entram como **catálogo e metodologia**, não como dependência de runtime.

### 3.4 Atualizar o lock

```bash
node scripts/check-skill-references.mjs --suggest > skill-policy.candidate.json
```

Revise o JSON candidato e copie para `src/core/skills/skill-validation-policy.json` somente depois de:

1. confirmar commit/tree/hash de cada referência;
2. atualizar `reviewedAt` e `reviewedBy`;
3. incrementar `ruleSetVersion` se as regras locais mudaram;
4. recalcular/validar `policyVersion` (o script sugere o valor);
5. registrar no PR o diff de regras e a decisão.

O script **não** grava o lock e **não** aprova nada. Apague o arquivo candidato depois de revisar.

### 3.5 Atualizar o skill guard e as regras locais

- `SkillSpectorRunner`: novas regras estáticas com `ruleId` estável e `referenceRule` quando houver
  equivalente NVIDIA.
- `CloudflareSkillAudit`: classes de fronteira, com severidade e peso justificados.
- `arestrix-skill-guard/SKILL.md`: contrato de revisão, rubrica, classes e processo.
- `SkillValidationPolicy.ts`: IDs, metadados e versão do motor.
- `docs/pt-BR/workspaces-memoria-skills-mcp.md` e `docs/en/workspaces-memory-skills-mcp.md`.

Toda regra nova precisa de teste positivo e, quando possível, de um caso negativo para evitar falso
positivo em skill legítima.

### 3.6 Validar

```bash
npm run check:skill-references
npm test
py CppCore/dev.py --test --no-frontend
npm run build
npm run check:secrets
```

Para release:

```bash
py CppCore/dev.py --full
```

### 3.7 Reauditar skills existentes

A mudança de `policyVersion` invalida automaticamente as entradas antigas do
`SkillLedgerService`: `getEntry`, `isApproved` e `getStatus` passam a devolver `PENDING` até uma nova
auditoria. Isso é intencional e fail-closed.

No Studio, reabra cada skill importada e execute **Auditar com SkillSpector** (o botão usa as duas
camadas locais). No workspace, o auditor grava a nova entrada com `policyVersion`,
`referenceVersions` e `auditCycles`.

## 4. Critérios de aprovação da revisão

- [ ] Commit, tree e `SKILL.md` das duas referências conferem com o lock.
- [ ] Hashes do guard skill e dos arquivos de regra local conferem com o lock.
- [ ] O diff upstream foi lido e as regras novas/removidas foram mapeadas.
- [ ] `ruleSetVersion` foi incrementado quando as regras locais mudaram.
- [ ] `policyVersion` foi recalculado e o gate está verde.
- [ ] Testes positivos e negativos das regras novas passam.
- [ ] O skill `arestrix-skill-guard` reflete o contrato atual.
- [ ] Docs PT/EN e `CHANGELOG.md` foram atualizados.
- [ ] A decisão registra quem revisou, quando e o que mudou.
- [ ] Skills existentes foram reauditadas (ou a invalidação automática foi comunicada).

## 5. Falha fechada

- Referência ausente, suja, em commit diferente ou com hash divergente: **gate vermelho**.
- Regra nova sem teste: **não entra**.
- Atualização de referência sem revisão humana: **não entra**.
- Dúvida sobre comportamento sensível: `CAUTION` ou `REJECT`, nunca `APPROVE` por reputação.
- Divergência entre doc e código: o lock e os testes são a fonte; a doc é corrigida no mesmo PR.
