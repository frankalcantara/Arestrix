# `arestrix-cli` — execução sem interface

> **Aplica-se a:** Arestrix Beta 1.5 (`1.5.0-beta.1`) · Última revisão: 2026-10-21

[English](../en/arestrix-cli.md) · [Índice](README.md)

O `arestrix-cli` é o executor **headless** do Arestrix: ele lê um grafo, gera o código C++20
Taskflow, compila e executa o pipeline, sem abrir o Studio ou a WebUI.

Ele usa o mesmo `TaskflowGenerator`, o mesmo runtime e o mesmo banco de memória do Studio. A
diferença é que não há janela, canvas, inspector nem editor visual.

```text
grafo JSON
   ↓
TaskflowGenerator
   ↓
C++20 + Taskflow
   ↓
compilador local (MSVC, GCC ou Clang)
   ↓
binário do pipeline
   ↓
agentes, MCPs, memória, outputs e telemetria no terminal
```

## Onde ele fica

O CMake gera dois executáveis:

| Binário | O que é |
| :--- | :--- |
| `arestrix` / `arestrix.exe` | O Studio completo: WebUI, canvas, editores, memória e MCP. |
| `arestrix-cli` / `arestrix-cli.exe` | O executor sem interface, para CI, servidores e scripts. |

No Windows, o build copia o CLI para a raiz do repositório:

```text
build\arestrix-cli.exe
arestrix-cli.exe
```

O `.exe` não é versionado. Para gerá-lo:

```powershell
python CppCore/dev.py --no-run
```

ou compile o alvo CMake `arestrix-cli` diretamente.

## Uso

```text
arestrix-cli run <grafo.json>|--project <pasta> [opções]
arestrix-cli gen <grafo.json>
arestrix-cli check [--project <pasta> | <grafo.json>] [--require-env NOME]
arestrix-cli doctor [--project <pasta>]
arestrix-cli env set|reset|list|check
arestrix-cli memory report [--project <pasta>]
arestrix-cli mcp list|test|tools [id]
arestrix-cli project info|graphs|validate
arestrix-cli completion powershell|bash|zsh
arestrix-cli version
```

Sem argumentos, ele imprime o uso. `--help` e `version` retornam `0`.

Opção global: `--no-user-env` (ou `ARESTRIX_DISABLE_USER_ENV=1`) ignora o cofre persistido do
usuário; apenas o ambiente explícito do processo é usado. Útil em CI e testes herméticos.

### `env`

As variáveis de ambiente ficam no mesmo cofre do Studio:

```powershell
# valor via stdin (recomendado para segredos)
"sk-..." | .\arestrix-cli.exe env set DEEPSEEK_API_KEY

# inspecionar nomes (nunca valores)
.\arestrix-cli.exe env list

# verificar presença
.\arestrix-cli.exe env check DEEPSEEK_API_KEY KEENABLE_API

# remover
.\arestrix-cli.exe env reset DEEPSEEK_API_KEY
```

`env set` também aceita `--value`, mas ele pode aparecer no histórico/lista de processos. Prefira
stdin. O armazenamento é o mesmo do Studio: Registro do usuário no Windows e
`~/.config/arestrix/env` no Linux.

Por padrão o CLI carrega essas variáveis persistidas para o processo. Em CI ou testes herméticos,
use `--no-user-env` (ou `ARESTRIX_DISABLE_USER_ENV=1`) para que apenas o ambiente explícito do
processo valha. Variáveis explícitas sempre têm precedência.

### `check`

```powershell
.\arestrix-cli.exe check --project .\examples\04-memoria `
  --require-env DEEPSEEK_API_KEY --json
