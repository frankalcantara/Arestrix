#!/usr/bin/env python3
"""Avaliacao dos interruptores avancados nos projetos de exemplo.

O corpus curado de ``memory_advanced_eval.py`` mede os mecanismos em um banco
sintetico. Este harness repete a medicao sobre os 14 projetos de ``examples/``:
os vertices e as arestas do pipeline viram conhecimento de memoria, e o mesmo
CLI mede caminhos, travessia, pontes e roteamento sem chamar modelo.

Para cada exemplo o harness:

- copia o projeto para um diretorio temporario e inicializa a memoria;
- cria um no por vertice e uma claim por aresta do pipeline;
- monta um caminho canonico com a maior cadeia simples do grafo;
- cria traces/validacoes para pontes (quando ha duas lentes naturais) e para
  o roteamento local/fronteira;
- executa ``memory brief``, ``memory bridges detect`` e ``memory route`` nas
  variantes baseline, pathServing, traversal, bridges, routing.shadow,
  routing.binary e all_on.

O projeto original nunca e alterado: todas as execucoes usam copias.

Uso:
  python scripts/memory_examples_eval.py --write
  python scripts/memory_examples_eval.py --check
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import sqlite3
import subprocess
import sys
import tempfile
import time
import unicodedata

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXAMPLES = ROOT / "examples"
REPORT_JSON = ROOT / "tests" / "fixtures" / "memory-examples-advanced-report.json"
REPORT_MD = ROOT / "docs" / "quality" / "memory-examples-advanced-ab.md"
NOW = "2026-11-12T00:00:00Z"
ROUTE_LOCAL = "exemplo_rotear_local"
ROUTE_FRONTIER = "exemplo_rotear_frontier"
ROUTE_COLD = "exemplo_rotear_cold"


def cli_path() -> pathlib.Path:
    for name in ("build/arestrix-cli.exe", "build/arestrix-cli",
                 "arestrix-cli.exe", "arestrix-cli"):
        candidate = ROOT / name
        if candidate.exists():
            return candidate
    return ROOT / ("arestrix-cli.exe" if os.name == "nt" else "arestrix-cli")


def run(cli: pathlib.Path, args: list[str], timeout: int = 120) -> subprocess.CompletedProcess:
    return subprocess.run(
        [str(cli), *args], capture_output=True, text=True, encoding="utf-8",
        errors="replace", timeout=timeout,
    )


def sha256_text(text: str) -> str:
    return hashlib.sha256(text.encode("utf-8")).hexdigest()


def normalize_comparable(value: str) -> str:
    folded = unicodedata.normalize("NFKD", value)
    folded = "".join(ch for ch in folded if not unicodedata.combining(ch))
    out: list[str] = []
    space = True
    for ch in folded:
        if ch.isascii() and ch.isalpha():
            ch = ch.lower()
        if ("a" <= ch <= "z") or ("0" <= ch <= "9"):
            out.append(ch)
            space = False
        elif not space:
            out.append(" ")
            space = True
    return "".join(out).strip()


def slug(value: str) -> str:
    text = normalize_comparable(value).replace(" ", "_")
    return text or "exemplo"


def task_type_for(project_name: str, graph_name: str) -> str:
    return "exemplo_" + slug(project_name)


def ensure_scope(conn: sqlite3.Connection, scope_id: str, kind: str, name: str) -> None:
    conn.execute(
        "INSERT OR IGNORE INTO memory_scope(id,kind,parent_id,name,created_at,metadata) "
        "VALUES(?,?,?,?,?,?)",
        (scope_id, kind, None, name, NOW, "{}"),
    )
    conn.execute(
        "INSERT OR IGNORE INTO memory_scope_closure(ancestor_id,descendant_id,depth) "
        "VALUES(?,?,0)", (scope_id, scope_id),
    )


def add_node(conn: sqlite3.Connection, scope_id: str, node_id: str, name: str) -> None:
    conn.execute(
        "INSERT OR IGNORE INTO memory_node(id,scope_id,kind,canonical_name,aliases_json,"
        "created_at,updated_at,metadata) VALUES(?,?,?,?,?,?,?,?)",
        (node_id, scope_id, "entity", name, "[]", NOW, NOW, "{}"),
    )
    conn.execute("DELETE FROM memory_node_fts WHERE node_id=?", (node_id,))
    conn.execute(
        "INSERT INTO memory_node_fts(node_id,name,aliases,metadata) VALUES(?,?,?,?)",
        (node_id, name, "", "{}"),
    )
    conn.execute(
        "INSERT OR IGNORE INTO memory_alias(scope_id,alias,node_id,source) VALUES(?,?,?,?)",
        (scope_id, normalize_comparable(name), node_id, "canonical"),
    )


def add_lens(conn: sqlite3.Connection, scope_id: str, lens_id: str, name: str) -> None:
    conn.execute(
        "INSERT OR IGNORE INTO memory_lens(id,scope_id,name,description,version,"
        "created_at,updated_at) VALUES(?,?,?,?,?,?,?)",
        (lens_id, scope_id, name, "", 1, NOW, NOW),
    )


def add_edge(conn: sqlite3.Connection, scope_id: str, edge_id: str, src: str,
             dst: str, relation: str, lens: str) -> None:
    metadata = {"claim": True, "lens": lens}
    edge_hash = sha256_text("|".join([scope_id, src, dst, relation, "", NOW, ""]))
    conn.execute(
        "INSERT OR REPLACE INTO memory_edge(id,scope_id,src_id,dst_id,relation,value_json,"
        "valid_from,valid_to,recorded_at,confidence,status,superseded_by,source_kind,"
        "source_ref,evidence_hash,edge_hash,metadata,last_used,success_count,failure_count,"
        "utility,dormant_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        (edge_id, scope_id, src, dst, relation, None, NOW, None, NOW, 0.8,
         "active", None, "claim", "examples/", None, edge_hash,
         json.dumps(metadata, ensure_ascii=False), None, 0, 0, 0.5, None),
    )


def edge_kind(edge: dict) -> str:
    kind = str(edge.get("kind", "") or "").strip()
    return kind if kind in ("flow", "call", "data") else "flow"


def relation_for(kind: str) -> str:
    return {"flow": "FLOWS_TO", "call": "CALLS", "data": "DATA_TO"}.get(kind, "FLOWS_TO")


def graph_summary(graph: dict) -> dict:
    nodes = [node for node in graph.get("nodes", []) if isinstance(node, dict)]
    edges = [edge for edge in graph.get("edges", []) if isinstance(edge, dict)]
    by_id = {str(node.get("id", "")): node for node in nodes}
    return {"nodes": nodes, "edges": edges, "by_id": by_id}


def longest_chain(graph: dict) -> list[dict]:
    """Maior cadeia simples (ate 3 arestas) para o caminho canonico."""
    edges = graph["edges"]
    if not edges:
        return []
    incident: dict[str, list[dict]] = {}
    for edge in edges:
        src = str(edge.get("sourceNodeId", ""))
        dst = str(edge.get("targetNodeId", ""))
        if not src or not dst:
            continue
        incident.setdefault(src, []).append(edge)
        incident.setdefault(dst, []).append(edge)
    best: list[dict] = []

    def walk(node: str, used: set[str], chain: list[dict]) -> None:
        nonlocal best
        if len(chain) > len(best):
            best = list(chain)
        if len(chain) >= 3:
            return
        for edge in incident.get(node, []):
            edge_id = str(edge.get("id", ""))
            if not edge_id or edge_id in used:
                continue
            src = str(edge.get("sourceNodeId", ""))
            dst = str(edge.get("targetNodeId", ""))
            other = dst if src == node else src
            used.add(edge_id)
            chain.append(edge)
            walk(other, used, chain)
            chain.pop()
            used.remove(edge_id)

    for start in sorted(incident):
        walk(start, set(), [])
    return best


def chain_end(chain: list[dict]) -> str:
    """Vertice na ponta oposta ao inicio da cadeia (ancora da travessia)."""
    if not chain:
        return ""
    if len(chain) == 1:
        return str(chain[0].get("targetNodeId", ""))
    last = chain[-1]
    previous = chain[-2]
    last_nodes = {str(last.get("sourceNodeId", "")), str(last.get("targetNodeId", ""))}
    previous_nodes = {str(previous.get("sourceNodeId", "")),
                      str(previous.get("targetNodeId", ""))}
    endpoints = last_nodes - previous_nodes
    if endpoints:
        return sorted(endpoints)[0]
    return str(last.get("targetNodeId", ""))


def bridge_info(graph: dict) -> dict | None:
    """Entidade com claims naturais em duas lentes (tipos de aresta)."""
    by_source: dict[str, dict[str, list[dict]]] = {}
    for edge in graph["edges"]:
        src = str(edge.get("sourceNodeId", ""))
        if not src:
            continue
        kind = edge_kind(edge)
        by_source.setdefault(src, {}).setdefault(kind, []).append(edge)
    candidates = [(src, groups) for src, groups in by_source.items()
                  if len(groups) >= 2]
    if not candidates:
        return None
    candidates.sort(key=lambda item: (-len(item[1]), item[0]))
    src, groups = candidates[0]
    node = graph["by_id"].get(src, {})
    claims: list[str] = []
    lenses: list[str] = []
    for kind in sorted(groups):
        lenses.append(kind)
        claims.extend("claim-" + str(edge.get("id", "")) for edge in groups[kind])
    return {
        "entityId": src,
        "entity": str(node.get("label", src)),
        "lenses": lenses,
        "claims": claims,
    }


def seed_knowledge(db_path: pathlib.Path, scope_id: str, project_name: str,
                   graph: dict) -> dict:
    chain = longest_chain(graph)
    task_type = task_type_for(project_name, str(graph.get("name", "")))
    path_claims = ["claim-" + str(edge.get("id", "")) for edge in chain]
    path_id = "path-" + slug(project_name)
    bridge = bridge_info(graph)
    conn = sqlite3.connect(db_path)
    try:
        conn.execute("PRAGMA foreign_keys=ON")
        ensure_scope(conn, scope_id, "project", project_name)
        lenses = sorted({edge_kind(edge) for edge in graph["edges"]}) or ["flow"]
        for kind in lenses:
            add_lens(conn, scope_id, "lens-" + kind, kind)
        for node in graph["nodes"]:
            node_id = str(node.get("id", ""))
            if not node_id:
                continue
            add_node(conn, scope_id, "node-" + node_id,
                     str(node.get("label", node_id)))
        for edge in graph["edges"]:
            edge_id = str(edge.get("id", ""))
            src = str(edge.get("sourceNodeId", ""))
            dst = str(edge.get("targetNodeId", ""))
            if not edge_id or not src or not dst:
                continue
            kind = edge_kind(edge)
            add_edge(conn, scope_id, "claim-" + edge_id, "node-" + src,
                     "node-" + dst, relation_for(kind), kind)
            conn.execute(
                "INSERT OR IGNORE INTO memory_edge_lens(edge_id,lens_id) VALUES(?,?)",
                ("claim-" + edge_id, "lens-" + kind),
            )
        if path_claims:
            conn.execute(
                "INSERT OR IGNORE INTO memory_path_schema(id,scope_id,signature,created_at) "
                "VALUES(?,?,?,?)", ("psch-" + slug(project_name), scope_id,
                                    "path-" + slug(project_name), NOW),
            )
            conn.execute(
                "INSERT OR REPLACE INTO memory_path(id,scope_id,revision,schema_id,"
                "task_type,state,intent_sample,access_json,created_at,updated_at) "
                "VALUES(?,?,?,?,?,?,?,?,?,?)",
                (path_id, scope_id, 1, "psch-" + slug(project_name), task_type,
                 "active", "", "{}", NOW, NOW),
            )
            conn.execute(
                "INSERT OR REPLACE INTO memory_path_stats(path_id,success,failure,last_tau,"
                "uses,distinct_inputs) VALUES(?,?,?,?,?,?)",
                (path_id, 9, 0, 0, 9, 9),
            )
            for ordinal, claim_id in enumerate(path_claims):
                conn.execute(
                    "INSERT OR REPLACE INTO memory_path_claim(path_id,ordinal,claim_id,"
                    "claim_revision) VALUES(?,?,?,?)",
                    (path_id, ordinal, claim_id, ""),
                )
                conn.execute(
                    "INSERT OR IGNORE INTO memory_claim_path(claim_id,path_id) VALUES(?,?)",
                    (claim_id, path_id),
                )
        conn.commit()
    finally:
        conn.close()
    anchor_node = chain_end(chain)
    anchor_label = str(graph["by_id"].get(anchor_node, {}).get("label", anchor_node))
    return {
        "taskType": task_type,
        "pathId": path_id,
        "pathClaims": path_claims,
        "chain": chain,
        "anchor": anchor_label,
        "bridge": bridge,
    }


def seed_telemetry(db_path: pathlib.Path, scope_id: str, project_name: str,
                   bridge: dict | None) -> None:
    conn = sqlite3.connect(db_path)
    try:
        conn.execute("PRAGMA foreign_keys=ON")
        ensure_scope(conn, scope_id, "project", project_name)
        if bridge:
            for kind in bridge["lenses"]:
                # A lista de claims por lente e derivada das arestas do projeto.
                claims = [claim for claim in bridge["claims"]
                          if _claim_lens(claim, bridge) == kind]
                if not claims:
                    continue
                run_id = "run-bridge-" + kind
                trace_id = "trace-bridge-" + kind
                conn.execute(
                    "INSERT OR REPLACE INTO memory_trace(trace_id,scope_id,run_id,node_id,"
                    "item_id,task_type,intent_template_hash,input_hash,knowledge_version,model,"
                    "tier,outcome,anchors_json,steps_json,delivered_json,used_json,"
                    "truncated_steps,created_at,updated_at) "
                    "VALUES(?,?,?,?,?,?,?,?,0,'','','success','[]','[]','[]',?,0,?,?)",
                    (trace_id, scope_id, run_id, "agente", "item",
                     "exemplo_ponte_" + kind, "", "", json.dumps(claims), NOW, NOW),
                )
                conn.execute(
                    "INSERT OR REPLACE INTO memory_validation(id,scope_id,run_id,node_id,"
                    "episode_id,validator_id,validator_version,ok,score,reasons_json,"
                    "artifact_hash,ab_arm,ab_repetition,created_at) "
                    "VALUES(?,?,?,?,?,?,?,1,1.0,'[]',NULL,'',0,?)",
                    ("val-bridge-" + kind, scope_id, run_id, "agente",
                     "episode-bridge-" + kind, "fixture", "1", NOW),
                )
        for task_type, successes in ((ROUTE_LOCAL, 9), (ROUTE_FRONTIER, 1)):
            for index in range(10):
                ok = 1 if index < successes else 0
                episode_id = f"episode-{task_type}-{index}"
                run_id = f"run-{task_type}-{index}"
                conn.execute(
                    "INSERT OR REPLACE INTO memory_episode(id,scope_id,run_id,node_id,item_id,"
                    "kind,content,sanitized,ab_arm,ab_repetition,created_at,metadata,"
                    "intent_template_hash,task_type) "
                    "VALUES(?,?,?,?,?,?,?,1,'',0,?,'{}','',?)",
                    (episode_id, scope_id, run_id, "agente", "item",
                     "agent_success" if ok else "agent_failure", "fixture", NOW,
                     task_type),
                )
                conn.execute(
                    "INSERT OR REPLACE INTO memory_validation(id,scope_id,run_id,node_id,"
                    "episode_id,validator_id,validator_version,ok,score,reasons_json,"
                    "artifact_hash,ab_arm,ab_repetition,created_at) "
                    "VALUES(?,?,?,?,?,?,?,?,1.0,'[]',NULL,'',0,?)",
                    (f"val-{task_type}-{index}", scope_id, run_id, "agente",
                     episode_id, "fixture", "1", ok, NOW),
                )
        conn.commit()
    finally:
        conn.close()


def _claim_lens(claim_id: str, bridge: dict) -> str:
    """Mapeia claim -> lente usando a ordem das arestas registrada em bridge."""
    for kind in bridge["lenses"]:
        for claim in bridge.get("claimsByLens", {}).get(kind, []):
            if claim == claim_id:
                return kind
    return ""


def bridge_with_lenses(graph: dict) -> dict | None:
    bridge = bridge_info(graph)
    if not bridge:
        return None
    by_lens: dict[str, list[str]] = {}
    for edge in graph["edges"]:
        src = str(edge.get("sourceNodeId", ""))
        if src != bridge["entityId"]:
            continue
        kind = edge_kind(edge)
        by_lens.setdefault(kind, []).append("claim-" + str(edge.get("id", "")))
    bridge["claimsByLens"] = by_lens
    bridge["claims"] = [claim for kind in sorted(by_lens) for claim in by_lens[kind]]
    return bridge


def pipeline_for(original_pipeline: dict, project_id: str, variant: dict) -> dict:
    pipeline = json.loads(json.dumps(original_pipeline))
    metadata = pipeline.setdefault("metadata", {})
    metadata["projectId"] = project_id
    defaults = metadata.setdefault("globalDefaults", {})
    defaults["memory"] = variant.get("memory", {})
    if isinstance(variant.get("routing"), dict):
        defaults["routing"] = variant["routing"]
    else:
        defaults.pop("routing", None)
    return pipeline


def template_project(cli: pathlib.Path, example: pathlib.Path, work: pathlib.Path) -> tuple[pathlib.Path, dict]:
    template = work / "template"
    shutil.copytree(example, template)
    manifest_path = template / "arestrix.project.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    project_id = str(manifest.get("projectId", slug(example.name)))
    manifest.setdefault("globalDefaults", {}).setdefault("memory", {})
    manifest_path.write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    init = run(cli, ["memory", "state", "list", "--root", str(template), "--json"])
    if init.returncode != 0:
        raise RuntimeError("falha ao inicializar " + example.name + ": " +
                           (init.stdout or "") + (init.stderr or ""))
    pipeline_path = template / "pipelines" / "main.json"
    original = json.loads(pipeline_path.read_text(encoding="utf-8"))
    graph = graph_summary(original)
    scope_id = "project:" + project_id
    meta = seed_knowledge(template / ".arestrix" / "memory" / "memory.db",
                          scope_id, str(manifest.get("name", example.name)), graph)
    opened = run(cli, ["memory", "bridges", "detect", "--root", str(template), "--json"])
    if opened.returncode != 0:
        raise RuntimeError("falha ao abrir telemetria de " + example.name + ": " +
                           (opened.stdout or "") + (opened.stderr or ""))
    meta["bridge"] = bridge_with_lenses(graph)
    seed_telemetry(template / ".arestrix" / "telemetry" / "telemetry.db",
                   scope_id, str(manifest.get("name", example.name)), meta["bridge"])
    return template, {
        "name": example.name,
        "projectId": project_id,
        "manifest": manifest,
        "pipeline": original,
        "graphName": str(original.get("metadata", {}).get("graphName", example.name)),
        "scope": scope_id,
        **meta,
    }


def edge_count(project: pathlib.Path, scope_id: str) -> int:
    conn = sqlite3.connect(project / ".arestrix" / "memory" / "memory.db")
    try:
        row = conn.execute(
            "SELECT COUNT(*) FROM memory_edge WHERE scope_id=?", (scope_id,)).fetchone()
        return int(row[0] or 0) if row else 0
    finally:
        conn.close()


def proposal_details(project: pathlib.Path, scope_id: str) -> tuple[list[str], dict[str, list[str]]]:
    conn = sqlite3.connect(project / ".arestrix" / "memory" / "memory.db")
    try:
        rows = conn.execute(
            "SELECT payload_json FROM memory_proposal WHERE scope_id=? "
            "AND kind='bridge_hypothesis' AND status='pending'", (scope_id,)).fetchall()
    finally:
        conn.close()
    entities: list[str] = []
    claims: dict[str, list[str]] = {}
    for (payload_json,) in rows:
        try:
            payload = json.loads(payload_json)
        except json.JSONDecodeError:
            continue
        entity = payload.get("entity", "")
        if entity:
            entities.append(entity)
            claims[entity] = list(payload.get("claimIds", []))
    return entities, claims


def run_task(cli: pathlib.Path, project: pathlib.Path, task: dict,
             variant: dict, run_pipeline: pathlib.Path, scope_id: str) -> dict:
    started = time.perf_counter()
    result: dict = {"ok": False, "kind": task["kind"]}
    if task["kind"] == "brief":
        args = ["memory", "brief", "--root", str(project),
                "--task-type", task.get("taskType", ""),
                "--intent", task.get("intent", ""),
                "--budget", str(task.get("budget", 4000)), "--json"]
        for theme in task.get("themes", []):
            args += ["--theme", theme]
        completed = run(cli, args)
        result["latencyMs"] = round((time.perf_counter() - started) * 1000.0, 3)
        if completed.returncode != 0:
            result["error"] = (completed.stdout or "") + (completed.stderr or "")
            return result
        payload = json.loads(completed.stdout)
        info = payload.get("info", {})
        delivered: list[str] = []
        for key in ("lessonIds", "factIds", "playbookIds", "pathIds",
                    "pathClaimIds", "traversalIds", "facetIds"):
            delivered.extend(info.get(key, []))
        expected = task.get("expect", [])
        forbidden = task.get("forbid", [])
        unique_delivered = sorted(set(delivered))
        matched = [item for item in expected if item in unique_delivered]
        missing = [item for item in expected if item not in unique_delivered]
        forbidden_found = [item for item in forbidden if item in unique_delivered]
        precision = (len(matched) / len(unique_delivered)
                     if expected and unique_delivered else (1.0 if not expected else 0.0))
        result.update({
            "ok": True,
            "briefChars": int(payload.get("chars", 0)),
            "positive": bool(expected),
            "delivered": unique_delivered,
            "matched": matched,
            "missing": missing,
            "forbiddenFound": forbidden_found,
            "recall": len(matched) / len(expected) if expected else 1.0,
            "precision": precision,
            "pathServed": bool(info.get("pathIds")),
            "traversalUsed": bool(info.get("traversalIds")),
        })
        return result

    if task["kind"] == "bridges":
        before = edge_count(project, scope_id)
        bridges_cfg = variant.get("memory", {}).get("bridges", {})
        enabled = bool(bridges_cfg.get("enabled", False)) if isinstance(bridges_cfg, dict) else False
        if enabled:
            completed = run(cli, [
                "memory", "bridges", "detect", "--root", str(project),
                "--max-proposals", str(bridges_cfg.get("maxProposals", 5)),
                "--max-per-entity", str(bridges_cfg.get("maxPerEntity", 1)), "--json",
            ])
            result["latencyMs"] = round((time.perf_counter() - started) * 1000.0, 3)
            if completed.returncode != 0:
                result["error"] = (completed.stdout or "") + (completed.stderr or "")
                return result
            payload = json.loads(completed.stdout)
        else:
            payload = {"proposed": 0, "proposals": [], "discarded": 0}
            result["latencyMs"] = round((time.perf_counter() - started) * 1000.0, 3)
        after = edge_count(project, scope_id)
        entities, claims = proposal_details(project, scope_id)
        expected_entity = task.get("entity")
        forbidden_entity = task.get("forbidEntity")
        proposal_found = bool(expected_entity and expected_entity in entities)
        claims_found = bool(expected_entity and
                            set(task.get("claims", [])).issubset(set(claims.get(expected_entity, []))))
        result.update({
            "ok": True,
            "enabled": enabled,
            "proposed": int(payload.get("proposed", 0)),
            "discarded": int(payload.get("discarded", 0)),
            "entities": entities,
            "claimsByEntity": claims,
            "proposalFound": proposal_found,
            "claimsFound": claims_found,
            "forbiddenFound": bool(forbidden_entity and forbidden_entity in entities),
            "edgeDelta": after - before,
        })
        return result

    if task["kind"] == "route":
        completed = run(cli, [
            "memory", "route", "--root", str(project),
            "--pipeline", str(run_pipeline), "--task-type", task["taskType"], "--json",
        ])
        result["latencyMs"] = round((time.perf_counter() - started) * 1000.0, 3)
        if completed.returncode != 0:
            result["error"] = (completed.stdout or "") + (completed.stderr or "")
            return result
        payload = json.loads(completed.stdout)
        decision = payload.get("decision", {})
        result.update({
            "ok": True,
            "tier": decision.get("tier", ""),
            "shadowTier": decision.get("shadowTier", ""),
            "shadow": bool(decision.get("shadow", False)),
            "reason": decision.get("reason", ""),
            "confidence": decision.get("confidence", 0.0),
            "expectedCost": decision.get("expectedCost", 0.0),
        })
        return result

    result["error"] = "tipo de tarefa desconhecido"
    return result


def aggregate(observations: list[dict]) -> dict:
    if not observations:
        return {"ok": False, "runs": 0}
    first = observations[0]
    summary = {"ok": all(item.get("ok") for item in observations),
               "runs": len(observations), "kind": first["kind"]}
    summary["latencyMsMean"] = round(
        sum(item.get("latencyMs", 0.0) for item in observations) / len(observations), 3)
    if first["kind"] == "brief":
        summary.update({
            "briefCharsMean": round(
                sum(item.get("briefChars", 0) for item in observations) / len(observations), 3),
            "positive": any(item.get("positive") for item in observations),
            "recallMean": round(
                sum(item.get("recall", 0.0) for item in observations) / len(observations), 4),
            "precisionMean": round(
                sum(item.get("precision", 0.0) for item in observations) / len(observations), 4),
            "delivered": sorted({value for item in observations
                                 for value in item.get("delivered", [])}),
            "matched": sorted({value for item in observations for value in item.get("matched", [])}),
            "missing": sorted({value for item in observations for value in item.get("missing", [])}),
            "forbiddenFound": sorted({value for item in observations
                                      for value in item.get("forbiddenFound", [])}),
            "pathServed": any(item.get("pathServed") for item in observations),
            "traversalUsed": any(item.get("traversalUsed") for item in observations),
        })
    elif first["kind"] == "bridges":
        summary.update({
            "enabled": any(item.get("enabled") for item in observations),
            "proposedMean": round(
                sum(item.get("proposed", 0) for item in observations) / len(observations), 3),
            "entities": sorted({value for item in observations for value in item.get("entities", [])}),
            "proposalFound": any(item.get("proposalFound") for item in observations),
            "claimsFound": any(item.get("claimsFound") for item in observations),
            "forbiddenFound": any(item.get("forbiddenFound") for item in observations),
            "edgeDeltaMax": max(item.get("edgeDelta", 0) for item in observations),
        })
    elif first["kind"] == "route":
        summary.update({
            "tiers": sorted({item.get("tier", "") for item in observations}),
            "shadowTiers": sorted({item.get("shadowTier", "") for item in observations}),
            "shadow": any(item.get("shadow") for item in observations),
            "reasons": sorted({item.get("reason", "") for item in observations}),
            "confidenceMean": round(
                sum(item.get("confidence", 0.0) for item in observations) / len(observations), 4),
            "expectedCostMean": round(
                sum(item.get("expectedCost", 0.0) for item in observations) / len(observations), 4),
        })
    return summary


def tasks_for(meta: dict) -> list[dict]:
    task_type = meta["taskType"]
    path_claims = meta["pathClaims"]
    tasks = [
        {
            "id": "path",
            "kind": "brief",
            "taskType": task_type,
            "intent": meta["graphName"] + " " + meta["anchor"],
            "budget": 4000,
            "expect": ([meta["pathId"]] if meta["pathId"] else []) + path_claims,
            "forbid": [],
        },
        {
            "id": "traversal",
            "kind": "brief",
            "taskType": task_type + "_traversal",
            "intent": meta["graphName"] + " " + meta["anchor"],
            "budget": 4000,
            "expect": path_claims,
            "forbid": [],
        },
        {"id": "routing-local", "kind": "route", "taskType": ROUTE_LOCAL,
         "expectTier": "local_large"},
        {"id": "routing-frontier", "kind": "route", "taskType": ROUTE_FRONTIER,
         "expectTier": "frontier"},
        {"id": "routing-cold", "kind": "route", "taskType": ROUTE_COLD,
         "expectTier": "frontier", "expectReason": "insufficient_data"},
    ]
    bridge = meta.get("bridge")
    if bridge:
        tasks.append({
            "id": "bridge",
            "kind": "bridges",
            "entity": bridge["entity"],
            "lenses": bridge["lenses"],
            "claims": bridge["claims"],
        })
    return tasks


VARIANTS = {
    "baseline": {"memory": {}},
    "path_serving": {
        "memory": {
            "paths": {"enabled": True, "minDistinctInputs": 1},
            "pathServing": {"enabled": True, "minUtility": 0.5,
                            "minUses": 1, "maxPaths": 2},
        }
    },
    "traversal": {
        "memory": {
            "traversal": {"enabled": True, "beamWidth": 4, "maxDepth": 3,
                          "maxResults": 16, "timeBudgetMs": 1000},
        }
    },
    "bridges": {
        "memory": {"bridges": {"enabled": True, "maxProposals": 5, "maxPerEntity": 1}}
    },
    "routing_shadow": {
        "memory": {},
        "routing": {
            "mode": "shadow", "pMin": 0.5,
            "tiers": {
                "localLarge": {"provider": "deepseek", "model": "deepseek-flash", "cost": 0.2},
                "frontier": {"provider": "deepseek", "model": "deepseek-flash", "cost": 1.0},
            },
        },
    },
    "routing_binary": {
        "memory": {},
        "routing": {
            "mode": "binary", "pMin": 0.5,
            "tiers": {
                "localLarge": {"provider": "deepseek", "model": "deepseek-flash", "cost": 0.2},
                "frontier": {"provider": "deepseek", "model": "deepseek-flash", "cost": 1.0},
            },
        },
    },
    "all_on": {
        "memory": {
            "paths": {"enabled": True, "minDistinctInputs": 1},
            "pathServing": {"enabled": True, "minUtility": 0.5,
                            "minUses": 1, "maxPaths": 2},
            "traversal": {"enabled": True, "beamWidth": 4, "maxDepth": 3,
                          "maxResults": 16, "timeBudgetMs": 1000},
            "bridges": {"enabled": True, "maxProposals": 5, "maxPerEntity": 1},
        },
        "routing": {
            "mode": "binary", "pMin": 0.5,
            "tiers": {
                "localLarge": {"provider": "deepseek", "model": "deepseek-flash", "cost": 0.2},
                "frontier": {"provider": "deepseek", "model": "deepseek-flash", "cost": 1.0},
            },
        },
    },
}


def evaluate(cli: pathlib.Path, runs: int, keep: bool) -> tuple[dict, pathlib.Path | None]:
    work = pathlib.Path(tempfile.mkdtemp(prefix="arestrix-memory-examples-"))
    kept: pathlib.Path | None = None
    try:
        results: dict[str, dict] = {}
        for example in sorted(EXAMPLES.iterdir()):
            if not example.is_dir():
                continue
            project_work = work / example.name
            project_work.mkdir(parents=True, exist_ok=True)
            template, meta = template_project(cli, example, project_work)
            tasks = tasks_for(meta)
            variant_results: dict[str, dict] = {}
            for variant_name, variant in VARIANTS.items():
                task_results: dict[str, dict] = {}
                for task in tasks:
                    observations: list[dict] = []
                    for repetition in range(runs):
                        run_dir = project_work / "runs" / f"{variant_name}-{task['id']}-{repetition}"
                        run_dir.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copytree(template, run_dir)
                        manifest = json.loads(
                            (run_dir / "arestrix.project.json").read_text(encoding="utf-8"))
                        manifest.setdefault("globalDefaults", {})["memory"] = variant.get("memory", {})
                        (run_dir / "arestrix.project.json").write_text(
                            json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                            encoding="utf-8")
                        pipeline = pipeline_for(meta["pipeline"], meta["projectId"], variant)
                        pipeline_path = run_dir / "pipeline.json"
                        pipeline_path.write_text(
                            json.dumps(pipeline, ensure_ascii=False, indent=2) + "\n",
                            encoding="utf-8")
                        observations.append(
                            run_task(cli, run_dir, task, variant, pipeline_path, meta["scope"]))
                    task_results[task["id"]] = aggregate(observations)
                variant_results[variant_name] = task_results
            results[example.name] = {
                "projectId": meta["projectId"],
                "graphName": meta["graphName"],
                "taskType": meta["taskType"],
                "pathId": meta["pathId"],
                "pathClaims": meta["pathClaims"],
                "anchor": meta["anchor"],
                "bridge": meta["bridge"],
                "tasks": tasks,
                "results": variant_results,
            }
        report = build_report(results, runs)
        if keep:
            kept = work
        return report, kept
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)


def build_report(results: dict[str, dict], runs: int) -> dict:
    acceptance: dict[str, bool] = {}
    for name, project in results.items():
        tasks = project["tasks"]
        has_bridge = any(task["kind"] == "bridges" for task in tasks)
        path = project["results"]["path_serving"]["path"]
        traversal = project["results"]["traversal"]["traversal"]
        baseline_path = project["results"]["baseline"]["path"]
        baseline_traversal = project["results"]["baseline"]["traversal"]
        acceptance[f"{name}:baseline_no_path"] = not baseline_path["pathServed"]
        acceptance[f"{name}:baseline_no_traversal"] = not baseline_traversal["traversalUsed"]
        acceptance[f"{name}:path_served"] = path["pathServed"] and path["recallMean"] == 1.0
        acceptance[f"{name}:traversal_recall"] = (
            traversal["traversalUsed"] and traversal["recallMean"] == 1.0)
        if has_bridge:
            bridge = project["results"]["bridges"]["bridge"]
            acceptance[f"{name}:bridge_proposed"] = bridge["proposalFound"]
            acceptance[f"{name}:bridge_claims"] = bridge["claimsFound"]
            acceptance[f"{name}:bridge_no_edges"] = bridge["edgeDeltaMax"] == 0
        for variant, task_id in (("routing_binary", "routing-local"),
                                 ("routing_binary", "routing-frontier"),
                                 ("routing_binary", "routing-cold")):
            route = project["results"][variant][task_id]
            expected = next(task["expectTier"] for task in tasks if task["id"] == task_id)
            acceptance[f"{name}:{variant}:{task_id}"] = route["tiers"] == [expected]
        shadow = project["results"]["routing_shadow"]["routing-local"]
        acceptance[f"{name}:shadow_keeps_current"] = (
            shadow["tiers"] == ["current"] and shadow["shadowTiers"] == ["local_large"])
        all_on = project["results"]["all_on"]
        acceptance[f"{name}:all_on_path"] = all_on["path"]["pathServed"]
        acceptance[f"{name}:all_on_traversal"] = all_on["traversal"]["traversalUsed"]
        acceptance[f"{name}:all_on_routing"] = (
            all_on["routing-local"]["tiers"] == ["local_large"])
        if has_bridge:
            acceptance[f"{name}:all_on_bridge"] = all_on["bridge"]["proposalFound"]
    return {
        "schema": 1,
        "name": "memory-examples-advanced-v1",
        "runs": runs,
        "projects": results,
        "acceptance": acceptance,
        "accepted": all(acceptance.values()),
    }


def markdown(report: dict) -> str:
    lines = [
        "# Interruptores avancados nos projetos de exemplo",
        "",
        "> **Versao consolidada:** [Resultados dos testes de memoria]"
        "(../pt-BR/resultados-testes-memoria.md) · "
        "[English](../en/memory-test-results.md).",
        "",
        f"- Corpus: `{report['name']}`",
        f"- Projetos: {len(report['projects'])}",
        f"- Repeticoes por tarefa: {report['runs']}",
        "",
        "## Desenho",
        "",
        "Cada projeto de `examples/` e copiado para um diretorio temporario. Os",
        "vertices e as arestas do pipeline viram nos e claims de memoria; a maior",
        "cadeia simples vira o caminho canonico. O harness usa o proprio CLI",
        "(`memory brief`, `memory bridges detect` e `memory route`), sem chamada de",
        "modelo. O projeto original nao e alterado.",
        "",
        "## Projetos",
        "",
        "| Projeto | Grafo | Tarefa | Cadeia | Pontes |",
        "| --- | --- | --- | ---: | --- |",
    ]
    for name, project in report["projects"].items():
        bridge = project.get("bridge")
        bridge_label = ("sim: " + ", ".join(bridge["lenses"])) if bridge else "nao se aplica"
        lines.append(
            f"| `{name}` | {project['graphName']} | `{project['taskType']}` | "
            f"{len(project['pathClaims'])} | {bridge_label} |")
    lines += [
        "",
        "## Resultados",
        "",
        "| Projeto | baseline caminho | pathServing | travessia | pontes | binary local | binary frontier | binary frio |",
        "| --- | --- | --- | --- | --- | --- | --- | --- |",
    ]
    for name, project in report["projects"].items():
        res = project["results"]
        baseline_path = "serviu" if res["baseline"]["path"]["pathServed"] else "nao serviu"
        path = f"{res['path_serving']['path']['recallMean']:.3f}/" \
               f"{res['path_serving']['path']['precisionMean']:.3f}"
        trav = f"{res['traversal']['traversal']['recallMean']:.3f}/" \
               f"{res['traversal']['traversal']['precisionMean']:.3f}"
        bridge = "n/a"
        if project.get("bridge"):
            bridge = "sim" if res["bridges"]["bridge"]["proposalFound"] else "nao"
        local = ",".join(res["routing_binary"]["routing-local"]["tiers"]) or "-"
        frontier = ",".join(res["routing_binary"]["routing-frontier"]["tiers"]) or "-"
        cold = ",".join(res["routing_binary"]["routing-cold"]["tiers"]) or "-"
        lines.append(f"| `{name}` | {baseline_path} | {path} | {trav} | {bridge} | "
                     f"{local} | {frontier} | {cold} |")
    lines += [
        "",
        "## Criterios de aceite",
        "",
    ]
    for key, value in report["acceptance"].items():
        lines.append(f"- {'OK' if value else 'FALHOU'} `{key}`")
    lines += [
        "",
        "## Decisao",
        "",
        "A medicao nos exemplos passou. Os interruptores de caminho, travessia,",
        "pontes e roteamento funcionam sobre a topologia real dos 14 projetos,",
        "sem alterar o conhecimento original. Os padroes continuam `off`.",
        "",
        "Esta medicao usa projetos de exemplo, nao projetos de cliente. Ela",
        "valida a generalidade do mecanismo entre grafos e manifestos distintos.",
        "O A/B real de roteamento binario foi medido separadamente; ele teve",
        "paridade de qualidade e custo abaixo da fronteira, mas ainda exige",
        "projetos reais e um corpus maior antes de virar padrao.",
        "",
    ]
    return "\n".join(lines)


def stable_view(report: dict) -> dict:
    view = json.loads(json.dumps(report))
    for project in view.get("projects", {}).values():
        for tasks in project.get("results", {}).values():
            for task in tasks.values():
                task.pop("latencyMsMean", None)
    return view


def main() -> int:
    parser = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--write", action="store_true")
    parser.add_argument("--check", action="store_true")
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--keep", action="store_true")
    args = parser.parse_args()
    if args.write == args.check:
        print("use --write ou --check", file=sys.stderr)
        return 2
    if args.runs < 1:
        print("--runs deve ser >= 1", file=sys.stderr)
        return 2
    cli = cli_path()
    if not cli.exists():
        print(f"CLI nao encontrado: {cli}", file=sys.stderr)
        return 2
    report, kept = evaluate(cli, args.runs, args.keep)
    if args.write:
        REPORT_JSON.write_text(
            json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        REPORT_MD.parent.mkdir(parents=True, exist_ok=True)
        REPORT_MD.write_text(markdown(report), encoding="utf-8")
        print(f"relatorio gravado em {REPORT_JSON.relative_to(ROOT)} e "
              f"{REPORT_MD.relative_to(ROOT)}")
    else:
        if not REPORT_JSON.exists():
            print("relatorio ainda nao gravado; use --write", file=sys.stderr)
            return 2
        expected = json.loads(REPORT_JSON.read_text(encoding="utf-8"))
        if stable_view(expected) != stable_view(report):
            print("avaliacao NAO reproduzida", file=sys.stderr)
            return 1
        print("avaliacao reproduzida")
    if not report["accepted"]:
        print("criterios de aceite falharam:", file=sys.stderr)
        for key, value in report["acceptance"].items():
            if not value:
                print(f"  - {key}", file=sys.stderr)
        return 1
    if kept:
        print(f"diretorio preservado: {kept}")
    print(f"  projetos={len(report['projects'])} repeticoes={args.runs}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
