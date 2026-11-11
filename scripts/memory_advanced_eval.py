#!/usr/bin/env python3
"""Avaliacao de mecanismos dos interruptores avancados da memoria.

Mede, com um corpus curado e um banco congelado, o que cada interruptor muda:

- ``pathServing``: caminhos elegiveis entram no brief antes da travessia;
- ``traversal``: consultas sem caminho recebem um pacote de travessia em feixe;
- ``bridges``: manutencao propoe hipoteses interdominio sem criar arestas;
- ``routing.mode``: ``shadow`` registra a decisao e ``binary`` troca o tier.

O harness usa o proprio CLI (``memory brief``, ``memory route`` e
``memory bridges detect``), sem chamada de modelo. Isso mede o mecanismo de
recuperacao e roteamento de forma deterministica; a qualidade final com um
modelo real continua sendo outro experimento.

Uso:
  python scripts/memory_advanced_eval.py --write
  python scripts/memory_advanced_eval.py --check
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
FIXTURE = ROOT / "tests" / "fixtures" / "memory-advanced-v1.json"
REPORT_JSON = ROOT / "tests" / "fixtures" / "memory-advanced-report.json"
REPORT_MD = ROOT / "docs" / "quality" / "memory-advanced-ab.md"
PROJECT_ID = "memoria-avancada"
SCOPE_ID = "project:" + PROJECT_ID
NOW = "2026-11-11T00:00:00Z"


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


def add_node(conn: sqlite3.Connection, node_id: str, kind: str, name: str) -> None:
    conn.execute(
        "INSERT OR IGNORE INTO memory_node(id,scope_id,kind,canonical_name,aliases_json,"
        "created_at,updated_at,metadata) VALUES(?,?,?,?,?,?,?,?)",
        (node_id, SCOPE_ID, kind, name, "[]", NOW, NOW, "{}"),
    )
    conn.execute("DELETE FROM memory_node_fts WHERE node_id=?", (node_id,))
    conn.execute(
        "INSERT INTO memory_node_fts(node_id,name,aliases,metadata) VALUES(?,?,?,?)",
        (node_id, name, "", "{}"),
    )
    conn.execute(
        "INSERT OR IGNORE INTO memory_alias(scope_id,alias,node_id,source) VALUES(?,?,?,?)",
        (SCOPE_ID, normalize_comparable(name), node_id, "canonical"),
    )


def add_lens(conn: sqlite3.Connection, lens_id: str, name: str) -> None:
    conn.execute(
        "INSERT OR IGNORE INTO memory_lens(id,scope_id,name,description,version,"
        "created_at,updated_at) VALUES(?,?,?,?,?,?,?)",
        (lens_id, SCOPE_ID, name, "", 1, NOW, NOW),
    )


def add_edge(conn: sqlite3.Connection, edge_id: str, src: str, dst: str,
             relation: str, lens: str, access: str | None = None,
             valid_to: str | None = None) -> None:
    metadata = {"claim": True, "lens": lens}
    if access:
        metadata["access"] = access
    edge_hash = sha256_text("|".join([SCOPE_ID, src, dst, relation, "", NOW, ""]))
    conn.execute(
        "INSERT OR REPLACE INTO memory_edge(id,scope_id,src_id,dst_id,relation,value_json,"
        "valid_from,valid_to,recorded_at,confidence,status,superseded_by,source_kind,"
        "source_ref,evidence_hash,edge_hash,metadata,last_used,success_count,failure_count,"
        "utility,dormant_at) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
        (edge_id, SCOPE_ID, src, dst, relation, None, NOW, valid_to, NOW, 0.8,
         "active", None, "claim", "tests/fixtures/memory-advanced-v1.json", None,
         edge_hash, json.dumps(metadata, ensure_ascii=False), None, 0, 0, 0.5, None),
    )


def seed_knowledge(db_path: pathlib.Path) -> None:
    conn = sqlite3.connect(db_path)
    try:
        conn.execute("PRAGMA foreign_keys=ON")
        conn.execute(
            "INSERT OR IGNORE INTO memory_scope(id,kind,parent_id,name,created_at,metadata) "
            "VALUES(?,?,?,?,?,?)",
            (SCOPE_ID, "project", None, PROJECT_ID, NOW, "{}"),
        )
        conn.execute(
            "INSERT OR IGNORE INTO memory_scope_closure(ancestor_id,descendant_id,depth) "
            "VALUES(?,?,0)", (SCOPE_ID, SCOPE_ID),
        )
        for lens_id, name in (("lens-contraste", "contraste"),
                              ("lens-tipografia", "tipografia"),
                              ("lens-seguranca", "seguranca"),
                              ("lens-publico", "publico"),
                              ("lens-origem", "origem")):
            add_lens(conn, lens_id, name)

        for node_id, name in (
            ("node-guia", "Guia"),
            ("node-contraste", "4.5:1"),
            ("node-wcag", "WCAG AA"),
            ("node-passo-1", "Passo 1"),
            ("node-passo-2", "Passo 2"),
            ("node-regra", "Regra 7731"),
            ("node-tipografia", "Tipografia X"),
            ("node-relatorio", "Relatorio"),
            ("node-fonte-a", "Fonte A"),
            ("node-fonte-b", "Fonte B"),
            ("node-chave", "Chave"),
            ("node-segredo", "Segredo Local"),
            ("node-nota", "Nota Publica"),
            ("node-expirado", "Contraste Expirado"),
        ):
            add_node(conn, node_id, "entity", name)

        add_edge(conn, "claim-guia-contraste", "node-guia", "node-contraste",
                 "HAS_CONTRAST", "contraste")
        add_edge(conn, "claim-contraste-wcag", "node-contraste", "node-wcag",
                 "ATENDE", "contraste")
        add_edge(conn, "claim-tipografia", "node-guia", "node-tipografia",
                 "USA", "tipografia")
        add_edge(conn, "claim-chain-1", "node-guia", "node-passo-1",
                 "DERIVA_DE", "origem")
        add_edge(conn, "claim-chain-2", "node-passo-1", "node-passo-2",
                 "DERIVA_DE", "origem")
        add_edge(conn, "claim-chain-3", "node-passo-2", "node-regra",
                 "CONFIRMA", "origem")
        add_edge(conn, "claim-chain-local", "node-passo-1", "node-segredo",
                 "DEPENDE_DE", "origem", access="local")
        add_edge(conn, "claim-relatorio-a", "node-relatorio", "node-fonte-a",
                 "USA", "contraste")
        add_edge(conn, "claim-relatorio-b", "node-relatorio", "node-fonte-b",
                 "USA", "contraste")
        add_edge(conn, "claim-chave-local", "node-chave", "node-segredo",
                 "CONTEM", "seguranca", access="local")
        add_edge(conn, "claim-chave-publica", "node-chave", "node-nota",
                 "CONTEM", "publico", access="public")
        add_edge(conn, "claim-expirado", "node-guia", "node-expirado",
                 "HAS_CONTRAST", "contraste", valid_to="2020-01-01T00:00:00Z")

        conn.execute(
            "INSERT OR IGNORE INTO memory_edge_lens(edge_id,lens_id) VALUES(?,?)",
            ("claim-guia-contraste", "lens-contraste"),
        )
        conn.execute(
            "INSERT OR IGNORE INTO memory_edge_lens(edge_id,lens_id) VALUES(?,?)",
            ("claim-tipografia", "lens-tipografia"),
        )
        conn.execute(
            "INSERT OR IGNORE INTO memory_edge_lens(edge_id,lens_id) VALUES(?,?)",
            ("claim-relatorio-a", "lens-contraste"),
        )
        conn.execute(
            "INSERT OR IGNORE INTO memory_edge_lens(edge_id,lens_id) VALUES(?,?)",
            ("claim-relatorio-b", "lens-contraste"),
        )
        conn.execute(
            "INSERT OR IGNORE INTO memory_edge_lens(edge_id,lens_id) VALUES(?,?)",
            ("claim-chave-local", "lens-seguranca"),
        )
        conn.execute(
            "INSERT OR IGNORE INTO memory_edge_lens(edge_id,lens_id) VALUES(?,?)",
            ("claim-chave-publica", "lens-publico"),
        )

        conn.execute(
            "INSERT OR IGNORE INTO memory_path_schema(id,scope_id,signature,created_at) "
            "VALUES(?,?,?,?)", ("psch-guia", SCOPE_ID, "path-guia", NOW),
        )
        conn.execute(
            "INSERT OR REPLACE INTO memory_path(id,scope_id,revision,schema_id,task_type,"
            "state,intent_sample,access_json,created_at,updated_at) "
            "VALUES(?,?,?,?,?,?,?,?,?,?)",
            ("path-guia", SCOPE_ID, 1, "psch-guia", "revisar_contraste", "active",
             "", "{}", NOW, NOW),
        )
        conn.execute(
            "INSERT OR REPLACE INTO memory_path_stats(path_id,success,failure,last_tau,"
            "uses,distinct_inputs) VALUES(?,?,?,?,?,?)",
            ("path-guia", 9, 0, 0, 9, 9),
        )
        for ordinal, claim_id in enumerate(("claim-guia-contraste", "claim-contraste-wcag")):
            conn.execute(
                "INSERT OR REPLACE INTO memory_path_claim(path_id,ordinal,claim_id,"
                "claim_revision) VALUES(?,?,?,?)",
                ("path-guia", ordinal, claim_id, ""),
            )
            conn.execute(
                "INSERT OR IGNORE INTO memory_claim_path(claim_id,path_id) VALUES(?,?)",
                (claim_id, "path-guia"),
            )

        conn.execute(
            "INSERT OR IGNORE INTO memory_path_schema(id,scope_id,signature,created_at) "
            "VALUES(?,?,?,?)", ("psch-expirado", SCOPE_ID, "path-expirado", NOW),
        )
        conn.execute(
            "INSERT OR REPLACE INTO memory_path(id,scope_id,revision,schema_id,task_type,"
            "state,intent_sample,access_json,created_at,updated_at) "
            "VALUES(?,?,?,?,?,?,?,?,?,?)",
            ("path-expirado", SCOPE_ID, 1, "psch-expirado", "revisar_expirado", "active",
             "", "{}", NOW, NOW),
        )
        conn.execute(
            "INSERT OR REPLACE INTO memory_path_stats(path_id,success,failure,last_tau,"
            "uses,distinct_inputs) VALUES(?,?,?,?,?,?)",
            ("path-expirado", 9, 0, 0, 9, 9),
        )
        conn.execute(
            "INSERT OR REPLACE INTO memory_path_claim(path_id,ordinal,claim_id,"
            "claim_revision) VALUES(?,?,?,?)",
            ("path-expirado", 0, "claim-expirado", ""),
        )
        conn.execute(
            "INSERT OR IGNORE INTO memory_claim_path(claim_id,path_id) VALUES(?,?)",
            ("claim-expirado", "path-expirado"),
        )
        conn.commit()
    finally:
        conn.close()


def seed_telemetry(db_path: pathlib.Path) -> None:
    conn = sqlite3.connect(db_path)
    try:
        conn.execute("PRAGMA foreign_keys=ON")
        conn.execute(
            "INSERT OR IGNORE INTO memory_scope(id,kind,parent_id,name,created_at,metadata) "
            "VALUES(?,?,?,?,?,?)",
            (SCOPE_ID, "project", None, PROJECT_ID, NOW, "{}"),
        )
        conn.execute(
            "INSERT OR IGNORE INTO memory_scope_closure(ancestor_id,descendant_id,depth) "
            "VALUES(?,?,0)", (SCOPE_ID, SCOPE_ID),
        )
        for index, (used, task_type) in enumerate((
            (["claim-guia-contraste", "claim-tipografia"], "analisar_guia"),
            (["claim-relatorio-a", "claim-relatorio-b"], "analisar_relatorio"),
            (["claim-chave-local", "claim-chave-publica"], "analisar_chave"),
        )):
            run_id = f"run-bridge-{index}"
            trace_id = f"trace-bridge-{index}"
            conn.execute(
                "INSERT OR REPLACE INTO memory_trace(trace_id,scope_id,run_id,node_id,"
                "item_id,task_type,intent_template_hash,input_hash,knowledge_version,model,"
                "tier,outcome,anchors_json,steps_json,delivered_json,used_json,"
                "truncated_steps,created_at,updated_at) "
                "VALUES(?,?,?,?,?,?,?,?,0,'','','success','[]','[]','[]',?,0,?,?)",
                (trace_id, SCOPE_ID, run_id, "agente", "item", task_type, "", "",
                 json.dumps(used), NOW, NOW),
            )
            conn.execute(
                "INSERT OR REPLACE INTO memory_validation(id,scope_id,run_id,node_id,"
                "episode_id,validator_id,validator_version,ok,score,reasons_json,"
                "artifact_hash,ab_arm,ab_repetition,created_at) "
                "VALUES(?,?,?,?,?,?,?,1,1.0,'[]',NULL,'',0,?)",
                (f"val-bridge-{index}", SCOPE_ID, run_id, "agente",
                 f"episode-bridge-{index}", "fixture", "1", NOW),
            )

        for index in range(10):
            ok = 1 if index < 9 else 0
            episode_id = f"episode-local-{index}"
            conn.execute(
                "INSERT OR REPLACE INTO memory_episode(id,scope_id,run_id,node_id,item_id,"
                "kind,content,sanitized,ab_arm,ab_repetition,created_at,metadata,"
                "intent_template_hash,task_type) "
                "VALUES(?,?,?,?,?,?,?,1,'',0,?,'{}','',?)",
                (episode_id, SCOPE_ID, f"run-local-{index}", "agente", "item",
                 "agent_success" if ok else "agent_failure", "fixture", NOW,
                 "rotear_local"),
            )
            conn.execute(
                "INSERT OR REPLACE INTO memory_validation(id,scope_id,run_id,node_id,"
                "episode_id,validator_id,validator_version,ok,score,reasons_json,"
                "artifact_hash,ab_arm,ab_repetition,created_at) "
                "VALUES(?,?,?,?,?,?,?,?,1.0,'[]',NULL,'',0,?)",
                (f"val-local-{index}", SCOPE_ID, f"run-local-{index}", "agente",
                 episode_id, "fixture", "1", ok, NOW),
            )
        for index in range(10):
            ok = 1 if index < 1 else 0
            episode_id = f"episode-frontier-{index}"
            conn.execute(
                "INSERT OR REPLACE INTO memory_episode(id,scope_id,run_id,node_id,item_id,"
                "kind,content,sanitized,ab_arm,ab_repetition,created_at,metadata,"
                "intent_template_hash,task_type) "
                "VALUES(?,?,?,?,?,?,?,1,'',0,?,'{}','',?)",
                (episode_id, SCOPE_ID, f"run-frontier-{index}", "agente", "item",
                 "agent_success" if ok else "agent_failure", "fixture", NOW,
                 "rotear_frontier"),
            )
            conn.execute(
                "INSERT OR REPLACE INTO memory_validation(id,scope_id,run_id,node_id,"
                "episode_id,validator_id,validator_version,ok,score,reasons_json,"
                "artifact_hash,ab_arm,ab_repetition,created_at) "
                "VALUES(?,?,?,?,?,?,?,?,1.0,'[]',NULL,'',0,?)",
                (f"val-frontier-{index}", SCOPE_ID, f"run-frontier-{index}", "agente",
                 episode_id, "fixture", "1", ok, NOW),
            )
        conn.commit()
    finally:
        conn.close()


def pipeline_for(variant: dict) -> dict:
    routing = variant.get("routing")
    defaults: dict = {"model": {"provider": "deepseek", "model": "deepseek-flash"}}
    if isinstance(routing, dict):
        defaults["routing"] = routing
    return {
        "version": "1.0.0",
        "metadata": {
            "projectId": PROJECT_ID,
            "graphName": "MemoryAdvancedEval",
            "globalDefaults": defaults,
        },
        "nodes": [],
        "edges": [],
    }


def template_project(cli: pathlib.Path, work: pathlib.Path) -> pathlib.Path:
    project = work / "template"
    project.mkdir()
    manifest = {
        "projectId": PROJECT_ID,
        "name": "Memoria Avancada",
        "globalDefaults": {"memory": {}},
    }
    (project / "arestrix.project.json").write_text(
        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    init = run(cli, ["memory", "state", "list", "--root", str(project), "--json"])
    if init.returncode != 0:
        raise RuntimeError("falha ao inicializar o projeto: " + (init.stdout or "") + (init.stderr or ""))
    seed_knowledge(project / ".arestrix" / "memory" / "memory.db")
    # Abre a telemetria e cria o schema antes de semear os traces.
    opened = run(cli, ["memory", "bridges", "detect", "--root", str(project), "--json"])
    if opened.returncode != 0:
        raise RuntimeError("falha ao abrir a telemetria: " + (opened.stdout or "") + (opened.stderr or ""))
    seed_telemetry(project / ".arestrix" / "telemetry" / "telemetry.db")
    return project


def edge_count(project: pathlib.Path) -> int:
    conn = sqlite3.connect(project / ".arestrix" / "memory" / "memory.db")
    try:
        row = conn.execute(
            "SELECT COUNT(*) FROM memory_edge WHERE scope_id=?", (SCOPE_ID,)).fetchone()
        return int(row[0] or 0) if row else 0
    finally:
        conn.close()


def proposal_details(project: pathlib.Path) -> tuple[list[str], dict[str, list[str]]]:
    conn = sqlite3.connect(project / ".arestrix" / "memory" / "memory.db")
    try:
        rows = conn.execute(
            "SELECT payload_json FROM memory_proposal WHERE scope_id=? "
            "AND kind='bridge_hypothesis' AND status='pending'", (SCOPE_ID,)).fetchall()
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
             variant: dict, run_pipeline: pathlib.Path) -> dict:
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
        before = edge_count(project)
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
        after = edge_count(project)
        entities, claims = proposal_details(project)
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


def evaluate(cli: pathlib.Path, spec: dict, runs: int, keep: bool) -> tuple[dict, pathlib.Path | None]:
    work = pathlib.Path(tempfile.mkdtemp(prefix="arestrix-memory-advanced-"))
    kept: pathlib.Path | None = None
    try:
        template = template_project(cli, work)
        variants = spec["variants"]
        results: dict[str, dict] = {}
        for variant_name, variant in variants.items():
            variant_result: dict[str, dict] = {}
            for task in spec["tasks"]:
                observations: list[dict] = []
                for repetition in range(runs):
                    run_dir = work / "runs" / f"{variant_name}-{task['id']}-{repetition}"
                    run_dir.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copytree(template, run_dir)
                    manifest = json.loads(
                        (run_dir / "arestrix.project.json").read_text(encoding="utf-8"))
                    manifest.setdefault("globalDefaults", {})["memory"] = variant.get("memory", {})
                    (run_dir / "arestrix.project.json").write_text(
                        json.dumps(manifest, ensure_ascii=False, indent=2) + "\n",
                        encoding="utf-8")
                    pipeline = run_dir / "pipeline.json"
                    pipeline.write_text(
                        json.dumps(pipeline_for(variant), ensure_ascii=False, indent=2) + "\n",
                        encoding="utf-8")
                    observations.append(run_task(cli, run_dir, task, variant, pipeline))
                variant_result[task["id"]] = aggregate(observations)
            results[variant_name] = variant_result
        report = build_report(spec, results, runs)
        if keep:
            kept = work
        return report, kept
    finally:
        if not keep:
            shutil.rmtree(work, ignore_errors=True)


def variant_summary(variant: dict[str, dict]) -> dict:
    brief_tasks = [task for task in variant.values() if task.get("kind") == "brief"]
    positive = [task for task in brief_tasks if task.get("positive")]
    negative = [task for task in brief_tasks if not task.get("positive")]
    return {
        "briefTasks": len(brief_tasks),
        "positiveTasks": len(positive),
        "negativeTasks": len(negative),
        "positiveRecallMean": round(
            sum(task.get("recallMean", 0.0) for task in positive) / len(positive), 4)
        if positive else 0.0,
        "precisionMean": round(
            sum(task.get("precisionMean", 0.0) for task in positive) / len(positive), 4)
        if positive else 0.0,
        "negativePass": all(not task.get("forbiddenFound") for task in negative),
        "forbiddenTotal": sum(len(task.get("forbiddenFound", [])) for task in brief_tasks),
        "briefCharsMean": round(
            sum(task.get("briefCharsMean", 0.0) for task in brief_tasks) / len(brief_tasks), 3)
        if brief_tasks else 0.0,
        "pathServedTasks": sum(1 for task in brief_tasks if task.get("pathServed")),
        "traversalTasks": sum(1 for task in brief_tasks if task.get("traversalUsed")),
    }


def build_report(spec: dict, results: dict[str, dict], runs: int) -> dict:
    summaries = {name: variant_summary(tasks) for name, tasks in results.items()}
    acceptance = {
        "baselinePathRecallZero": results["baseline"]["path-contrast"]["recallMean"] == 0.0,
        "pathServingRecallOne": results["path_serving"]["path-contrast"]["recallMean"] == 1.0,
        "pathServingPrecisionOne":
            results["path_serving"]["path-contrast"]["precisionMean"] == 1.0,
        "pathExpiredNotServed": not results["path_serving"]["path-expired"]["forbiddenFound"],
        "baselineTraversalRecallZero":
            results["baseline"]["traversal-chain"]["recallMean"] == 0.0,
        "traversalRecallOne": results["traversal"]["traversal-chain"]["recallMean"] == 1.0,
        "traversalPrecisionAtLeastHalf":
            results["traversal"]["traversal-chain"]["precisionMean"] >= 0.5,
        "traversalNoAnchorClean":
            not results["traversal"]["traversal-no-anchor"]["forbiddenFound"],
        "traversalAccessClean":
            "claim-chain-local" not in results["traversal"]["traversal-chain"]["delivered"],
        "bridgeProposalFound": results["bridges"]["bridge-cross-lens"]["proposalFound"],
        "bridgeClaimsFound": results["bridges"]["bridge-cross-lens"]["claimsFound"],
        "bridgeSameLensClean": not results["bridges"]["bridge-same-lens"]["forbiddenFound"],
        "bridgeAccessClean": not results["bridges"]["bridge-access-mismatch"]["forbiddenFound"],
        "bridgeNoEdgeCreation": results["bridges"]["bridge-cross-lens"]["edgeDeltaMax"] == 0,
        "routingBinaryLocal": results["routing_binary"]["routing-local"]["tiers"] == ["local_large"],
        "routingBinaryFrontier":
            results["routing_binary"]["routing-frontier"]["tiers"] == ["frontier"],
        "routingBinaryCold": results["routing_binary"]["routing-cold"]["tiers"] == ["frontier"],
        "routingShadowLocal":
            results["routing_shadow"]["routing-local"]["shadowTiers"] == ["local_large"],
        "routingShadowKeepsCurrent":
            results["routing_shadow"]["routing-local"]["tiers"] == ["current"],
        "routingShadowColdKeepsCurrent":
            results["routing_shadow"]["routing-cold"]["tiers"] == ["current"] and
            results["routing_shadow"]["routing-cold"]["shadowTiers"] == ["frontier"],
        "allOnPath": results["all_on"]["path-contrast"]["recallMean"] == 1.0,
        "allOnTraversal": results["all_on"]["traversal-chain"]["recallMean"] == 1.0,
        "allOnBridge": results["all_on"]["bridge-cross-lens"]["proposalFound"],
        "allOnRouting": results["all_on"]["routing-local"]["tiers"] == ["local_large"],
    }
    return {
        "schema": 1,
        "name": spec["name"],
        "seed": spec["seed"],
        "projectId": spec["projectId"],
        "runs": runs,
        "tasks": spec["tasks"],
        "variants": spec["variants"],
        "results": results,
        "summaries": summaries,
        "acceptance": acceptance,
        "accepted": all(acceptance.values()),
    }


def markdown(report: dict) -> str:
    lines = [
        "# Interruptores avancados da memoria: avaliacao de mecanismos",
        "",
        "> **Versao consolidada:** [Resultados dos testes de memoria]"
        "(../pt-BR/resultados-testes-memoria.md) · "
        "[English](../en/memory-test-results.md).",
        "",
        f"- Corpus: `{report['name']}` (semente {report['seed']})",
        f"- Repeticoes por tarefa: {report['runs']}",
        f"- Tarefas: {len(report['tasks'])}",
        "",
        "## Desenho",
        "",
        "O harness usa o proprio CLI, sem chamada de modelo: `memory brief` para",
        "caminhos e travessia, `memory bridges detect` para hipoteses e",
        "`memory route` para a decisao local/fronteira. O banco e copiado do",
        "template congelado antes de cada tarefa; nenhuma medicao altera o",
        "conhecimento original.",
        "",
        "## Tarefas",
        "",
        "| Tarefa | Tipo | O que exige |",
        "| --- | --- | --- |",
    ]
    for task in report["tasks"]:
        if task["kind"] == "brief":
            need = "recall de " + ", ".join(task.get("expect", [])) if task.get("expect") else "nao injetar itens proibidos"
        elif task["kind"] == "bridges":
            need = "hipotese para " + task["entity"] if task.get("entity") else "nao propor " + task.get("forbidEntity", "")
        else:
            need = "tier " + task.get("expectTier", "")
        lines.append(f"| `{task['id']}` | {task['kind']} | {need} |")

    lines += [
        "",
        "## Resultados",
        "",
        "| Variante | Recall positivo | Precisao | Negativos OK | Itens proibidos | Brief (chars) | Caminhos | Travessias |",
        "| --- | ---: | ---: | --- | ---: | ---: | ---: | ---: |",
    ]
    for name, summary in report["summaries"].items():
        lines.append(
            f"| {name} | {summary['positiveRecallMean']:.3f} | "
            f"{summary['precisionMean']:.3f} | {summary['negativePass']} | "
            f"{summary['forbiddenTotal']} | {summary['briefCharsMean']:.1f} | "
            f"{summary['pathServedTasks']} | {summary['traversalTasks']} |")

    lines += [
        "",
        "### Por tarefa (recall/precisao)",
        "",
        "| Tarefa | baseline | path_serving | traversal | all_on |",
        "| --- | ---: | ---: | ---: | ---: |",
    ]
    for task_id in ("path-contrast", "traversal-chain"):
        cells = []
        for variant_name in ("baseline", "path_serving", "traversal", "all_on"):
            task = report["results"][variant_name][task_id]
            cells.append(f"{task['recallMean']:.3f}/{task['precisionMean']:.3f}")
        lines.append(f"| `{task_id}` | " + " | ".join(cells) + " |")

    lines += [
        "",
        "## Pontes e roteamento",
        "",
        "| Variante | Ponte esperada | ClaimIds | Entidades proibidas | Delta de arestas |",
        "| --- | --- | --- | --- | ---: |",
    ]
    for name, tasks in report["results"].items():
        bridge = tasks["bridge-cross-lens"]
        lines.append(
            f"| {name} | {bridge['proposalFound']} | {bridge['claimsFound']} | "
            f"{tasks['bridge-same-lens']['forbiddenFound'] or tasks['bridge-access-mismatch']['forbiddenFound']} | "
            f"{bridge['edgeDeltaMax']} |")
    lines += [
        "",
        "| Variante | routing-local | routing-frontier | routing-cold | shadow local |",
        "| --- | --- | --- | --- | --- |",
    ]
    for name, tasks in report["results"].items():
        local = tasks["routing-local"]
        frontier = tasks["routing-frontier"]
        cold = tasks["routing-cold"]
        lines.append(
            f"| {name} | {local['tiers']} | {frontier['tiers']} | {cold['tiers']} | "
            f"{local['shadowTiers']} |")

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
        ("A avaliacao de mecanismos passou. `pathServing` e `traversal` "
         "entregam o conteudo esperado sem violar acesso nem injetar itens "
         "expirados; `bridges` produz somente hipoteses e nao cria arestas; "
         "`routing.shadow` nao troca o tier executado e `routing.binary` "
         "escolhe local/fronteira conforme a confianca."
         if report["accepted"] else
         "A avaliacao encontrou falhas. Os interruptores devem permanecer "
         "desligados por padrao ate a correcao e uma nova medicao."),
        "",
        "Esta medicao nao substitui um A/B de qualidade com modelo real. Ela",
        "valida o mecanismo de recuperacao e roteamento; a promocao de",
        "`routing.mode=binary` para padrao ainda exige custo e qualidade medidos",
        "com o modelo escolhido.",
        "",
    ]
    return "\n".join(lines)


def stable_view(report: dict) -> dict:
    """Remove latencia e outros campos nao deterministicos para o --check."""
    view = json.loads(json.dumps(report))
    for tasks in view.get("results", {}).values():
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
    spec = json.loads(FIXTURE.read_text(encoding="utf-8"))
    report, kept = evaluate(cli, spec, args.runs, args.keep)
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
    print(f"  variantes={len(report['variants'])} tarefas={len(report['tasks'])} "
          f"repeticoes={args.runs}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
