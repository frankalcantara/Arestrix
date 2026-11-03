#!/usr/bin/env python3
"""Confere o fixture SQL memory-v4 por um dump canonico e SHA-256.

Uso:
  python scripts/memory_fixture_check.py
  python scripts/memory_fixture_check.py --write

O fixture e montado em diretorios temporarios; nenhum .db entra no repositorio.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import sqlite3
import sys
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FIXTURE = ROOT / "tests" / "fixtures" / "memory-v4"
HASH_FILE = FIXTURE / "fixture.sha256"
SHADOW_SUFFIXES = ("_data", "_idx", "_docsize", "_config", "_content")


def load_db(kind: str, directory: Path) -> Path:
    db = directory / f"{kind}.db"
    conn = sqlite3.connect(db)
    try:
        conn.executescript((FIXTURE / "schema.sql").read_text(encoding="utf-8"))
        conn.executescript((FIXTURE / f"seed-{kind}.sql").read_text(encoding="utf-8"))
        conn.commit()
    finally:
        conn.close()
    return db


def is_shadow_table(name: str) -> bool:
    return any(name.endswith(suffix) for suffix in SHADOW_SUFFIXES)


def blob_hash(value: bytes) -> str:
    return "hex:" + hashlib.sha256(value).hexdigest()


def quote_value(value):
    if isinstance(value, bytes):
        return blob_hash(value)
    return value


def table_columns(conn: sqlite3.Connection, table: str) -> list[str]:
    return [row[1] for row in conn.execute(f'PRAGMA table_info("{table}")')]


def primary_key(conn: sqlite3.Connection, table: str) -> list[str]:
    rows = conn.execute(f'PRAGMA table_info("{table}")').fetchall()
    keys = [(row[5], row[1]) for row in rows if row[5]]
    return [name for _, name in sorted(keys)]


def canonical_dump(db: Path) -> str:
    conn = sqlite3.connect(db)
    conn.row_factory = sqlite3.Row
    try:
        names = [
            row[0]
            for row in conn.execute(
                "SELECT name FROM sqlite_master WHERE type IN ('table','view') ORDER BY name"
            )
        ]
        chunks: list[str] = []
        for name in names:
            if name.startswith("sqlite_") and name != "sqlite_sequence":
                continue
            if is_shadow_table(name):
                continue
            columns = table_columns(conn, name)
            if not columns:
                continue
            keys = primary_key(conn, name)
            is_virtual = bool(
                conn.execute(
                    "SELECT 1 FROM sqlite_master WHERE name=? AND sql LIKE 'CREATE VIRTUAL TABLE%'",
                    (name,),
                ).fetchone()
            )
            if is_virtual:
                order = "rowid"
                selected = ", ".join(['rowid'] + [f'"{c}"' for c in columns])
            elif keys:
                order = ", ".join(f'"{c}"' for c in keys)
                selected = ", ".join(f'"{c}"' for c in columns)
            else:
                order = ", ".join(f'"{c}"' for c in columns)
                selected = ", ".join(f'"{c}"' for c in columns)
            rows = conn.execute(f'SELECT {selected} FROM "{name}" ORDER BY {order}').fetchall()
            chunks.append(json.dumps({"table": name, "columns": columns}, ensure_ascii=False,
                                     sort_keys=True))
            for row in rows:
                if is_virtual:
                    values = [quote_value(v) for v in row]
                    chunks.append(json.dumps({"rowid": values[0], "values": values[1:]},
                                             ensure_ascii=False, sort_keys=True, default=str))
                else:
                    chunks.append(json.dumps([quote_value(v) for v in row],
                                             ensure_ascii=False, sort_keys=True, default=str))
        return "\n".join(chunks) + "\n"
    finally:
        conn.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--write", action="store_true", help="grava fixture.sha256")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="arestrix-memory-v4-") as tmp:
        directory = Path(tmp)
        knowledge = load_db("knowledge", directory)
        telemetry = load_db("telemetry", directory)
        dump = canonical_dump(knowledge) + canonical_dump(telemetry)
    digest = hashlib.sha256(dump.encode("utf-8")).hexdigest()
    if args.write:
        HASH_FILE.write_text(digest + "\n", encoding="utf-8")
        print(f"fixture.sha256 atualizado: {digest}")
        return 0
    if not HASH_FILE.exists():
        print("fixture.sha256 ausente; rode com --write", file=sys.stderr)
        return 1
    expected = HASH_FILE.read_text(encoding="utf-8").strip()
    print(f"dump: {digest}")
    print(f"esperado: {expected}")
    return 0 if digest == expected else 1


if __name__ == "__main__":
    raise SystemExit(main())
