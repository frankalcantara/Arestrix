# Primeiros passos

> **Aplica-se a:** Arestrix Beta 1.0 (`1.0.0-beta.1`) · Última revisão: 2026-10-19

[English](../en/getting-started.md) · [Índice](../README.md)

O Arestrix Studio roda em **Windows e Linux**. Os programas gerados a partir de um grafo, por enquanto,
têm o Windows como alvo.

## Requisitos

1. **Git com submódulos.** Taskflow, simdjson, WebUI, i18next, Monaco e SkillSpector ficam em `third_party/`.
2. **Node.js 20+ e npm** (Vite, TypeScript, Three.js, Vitest).
3. **Python 3.10+**, que conduz o script de build (`CppCore/dev.py`) e o teste ponta a ponta.
4. **Um compilador C++20 e CMake 3.20+.**
   - Windows: Visual Studio 2022 ou mais novo (Community ou Build Tools) com a carga *Desenvolvimento para desktop com C++*. O CMake e o Ninja que vêm com ele são detectados automaticamente.
   - Linux: `g++` 11+ (ou `clang++`), `cmake`, opcionalmente `ninja`, e `curl` (usado pelo runtime dos agentes para HTTPS).
5. Opcional: Docker (sandbox em contêiner), WSL2 (driver MicroVM), Graphviz.

## Instalar e executar

```bash
git clone --recurse-submodules https://github.com/frankalcantara/Arestrix.git
cd Arestrix
npm install
python CppCore/dev.py
```

Se você clonou sem `--recurse-submodules`, execute `git submodule update --init`.

O `dev.py` inicializa os submódulos, compila o frontend (`npm run build`) e o backend (CMake) e depois abre o Studio.

| Comando | Efeito |
| :--- | :--- |
| `python CppCore/dev.py` | Compila tudo e abre o Studio. |
| `python CppCore/dev.py --no-run` | Só compila. |
| `python CppCore/dev.py --test` | Compila e roda os testes C++ (`ctest`). |
| `python CppCore/dev.py --no-frontend` | Pula a compilação do frontend. |

### O que o build produz

| Saída | O que é |
| :--- | :--- |
| `arestrix` / `arestrix.exe` | O Studio: a janela WebUI e o motor. |
| `arestrix-cli` / `arestrix-cli.exe` | Executor sem interface: `arestrix-cli run graph.json --root <projeto>` e `arestrix-cli gen graph.json`. |
| `runtime/lib/arestrix_core.lib` (`libarestrix_core.a`) | O runtime ao qual todo programa gerado se liga. |

## Outras formas de trabalhar

| Modo | Comando | Para quê |
| :--- | :--- | :--- |
| Servidor de desenvolvimento no navegador | `npm run dev` (http://localhost:5173) | Editar canvas, estilos e modais com recarga instantânea. Execução, compilação e MCP exigem o aplicativo nativo. |
| Testes do frontend | `npm test` | Suíte Vitest (veja [Desenvolvimento](desenvolvimento.md)). |
| Só o build do frontend | `npm run build` | Gera o `dist/index.html` de arquivo único e o copia para `CppCore/dist/`. |

## Sua primeira execução

1. Abra o Studio. A primeira tela é a visão Backstage; crie ou abra um projeto.
2. Na aba **Página Inicial**, clique em **Catálogo** (a biblioteca de vértices) e escolha *Instanciar
   Arquitetura Completa de Agentes*, ou insira um único vértice.
3. Configure o modelo e a chave de API (veja abaixo).
4. Pressione **F5**. **Shift+F5** interrompe.

### Chaves de API e provedores

As chaves são lidas de variáveis de ambiente. O Studio as grava como variáveis do usuário (Registro do
Windows, ou `~/.config/arestrix/env` no Linux) e nunca as passa por um shell.

| Provedor | Variável da chave | Endpoint |
| :--- | :--- | :--- |
| `deepseek` | `DEEPSEEK_API_KEY` | `https://api.deepseek.com` |
| `openai` | `OPENAI_API_KEY` | `https://api.openai.com/v1` |
| `anthropic` | `ANTHROPIC_API_KEY` | `https://api.anthropic.com` |
| `google` | `GEMINI_API_KEY` | Endpoint do Gemini compatível com OpenAI |
| `jev` | `JEV_API_KEY` (opcional) | `JEV_BASE_URL` é **obrigatória** (qualquer servidor compatível com OpenAI) |
| `local` | `LOCAL_LLM_API_KEY` (opcional) | `LOCAL_LLM_BASE_URL`, padrão `http://localhost:11434/v1` (Ollama, llama.cpp, vLLM) |

Qualquer endpoint pode ser substituído com `<PROVEDOR>_BASE_URL`.

### Modelo padrão do Studio

Além do modelo de cada vértice, o Studio tem o seu próprio **modelo padrão** para pequenas tarefas dele, como
escrever o dicionário de sinônimos da busca de um idioma. Ele fica **só na sua máquina**, em variáveis de
ambiente do usuário, e nunca é gravado em um projeto:

| Variável | Significado |
| :--- | :--- |
| `ARESTRIX_DEFAULT_PROVIDER` | `deepseek`, `openai`, `anthropic`, `google`, `groq`, `jev` ou `local`. |
| `ARESTRIX_DEFAULT_MODEL` | Nome do modelo. Opcional nos provedores hospedados (cada um tem um padrão); **obrigatória** em `jev` e `local`. |

Você não precisa definir isso à mão: **Padrões Globais → Provedor & Modelo → Modelo padrão do Studio** (escopo Studio)
grava as variáveis, mostra qual modelo está em uso e pede também o endereço do servidor de `local` e `jev`.
Escolher *Automático* grava o valor `auto`, que significa "não definido".

Se `ARESTRIX_DEFAULT_PROVIDER` não existir (ou for `auto`), o Studio usa o primeiro provedor que tiver chave de API nesta máquina
(DeepSeek, OpenAI, Anthropic, Google, Groq, nessa ordem). As chaves e os endpoints são as mesmas variáveis
listadas acima; um modelo local (Ollama, llama.cpp, vLLM) precisa de `ARESTRIX_DEFAULT_PROVIDER=local`,
`ARESTRIX_DEFAULT_MODEL=<nome>` e `LOCAL_LLM_BASE_URL`.

### Teste o tutorial sem gastar dinheiro

```bash
python scripts/e2e_tutorial.py
```

Monta o grafo do [tutorial do agente SVG](tutorials/svg-agent.md) contra um
servidor de LLM falso e roteirizado, compila o programa e executa o fork/join, a nova tentativa após 429 e o laço de autocorreção.

## Relacionados

- [Arquitetura](arquitetura.md)
- [Agentes e execução](agentes-e-execucao.md)
