"""MCP metadata parser, secret sanitizer, tool discoverer, security pre-triage, and taxonomy classifier."""

from __future__ import annotations

import hashlib
import json
import math
import re
from typing import Any

from sources import LICENSE_NORMALIZATION, PERMISSIVE_LICENSES, TAXONOMY

MCP_AUDIT_POLICY_VERSION = "mcp-scanner-v1"

ENV_REF_RE = re.compile(r"^\$\{[A-Za-z_][A-Za-z0-9_]*\}$")
SAFE_HEADER_KEY_RE = re.compile(r"^[A-Za-z0-9_-]{1,100}$")
SAFE_SERVER_ID_RE = re.compile(r"[^a-z0-9_.-]+")
TOOL_NAME_RE = re.compile(r"^[a-zA-Z][a-zA-Z0-9_.-]{1,63}$")

RESERVED_HEADERS = {
    "host",
    "content-length",
    "connection",
    "transfer-encoding",
    "mcp-session-id",
    "mcp-protocol-version",
}

RESERVED_NATIVE_TOOL_NAMES = {
    "read_file",
    "write_file",
    "list_directory",
    "file_info",
    "make_directory",
    "search_text",
    "http_get",
    "http_post_json",
    "json_query",
    "csv_to_json",
    "now",
    "git_status",
    "sql_query",
    "sql_execute",
    "memory_get",
    "memory_set",
}

SHELL_COMMANDS = {"cmd", "cmd.exe", "powershell", "powershell.exe", "pwsh", "bash", "sh", "zsh"}
UNCONFINED_ROOT_ARGS = {"/", "\\", "c:", "c:/", "c:\\"}
SHELL_METACHAR_RE = re.compile(r"[&|<>^%!`;]")

HIDDEN_UNICODE_RE = re.compile(r"[\u200b-\u200f\u202a-\u202e\u2060-\u206f\ufeff]")
PIPE_TO_SHELL_RE = re.compile(r"(curl|wget)\s+[^\n|]+\|\s*(ba)?sh", re.IGNORECASE)
REAL_SECRET_RE = re.compile(
    r"(sk-[A-Za-z0-9]{20,}|ghp_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,}|"
    r"AIza[0-9A-Za-z_-]{20,}|xox[baprs]-[0-9A-Za-z-]{15,}|AKIA[0-9A-Z]{16})"
)
PROMPT_INJECTION_RE = re.compile(
    r"(ignore\s+all\s+(previous|prior)\s+instructions|system\s+override|"
    r"developer\s+mode\s+enabled|unrestricted\s+mode|bypass\s+(all\s+)?safety|jailbreak|"
    r"before\s+(calling|executing|running)|always\s+pass\s+the\s+content\s+of|"
    r"leak|exfiltrate|send\s+to\s+http|upload\s+secret)",
    re.IGNORECASE,
)


def normalize_license(raw: str | None) -> str | None:
    """Normalize a raw license string to canonical SPDX or return None."""
    if not raw:
        return None
    cleaned = raw.strip().strip('"').strip("'")
    upper = cleaned.upper()
    if upper in ("NOASSERTION", "NONE", "UNKNOWN", "UNSPECIFIED", "OTHER", "PROPRIETARY") or upper.startswith("SEE LICENSE"):
        return None
    if upper in LICENSE_NORMALIZATION:
        return LICENSE_NORMALIZATION[upper]
    # Handle SPDX OR expressions if all parts are permissive
    if " OR " in upper:
        parts = [p.strip(" ()") for p in upper.split(" OR ")]
        normalized_parts = [LICENSE_NORMALIZATION.get(p) for p in parts]
        if all(normalized_parts):
            return " OR ".join(normalized_parts)  # type: ignore[arg-type]
    return cleaned


def is_permissive_license(lic: str | None) -> bool:
    """Strictly verify whether a license is MIT or equivalent permissive SPDX."""
    norm = normalize_license(lic)
    if not norm:
        return False
    upper = norm.upper()
    if upper in PERMISSIVE_LICENSES:
        return True
    if " OR " in upper:
        parts = [p.strip(" ()") for p in upper.split(" OR ")]
        return all(p in PERMISSIVE_LICENSES for p in parts)
    return False


