# Exemplos do Arestrix

[English](README.md) · [Documentação](../docs/README.md)

Quatorze projetos prontos para executar, do simples ao complexo. Cada um é uma pasta de projeto Arestrix comum: abra no Studio,
escolha seu modelo, grave sua chave de API e pressione **F5**.

| # | Projeto | O que mostra | Precisa de |
| :--- | :--- | :--- | :--- |
| 1 | [`01-svg-agente`](01-svg-agente) | O tutorial: dois agentes, verificações em paralelo (fork/join) e um laço de autocorreção decidido em C++. | Um modelo. |
| 2 | [`02-triagem-de-suporte`](02-triagem-de-suporte) | Triagem de chamados: agentes, **vértices de decisão** (um modelo de IA escolhe o caminho; regras por palavras fecham um laço de qualidade) e um **MCP** (um vértice MCP e um agente usando ferramentas MCP). | Um modelo, Python. |
| 3 | [`03-atendimento-ilustrado`](03-atendimento-ilustrado) | Um "projeto de projetos": o exemplo 2 seguido, no ramo de dúvidas, pelo exemplo 1 (a resposta ganha um SVG animado). | Um modelo, Python. |
| 4 | [`04-memoria`](04-memoria) | Memória de projeto: uma lição semântica e o Blackboard são persistidos, carregados e injetados no prompt; a resposta vira artefato. | Um modelo. |
| 5 | [`05-matematica-com-memoria`](05-matematica-com-memoria) | Pesquisa web pelo vértice MCP Keenable, seguida de um texto de matemática escrito com memória do projeto e entregue em Markdown. | Um modelo, `KEENABLE_API`. |
| 6 | [`06-chamada-agente`](06-chamada-agente) | Um agente coordenador declara uma **chamada** a um agente especialista (aresta tracejada) e recebe a resposta de forma síncrona; o resultado é entregue como artefato. | Um modelo. |
| 7 | [`07-interacao`](07-interacao) | Um vértice de **interação humana** com formulário seguro; `--answers` fornece uma resposta roteirizada para CI/headless, e a resposta é entregue como artefato. | Nada (respostas roteirizadas). |
| 8 | [`08-formulario-gerado`](08-formulario-gerado) | Um modelo gera o schema do formulário em runtime a partir de contexto Markdown e de um artefato; o runtime valida e audita o schema gerado. | Um modelo (gerador). |
| 9 | [`09-chamadas-paralelas`](09-chamadas-paralelas) | Um coordenador pede dois especialistas declarados no mesmo turno; o loop do agente executa as chamadas `call_agent` em paralelo e consolida as respostas. | Um modelo. |
| 10 | [`10-subflow-chamavel`](10-subflow-chamavel) | Um coordenador chama um **subflow** com grafo aninhado; o compilador gera uma função nativa e o runtime passa o input aos agentes internos. | Um modelo. |
| 11 | [`11-call-vertex`](11-call-vertex) | Um vértice **Call** executa um alvo declarado dentro do fluxo, grava a resposta em `.arestrix/out` e entrega ao próximo vértice. | Um modelo. |
| 12 | [`12-chamadas-assincronas`](12-chamadas-assincronas) | Um coordenador inicia `call_agent` com `async: true`, recebe um `callId` pendente e consulta com `call_result`. | Um modelo. |
| 13 | [`13-trilhas-independentes`](13-trilhas-independentes) | Duas trilhas sem vértice comum: selos, contador de trilhas, aviso de conflito de escrita e as duas formas de propor uma junção. | Nada. |
| 14 | [`14-contexto-comprimido`](14-contexto-comprimido) | Uma observação JSON grande é compactada antes de voltar ao modelo; o agente usa o token CCR com `retrieve` para recuperar o original inteiro. | Um modelo. |

Os exemplos 3 e 5 são **gerados** por `python scripts/build_examples.py`, então as peças compartilhadas não ficam fora de
sincronia. Os vértices ainda não executam um grafo aninhado, por isso a composição é achatada em um único pipeline.

