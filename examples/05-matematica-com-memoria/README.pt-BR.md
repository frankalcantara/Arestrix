# Exemplo 5 — Matemática com memória e pesquisa web

Este exemplo mostra o fluxo que motivou o vértice MCP de pesquisa: **buscar fontes na web,
redigir um texto de matemática e registrar o que foi aprendido na memória do projeto**.

## Fluxo

```text
Semear_Memoria
      |
      v
Pesquisar_Fontes (MCP Keenable: search_web_pages)
      |
      v
Redigir_Texto_Matematico (agente + memória do projeto)
      |
      v
Guardar_Licao (proposta de lição)
      |
      v
Entrega_Texto (exports/texto-matematica.md)
```

1. `Semear_Memoria` propõe uma lição inicial: toda afirmação matemática deve citar a fonte ou
   justificar o passo. Aprove-a em **Memória do Projeto** antes da próxima execução.
2. `Pesquisar_Fontes` é um vértice **MCP** (sem modelo de IA). Ele lê `samples/tema.md` e chama
   `search_web_pages` no servidor `keenable`, gravando o resultado em `workspace/fontes.json`.
3. `Redigir_Texto_Matematico` recebe o brief de memória e lê `workspace/fontes.json`. A skill
   exige explicação, demonstração, exemplo e referências reais.
4. `Guardar_Licao` propõe uma lição de revisão; ela fica pendente até a aprovação.
5. `Entrega_Texto` copia o Markdown para `exports/texto-matematica.md`.

## Keenable

O servidor MCP está em `.workspace/mcps/keenable.json`:

```json
{
  "id": "keenable",
  "name": "Keenable Web Search",
  "transport": "http",
  "url": "https://api.keenable.ai/mcp",
  "headers": { "X-API-Key": "${KEENABLE_API}" },
  "isEnabled": true
}
```

A chave **nunca** fica no repositório. Defina a variável de ambiente antes de executar:

```powershell
$env:KEENABLE_API = "sua-chave"
```

Na interface, **Padrões Globais → Credenciais** também permite gravar `KEENABLE_API` no ambiente
do usuário; o arquivo do projeto continua guardando apenas a referência.

Sem a variável, o vértice MCP falha de forma explícita (`env_var_unset`), em vez de seguir sem
pesquisa. As ferramentas usadas são `search_web_pages` e, opcionalmente, `fetch_page_content`.

## Executar

```powershell
arestrix-cli run examples/05-matematica-com-memoria/pipelines/main.json `
  --root examples/05-matematica-com-memoria
```

O agente usa o modelo configurado nos Padrões Globais (DeepSeek por padrão) e precisa da chave
correspondente. Para inspecionar a memória e o consumo:

```powershell
arestrix-cli memory report --project examples/05-matematica-com-memoria
```

## O que observar

- `[MCP Pesquisar_Fontes] keenable.search_web_pages ok` no console.
- `workspace/fontes.json` com os resultados da busca.
- `[MEMORIA] contexto injetado em Redigir_Texto_Matematico`.
- `workspace/texto-matematica.md` e `exports/texto-matematica.md`.
- Lições `revisao_anterior` e `pos_redacao` no banco `.arestrix/memory/memory.db`.

## Limitações

- A qualidade da redação depende do modelo e das fontes retornadas.
- O exemplo não faz verificação automática de cada URL; a skill apenas proíbe inventar fontes.
- O escopo de memória é o projeto. Workspace e instituição entram nas próximas fases.