def detect_license_from_text(text: str | None) -> str | None:
    """Deterministically detect permissive SPDX license from LICENSE file contents."""
    if not text:
        return None
    sample = text[:2500]
    lower = sample.lower()
    if "mit license" in lower or (
        "permission is hereby granted, free of charge" in lower
        and "without restriction" in lower
    ):
        return "MIT"
    if "apache license" in lower and "version 2.0" in lower:
        return "Apache-2.0"
    if "isc license" in lower or (
        "permission to use, copy, modify, and/or distribute this software" in lower
    ):
        return "ISC"
    if "redistribution and use in source and binary forms" in lower:
        if "neither the name of" in lower or "names of its contributors" in lower:
            return "BSD-3-Clause"
        return "BSD-2-Clause"
    if "zero-clause bsd" in lower or "0bsd" in lower:
        return "0BSD"
    if "this is free and unencumbered software released into the public domain" in lower:
        return "Unlicense"
    if "cc0 1.0 universal" in lower:
        return "CC0-1.0"
    return None


def make_safe_server_id(raw_name: str) -> str:
    """Produce a safe serverId matching /^[a-z0-9_.-]{1,64}$/."""
    s = raw_name.strip().lower()
    s = re.sub(r"^@[^/]+/", "", s)
    s = SAFE_SERVER_ID_RE.sub("-", s).strip("-.")
    if not s:
        s = "mcp-server"
    return s[:64]


def _to_env_var_name(key: str, default_suffix: str = "TOKEN") -> str:
    var = re.sub(r"[^A-Za-z0-9_]+", "_", key.strip()).upper().strip("_")
    if not var or not re.match(r"^[A-Za-z_]", var):
        var = f"MCP_{var or default_suffix}"
    return var


def sanitize_env_map(raw_env: dict[str, Any] | None) -> dict[str, str]:
    """Ensure ZERO literal secrets in env: every value becomes ${VAR_NAME}."""
    if not isinstance(raw_env, dict):
        return {}
    out: dict[str, str] = {}
    for k, v in raw_env.items():
        if not isinstance(k, str) or not k.strip():
            continue
        key = k.strip()
        val = str(v).strip() if v is not None else ""
        if ENV_REF_RE.match(val):
            out[key] = val
            continue
        # Extract inner ${VAR} if embedded (e.g. "${MY_VAR}" or "$MY_VAR")
        m = re.search(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}", val)
        if m:
            out[key] = f"${{{m.group(1)}}}"
            continue
        out[key] = f"${{{_to_env_var_name(key)}}}"
    return out


def sanitize_headers_map(raw_headers: dict[str, Any] | None) -> dict[str, str]:
    """Ensure ZERO literal secrets in headers and valid header keys for Arestrix C++/TS."""
    if not isinstance(raw_headers, dict):
        return {}
    out: dict[str, str] = {}
    for k, v in raw_headers.items():
        if not isinstance(k, str):
            continue
        key = k.strip()
        if not SAFE_HEADER_KEY_RE.match(key) or key.lower() in RESERVED_HEADERS:
            continue
        val = str(v).strip() if v is not None else ""
        if ENV_REF_RE.match(val):
            out[key] = val
            continue
        m = re.search(r"\$\{([A-Za-z_][A-Za-z0-9_]*)\}", val)
        if m:
            out[key] = f"${{{m.group(1)}}}"
            continue
        out[key] = f"${{{_to_env_var_name(key, 'HEADER_TOKEN')}}}"
    return out


def _normalize_text_for_hash(text: str | None) -> str:
    if not text:
        return ""
    lines = text.replace("\r\n", "\n").replace("\r", "\n").split("\n")
    return "\n".join(line.rstrip() for line in lines).strip()


def _canonicalize_json(obj: Any) -> str:
    if obj is None:
        return "{}"
    return json.dumps(obj, sort_keys=True, separators=(",", ":"), ensure_ascii=False)