Todos os quatorze terminam em **vértices de saída** (`output`): o arquivo produzido é copiado para `exports/` e registrado na
aba **Artefatos**, com visualização por formato (SVG, Markdown, planilhas, PDF e JSON).

## Executar um exemplo

```bash
python scripts/copy_example.py 02-triagem-de-suporte C:/meus-projetos/triagem     # copie para FORA do repositório
```

1. No Studio: **Arquivo → Abrir** e escolha essa pasta.
2. **Padrões Globais → Provedor & Modelo**: escolha seu provedor e modelo. Os exemplos usam DeepSeek por padrão, mas qualquer
   provedor serve, porque todos os agentes herdam o modelo do projeto. Grave sua chave de API na mesma tela: ela vai para
   **as variáveis de ambiente do seu usuário**, nunca para o projeto.
3. Pressione **F5**.

Sem o Studio: `arestrix-cli run pipelines/main.json --root <a pasta copiada>`.
Veja [`arestrix-cli`](../docs/pt-BR/arestrix-cli.md) para build, `gen`, CI e solução de problemas.

Os exemplos 2 e 3 leem um chamado de suporte. Escolha um de `samples/` com a variável de ambiente `ARESTRIX_EXAMPLE_TICKET`
(`chamado_urgente.txt`, `chamado_normal.txt` ou `chamado_duvida.txt`), ou coloque seu próprio texto em `workspace/ticket.txt`.

Use **seus próprios agentes**: cada vértice de agente aponta para um arquivo `skills/*.md` (as instruções dele). Edite-os,
acrescente vértices, troque o modelo por vértice (botão **Modelo**) ou substitua o servidor MCP de demonstração em
`.workspace/mcps/` pelo seu servidor real.

## Teste sem gastar nada

```bash
python scripts/e2e_examples.py
```

Executa cada exemplo contra um servidor de LLM falso e roteirizado, só com chaves de teste: sem rede e sem custo. Confere os
caminhos percorridos, as chamadas MCP, os laços, que nenhuma chave real é usada nem impressa, e que os exemplos não contêm segredos.

## Segredos: como ficam fora do Git

Os exemplos **nunca contêm uma chave**. As regras e o que as faz valer:

- **As chaves de API vivem só nas variáveis de ambiente do seu usuário** (`DEEPSEEK_API_KEY`, `OPENAI_API_KEY`...). Os arquivos
  do projeto só podem citar o nome de uma variável (`DEEPSEEK_API_KEY`) ou referenciá-la como `${NOME}`. A configuração de um
  servidor MCP segue a mesma regra no seu `env`.
- **Copie um exemplo antes de usá-lo** (`scripts/copy_example.py`). Abri-lo dentro do repositório faz o Studio gravar saídas
  dos agentes e caminhos da sua máquina em pastas versionadas.
- O `examples/.gitignore` mantém fora do Git os artefatos gerados (`workspace/`, `output/`, `storage/`, `.arestrix/`),
  arquivos `.env`, chaves e bancos de dados.
- O `scripts/check-secrets.mjs` procura formatos de chave (OpenAI/DeepSeek/Anthropic, Google, GitHub, AWS, chaves privadas,
  JWT...), valores literais em campos `api_key`/`token`/`password`, caminhos de usuário, e-mails pessoais e arquivos proibidos,
  e confere que os manifestos usam `rootPath: "."` e `environment` vazio. Ele nunca imprime um segredo por inteiro.
- Um **gancho de pré-commit** o executa em tudo o que você adicionar ao commit. Ative uma vez por clone: `npm run hooks:install`.
- O `npm test` falha se qualquer arquivo em `examples/` for sinalizado.

Se você chegar a digitar uma chave real em um arquivo, **revogue-a no provedor**; apagar o arquivo não basta depois de um commit.

## Confiança

Um projeto pode executar código na sua máquina: lambdas C++ personalizadas e comandos de servidores MCP. As ferramentas dos
próprios agentes ficam confinadas à pasta do projeto, mas os servidores MCP rodam como processos comuns do seu usuário.
Abra projetos apenas de fontes em que você confia.
