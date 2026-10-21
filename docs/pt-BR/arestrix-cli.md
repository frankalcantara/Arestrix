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
arestrix-cli run <grafo.json> [--root <pasta-do-projeto>]
arestrix-cli gen <grafo.json> [--root <pasta-do-projeto>]
```

Sem argumentos, ele imprime o uso e retorna código `2`.

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
6. O código de saída é `0` em sucesso, `1` em falha de build/execução e `2` em erro de uso ou de
   leitura do grafo.

O CLI **não** chama o modelo diretamente. Quem chama o provedor é o pipeline gerado, usando as
chaves e os endpoints configurados.

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
# execução sem memória, mas registrando métricas
$env:ARESTRIX_MEMORY_METRICS = "1"
.\arestrix-cli.exe run .\examples\04-memoria\pipelines\main.json `
  --root .\examples\04-memoria

# execução com memória (configuração do grafo)
Remove-Item Env:ARESTRIX_MEMORY_METRICS
.\arestrix-cli.exe run .\examples\04-memoria\pipelines\main.json `
  --root .\examples\04-memoria

# relatório de consumo (ferramenta de desenvolvimento)
python scripts\memory_report.py .\examples\04-memoria
```

O relatório lê `.arestrix/memory/memory.db` e mostra tokens, chamadas, latência e contexto de
memória por agente e por modo. Veja [Workspaces, memória, skills e MCP](workspaces-memoria-skills-mcp.md).

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
| `ARESTRIX_MEMORY_METRICS=1` | Grava métricas mesmo com a memória desligada, para comparação. |
| `ARESTRIX_MEMORY_DISABLE=1` | Desliga brief e ferramentas de memória em runtime, sem editar o grafo; útil para A/B. |
| `ARESTRIX_PYTHON` | Usada apenas por exemplos que declaram um MCP stdio em Python. |

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
