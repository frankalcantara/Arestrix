# Primeiros passos

> **Aplica-se a:** Arestrix Beta 2.2 (`2.2.0-beta.1`) · Última revisão: 2026-10-21

[English](../en/getting-started.md) · [Índice](../README.md)

O Arestrix Studio roda em **Windows e Linux**. Os programas gerados a partir de um grafo, por enquanto,
têm o Windows como alvo.

## Requisitos

1. **Git com submódulos.** Taskflow, simdjson, WebUI, i18next, Monaco, SkillSpector (NVIDIA) e security-audit-skill (Cloudflare) ficam em `third_party/`.
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
| [`arestrix-cli`](arestrix-cli.md) / `arestrix-cli.exe` | Executor sem interface: `arestrix-cli run graph.json --root <projeto>` e `arestrix-cli gen graph.json`. No Windows, também é copiado para a raiz do repositório. |
| `runtime/lib/arestrix_core.lib` (`libarestrix_core.a`) | O runtime ao qual todo programa gerado se liga. |

No Windows, `arestrix.exe` é um aplicativo de console, mas **oculta a janela** quando iniciado fora
de um terminal (duplo clique ou atalho). Quando iniciado por um terminal (inclusive `dev.py`), ele
mantém o console anexado e espelha o log do backend nele. O log também é sempre gravado em
`%LOCALAPPDATA%\Arestrix\logs\studio.log` (com rotação em 5 MB). No Linux, o Studio continua
escrevendo no terminal.

## Outras formas de trabalhar

