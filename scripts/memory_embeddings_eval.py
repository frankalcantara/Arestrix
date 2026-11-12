#!/usr/bin/env python3
"""Live quality/latency check for an OpenAI-compatible embedding endpoint.

The corpus is intentionally small and curated. It is a smoke benchmark for the
runtime path, not a public retrieval benchmark. Prefixes mirror
CppCore/src/memory/EmbeddingProvider.cpp so the endpoint sees the same text the
runtime sends.
"""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
import sys
import time
import unicodedata
import urllib.error
import urllib.request
from datetime import datetime, timezone
from pathlib import Path

DOCS = [
    ("d1", "Lições aprovadas e playbooks ficam no banco de memória do projeto e só são ativados após revisão."),
    ("d2", "O memory.gate decide entre none, brief, search e learn para reduzir buscas desnecessárias do agente."),
    ("d3", "A suíte C++ executa 4.289 verificações e cobre memória, MCP, geração de código e segurança."),
    ("d4", "EmbeddingGemma 2 gera vetores de 768 dimensões e aceita truncamento MRL para 512, 256 e 128."),
    ("d5", "Qwen3-Embedding-4B produz 2560 dimensões e o runtime trunca para 768 por padrão."),
    ("d6", "O roteamento binário escolhe o nível local ou a fronteira conforme confiança e histórico de validação."),
    ("d7", "Pontes entre domínios são hipóteses informativas e nunca criam arestas automaticamente."),
    ("d8", "Caminhos são consolidados a partir de traços aprovados e podem ser invalidados quando uma fonte muda."),
    ("d9", "O brief injeta um bloco curto de contexto com lições, fatos e playbooks relevantes para a tarefa."),
    ("d10", "Se o provedor de embeddings falhar, a busca lexical continua funcionando como fallback seguro."),
    ("d11", "O workspace promove lições entre projetos e mantém isolamento por escopo."),
    ("d12", "O nível institution lê o escopo institution:<id> do workspace e retorna lições, playbooks e arestas ativas."),
    ("d13", "A captura Tracy é opcional, fica fora do pacote de release e não altera a semântica do pipeline."),
    ("d14", "O catálogo remoto assinado usa uma chave pública para validar pacotes e componentes antes da instalação."),
    ("d15", "O modo shadow registra a decisão do gate sem trocar o tier executado."),
    ("d16", "O modo enforce aplica a decisão do gate e pode limitar a quantidade de buscas de memória."),
]

QUERIES = [
    ("q1", "Onde encontro lições aprovadas e playbooks?", {"d1"}),
    ("q2", "Como evitar buscas de memória desnecessárias?", {"d2", "d16"}),
    ("q3", "How many checks does the C++ suite run?", {"d3"}),
    ("q4", "Which embedding model uses MRL truncation to 512, 256 and 128?", {"d4"}),
    ("q5", "Which embedding model has 2560 dimensions before truncation?", {"d5"}),
    ("q6", "Quando o roteador escolhe o modelo de fronteira?", {"d6"}),
    ("q7", "Does the bridge detector create edges automatically?", {"d7"}),
    ("q8", "De onde vêm os caminhos consolidados?", {"d8"}),
    ("q9", "What does the brief inject into the task?", {"d9"}),
    ("q10", "O que acontece se o provedor de embeddings estiver fora do ar?", {"d10"}),
    ("q11", "How are lessons shared across projects?", {"d11"}),
    ("q12", "O que o nível institution retorna?", {"d12"}),
    ("q13", "Is Tracy capture included in the release package?", {"d13"}),
    ("q14", "Como o catálogo remoto valida pacotes assinados?", {"d14"}),
    ("q15", "O modo shadow troca o tier executado?", {"d15"}),
    ("q16", "O modo enforce limita buscas de memória?", {"d16"}),
]


def endpoint_for(base: str) -> str:
    base = base.rstrip("/")
    if base.endswith("/embeddings"):
        return base
    if base.endswith("/v1"):
        return base + "/embeddings"
    return base + "/v1/embeddings"


def prefix_for(model: str, query: bool) -> str:
    name = model.lower()
    if "embeddinggemma" in name:
        return "task: search result | query: " if query else "title: none | text: "
    if "qwen3" in name or "qwen-3" in name:
        return (" Instruct: Given a task description, retrieve memory items that help complete it\nQuery:  "
                if query else "")
    if "e5" in name:
        return "query: " if query else "passage: "
    return ""


