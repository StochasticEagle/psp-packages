#!/usr/bin/env python3
"""Resolve PSP package dependencies once and emit a topological build plan."""

from __future__ import annotations

import pathlib
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
RECIPES = ROOT / "pspbuild"
PARSE = ROOT / "parse_pspbuild.sh"


def dependencies(package: str) -> list[str]:
    recipe = RECIPES / package / "PSPBUILD"
    result = subprocess.run(
        [str(PARSE), str(recipe), "depends"],
        text=True,
        capture_output=True,
    )
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or f"cannot parse dependencies for {package}")
    return sorted(set(result.stdout.split()))


def main() -> int:
    packages = sorted(path.parent.name for path in RECIPES.glob("*/PSPBUILD"))
    package_set = set(packages)
    roots = sys.argv[1:] or packages

    for root in roots:
        if root not in package_set:
            raise RuntimeError(f"package does not exist: {root}")

    graph: dict[str, list[str]] = {}
    for package in packages:
        deps = dependencies(package)
        missing = [dep for dep in deps if dep not in package_set]
        if missing:
            raise RuntimeError(
                f"{package}: PSP dependency has no recipe: {', '.join(missing)}"
            )
        graph[package] = deps

    state: dict[str, int] = {}
    order: list[str] = []
    closure: set[str] = set()

    def visit(package: str, stack: list[str]) -> None:
        status = state.get(package, 0)
        if status == 2:
            closure.add(package)
            return
        if status == 1:
            cycle = " -> ".join([*stack, package])
            raise RuntimeError(f"dependency cycle: {cycle}")

        state[package] = 1
        for dependency in graph[package]:
            visit(dependency, [*stack, package])
        state[package] = 2
        closure.add(package)
        order.append(package)

    for root in sorted(set(roots)):
        visit(root, [])

    required: set[str] = set()
    for package in closure:
        required.update(dep for dep in graph[package] if dep in closure)

    for package in order:
        deps = " ".join(graph[package])
        print(f"{package}|{1 if package in required else 0}|{deps}")

    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except RuntimeError as exc:
        print(f"ERROR: {exc}", file=sys.stderr)
        raise SystemExit(1)
