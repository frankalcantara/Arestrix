# Roadmap e limitações conhecidas

> **Aplica-se a:** Arestrix Beta 2.3 (`2.3.0-beta.2`) · Última revisão: 2026-10-28
>
> Esta página registra o que está intencionalmente incompleto e a ordem planejada para resolver. Não é
> um compromisso de datas.

## Entregue na Beta 1.5

- Endpoints locais/OpenAI-compatíveis nas configurações de projeto e vértice.
- Catálogo GGUF pré-aprovado com download retomável e SHA-256.
- Runtime llama.cpp gerenciado, com modo router nativo do `llama-server`, bind em loopback e chave
  efêmera.
- Detecção de hardware, recomendação de modelo, `--models-max` por VRAM e fallback para CPU.
- Tool calling verificado para Qwen2.5 7B/14B.
- Configuração rápida e "Usar no Assistente".
- `arestrix-gateway` on-prem com chaves virtuais, budgets por chave/projeto/usuário/provedor/modelo,
  rate limits, ledger de uso, métricas Prometheus e roteamento fail-closed.
- Suporte ao provedor Mistral.
- Validação de ativos locais e script de assinatura Authenticode no Windows.
- Gate de release local reproduzível com manifesto SHA-256, `SHA256SUMS` e SBOM.
- Pacote portátil com Studio, CLI, gateway, UI, biblioteca do runtime e headers de compilação.

## Limitações conhecidas

### Plataforma e CI

- Runtime local e gateway foram validados ponta a ponta apenas no **Windows**.
- Os caminhos Linux existem, mas build, download/extração do runtime, router e gateway ainda não têm
  teste end-to-end no Linux.
- O CI está desativado durante a estabilização da beta; a matriz de release ainda não é automatizada.

### Distribuição

- Certificado/HSM de assinatura de código e execução da assinatura dos artefatos finais estão pendentes.
- O pacote portátil (5B1) está implementado. O empacotamento de instalador (MSI/WiX, MSIX ou Inno
  Setup; AppImage/deb/tarball no Linux) está pendente na 5B2.
- Catálogo/manifesto remoto assinado (Ed25519/minisign) não foi implementado. O catálogo embutido é
  protegido pelo binário assinado; atualizações remotas exigem par de chaves e decisão de hospedagem.
- Atualizações de runtime dependem de uma nova versão do aplicativo, porque o manifesto é embutido.

### AI Gateway

- Somente SQLite single-node. HA ou múltiplas instâncias exigem Postgres/Redis e contadores
  distribuídos.
- Streaming (`stream: true`) retorna `400 streaming_unsupported`.
- Administração apenas por CLI; não há console web, SSO ou papéis.
- TLS é responsabilidade de um reverse proxy; o gateway escuta HTTP em loopback por padrão.
- A tabela de preços é mantida pelo operador. Modelo sem preço registra custo zero (budgets por tokens
  continuam valendo).
- O budget é reservado de forma conservadora e reconciliado com o uso real; um provedor que ignore
  `max_tokens` pode estourar levemente a reserva.
- Endpoints de anexos/imagens/embeddings são restritos de propósito; o gateway proxia apenas
  chat/completions no momento.
- A imposição empresarial real depende de não distribuir chaves de provedor aos runners e restringir a
  saída de rede deles ao gateway. `ARESTRIX_AI_GATEWAY_POLICY=locked` é a camada de política do
  cliente/SO, não substitui controles de rede.

### Memória de projeto

Implementado no núcleo:

- AMG em SQLite (`.arestrix/memory/memory.db`) com arestas temporais, evidências, auditoria e
  telemetria separada (`.arestrix/telemetry/telemetry.db`).
- Modos por vértice `off`/`consult`/`learn`, lições, `memory_search` e propostas pendentes via
  `memory_propose_lesson`, além de brief compilado com `contextBudgetTokens`, ordenado por
  confiança/recência.
