# PSPTEST package tests

This directory contains runtime tests for packages installed by `psp-packages`. Each implemented package test lives in a directory whose name matches its `pspbuild/<module>` directory and builds independently as a user-mode PRX linked against the package under test and the common PSPSDK `libpsptest.a` runtime.

## On-device launcher

The root `EBOOT.PBP` is the persistent hardware-test launcher. Package tests are loadable user PRXs rather than child EBOOTs.

```text
/PSP/GAME/psptest/
├── EBOOT.PBP
├── manifest.tsv
├── state.tsv
├── results/
├── oslib/
│   └── test.prx
└── <module>/
    └── test.prx
```

For each test, the launcher creates a supervisor thread, loads the PRX with `sceKernelLoadModuleMs()`, and starts it with `sceKernelStartModule()`. The PRX's `module_start()` performs only framework setup and creates a dedicated test thread. Test code therefore never runs on the launcher/UI thread or the supervisor thread. When the suite completes, the supervisor stops and unloads the PRX and the launcher reads its result log.

The launcher records the active module in `state.tsv`, so an interrupted run can still be identified on the next launch.

## Coverage contract

Completion is based on public-API coverage, not merely successful linking. Every public function exercised by a module should have a file-scope `PSPTEST_COVERS(function_name);` marker. Host-side tooling can extract the `.psptest_coverage` section and compare it with the package's exported/public API.

Do not add placeholder PASS tests. Runtime outcomes are `PASS`, `FAIL`, `SKIP`, `INTERACTIVE_PASS`, and `INTERACTIVE_FAIL`.

## Building

After installing a PSPSDK release that contains PSPTEST:

```bash
make -C psptest
make -C psptest bundle
```

The staged Memory Stick tree is written to `psptest/build/PSP/GAME/psptest/`. Copy that `PSP` tree to the Memory Stick root.

The `template` directory contains starting files and is not itself a test module.