def compute_mcp_install_hash(
    *,
    server_id: str,
    transport: str,
    command: str,
    args: list[str],
    url: str,
    env: dict[str, str],
    headers: dict[str, str],
    cached_tools: list[dict[str, Any]],
) -> str:
    """Compute SHA-256 hash matching src/core/skills/McpHasher.ts exactly."""
    normalized_tools = sorted(
        [
            {
                "name": str(t.get("name") or "").strip().lower(),
                "description": _normalize_text_for_hash(str(t.get("description") or "")),
                "inputSchema": _canonicalize_json(t.get("inputSchema") or {}),
            }
            for t in cached_tools
        ],
        key=lambda t: t["name"],
    )
    canonical = {
        "id": server_id.strip().lower(),
        "transport": (transport or "stdio").strip().lower(),
        "command": (command or "").strip(),
        "args": [str(a).strip() for a in (args or [])],
        "url": (url or "").strip(),
        "envKeys": sorted(k.strip() for k in (env or {}).keys()),
        "headerKeys": sorted(k.strip().lower() for k in (headers or {}).keys()),
        "tools": normalized_tools,
    }
    encoded = json.dumps(canonical, separators=(",", ":"), ensure_ascii=False).encode("utf-8")
    return hashlib.sha256(encoded).hexdigest()


def _strip_json_comments(text: str) -> str:
    """Remove // line comments and trailing commas commonly found in README JSON snippets."""
    no_line_comments = re.sub(r"(?m)^\s*//.*$", "", text)
    no_inline_comments = re.sub(r'(?<=[\[\]{},"\d])\s+//[^\n]*', "", no_line_comments)
    no_trailing_commas = re.sub(r",\s*([}\]])", r"\1", no_inline_comments)
    return no_trailing_commas


def _extract_json_blocks(readme: str) -> list[dict[str, Any]]:
    blocks: list[dict[str, Any]] = []
    for match in re.finditer(r"```(?:jsonc?|json5)?\s*\n([\s\S]*?)```", readme, re.IGNORECASE):
        raw = match.group(1).strip()
        if not raw.startswith("{"):
            continue
        cleaned = _strip_json_comments(raw)
        try:
            parsed = json.loads(cleaned)
            if isinstance(parsed, dict):
                blocks.append(parsed)
        except Exception:
            continue
    return blocks


def _extract_server_entry_from_dict(obj: dict[str, Any]) -> dict[str, Any] | None:
    """Extract a single MCP server config dict from mcpServers / servers / direct config."""
    for container_key in ("mcpServers", "servers"):
        container = obj.get(container_key)
        if isinstance(container, dict):
            # Prefer npx / uvx / https entries over docker when multiple examples exist
            candidates = [v for v in container.values() if isinstance(v, dict)]
            for pref in ("npx", "uvx", "https://"):
                for cand in candidates:
                    cmd = str(cand.get("command") or "")
                    url = str(cand.get("url") or cand.get("serverUrl") or "")
                    if cmd == pref or url.startswith(pref):
                        return cand
            if candidates:
                return candidates[0]
    if "command" in obj or "url" in obj or "serverUrl" in obj:
        return obj
    # MCP Registry server.json schema (packages / remotes)
    packages = obj.get("packages")
    if isinstance(packages, list) and packages:
        pkg = packages[0]
        if isinstance(pkg, dict):
            reg = str(pkg.get("registryType") or pkg.get("registry_name") or "").lower()
            ident = str(pkg.get("identifier") or pkg.get("name") or "").strip()
            ver = str(pkg.get("version") or "").strip()
            env_vars = {}
            for ev in pkg.get("environmentVariables") or []:
                if isinstance(ev, dict) and ev.get("name"):
                    env_vars[ev["name"]] = f"${{{ev['name']}}}"
            if ident and reg in ("npm", ""):
                pinned = f"{ident}@{ver}" if ver and "@" not in ident[1:] else ident
                return {"command": "npx", "args": ["-y", pinned], "env": env_vars}
            if ident and reg == "pypi":
                pinned = f"{ident}@{ver}" if ver else ident
                return {"command": "uvx", "args": [pinned], "env": env_vars}
    remotes = obj.get("remotes")
    if isinstance(remotes, list) and remotes:
        rem = remotes[0]
        if isinstance(rem, dict) and rem.get("url"):
            return {"transport": "sse", "url": str(rem["url"]), "headers": rem.get("headers") or {}}
    return None


