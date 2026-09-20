#!/usr/bin/env python3
"""Validate PSP package recipe metadata and packaging invariants."""

from __future__ import annotations

import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
RECIPES = ROOT / "pspbuild"
NETWORK_ASSETS = ROOT / "network-source-assets.tsv"

PKGDIR_INSTALL_PREFIX = re.compile(
    r"(?:CMAKE_INSTALL_PREFIX|--install-prefix)[^\n]*\$\{?pkgdir\}?"
)
PKGDIR_INSTALL_DIR = re.compile(
    r"CMAKE_INSTALL_(?:LIBDIR|INCLUDEDIR|BINDIR)[^\n]*\$\{?pkgdir\}?"
)


def probe_recipe(path: pathlib.Path) -> dict[str, list[str]]:
    probe = subprocess.run(
        [
            "bash",
            "-c",
            r"""
source "$1"
printf 'pkgname\t%s\n' "${pkgname[0]}"
printf 'pkgver\t%s\n' "${pkgver-}"
printf 'pkgrel\t%s\n' "${pkgrel-}"
printf 'pkgdesc\t%s\n' "${pkgdesc-}"
printf 'url\t%s\n' "${url-}"
for value in "${arch[@]}"; do printf 'arch\t%s\n' "$value"; done
for value in "${license[@]}"; do printf 'license\t%s\n' "$value"; done
for value in "${groups[@]}"; do printf 'group\t%s\n' "$value"; done
for value in "${source[@]}"; do printf 'source\t%s\n' "$value"; done
for value in "${sha256sums[@]}"; do printf 'sha256\t%s\n' "$value"; done
""",
            "_",
            str(path),
        ],
        text=True,
        capture_output=True,
    )
    if probe.returncode != 0:
        detail = probe.stderr.strip() or f"exit status {probe.returncode}"
        raise RuntimeError(f"cannot evaluate recipe: {detail}")

    values: dict[str, list[str]] = {}
    for line in probe.stdout.splitlines():
        kind, value = line.split("\t", 1)
        values.setdefault(kind, []).append(value)
    return values


def load_network_assets(errors: list[str]) -> dict[tuple[str, str], str]:
    assets: dict[tuple[str, str], str] = {}
    try:
        lines = NETWORK_ASSETS.read_text().splitlines()
    except OSError as exc:
        errors.append(f"cannot read network-source-assets.tsv: {exc}")
        return assets

    for lineno, line in enumerate(lines, 1):
        if not line.strip() or line.startswith("#"):
            continue
        fields = line.split("\t", 2)
        if len(fields) != 3:
            errors.append(
                f"network-source-assets.tsv:{lineno}: expected package<TAB>URL<TAB>reason"
            )
            continue
        package, url, reason = (field.strip() for field in fields)
        key = (package, url)
        if key in assets:
            errors.append(
                f"network-source-assets.tsv:{lineno}: duplicate entry: {package} {url}"
            )
            continue
        assets[key] = reason
    return assets


def package_function(text: str) -> str:
    match = re.search(r"(?m)^package\s*\(\s*\)\s*\{", text)
    if not match:
        match = re.search(r"(?m)^package\s+\(\s*\)\s*\{", text)
    if not match:
        return ""

    start = match.end()
    depth = 1
    index = start
    while index < len(text) and depth:
        if text[index] == "{":
            depth += 1
        elif text[index] == "}":
            depth -= 1
        index += 1
    return text[start : index - 1] if depth == 0 else ""


def main() -> int:
    errors: list[str] = []
    declared_assets = load_network_assets(errors)
    seen_assets: set[tuple[str, str]] = set()
    recipe_count = 0

    for recipe in sorted(RECIPES.glob("*/PSPBUILD")):
        recipe_count += 1
        rel = recipe.relative_to(ROOT)
        text = recipe.read_text()

        try:
            values = probe_recipe(recipe)
        except RuntimeError as exc:
            errors.append(f"{rel}: {exc}")
            continue

        pkgname = values.get("pkgname", [""])[0]
        for field in ("pkgname", "pkgver", "pkgrel", "pkgdesc", "url"):
            if not values.get(field, [""])[0]:
                errors.append(f"{rel}: missing {field}")

        if values.get("arch") != ["any"]:
            errors.append(f"{rel}: arch must be exactly (any)")
        if not values.get("license"):
            errors.append(f"{rel}: license must not be empty")
        if "psp-libraries" not in values.get("group", []):
            errors.append(f"{rel}: groups must include psp-libraries")

        sources = values.get("source", [])
        sums = values.get("sha256", [])
        if len(sources) != len(sums):
            errors.append(
                f"{rel}: source/sha256sums length mismatch: "
                f"{len(sources)} sources vs {len(sums)} checksums"
            )

        for source in sources:
            entry = source.split("::", 1)[1] if "::" in source else source
            if entry.startswith("git+"):
                continue
            if re.match(r"^(?:https?|ftp)://", entry):
                key = (pkgname, entry)
                if key not in declared_assets:
                    errors.append(
                        f"{rel}: external network source is not declared in "
                        f"network-source-assets.tsv: {entry}"
                    )
                else:
                    seen_assets.add(key)

        if PKGDIR_INSTALL_PREFIX.search(text):
            errors.append(
                f"{rel}: CMake install prefix must be /psp; stage with DESTDIR, not pkgdir"
            )
        if PKGDIR_INSTALL_DIR.search(text):
            errors.append(
                f"{rel}: CMake install directories must be target-relative, not under pkgdir"
            )

        for lineno, line in enumerate(text.splitlines(), 1):
            if ".pc" in line and "${PSPDEV}/psp" in line:
                errors.append(
                    f"{rel}:{lineno}: pkg-config metadata must use /psp, not PSPDEV"
                )
            if (
                not line.lstrip().startswith("#")
                and re.search(r"(^|\\s)patch\\s", line)
                and "--fuzz=0" not in line
            ):
                errors.append(
                    f"{rel}:{lineno}: patch commands must use --fuzz=0"
                )

        body = package_function(text)
        for line in body.splitlines():
            if "cmake --install" in line and "DESTDIR=" not in line:
                errors.append(
                    f"{rel}: package() cmake --install must stage with DESTDIR"
                )

    for key, reason in sorted(declared_assets.items()):
        if key not in seen_assets:
            errors.append(
                f"network-source-assets.tsv: stale declaration: "
                f"{key[0]} {key[1]} ({reason})"
            )

    order = subprocess.run(
        [sys.executable, str(ROOT / "get_build_order.py")],
        text=True,
        capture_output=True,
    )
    if order.returncode != 0:
        detail = order.stderr.strip() or f"exit status {order.returncode}"
        errors.append(f"dependency graph validation failed: {detail}")

    if errors:
        print("PSP package recipe audit failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print(
        f"Validated {recipe_count} package recipes and "
        f"{len(declared_assets)} explicitly declared external sample assets."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
