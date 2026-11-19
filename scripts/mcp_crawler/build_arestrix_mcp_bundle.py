"""Build C++20/23-compatible bundled MCP catalog for Arestrix (src/core/libraries/catalog/bundledMcpServers.json)."""

from __future__ import annotations

import json
import os
import re
import sys
from datetime import datetime, timezone
from typing import Any

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

from parser import (
    ENV_REF_RE,
    compute_mcp_install_hash,
    is_permissive_license,
    run_mcp_security_audit,
)

CATALOG_INPUT = os.path.join(SCRIPT_DIR, "output", "arestrix_mcp_catalog.json")
BUNDLE_OUTPUT = os.path.join(REPO_ROOT, "src", "core", "libraries", "catalog", "bundledMcpServers.json")

DOMAIN_ICONS: dict[str, str] = {
    "Desenvolvimento & Engenharia": "🛠️",
    "Dados & Persistência": "🗄️",
    "Nuvem & Infraestrutura": "☁️",
    "Comunicação & Produtividade": "💬",
    "Web, Busca & Conhecimento": "🌐",
    "Negócios & Finanças": "💳",
}

SAFE_SHELL_BAD = set('"&|<>^%!')
AUDIT_META_BAD = set("&|;><`")


def _make_bundled_server_id(raw_key: str, used: set[str]) -> str:
    """Create a C++-safe serverId (<= 52 chars, [a-z0-9_.-], never starting with arestrix.)."""
    slug = re.sub(r"[^a-z0-9_.-]+", "-", raw_key.strip().lower()).strip("-.")
    slug = re.sub(r"^mcp-", "", slug)
    slug = re.sub(r"-mcp(-server)?$", "", slug)
    if not slug:
        slug = "server"
    base = f"mcp-{slug}"[:48].rstrip("-.")
    candidate = base
    n = 2
    while candidate in used:
        candidate = f"{base[:44]}-{n}"
        n += 1
    used.add(candidate)
    return candidate


def _adapt_payload_for_cpp(item: dict[str, Any], server_id: str) -> tuple[dict[str, Any], list[str]]:
    """Apply deterministic C++20/23 compatibility adaptations to an MCP payload."""
    payload = dict(item["payload"])
    notes: list[str] = []
    tr = payload.get("transport", "stdio")

    if tr == "stdio":
        cmd = str(payload.get("command") or "").strip()
        args = [str(a) for a in (payload.get("args") or [])]
        env = dict(payload.get("env") or {})

        # Adapt 1: Replace /absolute/path/to/... with package name for uvx --from
        if cmd == "uvx" and len(args) >= 3 and args[0] == "--from" and "/path/to/" in args[1]:
            pkg = args[2]
            if "@" not in pkg:
                pkg = f"{pkg}@latest"
            args = [pkg]
            notes.append("adapted_local_from_path_to_uvx_package")

        # Adapt 2: Replace bracket placeholders like [your dsql cluster endpoint] with ${VAR}
        new_args: list[str] = []
        for idx, a in enumerate(args):
            if a.startswith("[") and a.endswith("]"):
                prev = args[idx - 1] if idx > 0 else ""
                if prev.startswith("--"):
                    var_name = re.sub(r"[^A-Za-z0-9_]+", "_", prev.lstrip("-")).upper()
                else:
                    var_name = re.sub(r"[^A-Za-z0-9_]+", "_", a.strip("[]")).upper()[:32]
                if not var_name or not re.match(r"^[A-Za-z_]", var_name):
                    var_name = f"MCP_ARG_{idx}"
                new_args.append(f"${{{var_name}}}")
                notes.append(f"placeholder_to_env_ref:{var_name}")
            else:
                new_args.append(a)
        args = new_args

        # Adapt 3: Remove bare --debug / local log file flags if present in awslabs examples
        if "--debug" in args:
            cleaned_args: list[str] = []
            skip_next = False
            for a in args:
                if skip_next:
                    skip_next = False
                    continue
                if a in ("--log-file", "--log_file"):
                    skip_next = True
                    continue
                if a == "--debug" or a.endswith(".log"):
                    continue
                cleaned_args.append(a)
            args = cleaned_args
            notes.append("stripped_debug_log_flags")

        # Adapt 4: Ensure first package arg for npx/uvx has an explicit version or @latest tag
        if cmd in ("npx", "uvx") and args:
            for idx, a in enumerate(args):
                if a.startswith("-"):
                    continue
                if "@" not in a[1:]:
                    ver = str(item.get("version") or "").strip()
                    if re.match(r"^[0-9]+\.[0-9]+", ver) and ver != "1.0.0":
                        args[idx] = f"{a}@{ver}"
                    else:
                        args[idx] = f"{a}@latest"
                    notes.append(f"tagged_package:{args[idx]}")
                break

        # Adapt 5: Ensure docker -e VAR is also in env map (Process::start scrubSecrets)
        if cmd.lower() == "docker":
            for idx, a in enumerate(args):
                if a == "-e" and idx + 1 < len(args):
                    ev = args[idx + 1].split("=")[0].strip()
                    if re.match(r"^[A-Za-z_][A-Za-z0-9_]*$", ev) and ev not in env:
                        env[ev] = f"${{{ev}}}"
                        notes.append(f"docker_env_passthrough:{ev}")

        payload["command"] = cmd
        payload["args"] = args
        if env:
            payload["env"] = env
        payload["url"] = ""
    else:
        payload["command"] = ""
        payload["args"] = []

    # Normalize cachedTools serverId to the new bundled server_id
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
    return payload, notes