def _pin_package_args(command: str, args: list[str], pkg_name: str | None, pkg_version: str | None) -> list[str]:
    """Pin npx/uvx package argument with @version when known and unpinned, satisfying McpSecurityScanner."""
    if command not in ("npx", "uvx", "bunx") or not pkg_version or not args:
        return args
    # Normalize version to major.minor.patch
    ver_clean = pkg_version.strip().lstrip("^~>=<v")
    if not re.match(r"^[0-9]+\.[0-9]+", ver_clean):
        return args
    new_args = list(args)
    for idx, arg in enumerate(new_args):
        if arg.startswith("-"):
            continue
        # Check if already pinned with @<digits>.<digits>
        at_pos = arg.rfind("@")
        if at_pos > 0 and re.match(r"^[0-9]+\.[0-9]+", arg[at_pos + 1:]):
            break
        if pkg_name and (arg == pkg_name or arg.endswith("/" + pkg_name.split("/")[-1])):
            base = arg[:at_pos] if at_pos > 0 else arg
            new_args[idx] = f"{base}@{ver_clean}"
        break
    return new_args


def _sanitize_args(args: list[Any]) -> list[str]:
    cleaned: list[str] = []
    for a in args:
        s = str(a).strip()
        if not s:
            continue
        # Replace common README placeholder paths like /path/to/allowed/dir with safe workspace dot
        if re.match(r"^(/path/to/|/Users/username/|<path|YOUR_)", s, re.IGNORECASE):
            s = "."
        # Strip literal inline URLs with passwords like postgresql://user:pass@localhost/db
        if re.match(r"^(postgres(ql)?|mysql|mongodb(\+srv)?|redis)://", s, re.IGNORECASE):
            s = "${DATABASE_URL}"
        cleaned.append(s)
    return cleaned


def extract_mcp_config(
    *,
    readme: str,
    package_json: dict[str, Any] | None = None,
    pyproject_meta: dict[str, Any] | None = None,
    server_json: dict[str, Any] | None = None,
) -> dict[str, Any]:
    """Extract transport, command/args or url, sanitized env/headers from available metadata."""
    pkg_name = None
    pkg_version = None
    if isinstance(package_json, dict):
        pkg_name = package_json.get("name")
        pkg_version = package_json.get("version")
    elif isinstance(pyproject_meta, dict):
        pkg_name = pyproject_meta.get("name")
        pkg_version = pyproject_meta.get("version")

    raw_entry: dict[str, Any] | None = None
    if isinstance(server_json, dict):
        raw_entry = _extract_server_entry_from_dict(server_json)

    if not raw_entry and readme:
        for block in _extract_json_blocks(readme):
            cand = _extract_server_entry_from_dict(block)
            if cand:
                raw_entry = cand
                # Prefer non-docker stdio or https sse
                if cand.get("command") in ("npx", "uvx", "node", "python", "python3") or str(
                    cand.get("url") or ""
                ).startswith("https://"):
                    break

    transport = "stdio"
    command = ""
    args: list[str] = []
    url = ""
    env: dict[str, str] = {}
    headers: dict[str, str] = {}

    if raw_entry:
        raw_transport = str(raw_entry.get("transport") or raw_entry.get("type") or "").strip().lower()
        raw_url = str(raw_entry.get("url") or raw_entry.get("serverUrl") or "").strip()
        raw_cmd = str(raw_entry.get("command") or "").strip()
        raw_args = raw_entry.get("args") if isinstance(raw_entry.get("args"), list) else []

        if raw_transport in ("sse", "http", "streamable-http", "streamable_http") or (raw_url and not raw_cmd):
            transport = "sse"
            url = raw_url
            headers = sanitize_headers_map(raw_entry.get("headers"))
        else:
            transport = "stdio"
            command = raw_cmd
            args = _sanitize_args(raw_args)
            args = _pin_package_args(command, args, pkg_name, pkg_version)
            env = sanitize_env_map(raw_entry.get("env"))

    # Fallback to package.json / pyproject.toml if README had no structured JSON snippet
    if transport == "stdio" and not command:
        if isinstance(package_json, dict) and pkg_name:
            command = "npx"
            pinned = f"{pkg_name}@{pkg_version}" if pkg_version else pkg_name
            args = ["-y", pinned]
        elif isinstance(pyproject_meta, dict) and pkg_name:
            command = "uvx"
            pinned = f"{pkg_name}@{pkg_version}" if pkg_version else pkg_name
            args = [pinned]

    # Also detect env vars mentioned in README (e.g. `GITHUB_PERSONAL_ACCESS_TOKEN`) if env is empty
    if transport == "stdio" and not env and readme:
        env_mentions = re.findall(
            r"`([A-Z][A-Z0-9_]{3,40}_(?:API_KEY|TOKEN|SECRET|PASSWORD|URI|URL|DSN|KEY))`",
            readme,
        )
        for var in sorted(set(env_mentions))[:5]:
            env[var] = f"${{{var}}}"

    return {
        "transport": transport,
        "command": command,
        "args": args,
        "env": env,
        "url": url,
        "headers": headers,
    }


