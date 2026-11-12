# Embeddings reais: qualidade e latência

> **Versão consolidada:** [Resultados dos testes de memória](../pt-BR/resultados-testes-memoria.md) · [English](../en/memory-test-results.md).
>
> **Status:** validação concluída em 2026-11-12 com os dois modelos reais, via
> endpoint local OpenAI-compatible. Não é uma medição de endpoint de produção.

## Ambiente

- Windows 11 x64, Python 3.11.9, PyTorch 2.9.1+cu126, Transformers 5.22.2,
  SentenceTransformers 6.3.0.
- GPU NVIDIA GeForce GTX 1650 (4 GB) para o EmbeddingGemma 2; CPU com `bfloat16`
  para o Qwen3-Embedding-4B, porque o modelo de 4B não cabe na GPU.
- Adaptador local `POST /v1/embeddings`, com os mesmos prefixos enviados pelo
  runtime (`CppCore/src/memory/EmbeddingProvider.cpp`).
- Revisões: `google/embeddinggemma-2@f9972ac2` e
  `Qwen/Qwen3-Embedding-4B@5cf2132a`.

## Corpus

16 documentos em pt-BR e 16 consultas mistas pt-BR/en, com 3 repetições. Cada
consulta tem um ou dois documentos esperados. O corpus é pequeno e curado: mede
o caminho real e a direção de qualidade, não substitui um benchmark público nem
dados de cliente.

## Resultados

| Modelo | Dimensões | Recall@1 | Recall@3 | MRR@10 | Consulta p50 | Consulta p95 | Índice/doc |
| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| EmbeddingGemma 2 | 768 / 768 | **0,875** | **1,000** | **0,938** | **134 ms** | **179 ms** | **56 ms** |
| Qwen3-Embedding-4B (768) | 2560 → 768 | 0,813 | 1,000 | 0,906 | 8.474 ms | 10.982 ms | 9.627 ms |
| Qwen3-Embedding-4B (2560) | 2560 / 2560 | 0,875 | 1,000 | 0,927 | 8.411 ms | 12.725 ms | 8.918 ms |
| Lexical BM25 | — | 0,813 | 0,938 | 0,865 | — | — | — |

Nos valores do Qwen3, o ponto é separador de milhar: 8.474 ms equivalem a
8,5 segundos; 9.627 ms equivalem a 9,6 segundos.

O Qwen3 completo (2560) recupera o mesmo top-1 do EmbeddingGemma 2, mas o
recall@1 cai para 0,813 quando o runtime trunca para 768 — o mesmo valor do
baseline lexical. O recall@3 continua 1,000 nos dois modelos.

## Integração com o runtime

| Modelo | `embeddings test` | Reindexação | Estado |
| :--- | :--- | :--- | :--- |
| EmbeddingGemma 2 | 768d, 1.016 ms | 4/4 indexados, 0 falhas | `ready`, 4 vetores, 0 pendentes |
| Qwen3-Embedding-4B (768) | 768d, 8.724 ms | 4/4 indexados, 0 falhas | índice gravado com fingerprint próprio |

Os quatro trechos vieram de uma fonte ingerida no projeto de teste. O projeto
original não foi alterado.

## Falha do provedor

Com o endpoint desligado, `memory embeddings test` retornou `ok=false`,
`error="Falha de transporte (erro WinHTTP 12029)"` e 2.052 ms. O runtime não
inventa vetores: a busca lexical continua sendo o caminho seguro, coberto pelos
testes existentes de fallback.

## Leitura

- **EmbeddingGemma 2** é a melhor opção local neste hardware: melhor recall@1,
  latência de consulta na casa de 134 ms e indexação de 56 ms por documento.
- **Qwen3-Embedding-4B** tem qualidade equivalente no vetor completo, mas o
  custo em CPU é proibitivo (8,5 s por consulta). O truncamento para 768 iguala
  o recall@1 ao lexical neste corpus.
- O fallback lexical ainda é competitivo em um corpus pequeno e com vocabulário
  compartilhado; o ganho dos embeddings aparece no recall@3 e no top-1 do
  EmbeddingGemma 2.
- A validação cobre HTTP, dimensão, prefixos, indexação, reindexação e falha de
  transporte. Não cobre um endpoint de fornecedor em produção nem dados de
  cliente.

## Reprodução

```powershell
# Endpoint local OpenAI-compatible com o modelo carregado.
py -3 scripts/memory_embeddings_eval.py `
  --base-url http://127.0.0.1:18081/v1 `
  --model embeddinggemma-2 --repetitions 3

py -3 scripts/memory_embeddings_eval.py `
  --base-url http://127.0.0.1:18082/v1 `
  --model qwen3-embedding-4b --truncate-dim 768 --repetitions 3

# Integração pelo CLI (projeto de teste, sem alterar o original).
arestrix-cli memory embeddings test --project <copia> --enable `
  --base-url http://127.0.0.1:18081/v1 --model embeddinggemma-2 --json
arestrix-cli memory reindex --project <copia> --enable `
  --base-url http://127.0.0.1:18081/v1 --model embeddinggemma-2 --json
```

Evidência: `tests/fixtures/memory-embeddings-live-report.json`.
