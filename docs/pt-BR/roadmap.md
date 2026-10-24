# Roadmap e limitações conhecidas

> **Aplica-se a:** Arestrix Beta 1.5 (`1.5.0-beta.1`) · Última revisão: 2026-10-21
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
- Empacotamento de instalador (MSI/WiX, MSIX ou Inno Setup; AppImage/deb/tarball no Linux) não foi
  implementado.
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

Ainda pendente:

- Escopos de workspace e instituição.
- Visualização dedicada do grafo e dos conflitos na UI.
- Ranking híbrido completo, embeddings e consolidação por LLM.
- `memory_handoff` e aprendizagem recursiva.

### Modelos

- Apenas Qwen2.5 7B/14B têm tool calling verificado. As demais entradas são pré-aprovadas, mas podem ser
  somente conversa ou não verificadas para ferramentas.
- Qualidade/velocidade do 14B depende de GPU/VRAM; execução somente em CPU é lenta.
- Ainda não há atualização automática de modelos, deduplicação ou cota de disco.
- Os pesos não são redistribuídos; o download depende do publicador upstream e da licença.

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
