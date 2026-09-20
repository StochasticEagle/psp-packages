#!/usr/bin/env python3
"""Verify that recipe host-tool requirements are covered by CI."""

from __future__ import annotations

import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
RECIPES = ROOT / "pspbuild"
WORKFLOW = ROOT / ".github/workflows/build.yml"

TOOLS = {
    "cmake": "cmake",
    "meson": "meson",
    "ninja": "ninja-build",
    "autoreconf": "autoconf",
    "autoconf": "autoconf",
    "automake": "automake",
    "libtool": "libtool",
    "libtoolize": "libtool",
    "patch": "patch",
    "pkg-config": "pkg-config",
    "python3": "python3",
    "make": "build-essential",
}

def main() -> int:
    workflow = WORKFLOW.read_text()
    required: dict[str, set[str]] = {}

    for recipe in sorted(RECIPES.glob("*/PSPBUILD")):
        text = "\n".join(
            line for line in recipe.read_text().splitlines()
            if not line.lstrip().startswith("#")
        )
        for tool, package in TOOLS.items():
            if re.search(rf"(?<![A-Za-z0-9_-]){re.escape(tool)}(?![A-Za-z0-9_-])", text):
                required.setdefault(package, set()).add(recipe.parent.name)

        if "import jinja2" in text:
            required.setdefault("python3-jinja2", set()).add(recipe.parent.name)
        if "jsonschema" in text:
            required.setdefault("python3-jsonschema", set()).add(recipe.parent.name)

    missing = [
        (package, sorted(users))
        for package, users in sorted(required.items())
        if not re.search(rf"(?<![A-Za-z0-9.+-]){re.escape(package)}(?![A-Za-z0-9.+-])", workflow)
    ]

    if missing:
        print("Undeclared CI host build dependencies:", file=sys.stderr)
        for package, users in missing:
            print(f"  - {package}: required by {', '.join(users)}", file=sys.stderr)
        return 1

    print(
        "Host build dependency envelope covers "
        + ", ".join(sorted(required))
        + "."
    )
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
