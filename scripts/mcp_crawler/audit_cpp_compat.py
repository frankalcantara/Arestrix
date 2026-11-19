"""Audit discovered canonical MCP servers against Arestrix C++20/23 runtime constraints."""

from __future__ import annotations

import json
import os
import re
from collections import Counter

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
CATALOG_PATH = os.path.join(SCRIPT_DIR, "output", "arestrix_mcp_catalog.json")

SAFE_SHELL_BAD = set('"&|<>^%!')
AUDIT_META_BAD = set("&|;><`")
WIN_CMD_WRAPPED = {"npx", "npm", "uvx"}
WIN_NATIVE_EXES = {"node", "python", "python3", "docker", "uv", "go", "java", "dotnet", "deno"}
WIN_BROKEN_CMD_SCRIPTS = {"pnpm", "bunx", "yarn", "pipx", "npx.cmd", "npm.cmd", "uvx.cmd"}


def audit_catalog() -> None:
    with open(CATALOG_PATH, encoding="utf-8") as f:
        doc = json.load(f)

    items = doc["catalog"]
    print(f"Total canonical MCPs: {len(items)}")

    stats: Counter[str] = Counter()
    hard_rows = []
    adapt_rows = []
    clean_rows = []

    for it in items:
        p = it["payload"]
        tr = p["transport"]
        cmd = (p.get("command") or "").strip()
        args = p.get("args") or []
        url = (p.get("url") or "").strip()
        env = p.get("env") or {}
        tools = p.get("cachedTools") or []
        sid = it["server_id"]

        hard_errors: list[str] = []
        adaptations: list[str] = []

        if not re.match(r"^[A-Za-z0-9_.-]{1,64}$", sid) or sid.startswith("arestrix."):
            hard_errors.append(f"invalid_server_id:{sid}")

        if tr == "sse":
            if not url.startswith("https://"):
                hard_errors.append(f"url_not_https:{url}")
            if any(h in url.lower() for h in ("localhost", "127.0.0.1", "0.0.0.0", "host.docker.internal", "<", "{")):
                hard_errors.append(f"url_non_public_or_placeholder:{url}")
            m_port = re.match(r"^https://[^/]+:(\d+)", url)
            if m_port and m_port.group(1) != "443":
                hard_errors.append(f"url_non_443_port:{url}")
            if url.rstrip("/").endswith("/sse"):
                adaptations.append(f"legacy_sse_path_risk:{url}")
        else:
            cmd_low = cmd.lower()
            if cmd_low in WIN_BROKEN_CMD_SCRIPTS:
                hard_errors.append(f"win_cmd_script_not_wrapped:{cmd}")
            elif "/" in cmd or "\\" in cmd or cmd_low not in (WIN_CMD_WRAPPED | WIN_NATIVE_EXES):
                hard_errors.append(f"unusual_or_local_binary:{cmd}")
            if cmd_low in WIN_CMD_WRAPPED:
                for a in args:
                    if any(c in SAFE_SHELL_BAD for c in a):
                        hard_errors.append(f"safeMcpShellArg_reject:{a}")
                non_flag_no_at = [a for a in args if a and not a.startswith("-") and "@" not in a]
                if len(non_flag_no_at) >= 5:
                    hard_errors.append(f"auditMcpValue_reject_unpinned_args({len(non_flag_no_at)})")
                elif non_flag_no_at:
                    adaptations.append(f"auditMcpValue_caution_unpinned({len(non_flag_no_at)}:{non_flag_no_at[0]})")
            for a in args:
                if any(c in AUDIT_META_BAD for c in a):
                    hard_errors.append(f"audit_metachar:{a}")
                if any(ph in a.lower() for ph in ("<your", "your_", "/path/to", "path/to", "replace_me", "example.com", "my-")):
                    adaptations.append(f"placeholder_arg:{a}")
            if cmd_low == "docker":
                for i, a in enumerate(args):
                    if a == "-e" and i + 1 < len(args):
                        ev = args[i + 1].split("=")[0]
                        if ev not in env:
                            adaptations.append(f"docker_scrubSecrets_missing_env:{ev}")
                adaptations.append("requires_docker_daemon")

        seen_clean: set[str] = set()
        for t in tools:
            tn = t["name"]
            if tn.lower().startswith("library_") or tn.lower().startswith("arestrix_"):
                adaptations.append(f"reserved_tool_prefix:{tn}")
            clean = re.sub(r"[^A-Za-z0-9_-]", "_", tn)
            if clean in seen_clean:
                adaptations.append(f"tool_name_collision:{tn}")
            seen_clean.add(clean)

        row = (it["name"], it["source_repo"], tr, cmd or url, args, hard_errors, adaptations)
        if hard_errors:
            stats["INCOMPATIBLE_OR_NEEDS_FIX"] += 1
            hard_rows.append(row)
        elif adaptations:
            stats["COMPATIBLE_WITH_ADAPTATION"] += 1
            adapt_rows.append(row)
        else:
            stats["READY_CLEAN"] += 1
            clean_rows.append(row)

    print("Stats:", dict(stats))
    print(f"\n=== HARD ISSUES ({len(hard_rows)}) ===")
    for name, repo, tr, ep, args, hard, adapt in hard_rows:
        print(f"  [{tr}] {name} ({repo}) -> {ep} {args[:4]} | HARD={hard} | ADAPT={adapt}")

    print(f"\n=== ADAPTATIONS ({len(adapt_rows)}) ===")
    for name, repo, tr, ep, args, hard, adapt in adapt_rows:
        print(f"  [{tr}] {name} ({repo}) -> {ep} {args[:3]} | ADAPT={adapt[:3]}")


if __name__ == "__main__":
    audit_catalog()
