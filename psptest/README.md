# PSPTEST package tests

psp-packages owns its package-specific PSPTEST source modules. Each runtime test remains in `psptest/<module>/` beside the package repository that owns it.

A package test PRX contains only test code, its `PspTestSuite` descriptor, and the minimal `module_start()`/ `module_stop()` registration shim supplied by `psptest.h`. It does not link `libpsptest.a` and does not contain runner/orchestrator logic.

The persistent PSPTEST EBOOT in PSPDEV is the sole orchestrator. It loads each PRX, obtains the suite descriptor, creates the test thread, executes cases, records progress/results, and stops/unloads the module.

Generated files are produced only under PSPDEV's ignored `build/` hierarchy. Test source is never copied into PSPDEV, PSPSDK, or the PSPTEST program tree.

## Coverage

Use `PSPTEST_COVERS(function_name);` for every public function actually exercised by the suite. Do not add placeholder passing tests.