- Episódios de sucesso/falha, `memory_hit` (injetado/consultado) e métricas com retries e outcome.
- Propostas automáticas de falha (nunca ativadas sem aprovação) e exemplos 04 e 05.
- Painel de consumo na UI, relatório nativo `arestrix-cli memory report`, impacto no AI Gateway e
  A/B de contexto no Studio. `memory_report.py` permanece opcional para desenvolvimento.
- Escopos de workspace e instituição. O `arestrix-memory` expõe REST autenticado, MCP HTTP `/mcp`,
  MCP stdio e tokens apenas em memória, com fail-closed sem tokens.
- Ranking híbrido com embeddings (EmbeddingGemma 2) e consolidação L2/M3 idempotente.
- Consolidação em background opt-in por projeto (`off`, `idle` ou `after_episodes`), com estado
  observável no painel e shutdown determinístico.
- `/metrics` autenticado no `arestrix-memory`, com contadores REST/MCP, falhas 401, respostas por
  classe, uptime e versão.
- Lista de conflitos e estado de consolidação no painel de memória do projeto.

Ainda pendente:

- Visualização completa do grafo cognitivo e edição dos conflitos na UI (a lista de conflitos já
  existe).
- Consolidação por LLM, `memory_handoff` e aprendizagem recursiva.
- Coordenação de consolidação entre múltiplos processos/instâncias do serviço institucional.

Limitações do serviço institucional e do background:

- SQLite single-node; não há HA, sincronização multi-máquina ou coordenação entre instâncias.
- O serviço não termina TLS nem aplica rate limit; ambos dependem de reverse proxy/gateway.
- Tokens são carregados do arquivo/JSON/env e vivem apenas em memória; reiniciar exige recarregar.
- O worker de consolidação roda dentro do processo do projeto, só em memória persistente, e não
  sobrevive ao encerramento do processo.

### Modelos

- Apenas Qwen2.5 7B/14B têm tool calling verificado. As demais entradas são pré-aprovadas, mas podem ser
  somente conversa ou não verificadas para ferramentas.
- Qualidade/velocidade do 14B depende de GPU/VRAM; execução somente em CPU é lenta.
- Ainda não há atualização automática de modelos, deduplicação ou cota de disco.
- Os pesos não são redistribuídos; o download depende do publicador upstream e da licença.

### Agentes: distância em relação a harnesses de chat

Comparação feita em 2026-10-28 com o modelo conceitual de agentes descrito na documentação do
VS Code/Copilot — loop, contexto, multiagente e controle —, não com a IDE. A lista cobre apenas o
loop do agente: execução determinística, governança de skills/MCP, memória de projeto e
observabilidade são eixos em que o Arestrix já vai além desse modelo.