| Modo | Comando | Para quê |
| :--- | :--- | :--- |
| Servidor de desenvolvimento no navegador | `npm run dev` (http://localhost:5173) | Editar canvas, estilos e modais com recarga instantânea. Execução, compilação e MCP exigem o aplicativo nativo. |
| Execução headless | `arestrix-cli run graph.json --root <projeto>` | CI, servidores e terminal; veja [`arestrix-cli`](arestrix-cli.md). |
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
| `mistral` | `MISTRAL_API_KEY` | `https://api.mistral.ai/v1` |
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

Você não precisa definir isso à mão: **Arquivo → Propriedades → Studio → Modelo do Studio**
grava as variáveis, mostra qual modelo está em uso e pede também o endereço do servidor de `local` e `jev`.
Escolher *Automático* grava o valor `auto`, que significa "não definido".

Se `ARESTRIX_DEFAULT_PROVIDER` não existir (ou for `auto`), o Studio usa o primeiro provedor que tiver chave de API nesta máquina
(DeepSeek, OpenAI, Anthropic, Google, Groq, nessa ordem). As chaves e os endpoints são as mesmas variáveis
listadas acima; um modelo local (Ollama, llama.cpp, vLLM) precisa de `ARESTRIX_DEFAULT_PROVIDER=local`,
`ARESTRIX_DEFAULT_MODEL=<nome>` e `LOCAL_LLM_BASE_URL`.

### Modelos locais (o Arestrix não instala nem inicia o servidor)

O Arestrix **não** empacota, baixa ou inicia runtimes como llama.cpp, Ollama ou vLLM. Para usar um modelo
local, você já precisa ter um servidor de inferência rodando com uma API compatível com OpenAI. Exemplos de
endereço base:

| Servidor | Endereço base típico |
| :--- | :--- |
| Ollama | `http://localhost:11434/v1` |
| LM Studio (servidor local) | `http://localhost:1234/v1` |
| llama.cpp (`llama-server`) | `http://localhost:8080/v1` |
| vLLM | `http://localhost:8000/v1` |

Passos:

1. Inicie o servidor e carregue o modelo (por exemplo, `ollama serve` e `ollama pull qwen2.5:7b`, ou
   `llama-server -m modelo.gguf --port 8080`).
2. No Studio, abra **Arquivo → Propriedades → Projeto → Modelo e ferramentas**, escolha **Local (Ollama / vLLM)** e informe o
   endereço base em `LOCAL_LLM_BASE_URL`.
3. Clique em **Buscar modelos**: o Arestrix consulta `GET /v1/models` pelo backend nativo (sem depender de
   CORS) e lista os modelos carregados. Se o servidor não listar modelos, digite o nome manualmente.
4. Para um vértice específico, o mesmo campo aparece em **Modelo de IA → Provedor & Modelo** quando o
   vértice não herda o modelo global.

Nenhuma chave de API é obrigatória; `LOCAL_LLM_API_KEY` só é enviada se você a definir (alguns servidores
exigem um token qualquer). A chamada de ferramentas depende do modelo e do servidor: modelos sem suporte a
tools continuam servindo para conversa, mas o Assistente do Studio informa `tools_unsupported` ao tentar
operar o grafo. Um runner exportado lê `LOCAL_LLM_BASE_URL` e o nome do modelo na máquina de destino; o
endereço não é gravado dentro do projeto.

#### Catálogo e download de modelos

O Arestrix traz um catálogo pré-aprovado em `CppCore/runtime/models.catalog.json`, com metadados de modelos
GGUF (Qwen2.5 e Mistral), licença, tamanho, SHA-256, requisitos de RAM/VRAM e suporte a ferramentas. Os pesos
**não** são distribuídos com o produto: o download vai direto da origem oficial pinada.

Em **Arquivo → Propriedades → Projeto → Modelo e ferramentas**, clique em **Gerenciar modelos locais** para:

- baixar um modelo com progresso, retomada e verificação SHA-256;
- verificar ou excluir um modelo já baixado;
- selecionar o modelo para o projeto.

Os arquivos ficam em `%LOCALAPPDATA%\Arestrix\models` no Windows e em
`~/.local/share/arestrix/models` no Linux (ou em `ARESTRIX_DATA_DIR`, quando definido).

#### Runtime gerenciado

Na mesma tela, o botão **Instalar runtime** baixa o `llama.cpp` pinado para a plataforma atual, verifica o
SHA-256 e extrai em `%LOCALAPPDATA%\Arestrix\runtime` (Windows) ou `~/.local/share/arestrix/runtime` (Linux).
O backend preferido é **Vulkan**, com fallback para **CPU**; para forçar outro backend, defina
`ARESTRIX_LOCAL_BACKEND=cuda`, `vulkan` ou `cpu` antes de instalar.

**Iniciar servidor** sobe o `llama-server` em modo router nativo, escutando apenas em `127.0.0.1` numa porta
livre, com chave efêmera por execução. O Arestrix gera o preset `.ini` com os modelos baixados e configura
`LOCAL_LLM_BASE_URL`/`LOCAL_LLM_API_KEY` somente no processo atual — esses valores não são gravados nas
variáveis do usuário. O modelo é carregado na primeira requisição, não no startup.

O botão **Usar** de um modelo baixado instala o runtime se necessário, inicia o servidor e preenche provedor
e modelo no projeto. Para inspecionar problemas, use **Logs** na mesma tela. O servidor é encerrado ao fechar
o Arestrix.

A tela também detecta RAM, threads de CPU e GPUs via `llama-server --list-devices`, mostra o modelo
recomendado para a máquina e limita `--models-max` conforme a VRAM discreta (1 por padrão; 2 com pelo menos
10 GiB livres). Para sobrescrever, defina `ARESTRIX_LOCAL_MODELS_MAX=1..4`. Se o backend principal falhar ao
iniciar, o Arestrix instala o binário **CPU** sob demanda e tenta novamente automaticamente.

O cartão **Configuração rápida** baixa o modelo recomendado, instala o runtime, inicia o servidor e configura
o projeto em um clique. O botão **Usar no Assistente** grava `ARESTRIX_DEFAULT_PROVIDER=local` e
`ARESTRIX_DEFAULT_MODEL` nas variáveis do usuário, sem persistir a porta/chave efêmeras do runtime.

### AI Gateway e budgets empresariais

Para impor limites de uso em todos os runners, use o `arestrix-gateway` on-prem. Ele é um executável
separado, compatível com OpenAI/Anthropic, que guarda as chaves reais e expõe **chaves virtuais** com
budget, rate limit e auditoria.

```bash
# Configuracao e banco
cp CppCore/gateway/arestrix-gateway.example.json arestrix-gateway.json
arestrix-gateway init --db arestrix-gateway.db

# Chave virtual com budget mensal de US$ 50
arestrix-gateway key create --name "runner-ana" --project "demo" --user "ana" \
  --budget-usd 50 --period monthly --db arestrix-gateway.db

# Budget por provedor
arestrix-gateway budget set --scope provider --id openai --period monthly \
  --usd 200 --db arestrix-gateway.db

# Servidor (coloque um reverse proxy TLS na frente)
arestrix-gateway serve --config arestrix-gateway.json
```

No Studio e nos runners:

```text
ARESTRIX_AI_GATEWAY_URL=https://gateway.empresa.com
ARESTRIX_AI_GATEWAY_KEY=av_...
ARESTRIX_REQUIRE_AI_GATEWAY=1
```

Com `ARESTRIX_REQUIRE_AI_GATEWAY=1`, provedores hospedados falham fechados se o gateway não estiver
configurado. `local` e `jev` continuam diretos e isentos de budget. O gateway expõe `/v1/usage`,
`/health` e `/metrics` (Prometheus). O guia completo está em `CppCore/gateway/README.md`.

Para política imutável, distribua `ARESTRIX_AI_GATEWAY_POLICY=locked` por GPO/MDM. O backend mantém o
gateway obrigatório e a tela fica somente leitura. A imposição real depende de não distribuir as chaves
reais aos runners e de restringir a saída de rede deles ao gateway.

### Distribuição e assinatura

Rode o gate de release reproduzível (build, testes, verificações, smoke e E2E) a partir de uma árvore
limpa:

```bash
python scripts/release_check.py
```

Ele grava `release/release-manifest.json`, `SHA256SUMS`, `SBOM.json` e `release-report.json`. Veja
[Gate de release e artefatos](release.md).

Antes de publicar, valide o catálogo e o manifesto:

```bash
npm run check:local-assets
```

O script verifica esquema, IDs únicos, URLs HTTPS, SHA-256, tamanhos, licenças, modelos verificados e a
cobertura mínima Windows/Linux com fallback CPU.

No Windows, assine os binários com Authenticode usando um certificado fornecido por variável de ambiente:

```powershell
$env:ARESTRIX_SIGN_PFX = "C:\secrets\arestrix.pfx"
$env:ARESTRIX_SIGN_PFX_PASSWORD = "..."
pwsh scripts/sign-windows.ps1
```

Alternativamente, use `ARESTRIX_SIGN_THUMBPRINT` para um certificado no repositório `My`. O script também
aceita `-RuntimePath` para assinar um runtime preparado para distribuição. Nenhum segredo é gravado no
repositório.

A atualização do runtime é controlada pelo manifesto embutido: quando o Arestrix sobe de versão, a nova
versão do `llama.cpp` é baixada, verificada e as versões antigas são removidas. Catálogo remoto assinado
(Ed25519/minisign) ainda depende da definição de par de chaves e hospedagem; até lá o catálogo é embutido no
binário assinado.

O manifesto de binários pinados fica em `CppCore/runtime/llama.manifest.json` e usa o build oficial
`llama.cpp b11669`, que já inclui o modo router nativo do `llama-server` (`--models-preset`).

### Teste o tutorial sem gastar dinheiro

```bash
python scripts/e2e_tutorial.py
```

Monta o grafo do [tutorial do agente SVG](tutorials/svg-agent.md) contra um
servidor de LLM falso e roteirizado, compila o programa e executa o fork/join, a nova tentativa após 429 e o laço de autocorreção.

## Relacionados

- [Arquitetura](arquitetura.md)
- [Agentes e execução](agentes-e-execucao.md)