def extract_tools_from_readme(readme: str, server_id: str, max_tools: int = 40) -> list[dict[str, Any]]:
    """Discover MCP tool definitions declared in README tables, bullet lists, or headings."""
    if not readme:
        return []

    tools_by_name: dict[str, dict[str, Any]] = {}

    def _add_tool(raw_name: str, raw_desc: str) -> None:
        name = raw_name.strip().strip("`*_ ")
        if not TOOL_NAME_RE.match(name):
            return
        # Ignore common non-tool words/flags
        if name.lower() in {
            "name", "tool", "tools", "description", "parameters", "parameter", "arguments",
            "returns", "type", "required", "optional", "default", "example", "usage",
            "install", "installation", "configuration", "license", "mit", "npx", "uvx",
            "docker", "node", "python", "env", "args", "command", "url", "headers",
        }:
            return
        # Tool names in MCP almost always contain _ or - or camelCase, or appear in Tools section
        desc = re.sub(r"\s+", " ", raw_desc).strip(" -:|*")
        if len(desc) < 4:
            desc = f"MCP tool {name}"
        key = name.lower()
        if key not in tools_by_name:
            tools_by_name[key] = {
                "name": name,
                "description": desc[:280],
                "inputSchema": {},
                "serverId": server_id,
            }

    # Isolate the "Tools" / "Available Tools" section if present
    section_match = re.search(
        r"(?im)^#{1,3}\s+(?:available\s+|supported\s+)?tools\b[^\n]*\n([\s\S]*?)(?=\n#{1,2}\s+(?!#)|\Z)",
        readme,
    )
    target_text = section_match.group(1) if section_match else readme

    # Pattern 1: Markdown table rows `| `tool_name` | Description |`
    for m in re.finditer(
        r"(?m)^\|\s*[`*_]*([a-zA-Z][a-zA-Z0-9_.-]{1,63})[`*_]*\s*\|\s*([^|\n]{4,300})\|",
        target_text,
    ):
        _add_tool(m.group(1), m.group(2))

    # Pattern 2: Bullet items `- `tool_name`: Description` or `- **tool_name** - Description`
    for m in re.finditer(
        r"(?m)^\s*[-*]\s+(?:`([a-zA-Z][a-zA-Z0-9_.-]{1,63})`|\*\*([a-zA-Z][a-zA-Z0-9_.-]{1,63})\*\*)\s*(?:[:—–-]\s*|\s+)([^\n]{4,300})",
        target_text,
    ):
        tname = m.group(1) or m.group(2)
        if tname and ("_" in tname or "-" in tname or section_match):
            _add_tool(tname, m.group(3))

    # Pattern 3: Subheadings `### `tool_name`` or `#### tool_name` followed by description line
    if section_match:
        for m in re.finditer(
            r"(?m)^#{3,4}\s+[`*_]*([a-zA-Z][a-zA-Z0-9_.-]{1,63})[`*_]*\s*\n+([^\n#`|>]{6,300})",
            target_text,
        ):
            _add_tool(m.group(1), m.group(2))

    return list(tools_by_name.values())[:max_tools]


