# Roadmap e limitações conhecidas

> **Aplica-se a:** Arestrix Beta 2.2 (`2.2.0-beta.1`) · Última revisão: 2026-10-27
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

### Memória de projeto (Beta 2)

Implementado no núcleo:

- AMG em SQLite (`.arestrix/memory/memory.db`) com arestas temporais, evidências, auditoria,
  working memory e métricas de uso.
- Lições, Blackboard, ferramentas `memory_search`/`memory_record_lesson` e brief compilado com
  `contextBudgetTokens`, ordenado por confiança/recência.
- Episódios de sucesso/falha, `memory_hit` (injetado/consultado) e métricas com retries e outcome.
- Importação única dos JSONs legados e registro automático de falhas de agente.
- Painel de consumo na UI, relatório nativo `arestrix-cli memory report`, impacto no AI Gateway,
  A/B no Studio e exemplos 04 e 05. `memory_report.py` permanece opcional para desenvolvimento.
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

## Beta 2.2 (pré-release)

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
  [Plano de testes do Beta 2.2](../quality/beta2.2-test-plan.md).
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
  [Plano de testes do Beta 2.2](../quality/beta2.2-test-plan.md).
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