| Lacuna | Peso | Situação |
| :--- | :--- | :--- |
| **Compactação e orçamento de contexto no loop.** Cada saída de ferramenta é limitada a 32 KB (`kMaxToolOutput`), mas `chat.messages` só cresce (até 30 turnos): não há sumarização, descarte nem orçamento global, e a janela do modelo não é medida. | Alto | Depois da memória. O passo barato não é limitar por ferramenta (isso já existe), e sim um teto **global** de caracteres por run, com sumarização/descarte do histórico antigo quando estourar. |
| **Política por chamada de ferramenta** (`allow` / `ask` / `deny`, com padrão de argumento). Hoje a allowlist é binária: a ferramenta existe ou não existe, e não há pausa para aprovação dentro do turno. | Alto | A decidir. O caminho `ask` reaproveita o canal de interação já existente (o C++ bloqueia, o Studio mostra o pedido, com timeout e auditoria). |
| **Handoff com transferência de contexto.** Chamadas isolam o alvo de propósito (só o input explícito); não existe passar o bastão levando a conversa. | Médio | Avaliar quando houver caso de uso conversacional; o vértice de interação cobre a aprovação entre etapas. |
| **Carregamento progressivo de skills.** O `SKILL.md` inteiro entra no system prompt; não há descoberta por nome/descrição nem leitura sob demanda dos recursos da pasta. | Médio | Cresce com o número de skills e de arquivos de apoio. |
| **Ferramenta `web` nativa.** O conjunto nativo é `read`, `write`, `edit`, `bash`, `validate_svg` e as duas de memória. | Médio-baixo | Coberta por MCP; avaliar demanda antes de criar tool nativa. |
| **Fallback de modelo por agente.** Há modelo por vértice, mas não lista de preferência (A → B) para 429/indisponibilidade. | Médio-baixo | Resiliência por provedor; o gateway faz fail-closed, não fallback. |
| **Instruções de projeto sempre ativas e escopadas por caminho** (`AGENTS.md`, `applyTo`) e **prompt files** parametrizados. | Médio para times | Skills por agente, defaults do projeto e templates cobrem parte; não há arquivo global nem escopo por glob. |
| **OAuth em MCP HTTP.** Suportamos stdio e HTTPS com headers/env e validação de URL pública. | Baixo | Sem fluxo interativo de autorização. |
| **Hooks de ciclo de vida** (`preToolUse`, `postToolUse`, `sessionStart`…). | Não planejado como sistema genérico | Cobertura parcial por vértices MCP, validação estática, seção crítica e interação — mas nenhum deles veta uma chamada **dentro** do loop. O caso real (comando destrutivo com `bash`) deve ser tratado por política de comandos, não por um runtime de hooks. |
| **Execução remota/cloud e sessões em background.** | Fora de escopo | O produto é desktop + CLI + gateway; o Runner standalone cobre a execução empacotada. |

Duas decisões registradas:

- **Memória antes de compactação.** A memória resolve o problema *entre* execuções (brief curado,
  notas, lições) e reduz a pressão sobre o contexto, mas não substitui a compactação *dentro* de um
  run: a lista de mensagens continua crescendo. O teto global de contexto não depende da memória e
  pode entrar antes dela.
- **Vértices MCP não substituem hooks.** Um vértice MCP é uma tarefa determinística no grafo: adiciona
  capacidade e impõe ordem. Um hook intercepta o ciclo de vida e pode negar uma ação que o modelo
  escolheu. O que os dois compartilham é "decisão do runtime, não do modelo"; o que falta é o veto
  dentro do loop.

## Beta 2.3 (pré-release)

A linha Beta 2.3 está versionada como `2.3.0-beta.2` e é publicada como pré-release de
desenvolvimento pelo workflow de deploy.

Entregue no Beta 2.3: a **Biblioteca Arestrix** nas fases 1-7, com assets de skill/MCP/template
auditados, escopos Studio/Workspace/Projeto, pins por projeto, fonte GitHub nativa com allowlist de
host, commit pin, checagem de licença e download de assets sob demanda; a revisão do **menu
Arquivo**, incluindo a tela **Abrir** redesenhada no padrão Office e o refresh de
Info/Novo/Salvar/Exportar/Opções; e a sincronização do fundo do canvas com o tema global
Claro/Escuro/Seguir o sistema.

Pendente após a pré-release `2.3.0-beta.2`:

- Instalação/upgrade/uninstall em máquina limpa, instalação silenciosa e validação em Intune/SCCM,
  além do pacote Linux (5B2).
- Aprovação da conta/projeto SignPath, primeira release assinada e assinatura do desinstalador do
  Inno.
- Matriz de CI Windows/Linux, catálogo remoto assinado (5C) e a matriz completa em
  [Plano de testes do Beta 2.3](../quality/beta2.3-test-plan.md).
- Três casos manuais de homologação das bibliotecas: tema/tamanhos, instalação limpa/upgrade e uso
  offline.

## Beta 2.2 (pré-release anterior)

A linha Beta 2.2 está versionada como `2.2.0-beta.1` e é publicada como pré-release de
desenvolvimento pelo workflow de deploy.

