# Example 14 - Compressed context

This example shows what the context-compression subsystem does to a **large tool observation**
inside the agent loop. A static vertex writes an 80-event JSON report; an agent reads it, receives a
compressed version with a `[CCR:...]` token, uses the `retrieve` tool to recover the original, and
writes a short summary.

No vertex is decorative: the contract is the summary at `exports/resumo-eventos.md`, and the
sentinel `SENTINELA-14-FIM` (the last line of the original JSON) only appears there if `retrieve`
returned the **whole** original.

## Run

```bat
set DEEPSEEK_API_KEY=...
arestrix-cli run pipelines\main.json --root .
```

Or open the folder in the Studio and press Run.

## What to observe

1. The static vertex creates `workspace/eventos.json` (80 events, ~30 KB).
2. On the next turn, the Console prints:

   ```
   [Agent Analista_de_Eventos] contexto compactado: 1 bloco(s), ~N -> ~M tokens (json)
   ```

3. The model sees the compacted JSON plus `[CCR:<token>]`. The `retrieve` tool returns the
   original; the summary then cites the number of events and the final sentinel.
4. `exports/resumo-eventos.md` contains the sentinel, proving the round trip.

The original is kept in `%LOCALAPPDATA%\Arestrix\ccr` (outside the repository), with a 7-day
retention and a 256 MB cap per project. A block with `[CCR:...]` is never compressed again.

## Configuration

`globalDefaults.compression` appears in both `arestrix.project.json` and
`pipelines/main.json`:

| Field | Value | Why |
| :--- | ---: | :--- |
| `minBlockTokens` | 200 | the report is far above it |
| `minRunTokens` | 300 | the whole conversation must justify the pass |
| `maxArrayItems` | 8 | keeps the first events; the rest stays in the CCR |
| `maxStringTokens` | 120 | shortens long messages |
| `code` | `true` | code observations also use the structural compressor |
| `prose` | `false` | no model download is required for this example |

To also compress **prose**, set `prose: true`, build with ONNX (the official installer already does),
and download the Kompress model in **Properties → Context compression**. On a machine with a
dedicated GPU, the same panel recommends the fp32 model and the DirectML provider.
