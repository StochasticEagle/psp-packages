# PSPTEST package tests

This directory contains runtime tests for packages installed by `psp-packages`. It is intentionally separate from package recipes and from educational examples.

Each implemented package test lives in a directory whose name exactly matches its `pspbuild/<module>` directory:

```text
psptest/
├── zlib/
│   ├── Makefile.test
│   └── ...
├── libpng/
│   ├── Makefile.test
│   └── ...
└── ...
```

A package may use multiple source files, but it has one test module directory. Each module builds independently into an `EBOOT.PBP` and links the common PSPSDK `libpsptest.a` runtime.

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

The `template` directory contains starting files and is not itself a test module.
