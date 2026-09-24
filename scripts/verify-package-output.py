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


def recipe_metadata(path: pathlib.Path) -> tuple[str, str]:
    parser = ROOT / "parse_pspbuild.sh"
    pkgname = subprocess.check_output([str(parser), str(path), "pkgname"], text=True).strip()
    archive = subprocess.check_output([str(parser), str(path), "pkgoutput"], text=True).strip()
    return pkgname, archive


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
        b"$PSPDEV",
        b"${PSPDEV}",
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

            for needle in needles:
                if needle and needle in data:
                    errors.append(
                        f"{archive.name}:{member.name}: contains build/staging path "
                        f"{needle.decode('utf-8', 'replace')}"
                    )
                    break

            if member.name.endswith(".pc"):
                text = data.decode("utf-8", "replace")
                bad_pc = [
                    "$pkgdir",
                    "${pkgdir}",
                    "$PSPDEV",
                    "${PSPDEV}",
                    "/home/runner/",
                ]
                bad_pc.extend(
                    value
                    for value in (
                        os.environ.get("PSPDEV"),
                        os.environ.get("GITHUB_WORKSPACE"),
                        os.environ.get("RUNNER_TEMP"),
                    )
                    if value
                )
                for bad in bad_pc:
                    if bad in text:
                        errors.append(
                            f"{archive.name}:{member.name}: pkg-config metadata "
                            f"contains forbidden path {bad}"
                        )
                        break


def verify_clean_install(archives: list[pathlib.Path]) -> None:
    pspdev = os.environ.get("PSPDEV")
    if not pspdev:
        raise RuntimeError("PSPDEV is not set")

    pacman = pathlib.Path(pspdev) / "share/pacman/bin/pacman"
    get_arch = pathlib.Path(pspdev) / "share/pacman/bin/get-arch"
    config = pathlib.Path(pspdev) / "etc/pacman.conf"
    for required in (pacman, get_arch, config):
        if not required.exists():
            raise RuntimeError(f"clean-prefix install prerequisite missing: {required}")

    with tempfile.TemporaryDirectory(prefix="psp-packages-install-") as tmp:
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

        arch = subprocess.check_output([str(get_arch)], text=True).strip()
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
                *map(str, archives),
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
    for recipe in sorted(RECIPES.glob("*/PSPBUILD")):
        pkgname, archive = recipe_metadata(recipe)
        if pkgname in expected_by_name:
            errors.append(f"duplicate recipe pkgname: {pkgname}")
        expected_by_name[pkgname] = archive

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

    ordered_archives = [
        PACKAGES / expected_by_name[pkgname]
        for pkgname in sorted(expected_by_name)
    ]

    try:
        verify_clean_install(ordered_archives)
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
