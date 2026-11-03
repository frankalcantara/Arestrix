#!/usr/bin/env python3
"""Gera o catálogo embutido e o JSON versionado de predicados (Fase 2).

O JSON é a fonte revisável; o header C++ é gerado para não exigir leitura de
arquivo em tempo de execução. Rode este script após alterar a lista abaixo.
"""
from __future__ import annotations

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
JSON_PATH = ROOT / "CppCore" / "src" / "memory" / "catalog" / "predicates.v1.json"
HEADER_PATH = ROOT / "CppCore" / "src" / "memory" / "catalog" / "PredicatesV1Data.hpp"

MULTI = [
    "USES", "USES_TOOL", "PRODUCES", "PRODUCES_FILE", "READS", "WRITES",
    "TOUCHES", "CITES", "INCLUDES", "TAGGED_WITH", "HAS", "DEPENDS_ON",
    "MENTIONS", "SUPPORTS", "REQUIRES", "IS_A", "PART_OF", "DEFINES",
    "AUTHORED_BY", "OWNS", "MANAGES",
]
FUNCTIONAL = ["HAS_VALUE", "MEASURES", "LOCATED_IN", "PREFERS"]
STRUCTURAL = ["CONTRADICTS", "SUPERSEDES", "GENERALIZES", "HAS_LESSON"]

ALIASES = {
    "USES_MODEL": "LOCATED_IN",
    "RUNS_ON": "LOCATED_IN",
    "PREFERENCE": "PREFERS",
    "ABOUT": "MENTIONS",
    "IS": "IS_A",
    "BELONGS_TO": "PART_OF",
}

CONTEXT = {
    "PREFERS": {
        "slot": {
            "beverage": "drink",
            "drink": "drink",
            "food": "food",
            "tool": "tool",
            "model": "model",
            "style": "style",
        }
    },
    "LOCATED_IN": {
        "context": {
            "draft": "draft",
            "drafting": "draft",
            "production": "production",
            "prod": "production",
            "test": "test",
            "testing": "test",
        }
    },
}

UNITS = {
    "HAS_VALUE": {"s": 1.0, "ms": 0.001, "min": 60.0, "m": 1.0, "cm": 0.01,
                  "mm": 0.001, "kg": 1000.0, "g": 1.0, "C": 1.0, "%": 1.0},
    "MEASURES": {"s": 1.0, "ms": 0.001, "min": 60.0, "m": 1.0, "cm": 0.01,
                 "mm": 0.001, "kg": 1000.0, "g": 1.0, "C": 1.0, "%": 1.0},
}


def entry(name: str) -> dict:
    multi = name in MULTI
    functional = name in FUNCTIONAL
    structural = name in STRUCTURAL
    data = {
        "name": name,
        "claimable": not structural,
        "cardinality": "multi" if multi else ("functional" if functional else "none"),
        "requiredContextKeys": ["slot"] if name == "PREFERS" else [],
        "valueType": "quantity" if name in UNITS else "entity",
        "caseInsensitive": name in {"PREFERS", "MENTIONS", "DEFINES", "IS_A", "PART_OF"},
        "canonicalUnit": "s" if name in UNITS else "",
        "units": UNITS.get(name, {}),
        "contextKeys": CONTEXT.get(name, {}),
        "aliases": [alias for alias, target in ALIASES.items() if target == name],
        "domains": [],
        "description": (
            "Relação estrutural; criada apenas pelo código interno."
            if structural else f"Predicado {name} do catálogo v1."
        ),
    }
    if structural:
        data["cardinality"] = "none"
    return data


def main() -> int:
    catalog = {
        "version": 1,
        "predicates": [entry(name) for name in MULTI + FUNCTIONAL + STRUCTURAL],
    }
    JSON_PATH.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(catalog, ensure_ascii=False, indent=2) + "\n"
    JSON_PATH.write_text(text, encoding="utf-8")
    header = (
        "#pragma once\n\n"
        "// Gerado por scripts/generate_memory_catalog.py. Não edite à mão.\n"
        "namespace taskforge { namespace memory { namespace catalog_data {\n"
        "inline constexpr const char* kPredicatesV1Json = R\"JSON(\n"
        + text +
        ")JSON\";\n"
        "} } } // namespace taskforge::memory::catalog_data\n"
    )
    HEADER_PATH.write_text(header, encoding="utf-8")
    print(f"catalog: {JSON_PATH.relative_to(ROOT)}")
    print(f"header:  {HEADER_PATH.relative_to(ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
