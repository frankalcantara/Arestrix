"""Semantic deduplication with objective canonical winner selection for MCP servers."""

from __future__ import annotations

import math
import re
from collections import defaultdict
from typing import Any

from sources import CANONICAL_SERVICE_ALIASES, OFFICIAL_VENDOR_ORGS

STOPWORDS: set[str] = {
    "mcp", "server", "servers", "model", "context", "protocol", "the", "and", "for",
    "with", "from", "that", "this", "into", "your", "official", "community", "api",
    "client", "integration", "tool", "tools", "using", "based", "support", "enables",
    "allows", "interact", "access", "manage", "management", "data", "service",
}


def canonical_service_key(name: str, source_repo: str = "") -> str:
    """Normalize an MCP server name into a canonical service key (e.g. postgresql, github, slack)."""
    raw = name.strip().lower()
    scope = ""
    scope_match = re.match(r"^@([^/]+)/(.+)$", raw)
    if scope_match:
        scope, raw = scope_match.group(1), scope_match.group(2)

    # Strip common MCP prefixes/suffixes iteratively
    for _ in range(3):
        raw = re.sub(r"^(?:mcp-server-|mcp_|mcp-|server-|official-|modelcontextprotocol-)", "", raw)
        raw = re.sub(r"(?:-mcp-server|_mcp_server|-mcp|_mcp|-server|_server)$", "", raw)
    raw = re.sub(r"[^a-z0-9-]+", "-", raw).strip("-")

    # If the package name itself was generic ("mcp", "server", "cli", ""), use @scope or repo name
    if raw in ("", "mcp", "server", "mcp-server", "cli"):
        fallback = scope or (source_repo.split("/")[-1].lower() if source_repo else "")
        fallback = re.sub(r"^(?:mcp-server-|mcp_|mcp-|server-|official-)", "", fallback)
        fallback = re.sub(r"(?:-mcp-server|_mcp_server|-mcp|_mcp|-server|_server|hq)$", "", fallback)
        raw = re.sub(r"[^a-z0-9-]+", "-", fallback).strip("-")

    return CANONICAL_SERVICE_ALIASES.get(raw, raw or "unknown")


def meaningful_tokens(text: str) -> set[str]:
    tokens = set(re.findall(r"[a-z0-9]{2,}", (text or "").lower()))
    return tokens - STOPWORDS


def jaccard(a: set[str], b: set[str]) -> float:
    if not a or not b:
        return 0.0
    inter = len(a & b)
    union = len(a | b)
    return inter / union if union else 0.0


def semantic_similarity(item_a: dict[str, Any], item_b: dict[str, Any]) -> float:
    """Weighted Jaccard similarity over service name, description, and discovered tool names."""
    na = meaningful_tokens(item_a.get("name", ""))
    nb = meaningful_tokens(item_b.get("name", ""))
    da = meaningful_tokens(item_a.get("description", ""))
    db = meaningful_tokens(item_b.get("description", ""))
    ta = meaningful_tokens(" ".join(t.get("name", "") for t in item_a.get("payload", {}).get("cachedTools", [])))
    tb = meaningful_tokens(" ".join(t.get("name", "") for t in item_b.get("payload", {}).get("cachedTools", [])))

    if ta and tb:
        return 0.40 * jaccard(na, nb) + 0.35 * jaccard(da, db) + 0.25 * jaccard(ta, tb)
    return 0.50 * jaccard(na, nb) + 0.50 * jaccard(da, db)


def compute_selection_score(item: dict[str, Any]) -> float:
    """Objective canonical selection score combining quality, authority, security, tools, and adoption."""
    score = float(item.get("quality_score", 0))
    repo = str(item.get("source_repo", "")).lower()
    owner = repo.split("/")[0] if "/" in repo else ""

    is_archived = "servers-archived" in repo or "archived" in repo
    if owner in OFFICIAL_VENDOR_ORGS and not is_archived:
        score += 16.0
    if is_archived:
        score -= 22.0

    verdict = item.get("audit", {}).get("verdict", "CAUTION")
    if verdict == "APPROVE":
        score += 8.0
    elif verdict == "REJECT":
        score -= 40.0

    tools_count = int(item.get("tools_count", 0))
    score += min(8.0, tools_count * 0.8)

    payload = item.get("payload", {})
    if payload.get("transport") == "stdio" and payload.get("command") in ("npx", "uvx"):
        score += 4.0
    elif payload.get("transport") == "sse" and str(payload.get("url", "")).startswith("https://"):
        score += 4.0

    stars = int(item.get("repo_stars", 0))
    score += min(5.0, math.log10(max(0, stars) + 1) * 1.1)

    return round(score, 2)


