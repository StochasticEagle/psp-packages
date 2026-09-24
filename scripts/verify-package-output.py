#!/usr/bin/env python3
"""Verify the complete package archive set and clean-prefix installability."""

from __future__ import annotations

import os
import pathlib
import re
import subprocess
import sys
import tarfile
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
RECIPES = ROOT / "pspbuild"
PACKAGES = ROOT / "packages"


def recipe_metadata(path: pathlib.Path) -> tuple[str, str, set[str]]:
    parser = ROOT / "parse_pspbuild.sh"
    pkgname = subprocess.check_output([str(parser), str(path), "pkgname"], text=True).strip()
    archive = subprocess.check_output([str(parser), str(path), "pkgoutput"], text=True).strip()
    raw_conflicts = subprocess.check_output([str(parser), str(path), "conflicts"], text=True).split()
    conflicts = {re.split(r"[<>=]", item, maxsplit=1)[0] for item in raw_conflicts}
    return pkgname, archive, conflicts


def archive_pkgname(archive: pathlib.Path) -> str:
    with tarfile.open(archive, "r:*") as tf:
        try:
            member = tf.getmember(".PKGINFO")
        except KeyError as exc:
            raise RuntimeError(f"{archive.name}: missing .PKGINFO") from exc
        handle = tf.extractfile(member)
        if handle is None:
            raise RuntimeError(f"{archive.name}: cannot read .PKGINFO")
        text = handle.read().decode("utf-8", "replace")
    match = re.search(r"(?m)^pkgname = (.+)$", text)
    if not match:
        raise RuntimeError(f"{archive.name}: .PKGINFO has no pkgname")
    return match.group(1).strip()


def forbidden_needles() -> list[bytes]:
    values = {
        b"$pkgdir",
        b"${pkgdir}",
        b"/home/runner/",
    }
    for name in ("PSPDEV", "GITHUB_WORKSPACE", "RUNNER_TEMP"):
        value = os.environ.get(name)
        if value:
            values.add(value.encode())
    values.add(str(ROOT).encode())
    values.add(str(ROOT / "build").encode())
    return sorted(values)


def scan_archive(archive: pathlib.Path, errors: list[str]) -> None:
    needles = forbidden_needles()
    with tarfile.open(archive, "r:*") as tf:
        for member in tf.getmembers():
            path = pathlib.PurePosixPath(member.name)
            if path.is_absolute() or ".." in path.parts:
                errors.append(f"{archive.name}: unsafe archive path: {member.name}")
                continue
            if not member.isfile():
                continue
            handle = tf.extractfile(member)
            if handle is None:
                continue
            data = handle.read()

            if member.name in {".BUILDINFO", ".MTREE", ".PKGINFO"}:
                continue

            basename = path.name
            text_metadata = (
                basename.endswith(("-config", ".pc", ".la", ".cmake", ".mk", ".conf", ".cfg"))
                or data.startswith(b"#!")
            )
            if not text_metadata:
                continue

            for needle in needles:
                if needle and needle in data:
                    errors.append(
                        f"{archive.name}:{member.name}: contains build/staging path "
                        f"{needle.decode('utf-8', 'replace')}"
                    )
                    break


def packages_conflict(name_a: str, name_b: str, conflicts: dict[str, set[str]]) -> bool:
    return name_b in conflicts.get(name_a, set()) or name_a in conflicts.get(name_b, set())


def verify_clean_install(
    archives_by_name: dict[str, pathlib.Path],
    conflicts: dict[str, set[str]],
) -> None:
    pspdev = os.environ.get("PSPDEV")
    if not pspdev:
        raise RuntimeError("PSPDEV is not set")

    pacman = pathlib.Path(pspdev) / "share/pacman/bin/pacman"
    get_arch = pathlib.Path(pspdev) / "share/pacman/bin/get-arch"
    config = pathlib.Path(pspdev) / "etc/pacman.conf"
    for required in (pacman, get_arch, config):
        if not required.exists():
            raise RuntimeError(f"clean-prefix install prerequisite missing: {required}")

    selected: list[str] = []
    alternatives: list[str] = []
    for name in sorted(archives_by_name):
        if any(packages_conflict(name, current, conflicts) for current in selected):
            alternatives.append(name)
        else:
            selected.append(name)

    transactions = [selected]
    for alternative in alternatives:
        transaction = [
            name for name in selected
            if not packages_conflict(alternative, name, conflicts)
        ]
        transaction.append(alternative)
        transactions.append(sorted(transaction))

    arch = subprocess.check_output([str(get_arch)], text=True).strip()
    for index, package_names in enumerate(transactions, start=1):
        with tempfile.TemporaryDirectory(prefix=f"psp-packages-install-{index}-") as tmp:
            root = pathlib.Path(tmp) / "pspdev"
            for directory in (
                root / "var/lib/pacman",
                root / "var/cache/pacman/pkg",
                root / "etc/pacman.d/gnupg",
                root / "var/log",
                root / "share/libalpm/hooks",
                root / "etc/pacman.d/hooks",
            ):
                directory.mkdir(parents=True, exist_ok=True)

            subprocess.run(
                [
                    str(pacman),
                    "--root", str(root),
                    "--dbpath", str(root / "var/lib/pacman"),
                    "--config", str(config),
                    "--cachedir", str(root / "var/cache/pacman/pkg"),
                    "--gpgdir", str(root / "etc/pacman.d/gnupg"),
                    "--logfile", str(root / "var/log/pacman.log"),
                    "--hookdir", str(root / "share/libalpm/hooks"),
                    "--hookdir", str(root / "etc/pacman.d/hooks"),
                    "--arch", arch,
                    "-U",
                    "--noconfirm",
                    *[str(archives_by_name[name]) for name in package_names],
                ],
                check=True,
            )


