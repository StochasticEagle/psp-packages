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
    "autoheader": "autoconf",
    "automake": "automake",
    "aclocal": "automake",
    "autopoint": "autopoint",
    "gettext": "gettext",
    "libtool": "libtool-bin",
    "libtoolize": "libtool",
    "patch": "patch",
    "pkg-config": "pkg-config",
    "python3": "python3",
    "make": "build-essential",
}

AUTOGEN_TOOLS = {
    "autoconf",
    "autoheader",
    "automake",
    "aclocal",
    "autoreconf",
    "autopoint",
    "gettext",
    "libtool",
    "libtoolize",
    "pkg-config",
}

def main() -> int:
    workflow = WORKFLOW.read_text()
    verify_match = re.search(
        r"- name: Verify host build tools\n(?P<body>.*?)(?=\n\s+- name:|\Z)",
        workflow,
        re.DOTALL,
    )
    verify_block = verify_match.group("body") if verify_match else ""

    required: dict[str, set[str]] = {}
    required_tools: dict[str, set[str]] = {}

    for recipe in sorted(RECIPES.glob("*/PSPBUILD")):
        text = "\n".join(
            line for line in recipe.read_text().splitlines()
            if not line.lstrip().startswith("#")
        )
        for tool, package in TOOLS.items():
            if re.search(rf"(?<![A-Za-z0-9_-]){re.escape(tool)}(?![A-Za-z0-9_-])", text):
                required.setdefault(package, set()).add(recipe.parent.name)
                required_tools.setdefault(tool, set()).add(recipe.parent.name)

        if re.search(r"(?:^|[\s/])autogen\.sh(?:\s|$)", text):
            for tool in AUTOGEN_TOOLS:
                package = TOOLS[tool]
                required.setdefault(package, set()).add(recipe.parent.name)
                required_tools.setdefault(tool, set()).add(recipe.parent.name)

        if "import jinja2" in text:
            required.setdefault("python3-jinja2", set()).add(recipe.parent.name)
        if "jsonschema" in text:
            required.setdefault("python3-jsonschema", set()).add(recipe.parent.name)

    missing = [
        (package, sorted(users))
        for package, users in sorted(required.items())
        if not re.search(rf"(?<![A-Za-z0-9.+-]){re.escape(package)}(?![A-Za-z0-9.+-])", workflow)
    ]

    missing_tools = [
        (tool, sorted(users))
        for tool, users in sorted(required_tools.items())
        if not re.search(
            rf"(?<![A-Za-z0-9_-]){re.escape(tool)}(?![A-Za-z0-9_-])",
            verify_block,
        )
    ]

    if missing or missing_tools:
        if missing:
            print("Undeclared CI host build dependencies:", file=sys.stderr)
            for package, users in missing:
                print(f"  - {package}: required by {', '.join(users)}", file=sys.stderr)
        if missing_tools:
            print("Host executables missing from CI command -v verification:", file=sys.stderr)
            for tool, users in missing_tools:
                print(f"  - {tool}: required by {', '.join(users)}", file=sys.stderr)
        return 1

    print(
        "Host build dependency envelope covers packages "
        + ", ".join(sorted(required))
        + " and verifies executables "
        + ", ".join(sorted(required_tools))
        + "."
    )
    return 0

if __name__ == "__main__":
    raise SystemExit(main())
