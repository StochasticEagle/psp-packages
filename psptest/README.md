# PSPTEST package tests

This directory contains runtime tests for packages installed by `psp-packages`. It is intentionally separate from package recipes and from educational examples.

Each implemented package test lives in a directory whose name exactly matches its `pspbuild/<module>` directory:

```text
psptest/
├── EBOOT.PBP
├── manifest.tsv
├── zlib/
│   ├── Makefile.test
│   └── ...
├── libpng/
│   ├── Makefile.test
│   └── ...
└── ...
```

A package may use multiple source files, but it has one test module directory. Each module builds independently into an `EBOOT.PBP` and links the common PSPSDK `libpsptest.a` runtime.

## On-device launcher

The root PSPTEST `EBOOT.PBP` is the hardware-test launcher. Tests remain isolated executables and return to the launcher through the PSPTEST runtime.

The deployment layout is:

```text
/PSP/GAME/psptest/
├── EBOOT.PBP
├── manifest.tsv
├── state.tsv
├── results/
├── oslib/
│   └── EBOOT.PBP
└── <module>/
    └── EBOOT.PBP
```

The launcher uses a fixed hardware-test palette:

- amber: page headers, titles, and warnings;
- white: normal text;
- light gray: notes and secondary guidance;
- red: failures;
- green: passes.

The launcher supports running the complete automated set, browsing and launching one module, rerunning failures, and reviewing aggregate results. Before launching a child EBOOT it records the active test in `state.tsv`. A missing completion result on the next launcher start is reported as an interrupted warning rather than a test failure.

## Coverage contract

The eventual completion criterion is public-API coverage, not merely successful linking. Every public function exercised by a module should have a file-scope `PSPTEST_COVERS(function_name);` marker. Host-side tooling can extract the `.psptest_coverage` section and compare it with the package's exported/public API.

Do not add placeholder PASS tests. An unimplemented module is more accurate than a test that claims coverage without exercising behavior.

Runtime outcomes are `PASS`, `FAIL`, `SKIP`, `INTERACTIVE_PASS`, and `INTERACTIVE_FAIL`. Hardware-dependent tests should use the interactive status only when human observation is actually required.

## Building

After installing a PSPSDK release that contains PSPTEST:

```bash
make -C psptest
```

Only implemented modules (directories containing `Makefile.test`) are built. `make -C psptest list` lists them.

To build the complete Memory Stick layout:

```bash
make -C psptest bundle
```

The staged tree is written to:

```text
psptest/build/PSP/GAME/psptest/
```

Copy that `PSP` tree to the Memory Stick root.

The `template` directory contains starting files and is not itself a test module.
