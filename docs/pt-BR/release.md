# Gate de release e artefatos

> **Fase 5A.** Esta é a camada de validação reproduzível antes do empacotamento e da assinatura.
> Certificado/HSM final, instaladores, CI Linux e catálogo remoto assinado continuam pendentes; veja o
> [roadmap](roadmap.md).

## 1. Prepare uma árvore limpa

Artefatos de release devem vir de um commit revisado:

```bash
git status --porcelain
git switch main
git pull --ff-only
```

O gate recusa árvore suja por padrão. `--allow-dirty` existe apenas para ensaio local e registra
`"dirty": true` no manifesto.

## 2. Rode o gate

Windows:

```powershell
py scripts/release_check.py
```

Linux/macOS:

```bash
python3 scripts/release_check.py
```

O gate executa a sequência canônica `CppCore/dev.py --full --e2e`:

- build do frontend (`tsc` + Vite);
- build nativo C++20 com hardening;
- CTest;
- Vitest;
- verificações de versão, ativos locais, segredos e licenças de terceiros;
- smoke test HTTP do gateway;
- E2E do tutorial e dos exemplos.

Use `--skip-e2e` apenas para um ensaio mais rápido. Um gate de produção deve incluir E2E.

## 3. Inspecione a pasta gerada

```text
release/
  release-report.json       etapas, códigos de saída, durações e status
  release-manifest.json     versão, commit, plataforma e hashes dos artefatos
  SHA256SUMS                linhas padrão "hash  caminho"
  SBOM.json                 submodules nativos e componentes npm
```

Verifique um download manualmente:

```powershell
Get-FileHash build\arestrix.exe -Algorithm SHA256
```

```bash
sha256sum -c release/SHA256SUMS
```

O manifesto é gerado a partir dos arquivos reais em disco. Ele nunca lê chaves de API, segredos de
projeto ou dados de usuário.

## 3.1 Pacote portátil (fase 5B1)

Depois que o manifesto de release existir, gere a distribuição portátil:

```powershell
py scripts/package_release.py
```

```bash
python3 scripts/package_release.py
```

O script monta exatamente o layout esperado pelo Studio e pelo compilador de pipelines:

```text
arestrix(.exe), arestrix-cli(.exe), arestrix-gateway(.exe), arestrix-memory(.exe)
dist/                         UI compilada do Studio
CppCore/src/                  headers/fontes usados pelos pipelines gerados
CppCore/runtime/              catálogo e manifesto do runtime local
runtime/lib/                  biblioteca arestrix_core
third_party/taskflow/         headers do Taskflow
third_party/simdjson/         headers do simdjson
examples/, docs/              exemplos e documentação
.agents/skills/               skills de desenvolvimento (Arestrix Skill Guard)
package-manifest.json         hashes dos arquivos
SHA256SUMS                    checksums do pacote
SBOM.json                     inventário de componentes
```

Ele executa `arestrix-cli version` e `arestrix-cli project validate` em um exemplo antes de arquivar.
Árvore limpa é exigida por padrão.

O arquivo gerado é `release/arestrix-<version>-<platform>-<arch>.zip` no Windows ou `.tar.gz` no
Linux/macOS; `--format both` cria os dois. O pacote portátil **não** inclui MSVC/GCC. Para compilar
pipelines no destino ainda é necessário um compilador C++20 e o Visual C++ Redistributable no
Windows.

## 3.2 Deploy portátil automatizado (Windows)

O `.github/workflows/deploy.yml` compila e publica o pacote portátil pelo GitHub Actions:

- um push para a `main` atualiza o pré-release contínuo
  [`latest`](https://github.com/frankalcantara/Arestrix/releases/tag/latest);
- o push de uma tag `v*` publica ou atualiza o release daquela tag;
- a execução manual segue a branch/tag selecionada.

O workflow roda o gate canônico (`release_check.py --skip-e2e`), gera o ZIP com
`package_release.py` e anexa ao release o arquivo, `.zip.sha256`, `SHA256SUMS`, `SBOM.json`,
`release-manifest.json` e `release-report.json`. A pasta extraída roda em qualquer diretório
(`start-arestrix.bat`); não é necessário um checkout de desenvolvimento.

O E2E continua sendo requisito de promoção: rode o `release_check.py` completo (com E2E) em uma
árvore limpa antes de taguear um release oficial.

## 4. Assine os binários do Windows

Depois do gate aprovado, assine os executáveis com o certificado corporativo:

```powershell
$env:ARESTRIX_SIGN_PFX = "C:\secrets\arestrix.pfx"
$env:ARESTRIX_SIGN_PFX_PASSWORD = "..."
pwsh scripts/sign-windows.ps1
```

Como alternativa, use `ARESTRIX_SIGN_THUMBPRINT` para um certificado instalado em `My`. O script cobre
`arestrix.exe`, `arestrix-cli.exe`, `arestrix-gateway.exe`, `arestrix-memory.exe` e um `-RuntimePath` opcional.

## 5. Fora da fase 5A

- Instalador MSI/MSIX ou Inno Setup e pacote Linux (5B2; o pacote portátil 5B1 está implementado).
- Provisionamento do certificado/HSM e assinatura dentro do CI.
- Matriz automatizada Linux (o deploy portátil Windows está implementado).
- Catálogo remoto assinado (Ed25519/minisign) e hospedagem.

Essas são as próximas fatias da fase 5.
