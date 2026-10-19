#!/usr/bin/env python3
"""Garante a politica de codigo externo do Arestrix.

Regras
  1. Todo codigo externo vive em `third_party/` (submodulos Git, ou arquivos com a licenca ao lado).
     Nenhum diretorio `vendor/` pode estar versionado.
  2. Todo submodulo declarado em .gitmodules fica em `third_party/`.
  3. Toda licenca precisa ser compativel com a do Taskflow (MIT): permissivas apenas.
     Excecao documentada: fontes (OFL-1.1) em third_party/fonts, pois a OFL permite embutir a fonte em
     software de qualquer licenca.
  4. Dependencias npm de producao (package-lock.json) seguem a mesma lista.

Uso: python scripts/check_third_party_licenses.py        (codigo de saida != 0 se houver violacao)
"""
import json
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
TP = ROOT / "third_party"

# SPDX permitidos (compativeis com MIT). OFL-1.1 so para fontes (ver FONT_ONLY).
ALLOWED = {"MIT", "ISC", "BSD-2-Clause", "BSD-3-Clause", "Apache-2.0", "Zlib", "BSL-1.0", "0BSD", "Unlicense", "CC0-1.0", "blessing"}   # blessing = dominio publico do SQLite
FONT_ONLY = {"OFL-1.1"}

# Componente -> (SPDX declarado, arquivo de licenca, trecho esperado no texto da licenca)
COMPONENTS = {
    "taskflow":      ("MIT",        "LICENSE",     "MIT LICENSE"),
    "simdjson":      ("Apache-2.0", "LICENSE",     "Apache License"),   # tambem distribuido sob MIT (LICENSE-MIT)
    "webui":         ("MIT",        "LICENSE",     "MIT License"),
    "i18next":       ("MIT",        "LICENSE",     "MIT License"),
    "monaco-editor": ("MIT",        "LICENSE.txt", "MIT License"),
    "SkillSpector":  ("Apache-2.0", "LICENSE",     "Apache License"),
    "fonts/inter":   ("OFL-1.1",    "OFL-Inter.txt", "SIL Open Font License"),
    "sqlite":        ("blessing",   "LICENSE.md",  "May you do good and not evil"),   # dominio publico
    "httplib":       ("MIT",        "LICENSE",     "MIT License"),
}

errors = []


def fail(msg):
    errors.append(msg)
    print("  ERRO  " + msg)


def ok(msg):
    print("  ok    " + msg)


def check_components():
    for name, (spdx, lic, needle) in COMPONENTS.items():
        base = TP / name
        if not base.exists() or not any(base.iterdir()):
            fail(f"third_party/{name} ausente ou vazio (git submodule update --init?)")
            continue
        path = base / lic
        if not path.exists():
            fail(f"third_party/{name}: arquivo de licenca '{lic}' nao encontrado")
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if needle.lower() not in text.lower():
            fail(f"third_party/{name}: o texto de {lic} nao parece ser {spdx}")
            continue
        if spdx in FONT_ONLY:
            if not name.startswith("fonts/"):
                fail(f"third_party/{name}: {spdx} so e permitida para fontes")
                continue
        elif spdx not in ALLOWED:
            fail(f"third_party/{name}: licenca {spdx} nao e compativel com o Taskflow (MIT)")
            continue
        ok(f"third_party/{name:<14} {spdx}")
    # componente nao catalogado
    known = {n.split("/")[0] for n in COMPONENTS}
    for child in sorted(TP.iterdir()):
        if child.is_dir() and child.name not in known:
            fail(f"third_party/{child.name} nao esta catalogado em COMPONENTS (declare a licenca)")


def check_no_vendor_dirs():
    files = subprocess.run(["git", "ls-files"], cwd=ROOT, capture_output=True, text=True).stdout.splitlines()
    bad = sorted({f.split("vendor/")[0] + "vendor/" for f in files if re.search(r"(^|/)vendor/", f)})
    for b in bad:
        fail(f"diretorio '{b}' versionado: codigo externo deve ficar em third_party/")
    if not bad:
        ok("nenhum diretorio vendor/ versionado")


def check_gitmodules():
    gm = ROOT / ".gitmodules"
    paths = re.findall(r"path\s*=\s*(\S+)", gm.read_text(encoding="utf-8")) if gm.exists() else []
    for p in paths:
        if not p.startswith("third_party/"):
            fail(f"submodulo fora de third_party/: {p}")
    if all(p.startswith("third_party/") for p in paths):
        ok(f"{len(paths)} submodulos, todos em third_party/")


def allowed_expr(expr):
    """Avalia expressoes SPDX simples: 'A OR B' (qualquer uma permitida) e 'A AND B' (todas)."""
    expr = expr.strip("() ")
    if " OR " in expr:
        return any(allowed_expr(p) for p in expr.split(" OR "))
    if " AND " in expr:
        return all(allowed_expr(p) for p in expr.split(" AND "))
    return expr in ALLOWED


def check_npm():
    lock = ROOT / "package-lock.json"
    if not lock.exists():
        return
    pkgs = json.loads(lock.read_text(encoding="utf-8")).get("packages", {})
    bad, count = [], 0
    for key, meta in pkgs.items():
        if not key or meta.get("dev"):
            continue
        count += 1
        lic = meta.get("license", "")
        if not lic or not allowed_expr(lic):
            bad.append(f"{key.split('node_modules/')[-1]} ({lic or 'sem licenca'})")
    for b in bad:
        fail(f"dependencia npm de producao com licenca nao permitida: {b}")
    if not bad:
        ok(f"{count} dependencias npm de producao com licencas permitidas")


print("Politica de codigo externo (licenca compativel com Taskflow/MIT, tudo em third_party/)")
check_components()
check_no_vendor_dirs()
check_gitmodules()
check_npm()
if errors:
    print(f"\n{len(errors)} violacao(oes).")
    sys.exit(1)
print("\nTudo conforme.")
