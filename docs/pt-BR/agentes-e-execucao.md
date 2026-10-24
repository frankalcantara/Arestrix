# Agentes e execução

> **Aplica-se a:** Arestrix Beta 1.5 (`1.5.0-beta.1`) · Última revisão: 2026-10-21

[English](../en/agents-and-execution.md) · [Índice](../README.md)

## Compilar e Executar (F5)

1. O grafo é validado e enviado ao backend C++.
2. O backend gera um `main.cpp` Taskflow, compila com MSVC (Windows) ou g++/clang++ (Linux) contra o
   `arestrix_core` e **executa dentro da pasta do projeto**. Um grafo inalterado reaproveita o binário compilado.
3. Logs e telemetria dos agentes chegam em fluxo ao console e aos selos nos vértices.

`F5` ou `Ctrl+Enter` executa, `Shift+F5` interrompe. O mesmo motor está disponível sem a interface:

```bash
arestrix-cli gen graph.json                    # imprime o C++ gerado
arestrix-cli run graph.json --root <projeto>   # gera, compila e executa
```

Veja [`arestrix-cli`](arestrix-cli.md) para requisitos, códigos de saída, CI e solução de problemas.

## O que um vértice de agente faz

Um vértice de agente executa um **laço real de uso de ferramentas** contra o provedor configurado:

- A **skill** (`SKILL.md`) é o prompt de sistema; o template de prompt do vértice é a tarefa.
- O modelo pode chamar ferramentas: `read`, `write`, `edit`, `bash`, `validate_svg`, mais as ferramentas dos servidores MCP que o agente
  declarar (inclusive as [ferramentas nativas do Arestrix](workspaces-memoria-skills-mcp.md#ferramentas-nativas-sem-instalação-sem-processo)) (veja [Servidores MCP](workspaces-memoria-skills-mcp.md#servidores-mcp)). As nativas ficam
  confinadas à pasta do projeto.
- O laço continua até o modelo parar de chamar ferramentas. Skills podem acrescentar ferramentas e servidores MCP.

### Chamadas entre agentes

Um agente pode chamar outro agente **declarado no grafo** por uma **aresta de chamada** tracejada. O
destino é somente chamável (não tem arestas de fluxo) e roda de forma síncrona dentro da task do
chamador; não é uma task agendada do Taskflow.

- O runtime expõe `call_agent` com um **enum de alvos declarados**; o modelo não consegue inventar
  um alvo.
- A chamada recebe um `input` explícito e devolve uma mensagem de ferramenta JSON com `callId`,
  `target`, `status`, `output` e `depth`.
- O skill orienta quando chamar; o grafo autoriza; o runtime aplica `maxCalls` por aresta,
  `ARESTRIX_MAX_CALL_DEPTH` (padrão 3) e `ARESTRIX_MAX_TOTAL_CALLS` (padrão 50).
- Se o modelo pedir várias chamadas `call_agent` no mesmo turno, o runtime as executa em paralelo e
  devolve os resultados na ordem pedida. Um turno que mistura chamadas com outras ferramentas continua
  sequencial. Use `ARESTRIX_PARALLEL_CALLS=0` para forçar a delegação sequencial.
- Os eventos `call_start` / `call_end` levam o caminho da chamada; o texto final do alvo também é
  gravado em `.arestrix/out/<target>.txt`.
- Chamadas publicam uma **notificação** não bloqueante em sucesso/falha (salvo se desligada por
  aresta ou por `ARESTRIX_NOTIFICATIONS=0`). O runtime grava em
  `.arestrix/runs/<runId>/notifications.jsonl` e o Studio mostra um toast. A notificação nunca inclui
  input ou output.
- Com **Permitir chamada assíncrona** na aresta, o modelo pode enviar `async: true` para
  `call_agent`. A chamada devolve `status: pending` e um `callId`; o modelo pode continuar e
  consultar com `call_result`. O resultado final é o mesmo de uma chamada síncrona.
  `ARESTRIX_ASYNC_CALLS=0` desliga o async globalmente. Chamadas pendentes são drenadas antes de o
  processo encerrar.
- Cada execução grava `.arestrix/runs/<runId>/trace.json` (tasks, chamadas, interações, status e
  tempos, sem payloads). O Studio aplica o trace mais recente no canvas e imprime uma árvore curta
  de chamadas no console.
- O alvo nunca recebe a conversa inteira do chamador: só o input explícito. Isso reduz custo e a
  superfície de prompt injection.
- Um alvo **subflow** declarado com `childGraph` é compilado como uma função nativa no mesmo
  executável. O grafo filho roda em um executor Taskflow aninhado; o input da chamada é passado como
  `inputContext` aos agentes/interações e o artefato de saída do filho é devolvido ao chamador.
  Arestas de chamada e subflows aninhados são rejeitados no v1.

Veja [`examples/06-chamada-agente`](../examples/06-chamada-agente) e
[Grafo e vértices](grafo-e-vertices.md#arestas-de-chamada-delegação).

### Vértices de interação humana

Um vértice **interaction** pergunta algo a uma pessoa por um schema declarativo de formulário. O
runtime monta o formulário somente a partir dos tipos de campo declarados (texto, textarea, número,
booleano, escolha, múltipla escolha, data) e nunca executa HTML ou JavaScript gerado por modelo.

- No Studio, **Compilar & Executar** abre o formulário como modal e envia a resposta pelo RPC
  `answer_interaction`. O processo do pipeline fica bloqueado no canal stdin/stdout do host enquanto
  o modal está aberto. O painel do Runner mostra **Aguardando resposta** durante a espera.
- Em um terminal interativo, `arestrix-cli run` pergunta campo a campo; automação CLI/CI usa
  `--answers <arquivo.json>` chaveado pelo id do vértice de interação. Uma lista de objetos de resposta
  é consumida em ordem para interações repetidas. Sem TTY e sem `--answers`, o CLI falha fechado.
- O runtime valida de novo campos obrigatórios, tipos e escolhas declaradas antes de devolver as
  respostas. O resultado é gravado em `.arestrix/out/<nodeId>.txt`/`.json` e pode alimentar o fluxo
  ou um agente chamador.
- Com `schemaSource: "vertex"`, um modelo gera o formulário a partir do contexto Markdown e dos
  artefatos; o runtime valida o schema JSON e aplica `generator.fallback` (`fail`/`free_text`) quando
  a geração falha. HTML/JavaScript gerado por modelo nunca é renderizado.
- Cada tentativa é auditada em `.arestrix/runs/<runId>/interactions/<requestId>.json` (formulário,
  respostas, canal, horários, `answeredBy`, metadados do gerador). Valores sensíveis ficam fora da
  telemetria/logs e campos de texto sensíveis são mascarados no Studio.
- Timeout, cancelamento e as políticas `fail`/`default`/`abort` são aplicados; sem canal, o vértice
  falha fechado.

Veja [`examples/07-interacao`](../examples/07-interacao) (formulário estático) e
[`examples/08-formulario-gerado`](../examples/08-formulario-gerado) (formulário gerado), além de
[Grafo e vértices](grafo-e-vertices.md#vértices-de-interação).

**Idioma do agente.** Em tempo de execução o agente é configurado no idioma da sua própria constituição: a skill e o template
de prompt (os textos que o definem). O runtime detecta o idioma e emite tudo o que o agente enxerga — o bloco de contexto do
sistema, o prompt padrão, as descrições e esquemas das ferramentas nativas e internas, e as mensagens de erro devolvidas a ele
— nesse idioma (português ou inglês; português é o padrão quando os textos não dão sinal claro). Descrições de ferramentas MCP
externas são dados de terceiros e passam como vieram. Logs de console e telemetria ficam no idioma da interface.

Os provedores estão em [Primeiros passos](primeiros-passos.md#chaves-de-api-e-provedores).

### As cinco ações rápidas

Todo vértice tem uma barra de cinco botões (também no menu de botão direito):

| Ação | Escopo | Configura |
| :--- | :--- | :--- |
| Sandbox | Workspace | Nível de isolamento, política de rede, memória, timeout, montagens. |
| Modelo | Projeto | Provedor, modelo, arquivo de skill, template de prompt. |
| Ferramentas | Projeto | Ferramentas permitidas e servidores MCP. |
| Memória | Projeto | Blackboard, lições semânticas, persistência. |
| Skills | Workspace / Projeto | Skills ligadas ao vértice. |

As configurações podem ser herdadas dos **padrões globais** (barra de ferramentas) ou substituídas por vértice.

## Sandboxes

| Nível | Isolamento | Observações |
| :--- | :--- | :--- |
| `host_direct` | Job Object / grupo de processos, timeout, limite de memória | **Não é uma fronteira de segurança.** Remove `*_API_KEY`, `*_TOKEN`, `*_SECRET` do ambiente do processo filho. |
| `container` | Docker | `--cap-drop=ALL`, raiz somente leitura, sem rede por padrão, montagens só do workspace. |
| `microvm` | WSL2 | Compartilha o sistema de arquivos do host; recusa políticas `offline` que não consegue impor. |

Política de rede por vértice: `offline`, `egress_llm_only` ou `full`.

## Propriedades de segurança

- Um nível de sandbox indisponível **falha de forma fechada**. Não recorre silenciosamente ao host, a menos que
  `allowFallbackToHost` esteja ativado.
- As ferramentas de arquivo e os endpoints de arquivo expostos à interface rejeitam caminhos fora do projeto:
  `..`, caminhos absolutos, links simbólicos e streams NTFS.
- Nenhum shell é usado para iniciar processos (`std::system` não é usado).
- Um laço de feedback não gasta dinheiro sem limite: cada vértice de agente é limitado a 10 execuções
  (`ARESTRIX_MAX_AGENT_RUNS`).

## Telemetria

O runtime transmite eventos que o canvas mostra como selos em cada vértice:

| Evento | Significado |
| :--- | :--- |
| `thinking` | Raciocínio em andamento, com contagem acumulada de tokens/bytes. |
| `tool_start` / `tool_end` | Uma chamada de ferramenta começou ou terminou, com argumentos e status. |
| `429 retry` | Limite de taxa detectado (`RESOURCE_EXHAUSTED`, "Rate limit reached"); uma contagem regressiva mostra a nova tentativa agendada. |
| `completed` | O vértice terminou. |

## Lotes e retomada

Para pipelines que processam muitos itens (artigos, lotes de dados), o **motor de retomada** mantém um
checkpoint por item e por etapa em `pipeline-state.json`, gravado de forma atômica.

- **Validação por artefato:** uma etapa é marcada como concluída sem chamar a API de novo se sua saída
  existe, tem um tamanho mínimo e casa com um padrão esperado (por exemplo, front matter YAML). Isso
  é registrado como `recoveredFromArtifact: true`.
- **Gargalos serializados:** etapas pesadas podem rodar um item por vez para respeitar os limites do provedor,
  enquanto as etapas seguintes rodam concorrentemente.
- A aba **Executar** tem o orquestrador de lotes: itens, concorrência, etapas serializadas e uma matriz de checkpoints ao vivo.

## Entrega de Artefatos e Vértice de Saída (Output)

Todo pipeline voltado a produzir entregáveis (gráficos vetoriais, relatórios, planilhas, modelos ou datasets) pode declarar um ou mais vértices do tipo **`output` (Saída / Artefato)**.

- **Configuração de Origem e Destino:** O vértice define `sourcePath` (ex.: `workspace/scene.svg` ou `workspace/relatorio.md`) gerado pelos agentes anteriores e opcionalmente um `destinationPath` (ex.: `exports/scene.svg`). No término da execução, o arquivo é copiado de forma atômica para a pasta final de entrega.
- **Suporte Multi-Formato Integrado:**
  - **SVG:** Visualizador vetorial interativo com controles de zoom (+, -, 100%, ajuste à tela), inspeção de código-fonte e garantia de nitidez (zero sombras/filtros em fontes).
  - **Planilhas (CSV, TSV, XLSX):** Grid de dados tabular interativo com busca instantânea por células e visualização dos dados brutos.
  - **Texto / Markdown:** Leitor formatado de documentos técnicos e relatórios com realce de sintaxe e cópia em um clique.
  - **PDF e JSON:** Visualização estruturada com indentação limpa e botão para abertura direta no leitor nativo do sistema operacional.
- **Aba Artefatos no Painel Inferior:** Ao lado do *Terminal* e da *Telemetria*, a aba *Artefatos* exibe a contagem de entregáveis em tempo real e permite alternar entre visualização gráfica e código, copiar o conteúdo ou abrir a pasta `exports/` no Windows Explorer com um clique.
- **Operação via Assistente Local (Chat):** O assistente de chat nativo do Studio é integrado ao ciclo de vida de artefatos pelas ferramentas `list_artifacts` e `get_artifact`, sendo capaz de inspecionar a saída produzida e reportar conclusões ao usuário.

## Runner Standalone (Fase 1: Executável Independente)

O **Runner Standalone** permite transformar qualquer grafo de agentes em uma aplicação autônoma para distribuição ou execução simplificada, sem exigir que o operador utilize o editor visual completo.

### Formas de Acionamento
1. **Botão no Ribbon:** Na aba **Executar**, o grupo **Distribuição** disponibiliza o botão **Runner Standalone** (ícone 🚀).
2. **Backstage (Arquivo):** No cartão **Runner Standalone** na tela de informações do projeto ativo.
3. **Propriedade no Manifesto:** Configuração direta no `arestrix.project.json` através do bloco `"runner": { "enabled": true, ... }`.

### As Três Telas Operacionais
O Runner provê uma interface moderna e limpa com 3 visões essenciais para acompanhamento e controle operacional:

1. **Tela de Progresso:**
   - **Indicador de Status em Tempo Real:** Selo de status (`Aguardando`, `Compilando`, `Executando`, `Sucesso`, `Erro`, `Interrompido`).
   - **Barra de Progresso Linear:** Indicador percentual suave e visual do avanço do pipeline.
   - **Cronômetro:** Duração decorrida da execução atual em tempo real.
   - **Linha do Tempo de Etapas:** Rastreamento individual de cada vértice do pipeline Taskflow com selos de status, timestamp de início e duração de cada etapa.

2. **Tela de Diagnóstico:**
   - **KPIs de Telemetria:** Cartões de métricas ao vivo para consumo de tokens (Prompt, Conclusão e Total), contexto de memória injetado, chamadas de ferramentas, retries de limite de taxa 429 e latência média. Os tokens chegam por um evento `usage` emitido pelo runtime C++; não há parsing de stdout nem processo Python/JS auxiliar.
   - **Streaming de Raciocínio (Thinking):** Visualização em tempo real das deliberações cognitivas dos agentes durante o laço de execução.
   - **Inspeção de Chamadas de Ferramenta (Tool Calls):** Registro de ferramentas invocadas, argumentos transmitidos, duração e status de sucesso/falha.
   - **Exportação de Diagnóstico:** Botão para copiar o relatório completo de diagnóstico em formato JSON estruturado.

3. **Tela de Operação:**
   - **Controles de Execução:** Iniciar Execução (`F5`), Parar (`Shift+F5`) e Reiniciar.
   - **Parâmetros Operacionais Dinâmicos:** Formulário de entrada gerado automaticamente a partir dos parâmetros declarados no manifesto do projeto (textos, números, booleanos), permitindo personalizar a execução sem alterar o grafo.
   - **Acesso a Entregáveis:** Botão direto para abrir a pasta de artefatos gerados no Windows Explorer.

### Empacotamento para Distribuição
A aba **Exportar** do Runner Standalone automatiza a compilação e empacotamento do pacote executável:
- Geração da pasta de distribuição configurável (padrão `dist/runner`).
- Cópia do executável C++ nativo e das skills locais necessárias.
- Criação do manifesto `runner_manifest.json` com parâmetros e metadados.
- Criação de scripts inicializadores de fácil execução (`run.bat`).

### Política Zero-Segredos no Executável e nos Arquivos do Runner
Por diretriz rígida de segurança e conformidade, **nenhum segredo, senha ou chave de API é gravado no executável (`.exe`) ou nos arquivos necessários para sua execução**:
- **Resolução Exclusiva via Ambiente do SO:** O executável C++ resolve chaves de API estritamente a partir das variáveis de ambiente do sistema operacional (ex.: `DEEPSEEK_API_KEY`, `OPENAI_API_KEY`) no computador onde for executado. Se a variável não estiver definida na máquina de destino, o Runner falha de forma fechada, solicitando a configuração no SO.
- **Auditoria Preventiva (Fail-Closed):** Tanto o frontend quanto o backend C++ realizam uma varredura rigorosa antes da exportação. Qualquer ocorrência de chaves de API reais (`sk-...`, `AIza...`, tokens GitHub, JWTs, chaves privadas ou senhas) bloqueia o empacotamento imediatamente.
- **Bloqueio de Arquivos Proibidos:** Arquivos de credenciais locais (`.env`, `.env.*`, `credentials.json`, `*.key`, `*.pem`, `id_rsa`) são filtrados e nunca são transferidos para a pasta de distribuição.
- **Higienização de Subprocessos:** O driver de processos do Arestrix remove automaticamente todas as variáveis com sufixos sensíveis (`_API_KEY`, `_TOKEN`, `_SECRET`, `_PASSWORD`) antes de lançar qualquer ferramenta filha no host.

## Governança & Empacotamento Corporativo (Fase 2: TI e Segurança Corporativa)

A Fase 2 estabelece os pilares de segurança corporativa, integridade criptográfica e homologação para equipes de TI e Segurança da Informação (SecOps).

### 1. Validação Criptográfica de Pacotes (Fail-Closed)
- **Hash Canônico SHA-256 e HMAC-SHA256 / Ed25519:** Cada pacote gerado recebe um hash criptográfico canônico do manifesto do pipeline.
- **Assinatura Desanexada e no Manifesto:** O arquivo `runner_manifest.json` recebe a seção `"signature"` e opcionalmente emite o arquivo `runner_manifest.json.sig`.
- **Execução Estrita (Fail-Closed):** Quando `"requiredForExecution": true`, o Runner verifica a integridade e autenticidade da assinatura digital antes de inicializar o runtime. Qualquer divergência de hash, corrupção de dados ou ausência de assinatura corporativa autorizada bloqueia a execução imediatamente com log explícito de auditoria.

### 2. Integração com Cofres Corporativos (Zero Secrets em Disco e Memória)
Os agentes e pipelines corporativos não precisam e **não devem** manter chaves de API ou segredos gravados em arquivos locais:
- **Windows Credential Vault (DPAPI):** Integração nativa via Win32 Credential Manager (`CredReadW`, `CredWriteW`, `CredDeleteW`) protegida pelo subsistema de segurança do Windows sob o namespace corporativo `Arestrix/`.
- **Azure Key Vault (REST API / IMDS):** Resolução direta de segredos corporativos no Azure Key Vault via REST API nativa (`api-version=7.4`), autenticada transparentemente por Managed Identity (IMDS no endereço `169.254.169.254`) ou credenciais de Service Principal do Entra ID.
- **Resolução em Cascata Segura:** O resolvedor corporativo busca primeiro no Azure Key Vault (se configurado), recai sobre o Windows Credential Vault e, por fim, consulta variáveis de ambiente autorizadas.
- **Limpeza Imediata de Memória RAM (`SecureZeroMemory`):** Buffers de texto e estruturas que manipulam tokens e chaves são imediatamente apagados da memória com chamadas nativas de `SecureZeroMemory` (`secureWipeMemory`), impedindo persistência de credenciais em dumps de memória, arquivos de paginação ou hibernação do sistema operacional.

### 3. Empacotamento Homologável para Microsoft Intune e SCCM
Para viabilizar a implantação automatizada em larga escala pelas equipes de TI corporativa, a exportação gera os artefatos de empacotamento corporativo:
- **Instalador MSI Corporativo (WiX Toolset):**
  - Geração de `installer.wxs` parametrizado com `UpgradeCode` determinístico RFC 4122 para suportar atualizações silenciosas automáticas (*major/minor upgrades*).
  - Scripts automatizados `build_msi.bat` e `build_msi.ps1`.
  - Suporte completo aos parâmetros de implantação silenciosa corporativa:
    ```cmd
    msiexec.exe /i "Arestrix_Runner.msi" /qn ALLUSERS=1 /l*v "%TEMP%\install.log"
    ```
- **Pacote Moderno MSIX (AppxManifest):**
  - Geração de `AppxManifest.xml` com capability `runFullTrust` para ambientes com restrição de execução (AppLocker / Windows Defender Application Control).
  - Script `package_msix.ps1` para geração com `MakeAppx.exe` e assinatura digital via `SignTool.exe`.
- **Regra de Detecção Personalizada para Intune (`intune_detection_rule.ps1`):**
  - Script em PowerShell compatível com o portal do Microsoft Intune (Endpoint Manager) que inspeciona o caminho físico da instalação em `%ProgramFiles%\Arestrix\Runners\` e o registro do Windows (`HKLM:\SOFTWARE\Microsoft\Windows\CurrentVersion\Uninstall`), retornando código `0` quando o pacote estiver instalado e na versão correta.
- **Manifesto de Integridade (`SHA256SUMS`):**
  - Lista canônica de hashes SHA-256 de todos os arquivos do pacote de distribuição para validação e auditoria por firewalls e ferramentas de EDR corporativo.

## Relacionados

- [Workspaces, memória, skills e MCP](workspaces-memoria-skills-mcp.md)
- [Arquitetura](arquitetura.md)
