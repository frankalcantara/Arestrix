"""CLI orchestrator for discovering, auditing, deduplicating, and exporting MCP servers for Arestrix."""

from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import sys
import time
from typing import Any

import requests

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
if SCRIPT_DIR not in sys.path:
    sys.path.insert(0, SCRIPT_DIR)

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8", errors="replace")

from arestrix_exporter import export_mcp_catalog
from parser import (
    detect_license_from_text,
    is_permissive_license,
    normalize_license,
    parse_mcp_artifact,
)
from sources import DISCOVERY_LIST_URLS, SEED_REPOSITORIES

CACHE_DIR = os.path.join(SCRIPT_DIR, "cache")
CACHE_TTL_SECONDS = 6 * 3600  # 6 hours

_RATE_LIMIT_EXHAUSTED = False


def build_headers() -> dict[str, str]:
    headers = {
        "User-Agent": "Arestrix-MCP-Crawler/1.0",
        "Accept": "application/vnd.github.v3+json",
    }
    token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if token:
        headers["Authorization"] = f"Bearer {token.strip()}"
    return headers


def _cache_path(key: str) -> str:
    safe_key = re.sub(r"[^A-Za-z0-9_-]+", "_", key)
    return os.path.join(CACHE_DIR, f"{safe_key}.json")


