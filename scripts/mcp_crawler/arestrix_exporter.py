"""Exporter that converts deduplicated MCP servers into Arestrix-compatible catalog, snapshot, and reports."""

from __future__ import annotations

import json
import os
from collections import defaultdict
from datetime import datetime, timezone
from typing import Any

from dedup import deduplicate_and_select_canonical

DEFAULT_LIBRARY_ID = "library-arestrix-mcp-catalog"


def to_arestrix_mcp_asset(
    item: dict[str, Any],
    *,
    library_id: str = DEFAULT_LIBRARY_ID,
    now_iso: str,
) -> dict[str, Any]:
    """Build an ILibraryAsset object 100% compatible with src/core/libraries/types.ts & mcpAssetAdapter.ts."""
    server_id = item["server_id"]
    asset_id = f"asset-{library_id}-mcp-{server_id}"[:120]
    owner = item["source_repo"].split("/")[0] if "/" in item["source_repo"] else "community"
    install_hash = item["install_hash"]

    audit = dict(item["audit"])
    audit["auditedAt"] = now_iso
    audit["hash"] = install_hash

    payload = dict(item["payload"])
    # Ensure cachedTools carry consistent serverId
    tools = []
    for t in payload.get("cachedTools") or []:
        tools.append(
            {
                "name": t["name"],
                "description": t["description"],
                "inputSchema": t.get("inputSchema") or {},
                "serverId": server_id,
            }
        )
    payload["cachedTools"] = tools

    return {
        "id": asset_id,
        "libraryId": library_id,
        "kind": "mcp",
        "name": item["name"],
        "description": item["description"],
        "category": item["subdomain"],
        "domain": item["domain"],
        "subdomain": item["subdomain"],
        "bundledInProduct": False,
        "qualityScore": item["quality_score"],
        "selectionScore": item["selection_score"],
        "version": item["version"],
        "license": item["license"],
        "status": "available",
        "origin": {
            "bundledInProduct": False,
            "sourceKind": "github",
            "libraryId": library_id,
            "assetId": asset_id,
            "templateId": server_id,
            "repo": item["source_repo"],
            "path": item["source_path"],
            "url": item["source_url"],
            "license": item["license"],
            "canonicalKey": item["canonical_key"],
            "installVersion": item["version"],
            "installHash": install_hash,
        },
        "provenance": {
            "sourceKind": "github",
            "sourceId": f"github:{item['source_repo']}",
            "repo": item["source_repo"],
            "path": item["source_path"],
            "url": item["source_url"],
            "authority": owner,
            "importedAt": now_iso,
        },
        "audit": audit,
        "payload": payload,
        "createdAt": now_iso,
        "updatedAt": now_iso,
    }


