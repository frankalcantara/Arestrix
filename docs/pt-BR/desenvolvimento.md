# Desenvolvimento

> **Aplica-se a:** Arestrix Beta 1.0 (`1.0.0-beta.1`) · Última revisão: 2026-10-19

[English](../en/development.md) · [Índice](../README.md)

## Mapa do repositório

| Caminho | Conteúdo |
| :--- | :--- |
| `src/` | Frontend em TypeScript (core, view, ui, i18n). |
| `CppCore/` | Backend C++20, o runtime e a CLI. `dev.py` é o ponto de entrada do build. |
| `tests/` | Suítes Vitest do frontend. |
| `scripts/` | Tutorial ponta a ponta, verificação de licenças, auxiliar de submódulos. |
| `third_party/` | Todo código externo (submódulos ou arquivos incluídos com sua licença). |
| `docs/` | Esta documentação, em `en/` e `pt-BR/`, além de tutoriais, qualidade e segurança. |
| `docs/pt-BR/tutorials/svg-agent.md` | Um exemplo completo passo a passo. |

## Exemplos e segredos

Os três projetos de [`examples/`](../../examples/README.pt-BR.md) são gerados por `python scripts/build_examples.py` (nunca edite o
`pipelines/main.json` deles à mão) e testados com `python scripts/e2e_examples.py` (um LLM falso roteirizado; sem chaves, sem custo).

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
| `python CppCore/dev.py --test` | Compila e executa a suíte de testes unitários do runtime via `ctest`. |
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

## Testes

```bash
npm test                      # frontend: Vitest
python CppCore/dev.py --test  # backend: ctest
python scripts/e2e_tutorial.py  # ponta a ponta, contra um LLM falso roteirizado
```

A suíte do frontend cobre validação de grafo e ciclos, desfazer/refazer, subfluxos, templates, herança de
configurações, telemetria, o motor de retomada, memória, MCP, projetos e workspaces, a interface Office,
vértices SVG e a paridade do i18n.

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

## Pendências conhecidas

- Avisos de dependências: veja [`dependency-advisories.md`](../security/dependency-advisories.md)
  (atualizar `vite`/`vitest`, depois `monaco-editor` ou fixar `dompurify`).
- Avisos do Vite: `webui.js` carregado sem `type="module"`, e `fs`/`path` importados em
  `GenericPiProjectScanner.ts`.

## Relacionados

- [Arquitetura](arquitetura.md)
- [Marca](marca.md)