def read_cache(key: str, refresh: bool = False) -> Any | None:
    if refresh:
        return None
    path = _cache_path(key)
    if not os.path.exists(path):
        return None
    age = time.time() - os.path.getmtime(path)
    if age > CACHE_TTL_SECONDS:
        return None
    try:
        with open(path, "r", encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return None


def write_cache(key: str, data: Any) -> None:
    os.makedirs(CACHE_DIR, exist_ok=True)
    path = _cache_path(key)
    try:
        with open(path, "w", encoding="utf-8") as f:
            json.dump(data, f, ensure_ascii=False)
    except Exception:
        pass


def cached_github_api_get(
    url: str,
    cache_key: str,
    *,
    refresh_cache: bool = False,
    timeout: int = 15,
) -> dict[str, Any] | None:
    """Fetch a GitHub REST API JSON endpoint with 6h disk cache and rate-limit guard."""
    global _RATE_LIMIT_EXHAUSTED
    cached = read_cache(cache_key, refresh=refresh_cache)
    if cached is not None:
        return cached
    if _RATE_LIMIT_EXHAUSTED:
        return None

    try:
        resp = requests.get(url, headers=build_headers(), timeout=timeout)
        remaining = resp.headers.get("X-RateLimit-Remaining")
        if remaining is not None and remaining.isdigit() and int(remaining) <= 2:
            _RATE_LIMIT_EXHAUSTED = True
        if resp.status_code == 403 and "rate limit" in resp.text.lower():
            _RATE_LIMIT_EXHAUSTED = True
            return None
        if resp.status_code != 200:
            return None
        data = resp.json()
        write_cache(cache_key, data)
        return data
    except Exception:
        return None


def fetch_raw_text(repo: str, branch: str, path: str, timeout: int = 12) -> str | None:
    """Download a raw file from raw.githubusercontent.com (consumes 0 GitHub API quota)."""
    url = f"https://raw.githubusercontent.com/{repo}/{branch}/{path.lstrip('/')}"
    try:
        resp = requests.get(url, headers={"User-Agent": "Arestrix-MCP-Crawler/1.0"}, timeout=timeout)
        if resp.status_code == 200:
            return resp.text
        return None
    except Exception:
        return None


def parse_simple_pyproject(text: str | None) -> dict[str, Any] | None:
    """Extract basic name, version, description, and license from pyproject.toml without extra deps."""
    if not text:
        return None
    meta: dict[str, Any] = {}
    for field in ("name", "version", "description"):
        m = re.search(rf'(?m)^\s*{field}\s*=\s*["\']([^"\']+)["\']', text)
        if m:
            meta[field] = m.group(1).strip()
    m_lic = re.search(r'(?m)^\s*license\s*=\s*(?:\{\s*text\s*=\s*)?["\']([^"\']+)["\']', text)
    if m_lic:
        meta["license"] = m_lic.group(1).strip()
    return meta or None


def resolve_repo_branch_and_license(
    repo: str,
    branch_hint: str,
    stars_hint: int | None,
    *,
    refresh_cache: bool = False,
) -> tuple[str | None, str | None, int]:
    """Resolve active branch, strict SPDX license, and stars using cache + raw files + optional API."""
    license_name: str | None = None
    stars = int(stars_hint or 0)
    branch = branch_hint or "main"

    # Try GitHub API if token is set OR stars_hint is missing (respects cache & rate-limit guard)
    has_token = bool(os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN"))
    if has_token or stars_hint is None:
        info = cached_github_api_get(
            f"https://api.github.com/repos/{repo}",
            f"repo_{repo}",
            refresh_cache=refresh_cache,
        )
        if isinstance(info, dict):
            branch = info.get("default_branch") or branch
            stars = int(info.get("stargazers_count") or stars)
            lic_obj = info.get("license")
            if isinstance(lic_obj, dict):
                license_name = normalize_license(lic_obj.get("spdx_id"))

    # Verify branch & license via raw.githubusercontent.com (0 API quota!) if not resolved yet
    branches_to_try = [branch] + [b for b in ("main", "master") if b != branch]
    active_branch: str | None = None
    for br in branches_to_try:
        readme_probe = fetch_raw_text(repo, br, "README.md")
        if readme_probe is not None:
            active_branch = br
            break

    if not active_branch:
        return None, None, stars

    if not license_name:
        for lic_file in ("LICENSE", "LICENSE.md", "LICENSE.txt", "LICENSE-MIT", "LICENSE-APACHE"):
            txt = fetch_raw_text(repo, active_branch, lic_file)
            if txt:
                detected = detect_license_from_text(txt)
                if detected:
                    license_name = detected
                    break

    if not license_name:
        pkg_txt = fetch_raw_text(repo, active_branch, "package.json")
        if pkg_txt:
            try:
                pkg = json.loads(pkg_txt)
                if isinstance(pkg.get("license"), str):
                    license_name = normalize_license(pkg["license"])
            except Exception:
                pass

    if not license_name:
        pyp_txt = fetch_raw_text(repo, active_branch, "pyproject.toml")
        pyp = parse_simple_pyproject(pyp_txt)
        if pyp and pyp.get("license"):
            license_name = normalize_license(pyp["license"])

    return active_branch, license_name, stars


def crawl_standalone_repo(
    repo_entry: dict[str, Any],
    *,
    refresh_cache: bool = False,
) -> tuple[list[dict[str, Any]], dict[str, Any] | None]:
    """Crawl a standalone MCP server repository and return parsed artifact(s) or skip reason."""
    repo = repo_entry["repo"]
    cache_key = f"standalone_{repo}"
    cached_item = read_cache(cache_key, refresh=refresh_cache)
    if cached_item is not None:
        if cached_item.get("skipped"):
            return [], cached_item["skipped"]
        return cached_item.get("items", []), None

    branch, lic, stars = resolve_repo_branch_and_license(
        repo,
        repo_entry.get("branch_hint", "main"),
        repo_entry.get("stars_hint"),
        refresh_cache=refresh_cache,
    )
    if not branch:
        skip = {"repo": repo, "reason": "unreachable_or_no_readme"}
        write_cache(cache_key, {"skipped": skip})
        return [], skip

    if not is_permissive_license(lic):
        skip = {"repo": repo, "reason": f"non_permissive_or_missing_license ({lic or 'None'})"}
        write_cache(cache_key, {"skipped": skip})
        return [], skip

    readme = fetch_raw_text(repo, branch, "README.md") or ""
    pkg_txt = fetch_raw_text(repo, branch, "package.json")
    pyp_txt = fetch_raw_text(repo, branch, "pyproject.toml")
    srv_txt = fetch_raw_text(repo, branch, "server.json")

    pkg_json = None
    if pkg_txt:
        try:
            pkg_json = json.loads(pkg_txt)
        except Exception:
            pkg_json = None

    srv_json = None
    if srv_txt:
        try:
            srv_json = json.loads(srv_txt)
        except Exception:
            srv_json = None

    pyp_meta = parse_simple_pyproject(pyp_txt)
    name = repo.split("/")[-1]
    if isinstance(pkg_json, dict) and pkg_json.get("name"):
        # Keep clean display name
        name = str(pkg_json["name"])

    artifact = parse_mcp_artifact(
        name=name,
        source_repo=repo,
        source_path="README.md",
        source_url=f"https://github.com/{repo}",
        raw_url=f"https://raw.githubusercontent.com/{repo}/{branch}/README.md",
        license_name=lic or "",
        repo_stars=stars,
        readme_text=readme,
        package_json=pkg_json,
        pyproject_meta=pyp_meta,
        server_json=srv_json,
        focus_hint=repo_entry.get("focus", ""),
    )
    if not artifact:
        skip = {"repo": repo, "reason": "no_extractable_mcp_config"}
        write_cache(cache_key, {"skipped": skip})
        return [], skip

    write_cache(cache_key, {"items": [artifact]})
    return [artifact], None


def crawl_monorepo(
    repo_entry: dict[str, Any],
    *,
    max_per_repo: int = 50,
    refresh_cache: bool = False,
) -> tuple[list[dict[str, Any]], dict[str, Any] | None]:
    """Crawl an MCP monorepo (e.g. modelcontextprotocol/servers, awslabs/mcp) via 1 Git Trees API call + raw files."""
    repo = repo_entry["repo"]
    branch, lic, stars = resolve_repo_branch_and_license(
        repo,
        repo_entry.get("branch_hint", "main"),
        repo_entry.get("stars_hint"),
        refresh_cache=refresh_cache,
    )
    if not branch:
        return [], {"repo": repo, "reason": "unreachable_monorepo"}
    if not is_permissive_license(lic):
        return [], {"repo": repo, "reason": f"non_permissive_license ({lic or 'None'})"}

    tree_data = cached_github_api_get(
        f"https://api.github.com/repos/{repo}/git/trees/{branch}?recursive=1",
        f"tree_{repo}_{branch}",
        refresh_cache=refresh_cache,
    )
    if not isinstance(tree_data, dict) or not isinstance(tree_data.get("tree"), list):
        return [], {"repo": repo, "reason": "tree_fetch_failed_or_rate_limited"}

    # Identify server directories containing src/<dir>/README.md or packages/<dir>/README.md
    all_paths = {node["path"] for node in tree_data["tree"] if node.get("type") == "blob" and node.get("path")}
    server_dirs: list[tuple[str, str]] = []
    for p in sorted(all_paths):
        m = re.match(r"^(src|packages|servers)/([^/]+)/README\.md$", p, re.IGNORECASE)
        if m:
            server_dir = f"{m.group(1)}/{m.group(2)}"
            server_name = m.group(2)
            server_dirs.append((server_dir, server_name))

    server_dirs = server_dirs[:max_per_repo]
    items: list[dict[str, Any]] = []

    def _fetch_subserver(entry: tuple[str, str]) -> dict[str, Any] | None:
        sdir, sname = entry
        sub_cache_key = f"mono_{repo}_{sname}"
        cached = read_cache(sub_cache_key, refresh=refresh_cache)
        if cached is not None:
            return cached.get("item")

        readme = fetch_raw_text(repo, branch, f"{sdir}/README.md") or ""
        pkg_json = None
        if f"{sdir}/package.json" in all_paths:
            txt = fetch_raw_text(repo, branch, f"{sdir}/package.json")
            if txt:
                try:
                    pkg_json = json.loads(txt)
                except Exception:
                    pkg_json = None

        pyp_meta = None
        if f"{sdir}/pyproject.toml" in all_paths:
            pyp_meta = parse_simple_pyproject(fetch_raw_text(repo, branch, f"{sdir}/pyproject.toml"))

        srv_json = None
        if f"{sdir}/server.json" in all_paths:
            txt = fetch_raw_text(repo, branch, f"{sdir}/server.json")
            if txt:
                try:
                    srv_json = json.loads(txt)
                except Exception:
                    srv_json = None

        # Sub-server license override if present, otherwise inherit monorepo permissive license
        sub_lic = lic
        if isinstance(pkg_json, dict) and isinstance(pkg_json.get("license"), str):
            sub_lic = normalize_license(pkg_json["license"]) or lic

        artifact = parse_mcp_artifact(
            name=sname,
            source_repo=repo,
            source_path=f"{sdir}/README.md",
            source_url=f"https://github.com/{repo}/tree/{branch}/{sdir}",
            raw_url=f"https://raw.githubusercontent.com/{repo}/{branch}/{sdir}/README.md",
            license_name=sub_lic or "",
            repo_stars=stars,
            readme_text=readme,
            package_json=pkg_json,
            pyproject_meta=pyp_meta,
            server_json=srv_json,
            focus_hint=f"{sname} MCP server from {repo}",
        )
        write_cache(sub_cache_key, {"item": artifact})
        return artifact

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        for res in pool.map(_fetch_subserver, server_dirs):
            if res is not None:
                items.append(res)

    return items, None


def discover_extra_repos(
    existing_repos: set[str],
    *,
    limit: int = 20,
    refresh_cache: bool = False,
) -> list[dict[str, Any]]:
    """Discover additional GitHub MCP repos from curated awesome-mcp markdown lists (0 GitHub API quota)."""
    if limit <= 0:
        return []

    cache_key = "discovered_awesome_mcp_repos"
    cached = read_cache(cache_key, refresh=refresh_cache)
    if isinstance(cached, list):
        candidates = cached
    else:
        found: list[str] = []
        seen: set[str] = set()
        for url in DISCOVERY_LIST_URLS:
            try:
                resp = requests.get(url, headers={"User-Agent": "Arestrix-MCP-Crawler/1.0"}, timeout=15)
                if resp.status_code != 200:
                    continue
                for m in re.finditer(r"https://github\.com/([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)(?=[)\s\"'>]|$)", resp.text):
                    repo = m.group(1).rstrip(".")
                    low = repo.lower()
                    if low in seen:
                        continue
                    # Heuristic: keep likely MCP server repositories
                    repo_name = low.split("/")[-1]
                    if "mcp" not in repo_name or any(
                        bad in repo_name for bad in ("awesome", "client", "inspector", "sdk", "spec", "docs", "cli")
                    ):
                        continue
                    seen.add(low)
                    found.append(repo)
            except Exception:
                continue
        write_cache(cache_key, found)
        candidates = found

    selected: list[dict[str, Any]] = []
    for repo in candidates:
        if repo.lower() in existing_repos:
            continue
        selected.append(
            {
                "repo": repo,
                "kind": "standalone",
                "branch_hint": "main",
                "stars_hint": 150,  # conservative default when unauthenticated
                "focus": f"Discovered MCP server ({repo})",
            }
        )
        if len(selected) >= limit:
            break
    return selected


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Arestrix MCP Crawler — discover, audit, deduplicate, and export permissive MCP servers."
    )
    parser.add_argument("--max-per-repo", type=int, default=50, help="Max sub-servers per monorepo (default: 50)")
    parser.add_argument(
        "--max-discovered",
        type=int,
        default=20,
        help="Max extra standalone repos discovered from awesome-mcp lists (default: 20)",
    )
    parser.add_argument(
        "--output-dir",
        type=str,
        default=os.path.join(SCRIPT_DIR, "output"),
        help="Output directory for catalog, snapshot, report, and samples",
    )
    parser.add_argument("--no-samples", action="store_true", help="Disable writing sample_mcps/*.json")
    parser.add_argument("--refresh-cache", action="store_true", help="Bypass 6h local disk cache")
    parser.add_argument("--skip-discovery", action="store_true", help="Crawl only curated seed repositories")
    parser.add_argument("--workers", type=int, default=8, help="Parallel worker threads for standalone repos")
    args = parser.parse_args()

    print("=== Arestrix MCP Crawler ===")
    print(f"Cache dir: {CACHE_DIR} (refresh={args.refresh_cache})")

    monorepos = [r for r in SEED_REPOSITORIES if r.get("kind") == "monorepo"]
    standalones = [r for r in SEED_REPOSITORIES if r.get("kind") != "monorepo"]
    existing_lower = {r["repo"].lower() for r in SEED_REPOSITORIES}

    if not args.skip_discovery and args.max_discovered > 0:
        discovered = discover_extra_repos(
            existing_lower,
            limit=args.max_discovered,
            refresh_cache=args.refresh_cache,
        )
        print(f"Discovered {len(discovered)} additional MCP candidate repos from awesome lists.")
        standalones.extend(discovered)

    raw_items: list[dict[str, Any]] = []
    skipped_repos: list[dict[str, Any]] = []

    # 1. Crawl monorepos
    for mr in monorepos:
        print(f"[Monorepo] Crawling {mr['repo']} ...")
        items, skip = crawl_monorepo(
            mr,
            max_per_repo=args.max_per_repo,
            refresh_cache=args.refresh_cache,
        )
        if skip:
            print(f"  -> Skipped: {skip['reason']}")
            skipped_repos.append(skip)
        else:
            print(f"  -> Collected {len(items)} permissive MCP servers.")
            raw_items.extend(items)

    # 2. Crawl standalone repos in parallel
    print(f"[Standalone] Crawling {len(standalones)} repositories (workers={args.workers}) ...")
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.workers) as pool:
        future_map = {
            pool.submit(crawl_standalone_repo, r, refresh_cache=args.refresh_cache): r["repo"]
            for r in standalones
        }
        for fut in concurrent.futures.as_completed(future_map):
            repo_name = future_map[fut]
            try:
                items, skip = fut.result()
                if skip:
                    skipped_repos.append(skip)
                else:
                    raw_items.extend(items)
            except Exception as exc:
                skipped_repos.append({"repo": repo_name, "reason": f"exception: {exc}"})

    print(f"Total raw permissive MCP candidates collected: {len(raw_items)}")
    print(f"Total skipped sources: {len(skipped_repos)}")

    summary = export_mcp_catalog(
        raw_items=raw_items,
        skipped_repos=skipped_repos,
        output_dir=args.output_dir,
        export_samples=not args.no_samples,
    )

    print("\n=== Export Summary ===")
    print(f"Canonical MCP servers : {summary['canonical_count']}")
    print(f"Semantic clusters     : {summary['clusters_merged']} ({summary['variants_collapsed']} variants collapsed)")
    print(f"Security verdicts     : {summary['verdict_counts']}")
    print(f"Total cachedTools     : {summary['total_tools_discovered']}")
    print(f"Catalog JSON          : {summary['catalog_path']}")
    print(f"Arestrix Snapshot JSON: {summary['snapshot_path']}")
    print(f"Markdown Report       : {summary['report_path']}")
    print(f"Sample MCP JSONs      : {summary['samples_written']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
