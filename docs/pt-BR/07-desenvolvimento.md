# Desenvolvimento

[English](../en/07-development.md) · [Índice](../../README.pt-BR.md#documentação)

## Mapa do repositório

| Caminho | Conteúdo |
| :--- | :--- |
| `src/` | Frontend em TypeScript (core, view, ui, i18n). |
| `CppCore/` | Backend C++20, o runtime e a CLI. `dev.py` é o ponto de entrada do build. |
| `tests/` | Suítes Vitest do frontend. |
| `scripts/` | Tutorial ponta a ponta, verificação de licenças, auxiliar de submódulos. |
| `third_party/` | Todo código externo (submódulos ou arquivos incluídos com sua licença). |
| `docs/` | Esta documentação, em `en/` e `pt-BR/`. |
| `TUTORIAL_AGENTE_SVG_ARESTRIX.md` | Um exemplo completo passo a passo. |

## Exemplos e segredos

Os três projetos de [`examples/`](../../examples/README.pt-BR.md) são gerados por `python scripts/build_examples.py` (nunca edite o
`pipelines/main.json` deles à mão) e testados com `python scripts/e2e_examples.py` (um LLM falso roteirizado; sem chaves, sem custo).

**Nenhum segredo pode chegar ao Git.** O `scripts/check-secrets.mjs` (`npm run check:secrets`) sinaliza formatos de chave de API,
valores literais em campos `api_key`/`token`/`password`, caminhos de usuário, e-mails pessoais, arquivos de credencial e artefatos
gerados em `examples/`. Rode `npm run hooks:install` uma vez por clone para torná-lo um **gancho de pré-commit**; o `npm test`
também falha com qualquer achado em `examples/`. Os segredos vivem só em variáveis de ambiente do usuário; os arquivos de projeto
guardam no máximo o *nome* de uma variável ou uma referência `${NOME}`.

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

Todo código externo fica em `third_party/` e precisa ser compatível com a licença **MIT** do Taskflow:
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

- Avisos de dependências: veja [`SEGURANCA_DEPENDENCIAS.md`](../../SEGURANCA_DEPENDENCIAS.md)
  (atualizar `vite`/`vitest`, depois `monaco-editor` ou fixar `dompurify`).
- Avisos do Vite: `webui.js` carregado sem `type="module"`, e `fs`/`path` importados em
  `GenericPiProjectScanner.ts`.

## Relacionados

- [Arquitetura](01-arquitetura.md)
- [Marca](08-marca.md)
