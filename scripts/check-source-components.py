#!/usr/bin/env python3
"""Validate PSP package Git-source/component invariants without network access."""

from __future__ import annotations

import configparser
import pathlib
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
RECIPES = ROOT / "pspbuild"
GITMODULES = ROOT / ".gitmodules"
SOURCE_COMPONENTS = ROOT / "source-components.tsv"

COMMIT_RE = re.compile(r"^commit=([0-9a-fA-F]{40})$")


def normalize_url(url: str) -> str:
    url = url.strip()
    if url.startswith("git+"):
        url = url[4:]
    url = url.split("#", 1)[0].rstrip("/")
    if url.endswith(".git"):
        url = url[:-4]
    return url


def load_gitlinks(errors: list[str]) -> dict[str, str]:
    try:
        listing = subprocess.check_output(
            ["git", "-C", str(ROOT), "ls-files", "--stage"],
            text=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        errors.append(f"cannot read repository gitlinks: {exc}")
        return {}

    gitlinks: dict[str, str] = {}
    for line in listing.splitlines():
        parts = line.split(None, 3)
        if len(parts) == 4 and parts[0] == "160000":
            gitlinks[parts[3]] = parts[1]
    return gitlinks


def load_modules(errors: list[str]) -> tuple[dict[str, str], dict[str, list[str]]]:
    parser = configparser.ConfigParser(interpolation=None, strict=True)
    try:
        with GITMODULES.open() as handle:
            parser.read_file(handle)
    except (OSError, configparser.Error) as exc:
        errors.append(f"cannot parse .gitmodules: {exc}")
        return {}, {}

    modules: dict[str, str] = {}
    by_url: dict[str, list[str]] = {}

    for section in parser.sections():
        if not section.startswith('submodule "'):
            continue

        path = parser[section].get("path", "").strip()
        url = parser[section].get("url", "").strip()
        shallow = parser[section].get("shallow", "").strip().lower()

        if not path:
            errors.append(f"{section}: missing path")
            continue
        if path in modules:
            errors.append(f".gitmodules: duplicate component path: {path}")
            continue
        if not path.startswith("components/"):
            errors.append(f"{section}: path is outside components/: {path}")
        if not url:
            errors.append(f"{section}: missing URL")
        if shallow != "true":
            errors.append(f"{section}: shallow must be true")

        modules[path] = url
        by_url.setdefault(normalize_url(url), []).append(path)

    return modules, by_url


def load_source_map(
    modules: dict[str, str],
    gitlinks: dict[str, str],
    errors: list[str],
) -> dict[str, str]:
    source_map: dict[str, str] = {}

    try:
        lines = SOURCE_COMPONENTS.read_text().splitlines()
    except OSError as exc:
        errors.append(f"cannot read source-components.tsv: {exc}")
        return source_map

    for lineno, raw in enumerate(lines, 1):
        if not raw.strip():
            continue

        fields = raw.split("\t")
        if len(fields) != 2:
            errors.append(
                f"source-components.tsv:{lineno}: expected URL<TAB>component"
            )
            continue

        url, path = (field.strip() for field in fields)
        key = normalize_url(url)

        if key in source_map:
            errors.append(
                f"source-components.tsv:{lineno}: duplicate upstream URL: {url}"
            )
            continue

        source_map[key] = path

        if path not in modules:
            errors.append(
                f"source-components.tsv:{lineno}: unknown component: {path}"
            )
            continue

        if path not in gitlinks:
            errors.append(
                f"source-components.tsv:{lineno}: component is not a gitlink: {path}"
            )

        if url != modules[path]:
            errors.append(
                f"source-components.tsv:{lineno}: URL differs from .gitmodules "
                f"for {path}: {url!r} != {modules[path]!r}"
            )

    return source_map


def recipe_git_sources(recipe: pathlib.Path, errors: list[str]):
    probe = subprocess.run(
        [
            "bash",
            "-c",
            r'''
source "$1"

if ! declare -p source >/dev/null 2>&1; then
    exit 0
fi

for i in "${!source[@]}"; do
    src="${source[$i]}"
    case "$src" in
        *git+*)
            mapped=""
            if declare -p psp_source_components >/dev/null 2>&1; then
                mapped="${psp_source_components[$i]-}"
            fi
            printf '%s\t%s\t%s\n' "$i" "$mapped" "$src"
            ;;
    esac
done
''',
            "_",
            str(recipe),
        ],
        text=True,
        capture_output=True,
    )

    if probe.returncode != 0:
        detail = probe.stderr.strip() or f"exit status {probe.returncode}"
        errors.append(
            f"{recipe.relative_to(ROOT)}: cannot evaluate sources: {detail}"
        )
        return []

    records = []
    for raw in probe.stdout.splitlines():
        fields = raw.split("\t", 2)
        if len(fields) != 3:
            errors.append(
                f"{recipe.relative_to(ROOT)}: malformed source probe output: {raw}"
            )
            continue
        records.append(tuple(fields))
    return records


def main() -> int:
    errors: list[str] = []

    gitlinks = load_gitlinks(errors)
    modules, modules_by_url = load_modules(errors)

    module_paths = set(modules)
    gitlink_paths = set(gitlinks)

    for path in sorted(module_paths - gitlink_paths):
        errors.append(f".gitmodules component is not a gitlink: {path}")
    for path in sorted(gitlink_paths - module_paths):
        errors.append(f"gitlink is missing from .gitmodules: {path}")

    source_map = load_source_map(modules, gitlinks, errors)

    for upstream, paths in sorted(modules_by_url.items()):
        if upstream not in source_map:
            errors.append(
                f"source-components.tsv is missing upstream {upstream} "
                f"({', '.join(paths)})"
            )

    git_source_count = 0

    for recipe in sorted(RECIPES.glob("*/PSPBUILD")):
        rel = recipe.relative_to(ROOT)

        for source_index, mapped, src in recipe_git_sources(recipe, errors):
            git_source_count += 1

            entry = src.split("::", 1)[1] if "::" in src else src
            if not entry.startswith("git+"):
                errors.append(
                    f"{rel}: source[{source_index}] contains git+ but cannot be parsed: {src}"
                )
                continue

            git_entry = entry[len("git+") :]
            if "#" not in git_entry:
                errors.append(
                    f"{rel}: source[{source_index}] must pin an exact #commit=: {src}"
                )
                continue

            remote, selector = git_entry.split("#", 1)
            match = COMMIT_RE.fullmatch(selector)
            if not match:
                errors.append(
                    f"{rel}: source[{source_index}] must use #commit=<40-hex>, "
                    f"not #{selector}"
                )
                continue

            recipe_commit = match.group(1).lower()
            upstream = normalize_url(remote)

            component = mapped.strip()
            if component:
                if not component.startswith("components/"):
                    errors.append(
                        f"{rel}: source[{source_index}] maps outside components/: "
                        f"{component}"
                    )
                    continue
            else:
                component = source_map.get(upstream, "")
                if not component:
                    errors.append(
                        f"{rel}: source[{source_index}] has no source-component "
                        f"mapping for {remote}"
                    )
                    continue

            if component not in modules:
                errors.append(
                    f"{rel}: source[{source_index}] maps to undeclared component: "
                    f"{component}"
                )
                continue

            if component not in gitlinks:
                errors.append(
                    f"{rel}: source[{source_index}] maps to non-gitlink component: "
                    f"{component}"
                )
                continue

            module_upstream = normalize_url(modules[component])
            if upstream != module_upstream:
                errors.append(
                    f"{rel}: source[{source_index}] URL disagrees with .gitmodules: "
                    f"{remote} -> {component} -> {modules[component]}"
                )

            gitlink_commit = gitlinks[component].lower()
            if recipe_commit != gitlink_commit:
                errors.append(
                    f"{rel}: source[{source_index}] commit disagrees with gitlink "
                    f"{component}: {recipe_commit} != {gitlink_commit}"
                )

    if errors:
        print("PSP source-component invariant check failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    print(
        f"Validated {len(gitlinks)} component gitlinks, "
        f"{len(source_map)} unique upstream mappings, and "
        f"{git_source_count} Git-backed recipe sources."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
