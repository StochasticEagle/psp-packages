# OSLib PSPTEST

This module provides OSLib runtime regression coverage. Its source lives here with the package it tests; generated files are produced only by PSPDEV stage 6 under PSPDEV's ignored `build/` hierarchy.

The runtime module covers the OSLib tracker-module path, memory/file helpers, image and palette handling, graphics, audio, utility dialogs, and selected platform-state queries.

Build the integrated test program from PSPDEV:

```bash
./build.sh 6
./build.sh p 6
```

The OSLib test is built as one PRX module and uses `PSPTEST_MODULE_WITH_HEAP(...)` because it requires a larger heap than the PSPTEST default.

The former source-directory linker-size matrix is intentionally disabled. Comparative builds must use separate ignored build directories so no generated ELF, map, PRX, symbol, or result files are ever written into the source tree.