def run_mcp_security_audit(
    *,
    readme: str,
    transport: str,
    command: str,
    args: list[str],
    url: str,
    env: dict[str, str],
    headers: dict[str, str],
    cached_tools: list[dict[str, Any]],
) -> dict[str, Any]:
    """Run security pre-triage aligned with McpSecurityScanner.ts & LibraryCatalog.cpp."""
    findings: list[str] = []
    warnings: list[str] = []
    risk = 0

    if HIDDEN_UNICODE_RE.search(readme or ""):
        findings.append("MCP-UNICODE-HIDDEN: Invisible Unicode control characters detected in README")
        risk += 45

    if PIPE_TO_SHELL_RE.search(readme or ""):
        warnings.append("MCP-PIPE-TO-SHELL: README mentions curl/wget pipe-to-shell installation")
        risk += 10

    if REAL_SECRET_RE.search(readme or ""):
        findings.append("MCP-HARDCODED-SECRET: Literal credential pattern detected in source README")
        risk += 35

    if transport == "stdio":
        if not command:
            warnings.append("MCP-NO-COMMAND: No executable command extracted")
            risk += 15
        elif command.lower() in SHELL_COMMANDS:
            warnings.append(f"MCP-SHELL-COMMAND: Direct shell command '{command}' used")
            risk += 25

        for arg in args:
            if SHELL_METACHAR_RE.search(arg):
                findings.append(f"MCP-LP2-SHELL-METACHARS: Forbidden shell metacharacter in arg '{arg}'")
                risk += 50
            if arg.lower() in UNCONFINED_ROOT_ARGS:
                warnings.append(f"MCP-LP3-UNCONFINED-ROOT-SCOPE: Root filesystem path '{arg}' in args")
                risk += 30

        if command in ("npx", "uvx", "bunx"):
            first_pkg = next((a for a in args if not a.startswith("-")), "")
            at_idx = first_pkg.rfind("@")
            if not (at_idx > 0 and re.match(r"^[0-9]+\.[0-9]+", first_pkg[at_idx + 1:])):
                warnings.append("MCP-RP1-UNPINNED-PACKAGE: Package argument is not pinned to an explicit version")
                risk += 15
    else:
        if not url:
            warnings.append("MCP-NO-URL: Missing SSE/HTTP endpoint URL")
            risk += 20
        elif not url.lower().startswith("https://"):
            warnings.append(f"MCP-INSECURE-HTTP: Non-HTTPS endpoint '{url}' rejected by Arestrix NetGuard")
            risk += 30

    # Verify zero literal secrets in emitted env / headers
    for k, v in env.items():
        if v and not ENV_REF_RE.match(v):
            findings.append(f"MCP-ENV-LITERAL: Non-reference env value for '{k}'")
            risk += 45
    for k, v in headers.items():
        if v and not ENV_REF_RE.match(v):
            findings.append(f"MCP-HEADER-LITERAL: Non-reference header value for '{k}'")
            risk += 45

    # Inspect discovered tools for collisions & prompt injection
    for tool in cached_tools:
        tname = str(tool.get("name") or "").lower()
        tdesc = str(tool.get("description") or "")
        if tname in RESERVED_NATIVE_TOOL_NAMES:
            warnings.append(f"MCP-RESERVED-TOOL-NAME: Tool '{tname}' collides with native Arestrix tool")
            risk += 35
        if tname.startswith("library_") or tname.startswith("arestrix_"):
            warnings.append(f"MCP-RESERVED-PREFIX: Tool '{tname}' uses reserved prefix")
            risk += 15
        if HIDDEN_UNICODE_RE.search(tdesc) or PROMPT_INJECTION_RE.search(tdesc):
            findings.append(f"MCP-PROMPT-INJECTION: Suspicious instruction in tool '{tname}' description")
            risk += 50

    risk = min(100, risk)
    if risk >= 45 or any("MCP-LP2" in f or "MCP-PROMPT-INJECTION" in f or "MCP-UNICODE" in f for f in findings):
        verdict = "REJECT"
    elif risk >= 15 or findings:
        verdict = "CAUTION"
    else:
        verdict = "APPROVE"

    return {
        "verdict": verdict,
        "riskScore": risk,
        "policyVersion": MCP_AUDIT_POLICY_VERSION,
        "findings": findings,
        "warnings": warnings,
    }