def explain_loser_reason(winner: dict[str, Any], loser: dict[str, Any]) -> str:
    """Generate an objective explanation of why a variant was discarded in favor of the canonical winner."""
    reasons: list[str] = []
    if loser.get("audit", {}).get("verdict") == "REJECT" and winner.get("audit", {}).get("verdict") != "REJECT":
        reasons.append("reprovado na pré-triagem de segurança")
    if "servers-archived" in str(loser.get("source_repo", "")).lower() and "servers-archived" not in str(
        winner.get("source_repo", "")
    ).lower():
        reasons.append("repositório arquivado substituído por servidor ativo/oficial")
    w_owner = str(winner.get("source_repo", "")).lower().split("/")[0]
    l_owner = str(loser.get("source_repo", "")).lower().split("/")[0]
    if w_owner in OFFICIAL_VENDOR_ORGS and l_owner not in OFFICIAL_VENDOR_ORGS:
        reasons.append(f"vencedor mantido por organização oficial ({w_owner})")
    if winner.get("tools_count", 0) > loser.get("tools_count", 0):
        reasons.append(
            f"menos ferramentas descobertas ({loser.get('tools_count', 0)} vs {winner.get('tools_count', 0)})"
        )
    if winner.get("quality_score", 0) > loser.get("quality_score", 0):
        reasons.append(
            f"quality_score inferior ({loser.get('quality_score', 0)} vs {winner.get('quality_score', 0)})"
        )
    if not reasons:
        reasons.append(
            f"selection_score inferior ({loser.get('_sel_score', 0):.1f} vs {winner.get('_sel_score', 0):.1f})"
        )
    return "; ".join(reasons)


def deduplicate_and_select_canonical(
    items: list[dict[str, Any]],
    similarity_threshold: float = 0.58,
) -> tuple[list[dict[str, Any]], list[dict[str, Any]]]:
    """Deduplicate MCP servers by exact ID and semantic service clusters, electing 1 canonical winner each."""
    if not items:
        return [], []

    # Step 1: Exact ID dedup
    by_id: dict[str, dict[str, Any]] = {}
    for raw in items:
        item = dict(raw)
        item["_sel_score"] = compute_selection_score(item)
        item["_canon_key"] = canonical_service_key(item["name"], item.get("source_repo", ""))
        item_id = item["id"]
        if item_id not in by_id or item["_sel_score"] > by_id[item_id]["_sel_score"]:
            by_id[item_id] = item

    unique_items = list(by_id.values())
    n = len(unique_items)

    # Step 2: Union-Find semantic clustering
    parent = list(range(n))

    def find(x: int) -> int:
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a: int, b: int) -> None:
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[rb] = ra

    # 2a. Exact canonical_service_key merge
    key_index: dict[str, int] = {}
    for idx, item in enumerate(unique_items):
        key = item["_canon_key"]
        if key and key != "unknown":
            if key in key_index:
                union(key_index[key], idx)
            else:
                key_index[key] = idx

    # 2b. Intra-subdomain semantic similarity merge
    by_subdomain: dict[tuple[str, str], list[int]] = defaultdict(list)
    for idx, item in enumerate(unique_items):
        by_subdomain[(item["domain"], item["subdomain"])].append(idx)

    for indices in by_subdomain.values():
        m = len(indices)
        for i in range(m):
            for j in range(i + 1, m):
                ia, ib = indices[i], indices[j]
                if find(ia) == find(ib):
                    continue
                if semantic_similarity(unique_items[ia], unique_items[ib]) >= similarity_threshold:
                    union(ia, ib)

    # Step 3: Elect 1 canonical winner per cluster and log discarded variants
    clusters: dict[int, list[dict[str, Any]]] = defaultdict(list)
    for idx, item in enumerate(unique_items):
        clusters[find(idx)].append(item)

    canonical_items: list[dict[str, Any]] = []
    multi_clusters: list[dict[str, Any]] = []

    for members in clusters.values():
        members.sort(
            key=lambda x: (x["_sel_score"], x.get("quality_score", 0), x.get("repo_stars", 0)),
            reverse=True,
        )
        winner = dict(members[0])
        canon_key = winner["_canon_key"]
        sel_score = winner["_sel_score"]

        variants: list[dict[str, Any]] = []
        for loser in members[1:]:
            sim = round(semantic_similarity(winner, loser), 3)
            variants.append(
                {
                    "name": loser["name"],
                    "server_id": loser["server_id"],
                    "source_repo": loser["source_repo"],
                    "source_path": loser["source_path"],
                    "source_url": loser["source_url"],
                    "license": loser["license"],
                    "quality_score": loser["quality_score"],
                    "selection_score": loser["_sel_score"],
                    "audit_verdict": loser["audit"]["verdict"],
                    "similarity": sim,
                    "reason_not_selected": explain_loser_reason(winner, loser),
                }
            )

        winner.pop("_sel_score", None)
        winner.pop("_canon_key", None)
        winner["canonical_key"] = canon_key
        winner["selection_score"] = sel_score
        winner["cluster_size"] = len(members)
        winner["variants"] = variants
        canonical_items.append(winner)

        if variants:
            multi_clusters.append(
                {
                    "canonical_key": canon_key,
                    "domain": winner["domain"],
                    "subdomain": winner["subdomain"],
                    "winner": {
                        "name": winner["name"],
                        "server_id": winner["server_id"],
                        "source_repo": winner["source_repo"],
                        "source_url": winner["source_url"],
                        "quality_score": winner["quality_score"],
                        "selection_score": sel_score,
                        "audit_verdict": winner["audit"]["verdict"],
                    },
                    "variants": variants,
                }
            )

    canonical_items.sort(
        key=lambda x: (x["selection_score"], x["quality_score"], x["repo_stars"]),
        reverse=True,
    )
    multi_clusters.sort(key=lambda c: len(c["variants"]), reverse=True)
    return canonical_items, multi_clusters