Entregue no Beta 2.2: hub de Propriedades no padrão Office com escopos Studio, Workspace e Projeto
editáveis; herança explícita e badges de origem; stores seguros de Studio/Workspace; chaves de
provedores write-only e edição do AI Gateway; defaults, ambiente e SQLite do Projeto; sandbox,
auditoria, reconstrução e reset do Workspace; deep link das configurações do Gerenciador de Projetos;
convergência dos modais de vértice Sandbox/Modelo/Ferramentas/Memória para `BaseModal`; e tema global
Claro/Escuro/Seguir o sistema.

Pendente após a pré-release `2.2.0-beta.1`:

- Instalação/upgrade/uninstall em máquina limpa, instalação silenciosa e validação em Intune/SCCM,
  além do pacote Linux (5B2).
- Aprovação da conta/projeto SignPath, primeira release assinada e assinatura do desinstalador do
  Inno.
- Matriz de CI Windows/Linux, catálogo remoto assinado (5C) e a matriz completa em
  [Plano de testes do Beta 2.3](../quality/beta2.3-test-plan.md).
- Revisão final de contraste dos editores legados secundários e homologação do Studio.

## Beta 2.1 (pré-release anterior)

A linha Beta 2.1 está versionada como `2.1.0-beta.1` e é publicada como pré-release de
desenvolvimento pelo workflow de deploy.

Já entregue para a linha Beta 2.1: grafo de chamadas fechado, interações, formulários gerados,
chamadas paralelas, subflow chamável, vértice Call em fluxo, notificações, `call_result` assíncrono,
trace aberto, gate de release, pacote portátil, instalador Windows Inno Setup, integração SignPath
Foundation, preferência de aparência do canvas, comportamento de console/HUD durante execução, a
consolidação da camada visual SVG com o novo registro de execução por vértice e o refresh do ribbon
no padrão Office 365 em todas as abas.

Pendente após a pré-release `2.1.0-beta.1`:

- Instalação/upgrade/uninstall em máquina limpa, instalação silenciosa e validação em Intune/SCCM,
  além do pacote Linux (5B2).
- Aprovação da conta/projeto SignPath, primeira release assinada e assinatura do desinstalador do
  Inno. Migração para certificado de empresa fica para depois.
- Matriz de CI Windows/Linux, catálogo remoto assinado (5C) e a matriz completa em
  [Plano de testes do Beta 2.3](../quality/beta2.3-test-plan.md).
- Homologação manual do Studio e publicação de checksums, SBOM, notices e notas de release.

## Próximos passos

1. **Linux + CI**
   - Build Linux, instalação do runtime, extração do llama.cpp, router e gateway end-to-end.
   - Matriz Windows/Linux e validação automatizada de catálogo/manifesto.

2. **Distribuição final**
   - Escolher e implementar o empacotamento do instalador.
   - Obter/configurar o certificado de assinatura e assinar os artefatos no CI.
   - Definir par de chaves, custódia, rotação e hospedagem do catálogo assinado.
   - Publicar checksums, notices e notas de release.

3. **Endurecimento do gateway**
   - Postgres/Redis para HA e rate limits distribuídos.
   - Streaming com reconciliação.
   - Admin web/API, SSO, papéis, exportação de auditoria e alertas (80%/100%).
   - Exportação opcional OTLP/SIEM.

4. **Operação de modelos**
   - Testes automatizados de tool calling para novas entradas do catálogo.
   - Perfis de hardware e ajuste de contexto/VRAM por modelo.
   - Verificação de atualização, cota de disco e políticas de limpeza.

5. **Polimento de UX**
   - Wizard de primeira execução com recomendação por hardware.
   - Diagnóstico melhor para falhas de GPU/driver/modelo.
   - Alternância com um clique entre runtime local gerenciado e Ollama/LM Studio/vLLM externos.

## Fronteira de confiança

- Modelos locais nunca passam pelo AI Gateway e são isentos de budget.
- Credenciais de provedores hospedados pertencem ao gateway, não aos runners.
- O gateway é o ponto de imposição de budgets e rate limits.
- Política do SO/MDM e regras de egress de rede são a fronteira final; a UI do desktop é uma
  conveniência e não consegue impor política contra um administrador local.
