#pragma once

// Gerado por scripts/generate_memory_catalog.py. Não edite à mão.
namespace taskforge { namespace memory { namespace catalog_data {
inline constexpr const char* kPredicatesV1Json = R"JSON(
{
  "version": 1,
  "predicates": [
    {
      "name": "USES",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado USES do catálogo v1."
    },
    {
      "name": "USES_TOOL",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado USES_TOOL do catálogo v1."
    },
    {
      "name": "PRODUCES",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado PRODUCES do catálogo v1."
    },
    {
      "name": "PRODUCES_FILE",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado PRODUCES_FILE do catálogo v1."
    },
    {
      "name": "READS",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado READS do catálogo v1."
    },
    {
      "name": "WRITES",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado WRITES do catálogo v1."
    },
    {
      "name": "TOUCHES",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado TOUCHES do catálogo v1."
    },
    {
      "name": "CITES",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado CITES do catálogo v1."
    },
    {
      "name": "INCLUDES",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado INCLUDES do catálogo v1."
    },
    {
      "name": "TAGGED_WITH",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado TAGGED_WITH do catálogo v1."
    },
    {
      "name": "HAS",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado HAS do catálogo v1."
    },
    {
      "name": "DEPENDS_ON",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado DEPENDS_ON do catálogo v1."
    },
    {
      "name": "MENTIONS",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": true,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [
        "ABOUT"
      ],
      "domains": [],
      "description": "Predicado MENTIONS do catálogo v1."
    },
    {
      "name": "SUPPORTS",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado SUPPORTS do catálogo v1."
    },
    {
      "name": "REQUIRES",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado REQUIRES do catálogo v1."
    },
    {
      "name": "IS_A",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": true,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [
        "IS"
      ],
      "domains": [],
      "description": "Predicado IS_A do catálogo v1."
    },
    {
      "name": "PART_OF",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": true,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [
        "BELONGS_TO"
      ],
      "domains": [],
      "description": "Predicado PART_OF do catálogo v1."
    },
    {
      "name": "DEFINES",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": true,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado DEFINES do catálogo v1."
    },
    {
      "name": "AUTHORED_BY",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado AUTHORED_BY do catálogo v1."
    },
    {
      "name": "OWNS",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado OWNS do catálogo v1."
    },
    {
      "name": "MANAGES",
      "claimable": true,
      "cardinality": "multi",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado MANAGES do catálogo v1."
    },
    {
      "name": "HAS_VALUE",
      "claimable": true,
      "cardinality": "functional",
      "requiredContextKeys": [],
      "valueType": "quantity",
      "caseInsensitive": false,
      "canonicalUnit": "s",
      "units": {
        "s": 1.0,
        "ms": 0.001,
        "min": 60.0,
        "m": 1.0,
        "cm": 0.01,
        "mm": 0.001,
        "kg": 1000.0,
        "g": 1.0,
        "C": 1.0,
        "%": 1.0
      },
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado HAS_VALUE do catálogo v1."
    },
    {
      "name": "MEASURES",
      "claimable": true,
      "cardinality": "functional",
      "requiredContextKeys": [],
      "valueType": "quantity",
      "caseInsensitive": false,
      "canonicalUnit": "s",
      "units": {
        "s": 1.0,
        "ms": 0.001,
        "min": 60.0,
        "m": 1.0,
        "cm": 0.01,
        "mm": 0.001,
        "kg": 1000.0,
        "g": 1.0,
        "C": 1.0,
        "%": 1.0
      },
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Predicado MEASURES do catálogo v1."
    },
    {
      "name": "LOCATED_IN",
      "claimable": true,
      "cardinality": "functional",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {
        "context": {
          "draft": "draft",
          "drafting": "draft",
          "production": "production",
          "prod": "production",
          "test": "test",
          "testing": "test"
        }
      },
      "aliases": [
        "USES_MODEL",
        "RUNS_ON"
      ],
      "domains": [],
      "description": "Predicado LOCATED_IN do catálogo v1."
    },
    {
      "name": "PREFERS",
      "claimable": true,
      "cardinality": "functional",
      "requiredContextKeys": [
        "slot"
      ],
      "valueType": "entity",
      "caseInsensitive": true,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {
        "slot": {
          "beverage": "drink",
          "drink": "drink",
          "food": "food",
          "tool": "tool",
          "model": "model",
          "style": "style"
        }
      },
      "aliases": [
        "PREFERENCE"
      ],
      "domains": [],
      "description": "Predicado PREFERS do catálogo v1."
    },
    {
      "name": "CONTRADICTS",
      "claimable": false,
      "cardinality": "none",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Relação estrutural; criada apenas pelo código interno."
    },
    {
      "name": "SUPERSEDES",
      "claimable": false,
      "cardinality": "none",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Relação estrutural; criada apenas pelo código interno."
    },
    {
      "name": "GENERALIZES",
      "claimable": false,
      "cardinality": "none",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Relação estrutural; criada apenas pelo código interno."
    },
    {
      "name": "HAS_LESSON",
      "claimable": false,
      "cardinality": "none",
      "requiredContextKeys": [],
      "valueType": "entity",
      "caseInsensitive": false,
      "canonicalUnit": "",
      "units": {},
      "contextKeys": {},
      "aliases": [],
      "domains": [],
      "description": "Relação estrutural; criada apenas pelo código interno."
    }
  ]
}
)JSON";
} } } // namespace taskforge::memory::catalog_data