def verify_cpp_entry(entry: dict[str, Any]) -> list[str]:
    """Verify zero C++20/23 runtime incompatibilities."""
    errs: list[str] = []
    sid = entry["serverId"]
    aid = entry["id"]
    p = entry["payload"]

    if not re.match(r"^[A-Za-z0-9_.-]{1,64}$", sid) or sid.startswith("arestrix."):
        errs.append(f"invalid serverId: {sid}")
    if len(aid) > 64 or ":" in aid:
        errs.append(f"invalid assetId: {aid}")
    if not is_permissive_license(entry["license"]):
        errs.append(f"non-permissive license: {entry['license']}")

    if p["transport"] == "sse":
        url = p.get("url") or ""
        if not url.startswith("https://"):
            errs.append(f"non-https url: {url}")
        if any(h in url.lower() for h in ("localhost", "127.0.0.1", "0.0.0.0", "<", "{")):
            errs.append(f"non-public host: {url}")
        if url.rstrip("/").endswith("/sse"):
            errs.append(f"legacy sse endpoint unsupported by C++ Streamable HTTP: {url}")
    else:
        cmd = (p.get("command") or "").lower()
        if cmd not in ("npx", "npm", "uvx", "node", "python", "python3", "docker", "uv"):
            errs.append(f"unsupported command: {cmd}")
        for a in p.get("args") or []:
            if cmd in ("npx", "npm", "uvx") and any(c in SAFE_SHELL_BAD for c in a):
                errs.append(f"safeMcpShellArg bad char in {a}")
            if any(c in AUDIT_META_BAD for c in a):
                errs.append(f"audit metachar in {a}")
            if "github_pat_" in a or "sk-" in a or "ghp_" in a:
                errs.append(f"literal secret-like token in arg: {a}")

    for k, v in (p.get("env") or {}).items():
        if v and not ENV_REF_RE.match(v):
            errs.append(f"literal env {k}={v}")
    for k, v in (p.get("headers") or {}).items():
        if v and not ENV_REF_RE.match(v):
            errs.append(f"literal header {k}={v}")

    return errs


def build_bundle() -> int:
    with open(CATALOG_INPUT, encoding="utf-8") as f:
        doc = json.load(f)

    used_ids: set[str] = set()
    bundled_items: list[dict[str, Any]] = []
    adapted_count = 0
    total_errors = 0

    for item in doc["catalog"]:
        if item.get("audit", {}).get("verdict") == "REJECT":
            continue

        raw_key = item.get("canonical_key") or item.get("server_id") or item["name"]
        server_id = _make_bundled_server_id(raw_key, used_ids)
        asset_id = f"asset-{server_id}"  # guaranteed <= 58 chars, valid for McpToolbox & LibraryCatalog

        payload, notes = _adapt_payload_for_cpp(item, server_id)
        if notes:
            adapted_count += 1

        install_hash = compute_mcp_install_hash(
            server_id=server_id,
            transport=payload["transport"],
            command=payload.get("command", ""),
            args=payload.get("args", []),
            url=payload.get("url", ""),
            env=payload.get("env", {}),
            headers=payload.get("headers", {}),
            cached_tools=payload.get("cachedTools", []),
        )

        # Re-run security audit on adapted payload
        audit = run_mcp_security_audit(
            readme="",
            transport=payload["transport"],
            command=payload.get("command", ""),
            args=payload.get("args", []),
            url=payload.get("url", ""),
            env=payload.get("env", {}),
            headers=payload.get("headers", {}),
            cached_tools=payload.get("cachedTools", []),
        )
        audit["hash"] = install_hash
        if audit["verdict"] == "REJECT":
            continue

        domain = item["domain"]
        subdomain = item["subdomain"]
        icon = DOMAIN_ICONS.get(domain, "🔌")
        default_tool = payload["cachedTools"][0]["name"] if payload.get("cachedTools") else ""

        entry = {
            "id": asset_id,
            "serverId": server_id,
            "name": item["name"],
            "description": item["description"],
            "domain": domain,
            "subdomain": subdomain,
            "category": subdomain,
            "icon": icon,
            "version": item.get("version", "1.0.0"),
            "license": item["license"],
            "bundledInProduct": True,
            "qualityScore": item["quality_score"],
            "selectionScore": item["selection_score"],
            "defaultTool": default_tool,
            "cppAdaptations": notes,
            "origin": {
                "bundledInProduct": True,
                "sourceKind": "builtin",
                "libraryId": "library-arestrix",
                "assetId": asset_id,
                "templateId": server_id,
                "repo": item["source_repo"],
                "path": item["source_path"],
                "url": item["source_url"],
                "license": item["license"],
                "canonicalKey": item.get("canonical_key", server_id),
                "installVersion": item.get("version", "1.0.0"),
                "installHash": install_hash,
            },
            "audit": audit,
            "payload": payload,
        }

        errs = verify_cpp_entry(entry)
        if errs:
            print(f"[SKIP-CPP] {entry['name']} ({item['source_repo']}): {errs}")
            total_errors += 1
            continue

        bundled_items.append(entry)

    os.makedirs(os.path.dirname(BUNDLE_OUTPUT), exist_ok=True)
    bundle = {
        "format": "arestrix-bundled-mcps",
        "version": "1.0.0",
        "generatedAt": datetime.now(timezone.utc).isoformat(),
        "totalCount": len(bundled_items),
        "items": bundled_items,
    }
    with open(BUNDLE_OUTPUT, "w", encoding="utf-8") as f:
        json.dump(bundle, f, indent=2, ensure_ascii=False)

    print(f"Built {BUNDLE_OUTPUT}")
    print(f"Total C++20/23-verified bundled MCPs: {len(bundled_items)} (adapted: {adapted_count}, skipped_incompatible: {total_errors})")
    return 0


if __name__ == "__main__":
    raise SystemExit(build_bundle())
