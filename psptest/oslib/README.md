# OSLib PSPTEST

OSLib runtime regression coverage remains a loadable PSPTEST PRX. Its source lives here with the package it tests; generated files are produced only by PSPDEV stage 6 under PSPDEV's ignored `build/` hierarchy.

The PRX contains the OSLib test functions, case table, suite descriptor, and the minimal PSPTEST registration entrypoint. It does not contain or link the PSPTEST runner.

The persistent PSPTEST EBOOT loads the PRX, receives its suite descriptor, creates the runner-owned worker thread with the suite's VFPU requirement, executes the cases, records progress/results, and then stops and unloads the PRX.

Build the integrated test program from PSPDEV:

```bash
./build.sh 6
./build.sh p 6
```

Generated ELF, map, PRX, symbol, or result files must never be written into this source tree.