```

Valida grafo, codegen, raiz do projeto, runtime, toolchain e variáveis exigidas sem compilar nem
chamar modelo. Retorna `5` se o preflight falhar.

### `doctor`

```powershell
.\arestrix-cli.exe doctor --json
```

Diagnostica runtime, toolchain, raiz e presença de chaves de provedor. Chaves são apenas
`presente`/`ausente`; valores nunca são exibidos.

### `memory report`

```powershell
.\arestrix-cli.exe memory report --project .\examples\04-memoria
.\arestrix-cli.exe memory report --project .\examples\04-memoria --json
```

Lê o SQLite nativamente e mostra tokens, chamadas, retries, sucesso/falha, contexto e memórias
injetadas/usadas. Substitui o `memory_report.py` no fluxo de automação; o script Python continua
opcional para desenvolvimento.

### `mcp`

```powershell
.\arestrix-cli.exe mcp list --project .\examples\02-triagem-de-suporte
.\arestrix-cli.exe mcp tools clientes --project .\examples\02-triagem-de-suporte
.\arestrix-cli.exe mcp test clientes --project .\examples\02-triagem-de-suporte --json
```

Lista servidores, descobre ferramentas e testa a conexão antes do pipeline.

### `project`

```powershell
.\arestrix-cli.exe project info --project .\examples\04-memoria
.\arestrix-cli.exe project graphs --project .\examples\04-memoria
.\arestrix-cli.exe project validate --project .\examples\04-memoria --json
```

Inspeciona o manifesto, lista pipelines e valida grafo + codegen sem compilar.

### `completion`

```powershell
.\arestrix-cli.exe completion powershell | Out-String | Invoke-Expression
.\arestrix-cli.exe completion bash
.\arestrix-cli.exe completion zsh
```

Imprime o script de autocompletar para o shell.

### `run`

Gera, compila e executa o pipeline:

```powershell
.\arestrix-cli.exe run .\examples\04-memoria\pipelines\main.json `
  --root .\examples\04-memoria
```

O `--root` é a raiz do projeto. É dela que o runtime resolve:

- `skills/`;
- `.workspace/mcps/`;
- `workspace/` e `output/`;
- `exports/`;
- `.arestrix/memory/memory.db`;
- caminhos relativos usados pelos agentes e vértices.

Se `--root` for omitido, a raiz é o diretório atual. Em automação, passe sempre o caminho
explícito.

Opções de automação:

```powershell
.\arestrix-cli.exe run --project .\examples\04-memoria --json --timeout 300 --report .\run.json
```

- `--project` descobre o `arestrix.project.json` e o pipeline primário;
- `--json` emite JSONL (`phase`, `log`, `telemetry`, `result`);
- `--timeout` cancela o pipeline e retorna `124`;
- `--report` grava um resumo JSON mesmo em falha;
- `--quiet` suprime fases/logs/telemetria;
- `--no-user-env` ignora credenciais persistidas do usuário (só o ambiente do processo vale);
- `--ci` equivale a `--json --quiet --no-color`, com timeout padrão de 900s;
- `--batch <arquivo.json>` executa itens sequenciais;
- `--state <arquivo>` define o checkpoint (padrão `.arestrix/batch-state.json`);
- `--resume` pula itens já concluídos;
- `--answers <arquivo.json>` fornece respostas headless para vértices `interaction`.

### Interação humana headless

Um vértice `interaction` pergunta ao usuário com um formulário seguro. Em execuções headless,
forneça as respostas em JSON, chaveadas pelo id do vértice de interação:

```json
{
  "pergunta": { "tipo": "pj", "obs": "Cliente empresarial" }
}
```

```powershell
.\arestrix-cli.exe run --project .\examples\07-interacao --answers .\examples\07-interacao\answers.json
```

O runtime valida campos obrigatórios e escolhas, grava `.arestrix/out/<nodeId>.txt` e `.json`, e
devolve as respostas ao chamador. Uma interação repetida pode usar uma lista de objetos de resposta;
eles são consumidos em ordem. Sem `--answers` (e sem `defaultAnswers` com `onTimeout: default`), o
pipeline falha fechado. No Studio, o mesmo formulário abre como modal e envia a resposta pelo RPC
`answer_interaction`. Em um terminal interativo, `arestrix-cli run` pergunta campo a campo (`:q`
cancela); um stdin por pipe/sem TTY e sem `--answers` continua falhando fechado.