def detect_domain_and_subdomain(
    *,
    name: str,
    repo: str,
    path: str,
    description: str,
    tools: list[dict[str, Any]],
    readme_excerpt: str,
) -> tuple[str, str]:
    """Classify MCP server into 2-level taxonomy (Domínio -> Subdomínio) using weighted matching."""
    tool_text = " ".join(str(t.get("name") or "") for t in tools[:20])
    fields = [
        (name.lower(), 5),
        (f"{repo} {path}".lower(), 4),
        (description.lower(), 3),
        (tool_text.lower(), 2),
        (readme_excerpt[:500].lower(), 1),
    ]

    best_score = 0
    best_pair = ("Web, Busca & Conhecimento", "Utilitários & Sistema Local")

    for domain, subdomains in TAXONOMY.items():
        for subdomain, keywords in subdomains.items():
            score = 0
            for text, weight in fields:
                if not text:
                    continue
                tokens = set(re.findall(r"[a-z0-9_+-]+", text))
                for kw in keywords:
                    if kw in tokens or (len(kw) >= 5 and kw in text):
                        score += weight
            if score > best_score:
                best_score = score
                best_pair = (domain, subdomain)

    return best_pair


def extract_description(readme: str, pkg_desc: str | None, fallback: str) -> str:
    """Extract a clean single-paragraph description from package metadata or README."""
    if pkg_desc and len(pkg_desc.strip()) >= 15:
        return re.sub(r"\s+", " ", pkg_desc).strip()[:280]
    if readme:
        # Remove badges, HTML tags, code blocks, and headings to grab first meaningful paragraph
        cleaned = re.sub(r"```[\s\S]*?```", "", readme)
        cleaned = re.sub(r"<[^>]+>", "", cleaned)
        for line in cleaned.splitlines():
            s = line.strip()
            if not s or s.startswith("#") or s.startswith("![") or s.startswith("[![") or s.startswith("|") or s.startswith(">"):
                continue
            s = re.sub(r"\[([^\]]+)\]\([^)]+\)", r"\1", s)
            s = re.sub(r"\s+", " ", s).strip()
            if len(s) >= 24:
                return s[:280]
    return fallback[:280]


def compute_quality_score(
    *,
    audit_verdict: str,
    transport: str,
    command: str,
    url: str,
    description: str,
    tools_count: int,
    has_env_or_headers: bool,
    has_pinned_version: bool,
    repo_stars: int,
    word_count: int,
) -> int:
    """Objective 0..100 quality score for an MCP server asset."""
    if audit_verdict == "REJECT":
        return 0
    score = 0
    if (transport == "stdio" and command) or (transport == "sse" and url.startswith("https://")):
        score += 25
    if len(description) >= 30:
        score += 12
    if tools_count >= 5:
        score += 22
    elif tools_count >= 1:
        score += 14
    if has_pinned_version:
        score += 12
    if word_count >= 150:
        score += 10
    elif word_count >= 60:
        score += 5
    if has_env_or_headers:
        score += 5
    score += min(14, int(math.log10(max(0, repo_stars) + 1) * 3.0))
    if audit_verdict == "CAUTION":
        score = max(0, score - 10)
    return min(100, score)


