#!/usr/bin/env python3
"""Copia um projeto de examples/ para uma pasta SUA, pronta para abrir no Arestrix Studio.

Por que copiar: abrir o exemplo no proprio repositorio faria o Studio gravar artefatos (respostas dos agentes, caminhos da
sua maquina) dentro de uma pasta versionada. A copia fica fora do Git, com um projectId proprio.

Uso:  python scripts/copy_example.py 02-triagem-de-suporte C:/meus-projetos/triagem
      python scripts/copy_example.py --list
"""
import json
import pathlib
import re
import shutil
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
EXAMPLES = ROOT / "examples"
SKIP = {"workspace", "output", "storage", ".arestrix", "__pycache__"}


def slug(text):
    return re.sub(r"[^a-z0-9]+", "-", text.lower()).strip("-") or "projeto"


def main(argv):
    names = sorted(p.name for p in EXAMPLES.iterdir() if p.is_dir())
    if len(argv) == 1 and argv[0] == "--list":
        print("\n".join(names))
        return 0
    if len(argv) != 2 or argv[0] not in names:
        print(__doc__)
        print("Exemplos disponiveis:", ", ".join(names))
        return 2

    src = EXAMPLES / argv[0]
    dest = pathlib.Path(argv[1]).expanduser().resolve()
    if EXAMPLES.resolve() in dest.parents or dest == EXAMPLES.resolve():
        print("Escolha uma pasta FORA de examples/: a copia nao deve ficar dentro do repositorio.")
        return 2
    if dest.exists() and any(dest.iterdir()):
        print(f"A pasta {dest} ja existe e nao esta vazia.")
        return 2

    shutil.copytree(src, dest, ignore=lambda d, files: [f for f in files if f in SKIP or f.endswith(".pyc")], dirs_exist_ok=True)

    manifest_path = dest / "arestrix.project.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    manifest["projectId"] = f"{slug(dest.name)}"
    manifest["name"] = dest.name
    manifest["environment"] = {}
    manifest_path.write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")

    print(f"Projeto copiado para {dest}")
    print("Proximos passos:")
    print("  1. Abra o Arestrix Studio e use Arquivo > Abrir para escolher essa pasta.")
    print("  2. Em Padroes Globais > Provedor & Modelo, escolha o seu provedor e modelo e grave a sua chave de API")
    print("     (ela vai para as variaveis de ambiente do seu usuario, nunca para o projeto).")
    print("  3. Pressione F5.")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
