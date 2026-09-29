# PSPTEST package tests

psp-packages owns its package recipes, package source selections, and package-specific PSPTEST source modules. Each implemented runtime test lives under `psptest/<module>/`, matching a package module.

Generated files are not written into the psp-packages source tree. PSPDEV stage 6 discovers these modules and builds them directly from their checked-out source locations into PSPDEV's ignored `build/` hierarchy.

## Runtime module contract

Each implemented package test:

- contains `Makefile.test`;
- builds as one user PRX;
- links against the package under test and `libpsptest`;
- uses `PSPTEST_MODULE(...)` or `PSPTEST_MODULE_WITH_HEAP(...)`;
- emits no generated files into its source directory.

`make -C psptest list` lists implemented package tests. Build the integrated PSPTEST program from PSPDEV with:

```bash
./build.sh 6
./build.sh p 6
```

The template directory contains source templates only and is not itself a test module.

## Coverage contract

Completion is based on public-API coverage, not merely successful linking. Every public function exercised by a module should have a file-scope `PSPTEST_COVERS(function_name);` marker. Host-side tooling can extract the `.psptest_coverage` section and compare it with the package's exported/public API.

Do not add placeholder PASS tests. Runtime outcomes are `PASS`, `FAIL`, `SKIP`, `INTERACTIVE_PASS`, and `INTERACTIVE_FAIL`.
