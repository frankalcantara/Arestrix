# Exemplo 12 — Chamadas assíncronas

Um coordenador inicia uma chamada com `async: true`, recebe um `callId` **pendente**, continua
trabalhando e consulta o resultado com `call_result`.

## Fluxo

```text
Coordenador_Assincrono ──(fluxo)──► Entrega_Assincrona
        │ call (allowAsync)
        ▼
Especialista_Assincrono
```

1. `call_agent` com `async: true` inicia o alvo em segundo plano e devolve
   `{"status":"pending","callId":"..."}`.
2. O coordenador consulta `call_result` com esse `callId`.
3. Enquanto o alvo roda, `call_result` devolve `pending`; ao concluir, devolve o mesmo JSON de uma
   chamada síncrona.
4. O coordenador grava a resposta em `workspace/resposta.txt` e o vértice de saída exporta.

A aresta de chamada precisa marcar **Permitir chamada assíncrona**. Isso mantém o async opt-in por
alvo. Chamadas síncronas continuam o padrão, e `ARESTRIX_ASYNC_CALLS=0` desliga o async globalmente.

O runtime espera chamadas pendentes antes de encerrar o processo, para não perder notificações nem
auditoria.
