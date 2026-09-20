#!/usr/bin/env python3
"""Compute the complete cache key for one PSP package build."""

from __future__ import annotations

import hashlib
import os
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE_COMPONENTS = ROOT / "source-components.tsv"


def run(*args: str) -> str:
    result = subprocess.run(args, cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or " ".join(args))
    return result.stdout


def source_map() -> dict[str, str]:
    result: dict[str, str] = {}
    for line in SOURCE_COMPONENTS.read_text().splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        remote, component = line.split("\t", 1)
        result[remote.removesuffix("/").removesuffix(".git")] = component.strip()
    return result


def recipe_git_sources(pspbuild: pathlib.Path) -> list[tuple[int, str, str]]:
    output = run(
        "bash", "-c",
        r"""
source "$1"
for i in "${!source[@]}"; do
    case "${source[$i]}" in
        *git+*)
            override=""
            if declare -p psp_source_components >/dev/null 2>&1; then
                override="${psp_source_components[$i]-}"
            fi
            printf '%s\t%s\t%s\n' "$i" "${source[$i]}" "$override"
            ;;
    esac
done
""",
        "_", str(pspbuild),
    )
    result = []
    for line in output.splitlines():
        index, source, override = line.split("\t", 2)
        entry = source.split("::", 1)[-1]
        remote = entry.removeprefix("git+").split("#", 1)[0]
        result.append((int(index), remote, override))
    return result


def dependencies(pspbuild: pathlib.Path) -> list[str]:
    output = run(str(ROOT / "parse_pspbuild.sh"), str(pspbuild), "depends")
    return [value for value in output.split() if value]


def update_file(digest, label: str, path: pathlib.Path) -> None:
    digest.update(label.encode())
    digest.update(b"\0")
    digest.update(str(path.relative_to(ROOT)).encode())
    digest.update(b"\0")
    digest.update(path.read_bytes())
    digest.update(b"\0")


def main() -> int:
    if len(sys.argv) != 5:
        print(
            "usage: package-input-fingerprint.py PACKAGE PSPBUILD RECIPE_DIR PACKAGES",
            file=sys.stderr,
        )
        return 2

    package, pspbuild_arg, recipe_dir_arg, packages_arg = sys.argv[1:]
    pspbuild = pathlib.Path(pspbuild_arg).resolve()
    recipe_dir = pathlib.Path(recipe_dir_arg).resolve()
    packages = pathlib.Path(packages_arg).resolve()
    digest = hashlib.sha256()
    digest.update(b"psp-package-input-v2\0")
    digest.update(package.encode() + b"\0")

    for path in sorted(recipe_dir.iterdir()):
        if path.is_file() and not path.name.startswith(".PSPBUILD.local."):
            update_file(digest, "recipe", path)

    mappings = source_map()
    for index, remote, override in recipe_git_sources(pspbuild):
        component = override or mappings.get(remote.removesuffix("/").removesuffix(".git"))
        if not component:
            raise RuntimeError(f"unmapped Git source[{index}]: {remote}")
        stage = run("git", "ls-files", "--stage", "--", component).strip()
        fields = stage.split()
        if len(fields) < 2 or fields[0] != "160000":
            raise RuntimeError(f"source component is not a gitlink: {component}")
        digest.update(f"gitlink\0{component}\0{fields[1]}\0".encode())

    for dependency in dependencies(pspbuild):
        dep_recipe = ROOT / "pspbuild" / dependency / "PSPBUILD"
        if not dep_recipe.is_file():
            raise RuntimeError(f"required PSP dependency has no recipe: {dependency}")
        pkgfile = run(
            str(ROOT / "parse_pspbuild.sh"), str(dep_recipe), "pkgoutput"
        ).strip()
        archive = packages / pkgfile
        if not archive.is_file():
            raise RuntimeError(f"required PSP dependency archive is missing: {archive}")
        digest.update(f"dependency\0{dependency}\0".encode())
        digest.update(hashlib.sha256(archive.read_bytes()).digest())

    digest.update(b"XTRA_OPTS\0")
    digest.update(os.environ.get("XTRA_OPTS", "").encode())
    print(digest.hexdigest())
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