Cada tentativa é auditada em `.arestrix/runs/<runId>/interactions/<requestId>.json` com o formulário,
as respostas, o canal, os horários e `answeredBy`. Respostas sensíveis não vão para telemetria/logs, e
o Studio mascara os campos de texto sensíveis. Com `schemaSource: "vertex"` o runtime chama o modelo
gerador mesmo em CLI/CI (configure provedor/modelo/chave); schemas inválidos usam
`generator.fallback` (`fail` ou `free_text`).

Manifesto de lote:

```json
{
  "graph": "pipelines/main.json",
  "items": [
    { "id": "a", "env": { "ARESTRIX_EXAMPLE_TICKET": "chamado_a.txt" } },
    { "id": "b", "env": { "ARESTRIX_EXAMPLE_TICKET": "chamado_b.txt" } }
  ]
}
```

```powershell
.\arestrix-cli.exe run --batch .\batch.json --root .\meu-projeto --json --resume
```

O lote é sequencial; cada item recebe suas variáveis de ambiente e tem seu resultado persistido no
checkpoint. `completion powershell|bash|zsh` imprime o script de autocompletar.

### `gen`

Não compila nem executa: imprime o C++ gerado no stdout.

```powershell
.\arestrix-cli.exe gen .\examples\05-matematica-com-memoria\pipelines\main.json
```

É útil para depurar o codegen, revisar o corpo de um vértice ou comparar o resultado de duas
configurações de grafo.

## O que acontece em `run`

1. O grafo JSON é lido e validado pelo mesmo modelo de `core::Graph` do Studio.
2. O `TaskflowGenerator` gera o `main.cpp` do pipeline.
3. O `BuildService` compila o programa com o compilador configurado.
4. O binário gerado é executado.
5. Logs, telemetria dos agentes e linhas `[OUTPUT_DELIVERY]` aparecem no terminal.
6. O código de saída é `0` em sucesso, `1` em falha de build/execução, `2` em erro de uso, `3` em
   configuração, `4` em dependência ausente, `5` em preflight e `124` em timeout.

O CLI **não** chama o modelo diretamente. Quem chama o provedor é o pipeline gerado, usando as
chaves e os endpoints configurados.

| Código | Significado |
| :---: | :--- |
| `0` | Sucesso |
| `1` | Falha de build/execução |
| `2` | Uso inválido |
| `3` | Configuração/projeto inválido |
| `4` | Dependência ausente (toolchain) |
| `5` | Preflight falhou |
| `124` | Timeout |

## Requisitos

- Compilador C++20: MSVC no Windows; GCC ou Clang no Linux.
- CMake e o runtime do Arestrix.
- Taskflow (já incluído em `third_party/`).
- Chave do provedor de modelo, quando o pipeline tiver agentes.
- Nada de Python ou Node no runtime do pipeline.

O CLI é um binário nativo. Python aparece apenas em ferramentas opcionais de desenvolvimento (scripts
de e2e e `memory_report.py`) e nos MCPs de demonstração dos exemplos 2 e 3; veja
[Desenvolvimento](desenvolvimento.md).

## Exemplos

### Rodar um projeto de exemplo

```powershell
.\arestrix-cli.exe run .\examples\05-matematica-com-memoria\pipelines\main.json `
  --root .\examples\05-matematica-com-memoria
```

O exemplo 05 usa o MCP remoto da Keenable. Defina a chave antes:

```powershell
$env:KEENABLE_API = "sua-chave"
```

### Comparar memória ligada e desligada

```powershell
# memória ON, gravando métricas
$env:ARESTRIX_MEMORY_METRICS = "1"
.\arestrix-cli.exe run .\examples\04-memoria\pipelines\main.json `
  --root .\examples\04-memoria

# memória OFF no MESMO grafo
$env:ARESTRIX_MEMORY_DISABLE = "1"
.\arestrix-cli.exe run .\examples\04-memoria\pipelines\main.json `
  --root .\examples\04-memoria