def parse_mcp_artifact(
    *,
    name: str,
    source_repo: str,
    source_path: str,
    source_url: str,
    raw_url: str,
    license_name: str,
    repo_stars: int,
    readme_text: str,
    package_json: dict[str, Any] | None = None,
    pyproject_meta: dict[str, Any] | None = None,
    server_json: dict[str, Any] | None = None,
    focus_hint: str = "",
) -> dict[str, Any] | None:
    """Parse raw repository files into a normalized, audited MCP candidate record."""
    norm_license = normalize_license(license_name)
    if not is_permissive_license(norm_license):
        return None

    server_id = make_safe_server_id(name)
    cfg = extract_mcp_config(
        readme=readme_text,
        package_json=package_json,
        pyproject_meta=pyproject_meta,
        server_json=server_json,
    )

    # Require at least a real command or https URL so we never emit hollow stubs
    if cfg["transport"] == "stdio" and not cfg["command"]:
        return None
    if cfg["transport"] == "sse" and not cfg["url"]:
        return None

    cached_tools = extract_tools_from_readme(readme_text, server_id=server_id)

    pkg_desc = None
    version = "1.0.0"
    if isinstance(package_json, dict):
        pkg_desc = package_json.get("description")
        if package_json.get("version"):
            version = str(package_json["version"])
    elif isinstance(pyproject_meta, dict):
        pkg_desc = pyproject_meta.get("description")
        if pyproject_meta.get("version"):
            version = str(pyproject_meta["version"])

    description = extract_description(
        readme_text,
        pkg_desc,
        fallback=focus_hint or f"MCP server from {source_repo}",
    )

    domain, subdomain = detect_domain_and_subdomain(
        name=name,
        repo=source_repo,
        path=source_path,
        description=description,
        tools=cached_tools,
        readme_excerpt=readme_text,
    )

    audit = run_mcp_security_audit(
        readme=readme_text,
        transport=cfg["transport"],
        command=cfg["command"],
        args=cfg["args"],
        url=cfg["url"],
        env=cfg["env"],
        headers=cfg["headers"],
        cached_tools=cached_tools,
    )

    install_hash = compute_mcp_install_hash(
        server_id=server_id,
        transport=cfg["transport"],
        command=cfg["command"],
        args=cfg["args"],
        url=cfg["url"],
        env=cfg["env"],
        headers=cfg["headers"],
        cached_tools=cached_tools,
    )
    audit["hash"] = install_hash

    has_pinned = not any("MCP-RP1-UNPINNED-PACKAGE" in w for w in audit["warnings"])
    word_count = len(re.findall(r"\w+", readme_text or ""))
    quality_score = compute_quality_score(
        audit_verdict=audit["verdict"],
        transport=cfg["transport"],
        command=cfg["command"],
        url=cfg["url"],
        description=description,
        tools_count=len(cached_tools),
        has_env_or_headers=bool(cfg["env"] or cfg["headers"]),
        has_pinned_version=has_pinned,
        repo_stars=repo_stars,
        word_count=word_count,
    )

    unique_id = f"{source_repo.lower().replace('/', '__')}__{server_id}"
    return {
        "id": unique_id,
        "server_id": server_id,
        "name": name,
        "description": description,
        "version": version,
        "domain": domain,
        "subdomain": subdomain,
        "category": subdomain,
        "license": norm_license,
        "source_repo": source_repo,
        "source_path": source_path,
        "source_url": source_url,
        "raw_url": raw_url,
        "repo_stars": repo_stars,
        "word_count": word_count,
        "payload": {
            "kind": "mcp",
            "transport": cfg["transport"],
            "command": cfg["command"] if cfg["transport"] == "stdio" else "",
            "args": cfg["args"] if cfg["transport"] == "stdio" else [],
            **({"env": cfg["env"]} if cfg["env"] else {}),
            "url": cfg["url"] if cfg["transport"] == "sse" else "",
            **({"headers": cfg["headers"]} if cfg["headers"] else {}),
            "cachedTools": cached_tools,
        },
        "tools_count": len(cached_tools),
        "audit": audit,
        "install_hash": install_hash,
        "quality_score": quality_score,
    }
