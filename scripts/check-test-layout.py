#!/usr/bin/env python3

from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
PSPBUILD = ROOT / "pspbuild"
PSPTEST = ROOT / "psptest"
IGNORED_DIRECTORIES = {"template"}

package_modules = {path.parent.name for path in PSPBUILD.glob("*/PSPBUILD")}
implemented = set()
errors = []

if not PSPTEST.is_dir():
    errors.append("psptest directory is missing")
else:
    for path in sorted(PSPTEST.iterdir()):
        if not path.is_dir() or path.name in IGNORED_DIRECTORIES:
            continue

        if path.name not in package_modules:
            errors.append(f"psptest/{path.name}: no matching pspbuild/{path.name}/PSPBUILD")
            continue

        test_makefile = path / "Makefile.test"
        if not test_makefile.is_file():
            errors.append(f"psptest/{path.name}: package test directories must contain Makefile.test")
            continue

        implemented.add(path.name)

if errors:
    for error in errors:
        print(f"ERROR: {error}", file=sys.stderr)
    raise SystemExit(1)

print(f"PSPTEST modules implemented: {len(implemented)} / {len(package_modules)}")
if implemented:
    print("Implemented: " + " ".join(sorted(implemented)))
