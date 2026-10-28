# Desenvolvimento

> **Aplica-se a:** Arestrix Beta 2.3 (`2.3.0-beta.1`) · Última revisão: 2026-10-21

[English](../en/development.md) · [Índice](../README.md)

## Mapa do repositório

| Caminho | Conteúdo |
| :--- | :--- |
| `src/` | Frontend em TypeScript (core, view, ui, i18n). |
| `src/core/skills/` | Skill Guard: hashing, ledger, regras NVIDIA e fronteira Cloudflare, lock de referências. |
| `.agents/skills/` | Skills usados pelos agentes de desenvolvimento deste repositório; inclui `arestrix-skill-guard`. |
| `CppCore/` | Backend C++20, o runtime e a CLI. `dev.py` é o ponto de entrada do build. |
| `CppCore/src/cli/` | O executor headless `arestrix-cli` (`run` e `gen`). |
| `CppCore/src/local/` | Runtime local gerenciado: catálogo GGUF, downloads, processo llama.cpp e router nativo. |
| `CppCore/src/gateway/` | Núcleo do `arestrix-gateway`: chaves virtuais, budgets, ledger de uso e proxy HTTP. |
| `CppCore/gateway/` | Configuração de exemplo e guia do operador do gateway (`README.md`). |
| `tests/` | Suítes Vitest do frontend. |
| `scripts/` | Testes e2e, geração de exemplos, relatório de memória, verificação de licenças, validação de ativos locais, assinatura Authenticode e auxiliares de submódulos. |
| `third_party/` | Todo código externo (submódulos ou arquivos incluídos com sua licença), incluindo SQLite e `cpp-httplib`. |
| `docs/` | Esta documentação, em `en/` e `pt-BR/`, além de tutoriais, qualidade e segurança. |
| `docs/pt-BR/tutorials/svg-agent.md` | Um exemplo completo passo a passo. |

## Exemplos e segredos

Os cinco primeiros projetos de [`examples/`](../../examples/README.pt-BR.md) (01–05) são gerados por `python scripts/build_examples.py` (nunca edite o
`pipelines/main.json` deles à mão) e testados com `python scripts/e2e_examples.py` (um LLM falso roteirizado; sem chaves, sem custo).
O cenário do exemplo 05 só acessa a rede quando `KEENABLE_API` está definida.
A memória de projeto tem um teste próprio de duas execuções: `python scripts/e2e_memory.py`.
Para comparar o consumo de API com a memória ligada/desligada, use
`arestrix-cli memory report --project <raiz-do-projeto>` (lê `memory_usage` no banco). O
`scripts/memory_report.py` permanece opcional para desenvolvimento; o runtime do produto não depende
de Python. Veja [`arestrix-cli`](arestrix-cli.md).

**Nenhum segredo pode chegar ao Git.** O `scripts/check-secrets.mjs` (`npm run check:secrets`) sinaliza formatos de chave de API,
valores literais em campos `api_key`/`token`/`password`, caminhos de usuário, e-mails pessoais, arquivos de credencial e artefatos
gerados em `examples/`. Rode `npm run hooks:install` uma vez por clone para torná-lo um **gancho de pré-commit**; o `npm test`
também falha com qualquer achado em `examples/`. Os segredos vivem só em variáveis de ambiente do usuário; os arquivos de projeto
guardam no máximo o *nome* de uma variável ou uma referência `${NOME}`.

## Compilação, Hardening e Segurança em C++20

O script `CppCore/dev.py` automatiza o ciclo completo de build local (TypeScript + Vite + CMake + MSVC/GCC). O processo de compilação C++ incorpora as melhores práticas de segurança de software livre, mitigação de vulnerabilidades e análise de código:

### Opções do orquestrador (`dev.py`)