# relatório nativo
.\arestrix-cli.exe memory report --project .\examples\04-memoria
```

O relatório lê `.arestrix/memory/memory.db` e mostra tokens, chamadas, retries, sucesso/falha,
contexto e memórias injetadas/usadas. Veja
[Workspaces, memória, skills e MCP](workspaces-memoria-skills-mcp.md).

### CI

```yaml
- run: cmake --build build --target arestrix-cli
- run: ./arestrix-cli run pipelines/main.json --root .
  env:
    DEEPSEEK_API_KEY: ${{ secrets.DEEPSEEK_API_KEY }}
```

Para testes sem chave real, os scripts de e2e usam LLMs falsos e MCPs de teste; veja
[Desenvolvimento](desenvolvimento.md).

## Variáveis de ambiente

| Variável | Efeito |
| :--- | :--- |
| `DEEPSEEK_API_KEY`, `OPENAI_API_KEY`, etc. | Chave do provedor usado pelos agentes. |
| `KEENABLE_API` | Chave do MCP Keenable no exemplo 05. |
| `ARESTRIX_MEMORY_METRICS=1` | Grava métricas mesmo com a memória desligada. No Studio, use **Padrões Globais → Memória → Registrar métricas**. |
| `ARESTRIX_MEMORY_DISABLE=1` | Desliga brief e ferramentas de memória em runtime. No Studio, use **Modo de Medição / A-B**. |
| `ARESTRIX_PYTHON` | Usada apenas por exemplos que declaram um MCP stdio em Python. |
| `ARESTRIX_NOTIFICATIONS=0` | Desliga o canal de notificações não bloqueantes de chamadas e seu registro JSONL. |
| `ARESTRIX_ASYNC_CALLS=0` | Rejeita pedidos `async: true`, mantendo todas as chamadas síncronas. |

Nunca coloque chaves no grafo, no `arestrix.project.json` ou em `.workspace/mcps`. Use variáveis
de ambiente e referências `${NOME}`.

## Quando usar o CLI

- CI que compila e executa um pipeline sem abrir o Studio.
- Servidor ou máquina sem ambiente gráfico.
- Reproduzir um bug de runtime com logs limpos.
- Rodar exemplos e tutoriais em automação.
- Comparar consumo de API entre configurações de memória.
- Inspecionar o C++ gerado com `gen`.

## Quando usar o Studio

- Montar e editar o grafo visualmente.
- Configurar vértices, skills, memória e MCPs.
- Ver telemetria, artefatos e o grafo de memória.
- Testar MCPs e provedores pelos painéis.

## Problemas comuns

| Sintoma | Causa provável | Solução |
| :--- | :--- | :--- |
| `arestrix-cli` não é reconhecido | O binário não está no `PATH`. | Use `.\arestrix-cli.exe` ou adicione a raiz do repositório ao `PATH`. |
| `Erro ao ler o grafo` | Foi passado o `arestrix.project.json` em vez do grafo. | Passe `pipelines/main.json`. |
| Falha de compilação | Toolchain C++20/CMake ausente ou incompatível. | Rode `python CppCore/dev.py --no-run` e confira o compilador. |
| Falha de chave de API | Variável do provedor não definida. | Defina a chave no ambiente do usuário. |
| Arquivos não aparecem em `exports/` | `--root` aponta para outra pasta. | Passe a raiz real do projeto. |
| MCP não encontrado | Servidor não está em `.workspace/mcps`. | Cadastre o MCP no projeto ou use o preset da UI. |

## Relacionados

- [Primeiros passos](primeiros-passos.md)
- [Agentes e execução](agentes-e-execucao.md)
- [Workspaces, memória, skills e MCP](workspaces-memoria-skills-mcp.md)
- [Desenvolvimento](desenvolvimento.md)
- [Exemplos](../../examples/README.pt-BR.md)