def export_mcp_catalog(
    *,
    raw_items: list[dict[str, Any]],
    skipped_repos: list[dict[str, Any]],
    output_dir: str,
    export_samples: bool = True,
    sample_limit: int = 15,
    similarity_threshold: float = 0.58,
) -> dict[str, Any]:
    """Deduplicate raw MCP candidates and write catalog JSON, Arestrix library snapshot, report MD, and samples."""
    os.makedirs(output_dir, exist_ok=True)
    now_iso = datetime.now(timezone.utc).isoformat()

    canonical_items, multi_clusters = deduplicate_and_select_canonical(
        raw_items, similarity_threshold=similarity_threshold
    )

    # Taxonomy summary (2 levels: Domain -> Subdomain -> count)
    taxonomy_summary: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    verdict_counts = {"APPROVE": 0, "CAUTION": 0, "REJECT": 0}
    transport_counts = {"stdio": 0, "sse": 0}
    total_tools_discovered = 0

    arestrix_assets: list[dict[str, Any]] = []
    for item in canonical_items:
        taxonomy_summary[item["domain"]][item["subdomain"]] += 1
        v = item["audit"]["verdict"]
        verdict_counts[v] = verdict_counts.get(v, 0) + 1
        tr = item["payload"]["transport"]
        transport_counts[tr] = transport_counts.get(tr, 0) + 1
        total_tools_discovered += item["tools_count"]
        arestrix_assets.append(to_arestrix_mcp_asset(item, now_iso=now_iso))

    taxonomy_dict = {
        dom: dict(sorted(subs.items(), key=lambda kv: kv[1], reverse=True))
        for dom, subs in sorted(taxonomy_summary.items())
    }

    variants_collapsed = sum(len(c["variants"]) for c in multi_clusters)

    catalog_doc = {
        "generated_at": now_iso,
        "raw_items_collected": len(raw_items),
        "canonical_items_count": len(canonical_items),
        "semantic_clusters_merged": len(multi_clusters),
        "variants_collapsed": variants_collapsed,
        "transport_counts": transport_counts,
        "security_verdicts": verdict_counts,
        "total_tools_discovered": total_tools_discovered,
        "skipped_sources_count": len(skipped_repos),
        "skipped_sources": skipped_repos,
        "taxonomy_summary": taxonomy_dict,
        "semantic_clusters": multi_clusters,
        "catalog": canonical_items,
    }

    catalog_path = os.path.join(output_dir, "arestrix_mcp_catalog.json")
    with open(catalog_path, "w", encoding="utf-8") as f:
        json.dump(catalog_doc, f, indent=2, ensure_ascii=False)

    # Ready-to-import Arestrix Library Snapshot (arestrix-library v1) excluding REJECT assets
    importable_assets = [a for a in arestrix_assets if a["audit"]["verdict"] != "REJECT"]
    snapshot_doc = {
        "format": "arestrix-library",
        "version": 1,
        "exportedAt": now_iso,
        "library": {
            "id": DEFAULT_LIBRARY_ID,
            "name": "Arestrix Curated MCP Servers",
            "description": "Permissive-licensed, deduplicated, security-triaged MCP servers discovered by mcp_crawler.",
            "scope": "studio",
            "kind": "repository",
            "trust": "community",
            "isReadOnly": False,
            "createdAt": now_iso,
            "updatedAt": now_iso,
        },
        "assets": importable_assets,
    }
    snapshot_path = os.path.join(output_dir, "arestrix_mcp_library_snapshot.json")
    with open(snapshot_path, "w", encoding="utf-8") as f:
        json.dump(snapshot_doc, f, indent=2, ensure_ascii=False)

    # Sample standalone MCP JSON assets (APPROVE/CAUTION only, <= 64 KiB)
    samples_written = 0
    if export_samples:
        samples_dir = os.path.join(output_dir, "sample_mcps")
        os.makedirs(samples_dir, exist_ok=True)
        for asset in importable_assets:
            if samples_written >= sample_limit:
                break
            server_id = asset["origin"]["templateId"]
            out_file = os.path.join(samples_dir, f"{server_id}.json")
            encoded = json.dumps(asset, indent=2, ensure_ascii=False)
            if len(encoded.encode("utf-8")) <= 65536:
                with open(out_file, "w", encoding="utf-8") as f:
                    f.write(encoded)
                samples_written += 1

    # Human-readable Markdown report
    report_lines = [
        "# Arestrix MCP Crawler — Relatório de Catálogo Canônico",
        "",
        f"- **Gerado em:** `{now_iso}`",
        f"- **Candidatos coletados (licença permissiva):** `{len(raw_items)}`",
        f"- **Servidores MCP canônicos (pós-dedup):** `{len(canonical_items)}`",
        f"- **Clusters semânticos consolidados:** `{len(multi_clusters)}` (`{variants_collapsed}` variantes descartadas)",
        f"- **Transportes:** `stdio={transport_counts['stdio']}`, `sse={transport_counts['sse']}`",
        f"- **Pré-triagem de segurança:** `APPROVE={verdict_counts['APPROVE']}`, `CAUTION={verdict_counts['CAUTION']}`, `REJECT={verdict_counts['REJECT']}`",
        f"- **Ferramentas (`cachedTools`) descobertas:** `{total_tools_discovered}`",
        f"- **Fontes ignoradas (licença não-permissiva / erro / sem config):** `{len(skipped_repos)}`",
        "",
        "## Taxonomia de 2 Níveis (`Domínio → Subdomínio`)",
        "",
        "| Domínio | Subdomínio | MCPs Canônicos |",
        "| --- | --- | ---: |",
    ]
    for dom, subs in taxonomy_dict.items():
        for sub, count in subs.items():
            report_lines.append(f"| {dom} | {sub} | {count} |")

    report_lines.extend(
        [
            "",
            "## Deduplicação Semântica (Seleção Canônica)",
            "",
        ]
    )
    if multi_clusters:
        report_lines.extend(
            [
                "| Serviço Canônico | Vencedor | Score | Variantes Descartadas |",
                "| --- | --- | ---: | --- |",
            ]
        )
        for cl in multi_clusters[:20]:
            w = cl["winner"]
            losers = "; ".join(
                f"`{v['source_repo']}` ({v['name']}, score={v['selection_score']:.1f}: {v['reason_not_selected']})"
                for v in cl["variants"]
            )
            report_lines.append(
                f"| `{cl['canonical_key']}` | `{w['source_repo']}` (`{w['name']}`) | {w['selection_score']:.1f} | {losers} |"
            )
    else:
        report_lines.append("_Nenhum cluster com múltiplos candidatos foi encontrado nesta execução._")

    report_lines.extend(
        [
            "",
            "## Top 25 MCPs Canônicos (por `selection_score`)",
            "",
            "| # | MCP | Domínio → Subdomínio | Transporte | Licença | Tools | Segurança | Score | Origem |",
            "| ---: | --- | --- | --- | --- | ---: | --- | ---: | --- |",
        ]
    )
    for idx, item in enumerate(canonical_items[:25], start=1):
        report_lines.append(
            f"| {idx} | `{item['name']}` | {item['domain']} → {item['subdomain']} | `{item['payload']['transport']}` "
            f"| `{item['license']}` | {item['tools_count']} | `{item['audit']['verdict']}` "
            f"| {item['selection_score']:.1f} | [{item['source_repo']}]({item['source_url']}) |"
        )

    report_path = os.path.join(output_dir, "arestrix_mcp_report.md")
    with open(report_path, "w", encoding="utf-8") as f:
        f.write("\n".join(report_lines) + "\n")

    return {
        "catalog_path": catalog_path,
        "snapshot_path": snapshot_path,
        "report_path": report_path,
        "samples_written": samples_written,
        "raw_count": len(raw_items),
        "canonical_count": len(canonical_items),
        "clusters_merged": len(multi_clusters),
        "variants_collapsed": variants_collapsed,
        "verdict_counts": verdict_counts,
        "total_tools_discovered": total_tools_discovered,
    }