def verify_repo_add(archives: list[pathlib.Path]) -> None:
    pspdev = os.environ.get("PSPDEV")
    if not pspdev:
        raise RuntimeError("PSPDEV is not set")
    repo_add = pathlib.Path(pspdev) / "share/pacman/bin/repo-add"
    if not repo_add.exists():
        raise RuntimeError(f"repo-add is missing: {repo_add}")

    with tempfile.TemporaryDirectory(prefix="psp-packages-repo-") as tmp:
        tmpdir = pathlib.Path(tmp)
        local_archives: list[str] = []
        for archive in archives:
            target = tmpdir / archive.name
            os.link(archive, target)
            local_archives.append(target.name)

        subprocess.run(
            [str(repo_add), "pspdev.db.tar.gz", *local_archives],
            cwd=tmpdir,
            check=True,
        )
        for expected in ("pspdev.db.tar.gz", "pspdev.files.tar.gz"):
            if not (tmpdir / expected).is_file():
                raise RuntimeError(f"repo-add did not produce {expected}")


def main() -> int:
    errors: list[str] = []
    expected_by_name: dict[str, str] = {}
    conflicts_by_name: dict[str, set[str]] = {}
    for recipe in sorted(RECIPES.glob("*/PSPBUILD")):
        pkgname, archive, conflicts = recipe_metadata(recipe)
        if pkgname in expected_by_name:
            errors.append(f"duplicate recipe pkgname: {pkgname}")
        expected_by_name[pkgname] = archive
        conflicts_by_name[pkgname] = conflicts

    archives = sorted(PACKAGES.glob("*.pkg.tar.*"))
    if not archives:
        errors.append("no package archives were produced")

    actual_by_name: dict[str, list[pathlib.Path]] = {}
    for archive in archives:
        try:
            pkgname = archive_pkgname(archive)
        except (RuntimeError, tarfile.TarError) as exc:
            errors.append(str(exc))
            continue
        actual_by_name.setdefault(pkgname, []).append(archive)
        scan_archive(archive, errors)

    expected_files = set(expected_by_name.values())
    actual_files = {archive.name for archive in archives}

    for pkgname, expected in sorted(expected_by_name.items()):
        matches = actual_by_name.get(pkgname, [])
        if len(matches) != 1:
            errors.append(
                f"{pkgname}: expected exactly one current archive, found {len(matches)}"
            )
        if expected not in actual_files:
            errors.append(f"{pkgname}: missing expected archive {expected}")

    for unexpected in sorted(actual_files - expected_files):
        errors.append(f"unexpected/stale package archive: {unexpected}")

    for pkgname in sorted(set(actual_by_name) - set(expected_by_name)):
        errors.append(f"archive has no matching PSPBUILD recipe: {pkgname}")

    if errors:
        print("Package output verification failed:", file=sys.stderr)
        for error in errors:
            print(f"  - {error}", file=sys.stderr)
        return 1

    archives_by_name = {
        pkgname: PACKAGES / expected_by_name[pkgname]
        for pkgname in sorted(expected_by_name)
    }
    ordered_archives = list(archives_by_name.values())

    try:
        verify_clean_install(archives_by_name, conflicts_by_name)
        verify_repo_add(ordered_archives)
    except (OSError, RuntimeError, subprocess.CalledProcessError) as exc:
        print(f"Package transaction verification failed: {exc}", file=sys.stderr)
        return 1

    print(
        f"Verified {len(ordered_archives)} package archives, clean-prefix "
        "installability, metadata paths, and repository generation."
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