| Comando | Descrição |
| :--- | :--- |
| `python CppCore/dev.py` | Compila frontend, compila backend nativo C++20 com hardening e abre o Studio. |
| `python CppCore/dev.py --no-run` | Apenas compila (frontend e backend) sem abrir a interface gráfica. |
| `python CppCore/dev.py --no-frontend` | Pula a compilação do TypeScript/Vite (útil durante iterações puras de C++). |
| `python CppCore/dev.py --test` | Compila frontend + todos os alvos nativos (incluindo `arestrix-gateway` e `arestrix-memory`) e executa a suíte **C++** via `ctest`. Não roda Vitest; use `npm test` para o frontend. |
| `python CppCore/dev.py --test-all` | Build + CTest + Vitest. |
| `python CppCore/dev.py --check` | Versão, ativos locais, segredos e licenças de terceiros; sem build. |
| `python CppCore/dev.py --gateway-smoke` | Smoke test HTTP do `arestrix-gateway` com upstream falso e bloqueio de budget. |
| `python CppCore/dev.py --e2e` | Scripts ponta a ponta de tutorial/exemplos. |
| `python CppCore/dev.py --full` | Build + CTest + Vitest + checks + gateway smoke; não abre o Studio e não inclui `--e2e`. |
| `python CppCore/dev.py --asan` | Instrumenta os binários com **AddressSanitizer** para detecção em tempo de execução de estouros de buffer, *use-after-free* e vazamentos de memória. |
| `python CppCore/dev.py --clang-tidy` | Executa análise estática integrada via **Clang-Tidy** usando as regras corporativas de `.clang-tidy` (C++ Core Guidelines e CERT C++). |
| `python CppCore/dev.py --cppcheck` | Executa análise estática de bugs comportamentais via **Cppcheck**. |
| `python CppCore/dev.py --no-hardening` | Desativa as flags estritas de mitigação de memória (útil para profiling específico). |

### Camadas de Proteção Ativas no CMake

A biblioteca de interface `arestrix_security_flags` em [`CMakeLists.txt`](../../CMakeLists.txt) injeta proteções nativas no compilador e no linker:

- **No Windows (MSVC):**
  - **Canários de Pilha (`/GS`):** Detecta sobrescrita de buffers na pilha antes do retorno de funções.
  - **Control Flow Guard (`/guard:cf`):** Validação de alvos de chamadas indiretas no compilador e no linker contra ataques ROP.
  - **Security Development Lifecycle (`/sdl`):** Ativa verificações adicionais de runtime da Microsoft para funções com risco de segurança.
  - **Randomização de Endereçamento (`/DYNAMICBASE` e `/HIGHENTROPYVA`):** Habilita ASLR de 64 bits com espaço de busca expandido.
  - **Data Execution Prevention (`/NXCOMPAT`):** Bloqueia execução de código arbitrário em regiões de memória marcadas como dados (pilha/heap).
  - **Rigor de Código e Isolamento de Terceiros:** Compilação em nível `/W4` para o código do Arestrix, isolando bibliotecas em C (`sqlite3.c`, `simdjson.cpp`, `webui.c`) com `/w` para evitar falso-positivos.
- **No Linux (GCC/Clang):**
  - **Canários Reforçados:** `-fstack-protector-strong`.
  - **Validação de Buffers:** `-D_FORTIFY_SOURCE=2`.
  - **Full RELRO:** `-Wl,-z,relro,-z,now` (marca tabelas de resolução de símbolos como somente leitura).
  - **Pilha Não-Executável:** `-Wl,-z,noexecstack`.
  - **Format Security:** `-Wformat=2 -Wformat-security`.

## Testes e gate de verificação

O `dev.py` compila o frontend e todos os alvos nativos (Studio, CLI, gateway e testes). O gate local
canônico é:

```bash
python CppCore/dev.py --full   # build + CTest + Vitest + checks + gateway smoke
python CppCore/dev.py --e2e    # opcional: tutorial/exemplos ponta a ponta
```

Se preferir passos individuais:

```bash
npm test                                # frontend: Vitest
python CppCore/dev.py --no-run --test   # build do frontend + alvos nativos + CTest
npm run check:version                   # version.txt, package.json, package-lock.json, CHANGELOG
npm run check:local-assets              # catálogo GGUF + manifesto do llama.cpp
npm run check:secrets                   # segredos e dados pessoais
npm run check:skill-references          # commits, trees e hashes das referências de skills
python scripts/check_third_party_licenses.py
node scripts/gateway-smoke.mjs          # smoke do proxy/budget do gateway
python scripts/e2e_memory_institution.py # HTTP + MCP do arestrix-memory
```

A suíte do frontend cobre validação de grafo e ciclos, desfazer/refazer, subfluxos, templates, herança de
configurações, telemetria, o motor de retomada, memória, MCP, projetos e workspaces, a interface Office,
vértices SVG e a paridade do i18n. A suíte C++ inclui o budget store do gateway e o catálogo do runtime local;
quando as rotas do gateway mudarem, faça também um smoke test do proxy HTTP contra um upstream falso.

## Runtime local e AI Gateway

O runtime local fica em `CppCore/src/local/` e grava em `%LOCALAPPDATA%\Arestrix` (Windows) ou
`~/.local/share/arestrix` (Linux); `ARESTRIX_DATA_DIR` sobrescreve a raiz em testes. O catálogo e o
manifesto pinado do llama.cpp são embutidos a partir de `CppCore/runtime/*.json` e validados por
`npm run check:local-assets`.

