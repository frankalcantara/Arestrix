# arestrix-gateway

Gateway on-prem, OpenAI/Anthropic-compatible, com chaves virtuais, budgets por escopo,
rate limit e ledger de uso. Fica entre o Studio/runners e os provedores hospedados.

```text
Studio/Runner --(chave virtual)--> arestrix-gateway --(chaves reais)--> provedores
Studio/Runner --(sem gateway)----> llama-server (modelos locais, isentos)
```

## Build

```bash
python CppCore/dev.py --no-run --no-frontend
```

O executável fica em `build/arestrix-gateway`.

## Configuração

1. Copie `CppCore/gateway/arestrix-gateway.example.json` para `arestrix-gateway.json`.
2. Ajuste rotas, preços e o caminho do banco.
3. Exporte as chaves reais dos provedores no ambiente do gateway:
   `OPENAI_API_KEY`, `ANTHROPIC_API_KEY`, `GEMINI_API_KEY`, `DEEPSEEK_API_KEY`,
   `GROQ_API_KEY`, `MISTRAL_API_KEY`.
4. Inicie:

```bash
arestrix-gateway serve --config arestrix-gateway.json
```

O gateway escuta em `127.0.0.1:8787` por padrão. Para exposição em rede, coloque um
reverse proxy TLS (nginx, Caddy etc.) na frente e restrinja o acesso.

## Chaves e budgets

```bash
# Cria uma chave virtual com budget mensal de US$ 50
arestrix-gateway key create --name "runner-ana" --project "demo" --user "ana" \
  --budget-usd 50 --period monthly --db arestrix-gateway.db

# Budget por provedor
arestrix-gateway budget set --scope provider --id openai --period monthly \
  --usd 200 --db arestrix-gateway.db

# Rate limit opcional por chave
arestrix-gateway key create --name "runner-lab" --rpm 30 --tpm 50000 --db arestrix-gateway.db

# Auditoria
arestrix-gateway usage report --since 2026-10-01T00:00:00Z --db arestrix-gateway.db
```

A chave `av_...` é exibida apenas na criação. O gateway guarda só o SHA-256.

## Studio e runners

Configuração no ambiente do Studio e de cada runner:

```text
ARESTRIX_AI_GATEWAY_URL=https://gateway.empresa.com
ARESTRIX_AI_GATEWAY_KEY=av_...
ARESTRIX_REQUIRE_AI_GATEWAY=1
```

- provedores hospedados usam o gateway e a chave virtual;
- `local` e `jev` continuam diretos e isentos;
- com `ARESTRIX_REQUIRE_AI_GATEWAY=1`, o runner falha fechado se o gateway não estiver
  configurado;
- as chaves reais dos provedores não devem existir nos runners em produção.

### Impacto da memória no gateway

Quando o runtime usa o gateway, ele envia metadados por requisição:

```text
X-Arestrix-Agent
X-Arestrix-Run-Id
X-Arestrix-Memory-Scope
X-Arestrix-Memory-Enabled
X-Arestrix-Memory-Context-Chars
```

O gateway grava esses campos em `usage_events` e calcula `memory_tokens` como uma aproximação
(caracteres / 4). O relatório e `/v1/usage` mostram:

- requisições com memória;
- caracteres de contexto injetados;
- tokens aproximados de memória;
- proporção sobre os tokens de entrada;
- comparativo memória ON/OFF;
- detalhamento por agente.

Esses headers só são enviados quando a chamada passa pelo gateway. Em chamadas diretas ao
provedor, o impacto continua disponível na tabela `memory_usage` do projeto, mas não no
`usage_events` do gateway. Nenhum Python ou processo JS participa da coleta: o runtime C++ envia
os headers e o gateway C++ agrega no SQLite.

Para política empresarial imutável, distribua `ARESTRIX_AI_GATEWAY_POLICY=locked` por
GPO/MDM. O backend trata o gateway como obrigatório mesmo que o usuário tente desativar, e a
tela de Padrões Globais fica somente leitura. A fronteira de confiança real é o sistema
operacional: não distribua chaves de provedor e restrinja a saída de rede dos runners ao
gateway.

## Endpoints

- `POST /{provider}/{path}` — proxy OpenAI/Anthropic (`/openai/chat/completions`,
  `/anthropic/v1/messages`, etc.).
- `GET /v1/usage` — orçamento e uso da chave virtual.
- `GET /health` — saúde do gateway e do banco.
- `GET /metrics` — métricas Prometheus.

Streaming (`stream: true`) ainda não é suportado e retorna `400 streaming_unsupported`.

## Smoke test

```bash
python CppCore/dev.py --gateway-smoke --no-frontend
# ou, com o binário já compilado:
node scripts/gateway-smoke.mjs
```

O teste sobe um upstream OpenAI falso, cria uma chave virtual com budget, valida a primeira chamada
(200), o bloqueio seguinte (402), `/metrics` e `/v1/usage`, incluindo os campos de impacto da memória.
Não usa rede externa nem chaves reais.

## Limitações do MVP

- SQLite single-node; para HA/multi-instância, migrar contadores para Postgres/Redis.
- Preços são configuráveis e devem ser mantidos pela empresa.
- TLS é responsabilidade do reverse proxy.
- A reserva de budget usa estimativa conservadora e reconcilia com o `usage` real.
