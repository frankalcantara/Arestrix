# Exemplo 14 - Contexto comprimido

Este exemplo mostra o efeito do subsistema de compactação de contexto sobre uma **observação de
ferramenta grande** dentro do loop do agente. Um vértice estático grava um relatório JSON com 80
eventos; um agente lê o arquivo, recebe a versão compactada com um token `[CCR:...]`, usa a
ferramenta `retrieve` para recuperar o original e grava um resumo curto.

Nenhum vértice é decorativo: o contrato é o resumo em `exports/resumo-eventos.md`, e a sentinela
`SENTINELA-14-FIM` (a última linha do JSON original) só aparece lá se o `retrieve` devolveu o
original **inteiro**.

## Executar

```bat
set DEEPSEEK_API_KEY=...
arestrix-cli run pipelines\main.json --root .
```

Ou abra a pasta no Studio e clique em Executar.

## O que observar

1. O vértice estático cria `workspace/eventos.json` (80 eventos, ~30 KB).
2. No turno seguinte, o Console mostra:

   ```
   [Agent Analista_de_Eventos] contexto compactado: 1 bloco(s), ~N -> ~M tokens (json)
   ```

3. O modelo vê o JSON compactado e o aviso `[CCR:<token>]`. A ferramenta `retrieve` devolve o
   original; o resumo cita a contagem de eventos e a sentinela do fim.
4. `exports/resumo-eventos.md` contém a sentinela, provando o ciclo completo.

O original fica em `%LOCALAPPDATA%\Arestrix\ccr` (fora do repositório), com retenção de 7 dias e
teto de 256 MB por projeto. Um bloco com `[CCR:...]` nunca é comprimido de novo.

## Configuração

`globalDefaults.compression` aparece em `arestrix.project.json` e em `pipelines/main.json`:

| Campo | Valor | Por quê |
| :--- | ---: | :--- |
| `minBlockTokens` | 200 | o relatório fica muito acima disso |
| `minRunTokens` | 300 | a conversa inteira precisa justificar a passada |
| `maxArrayItems` | 8 | mantém os primeiros eventos; o resto fica no CCR |
| `maxStringTokens` | 120 | encurta mensagens longas |
| `code` | `true` | observações de código também usam o compressor estrutural |
| `prose` | `false` | este exemplo não exige baixar modelo |

Para compactar também **prosa**, ligue `prose: true`, use um build com ONNX (o instalador oficial
já tem) e baixe o modelo Kompress em **Propriedades → Compactação de contexto**. Em máquina com GPU
dedicada, o mesmo painel recomenda o modelo fp32 e o provedor DirectML.