O AI Gateway on-prem é um executável separado, `arestrix-gateway`, construído pelo mesmo projeto CMake.
Configuração, budgets, chaves e deployment estão em
[`CppCore/gateway/README.md`](../../CppCore/gateway/README.md). Para desenvolvimento, copie
`CppCore/gateway/arestrix-gateway.example.json` e rode:

```bash
arestrix-gateway init --db arestrix-gateway.db
arestrix-gateway key create --name dev --budget-usd 10 --period monthly --db arestrix-gateway.db
arestrix-gateway serve --config arestrix-gateway.json
```

O Studio e os runners usam `ARESTRIX_AI_GATEWAY_URL`, `ARESTRIX_AI_GATEWAY_KEY` e
`ARESTRIX_REQUIRE_AI_GATEWAY`. Provedores locais são isentos; provedores hospedados falham fechados quando
exigidos e o gateway não está configurado. Veja o [roadmap](roadmap.md) para as lacunas de gateway/HA e
Linux/CI.

## Adicionando um idioma

O Arestrix traz `pt-BR` e `en-US`. Um novo idioma exige cinco edições (exemplo: `es-ES`):

1. **Dicionário.** `cp src/i18n/locales/en-US.json src/i18n/locales/es-ES.json` e traduza os valores;
   mantenha todas as chaves.
2. **Tipo.** Em `src/i18n/types.ts`, acrescente o código a `SupportedLocale` e uma entrada em `SUPPORTED_LOCALES`.
3. **Catálogo.** Em `src/i18n/index.ts`, importe o JSON, inclua-o em `resources` e permita o código na
   verificação de `setLocale`.
4. **Seletor.** Acrescente uma `<option>` a `#backstage-language-select` em `src/ui/ribbon/BackstageView.ts`.
5. **Verificar.** `npm test` falha se faltar ou sobrar alguma chave entre os catálogos. Depois `npm run build`
   (ele também copia `dist/` para `CppCore/dist/`).

Todo texto visível ao usuário passa por `t('chave', 'texto padrão')`, nunca fixo no código.

## Política de código de terceiros

Todo código externo fica em `third_party/` e precisa ser compatível com a licença **MIT** do Taskflow.
Versões pinadas, papéis e o processo de atualização estão em [`third_party/README.md`](../../third_party/README.md).
apenas licenças permissivas (MIT, BSD, ISC, Apache-2.0). A única exceção documentada é a fonte Inter
(SIL OFL 1.1), que permite o uso embutido. As dependências JavaScript são declaradas em `package.json` e
conferidas contra a mesma lista.

```bash
python scripts/check_third_party_licenses.py
```

Submódulos:

```bash
npm run submodule:check    # informa novos commits no upstream
npm run submodule:update   # atualiza para o upstream mais recente
```

## Convenções que importam

- **Nada de sombras ou brilhos** em qualquer texto, em qualquer tema. O contraste vem de cor e peso.
- **Regras de ignore ancoradas com `/`** (`/build/`, `/runtime/`): um `build/` sem âncora já escondeu
  `CppCore/src/build/` do repositório.
- Edições no grafo passam por implementações de `ICommand`, para continuarem desfazíveis.
- O JSON no backend C++ passa pelo `Json` (simdjson), não por outro parser.

## Gate de release

Antes de empacotar ou assinar uma release, rode o gate reproduzível a partir de uma árvore limpa:

```bash
python scripts/release_check.py
```

Ele envolve `CppCore/dev.py --full --e2e` e gera `release/release-manifest.json`,
`release/SHA256SUMS`, `release/SBOM.json` e `release/release-report.json`. A pasta é ignorada pelo
Git. Veja [Gate de release e artefatos](release.md).

## Pendências conhecidas

- Validação Linux ponta a ponta, matriz de CI e distribuição/assinatura final: veja o
  [roadmap e limitações](roadmap.md).
- HA, streaming e admin/SSO do AI Gateway ainda não existem; o gateway atual é single-node em SQLite.
- Avisos de dependências: veja [`dependency-advisories.md`](../security/dependency-advisories.md)
  (atualizar `vite`/`vitest`, depois `monaco-editor` ou fixar `dompurify`).
- Avisos do Vite: `webui.js` carregado sem `type="module"`, e `fs`/`path` importados em
  `GenericPiProjectScanner.ts`.

## Relacionados

- [Arquitetura](arquitetura.md)
- [Marca](marca.md)
