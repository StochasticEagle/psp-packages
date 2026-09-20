#!/usr/bin/env python3
"""Compute PSP package build order from evaluated recipe dependencies."""

from __future__ import annotations

import json
import pathlib
import re
import subprocess
import sys
from dataclasses import dataclass

ROOT = pathlib.Path(__file__).resolve().parent
RECIPES = ROOT / "pspbuild"


@dataclass(frozen=True)
class Package:
    name: str
    directory: str
    path: pathlib.Path
    dependency_names: tuple[str, ...]


def normalize_dependency(value: str) -> str:
    value = value.split(":", 1)[0]
    return re.split(r"[<>=]", value, maxsplit=1)[0].strip()


def read_package(path: pathlib.Path) -> Package:
    probe = subprocess.run(
        [
            "bash",
            "-c",
            r"""
source "$1"
printf 'name\t%s\n' "${pkgname[0]}"
for dep in "${depends[@]}" "${makedepends[@]}"; do
    [[ -n "$dep" ]] && printf 'dep\t%s\n' "$dep"
done
""",
            "_",
            str(path),
        ],
        text=True,
        capture_output=True,
    )
    if probe.returncode != 0:
        detail = probe.stderr.strip() or f"exit status {probe.returncode}"
        raise RuntimeError(f"{path.relative_to(ROOT)}: cannot evaluate recipe: {detail}")

    name = ""
    dependencies: list[str] = []
    for line in probe.stdout.splitlines():
        kind, value = line.split("\t", 1)
        if kind == "name":
            name = value
        elif kind == "dep":
            dep = normalize_dependency(value)
            if dep:
                dependencies.append(dep)

    if not name:
        raise RuntimeError(f"{path.relative_to(ROOT)}: missing pkgname")

    return Package(
        name=name,
        directory=path.parent.name,
        path=path,
        dependency_names=tuple(dict.fromkeys(dependencies)),
    )


def load_packages() -> tuple[list[Package], dict[str, Package]]:
    packages = [read_package(path) for path in sorted(RECIPES.glob("*/PSPBUILD"))]
    by_name: dict[str, Package] = {}

    for package in packages:
        if package.name in by_name:
            raise RuntimeError(
                f"duplicate pkgname {package.name!r}: "
                f"{by_name[package.name].path.relative_to(ROOT)} and "
                f"{package.path.relative_to(ROOT)}"
            )
        by_name[package.name] = package

    for package in packages:
        for dependency in package.dependency_names:
            if dependency not in by_name:
                raise RuntimeError(
                    f"{package.path.relative_to(ROOT)}: "
                    f"unknown required PSP package dependency {dependency!r}"
                )

    return packages, by_name


def build_order_for(package: Package, by_name: dict[str, Package]) -> list[str]:
    ordered: list[str] = []
    permanent: set[str] = set()
    visiting: list[str] = []

    def visit(current: Package) -> None:
        if current.name in permanent:
            return
        if current.name in visiting:
            start = visiting.index(current.name)
            cycle = visiting[start:] + [current.name]
            raise RuntimeError("dependency cycle: " + " -> ".join(cycle))

        visiting.append(current.name)
        for dep_name in current.dependency_names:
            visit(by_name[dep_name])
        visiting.pop()

        permanent.add(current.name)
        ordered.append(current.directory)

    visit(package)
    return ordered


def main() -> int:
    try:
        packages, by_name = load_packages()
        result = [
            " ".join(build_order_for(package, by_name))
            for package in packages
        ]
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        return 1

    print(json.dumps(result))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
