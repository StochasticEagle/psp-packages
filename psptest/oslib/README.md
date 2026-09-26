# OSLib PSPTEST

This module provides a focused runtime regression test for OSLib's tracker-module path and a link-size comparison harness for compiler/linker optimization work.

The runtime test creates a minimal ProTracker module at runtime, loads it through `oslLoadSoundFileMOD()`, validates the resulting OSLib sound object, and releases it through the public API. No binary fixture is required.

## Normal package test

The default build uses the package's current dependency, `libxmp-lite`:

```bash
make -C psptest/oslib -f Makefile.test
```

The resulting `EBOOT.PBP` is an ordinary PSPTEST runtime test.

## libxmp/linker comparison matrix

Both libxmp implementations export the XMP API symbols used by OSLib, so the same installed `libosl.a` can be linked against either implementation:

```bash
make -C psptest/oslib -f Makefile.test matrix
```

The matrix builds six variants:

| XMP backend | Link profile |
| --- | --- |
| `libxmp-lite` | baseline |
| `libxmp-lite` | section GC |
| `libxmp-lite` | LTO + section GC |
| `libxmp` | baseline |
| `libxmp` | section GC |
| `libxmp` | LTO + section GC |

Results are written to `psptest/oslib/results/`:

- `sizes.txt`: `psp-size` output for each ELF.
- `*.map`: linker map for reachability analysis.
- `*.symbols.txt`: symbols sorted by size.
- `*.elf`: linked ELF for detailed inspection.
- `*.EBOOT.PBP`: packaged executable for deployment/runtime testing.

The profiles change the test application's compile/link flags. To measure the full effect of `-ffunction-sections -fdata-sections` or `-flto` on OSLib/libxmp themselves, rebuild the corresponding packages with those flags first, then rerun the same matrix. This keeps the workload constant while changing only the package/toolchain configuration under test.

Use `make -C psptest/oslib -f Makefile.test matrix-clean` to remove matrix artifacts.