def post_embeddings(url: str, model: str, inputs: list[str], api_key: str, timeout: float):
    payload = json.dumps({"model": model, "input": inputs}).encode("utf-8")
    headers = {"Content-Type": "application/json"}
    if api_key:
        headers["Authorization"] = f"Bearer {api_key}"
    request = urllib.request.Request(url, data=payload, headers=headers, method="POST")
    started = time.perf_counter()
    with urllib.request.urlopen(request, timeout=timeout) as response:
        body = json.loads(response.read().decode("utf-8"))
    elapsed_ms = (time.perf_counter() - started) * 1000.0
    if "data" not in body or len(body["data"]) != len(inputs):
        raise RuntimeError("endpoint did not return one embedding per input")
    vectors = [item["embedding"] for item in sorted(body["data"], key=lambda item: item.get("index", 0))]
    return vectors, elapsed_ms


def normalize(vector: list[float]) -> list[float]:
    norm = math.sqrt(sum(value * value for value in vector))
    return [value / norm for value in vector] if norm else vector


def cosine(a: list[float], b: list[float]) -> float:
    return sum(x * y for x, y in zip(a, b))


def tokens(text: str) -> list[str]:
    folded = unicodedata.normalize("NFKD", text.lower())
    folded = "".join(char for char in folded if not unicodedata.combining(char))
    return re.findall(r"[a-z0-9_]+", folded)


def bm25_rank(query: str, docs: list[tuple[str, str]]) -> list[list[float]]:
    doc_tokens = [tokens(text) for _, text in docs]
    query_tokens = tokens(query)
    n = len(doc_tokens)
    avg_len = sum(len(item) for item in doc_tokens) / max(1, n)
    df: dict[str, int] = {}
    for item in doc_tokens:
        for token in set(item):
            df[token] = df.get(token, 0) + 1
    scores = []
    for item in doc_tokens:
        counts: dict[str, int] = {}
        for token in item:
            counts[token] = counts.get(token, 0) + 1
        score = 0.0
        for token in query_tokens:
            if token not in counts:
                continue
            idf = math.log(1.0 + (n - df[token] + 0.5) / (df[token] + 0.5))
            tf = counts[token]
            denom = tf + 1.5 * (1.0 - 0.75 + 0.75 * len(item) / max(1.0, avg_len))
            score += idf * (tf * 2.5) / denom
        scores.append(score)
    return scores


def metrics(rankings: list[list[str]], expected: list[set[str]]) -> dict:
    hits1 = hits3 = 0
    reciprocal = 0.0
    for ranked, wanted in zip(rankings, expected):
        if ranked and ranked[0] in wanted:
            hits1 += 1
        if any(item in wanted for item in ranked[:3]):
            hits3 += 1
        for index, item in enumerate(ranked[:10], start=1):
            if item in wanted:
                reciprocal += 1.0 / index
                break
    total = len(rankings)
    return {
        "queries": total,
        "recallAt1": round(hits1 / total, 4),
        "recallAt3": round(hits3 / total, 4),
        "mrrAt10": round(reciprocal / total, 4),
    }


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, max(0, math.ceil(len(ordered) * fraction) - 1))
    return round(ordered[index], 3)


def evaluate(args) -> dict:
    url = endpoint_for(args.base_url)
    doc_ids = [doc_id for doc_id, _ in DOCS]
    doc_texts = [prefix_for(args.model, False) + text for _, text in DOCS]
    query_texts = [prefix_for(args.model, True) + text for _, text, _ in QUERIES]
    expected = [wanted for _, _, wanted in QUERIES]

    doc_vectors, index_ms = post_embeddings(url, args.model, doc_texts, args.api_key, args.timeout)
    source_dim = len(doc_vectors[0])
    if args.truncate_dim > 0:
        if args.truncate_dim > source_dim:
            raise RuntimeError(f"truncate-dim {args.truncate_dim} exceeds source dimension {source_dim}")
        doc_vectors = [vector[:args.truncate_dim] for vector in doc_vectors]
    doc_vectors = [normalize(vector) for vector in doc_vectors]
    dim = len(doc_vectors[0])

    # Warm up the endpoint before timing single-query calls.
    post_embeddings(url, args.model, [query_texts[0]], args.api_key, args.timeout)
    query_vectors = []
    query_latencies = []
    for _ in range(args.repetitions):
        for text in query_texts:
            vectors, elapsed = post_embeddings(url, args.model, [text], args.api_key, args.timeout)
            vector = vectors[0]
            if args.truncate_dim > 0:
                vector = vector[:args.truncate_dim]
            query_vectors.append(normalize(vector))
            query_latencies.append(elapsed)
    # The loop above produced repetitions * queries vectors; keep one vector per query.
    query_vectors = query_vectors[:len(query_texts)]

    embedding_rankings = []
    for vector in query_vectors:
        scores = [(cosine(vector, doc_vector), doc_id) for doc_id, doc_vector in zip(doc_ids, doc_vectors)]
        embedding_rankings.append([doc_id for _, doc_id in sorted(scores, reverse=True)])

    lexical_rankings = []
    for _, query, _ in QUERIES:
        scores = bm25_rank(query, DOCS)
        lexical_rankings.append([doc_id for _, doc_id in sorted(zip(scores, doc_ids), reverse=True)])

    return {
        "schema": "arestrix.memory.embeddings-live.v1",
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "provider": args.provider,
        "model": args.model,
        "endpoint": url,
        "sourceDim": source_dim,
        "dim": dim,
        "corpus": {
            "documents": len(DOCS),
            "queries": len(QUERIES),
            "languages": "documents in pt-BR; queries mixed pt-BR/en",
        },
        "prefixes": {
            "query": prefix_for(args.model, True),
            "document": prefix_for(args.model, False),
        },
        "results": {
            "embedding": {
                **metrics(embedding_rankings, expected),
                "indexBatchMs": round(index_ms, 3),
                "indexPerDocMs": round(index_ms / len(DOCS), 3),
                "queryLatencyMs": {
                    "p50": percentile(query_latencies, 0.50),
                    "p95": percentile(query_latencies, 0.95),
                    "mean": round(statistics.fmean(query_latencies), 3),
                    "samples": len(query_latencies),
                },
            },
            "lexical": metrics(lexical_rankings, expected),
        },
        "limitations": [
            "Small curated corpus (16 documents, 16 queries); not a public benchmark.",
            "The endpoint runs the real model locally; latency includes HTTP and local inference.",
            "Quality is measured on this corpus only and does not replace customer data.",
        ],
    }


def markdown(report: dict) -> str:
    emb = report["results"]["embedding"]
    lex = report["results"]["lexical"]
    lines = [
        "# Embeddings reais: qualidade e latência",
        "",
        "> **Versão consolidada:** [Resultados dos testes de memória](../pt-BR/resultados-testes-memoria.md) · [English](../en/memory-test-results.md).",
        "",
        f"- Provedor: `{report['provider']}`",
        f"- Modelo: `{report['model']}`",
        f"- Dimensões de origem: {report.get('sourceDim', report['dim'])}",
        f"- Dimensões efetivas: {report['dim']}",
        f"- Corpus: {report['corpus']['documents']} documentos, {report['corpus']['queries']} consultas",
        f"- Prefixo de consulta: `{report['prefixes']['query']}`",
        f"- Prefixo de documento: `{report['prefixes']['document']}`",
        "",
        "## Resultados",
        "",
        "| Métrica | Embeddings | Lexical (BM25) |",
        "| :--- | ---: | ---: |",
        f"| Recall@1 | {emb['recallAt1']:.3f} | {lex['recallAt1']:.3f} |",
        f"| Recall@3 | {emb['recallAt3']:.3f} | {lex['recallAt3']:.3f} |",
        f"| MRR@10 | {emb['mrrAt10']:.3f} | {lex['mrrAt10']:.3f} |",
        "",
        "| Latência | Valor |",
        "| :--- | ---: |",
        f"| Indexação do lote | {emb['indexBatchMs']:.0f} ms |",
        f"| Indexação por documento | {emb['indexPerDocMs']:.0f} ms |",
        f"| Consulta p50 | {emb['queryLatencyMs']['p50']:.0f} ms |",
        f"| Consulta p95 | {emb['queryLatencyMs']['p95']:.0f} ms |",
        "",
        "## Limitações",
        "",
    ]
    for limitation in report["limitations"]:
        lines.append(f"- {limitation}")
    lines.append("")
    return "\n".join(lines)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-url", required=True)
    parser.add_argument("--model", required=True)
    parser.add_argument("--provider", default="local-transformers")
    parser.add_argument("--api-key", default="")
    parser.add_argument("--timeout", type=float, default=180.0)
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--truncate-dim", type=int, default=0)
    parser.add_argument("--json-out", default="")
    parser.add_argument("--md-out", default="")
    args = parser.parse_args()
    try:
        report = evaluate(args)
    except (urllib.error.URLError, TimeoutError, RuntimeError, KeyError) as exc:
        print(f"falha ao avaliar embeddings: {exc}", file=sys.stderr)
        return 1
    if args.json_out:
        Path(args.json_out).write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding="utf-8")
    if args.md_out:
        Path(args.md_out).write_text(markdown(report), encoding="utf-8")
    if not args.json_out and not args.md_out:
        print(json.dumps(report, indent=2, ensure_ascii=False))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
