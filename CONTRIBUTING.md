# Contributing

This repository contains the build scripts for the libraries contained in the PSPDEV toolchain. If a library is missing or could use an update, please open an issue or submit a pull request.

## Repository layout

Package build instructions live under `pspbuild/<package>/`. Each package directory contains its tracked `PSPBUILD` plus any local patches or support files required by that recipe.

Generated state is separate:

- `build/<package>/` contains makepkg's temporary `src/` and `pkg/` trees plus downloaded/local source snapshots.
- `packages/` contains final package archives and is flat.

Neither directory is tracked by Git. A complete local reset is:

```sh
rm -rf build packages
```

`build.sh` runs makepkg with the recipe directory as `startdir`, so recipes and local support files retain normal makepkg path semantics while build output is redirected through `BUILDDIR`, `SRCDEST`, and `PKGDEST`.

## How to add a library

A `PSPBUILD` file is a `PKGBUILD`-style recipe. The Arch Linux PKGBUILD documentation is a useful reference for the format.

Create `pspbuild/<package>/PSPBUILD` and place any local patches or support files in that same package directory. It is recommended to base a new recipe on an existing package using the same build system.

Build and install a package from the repository root with:

```sh
./build.sh --install <package>
```

Before making a pull request, make sure the library builds, installs, and works.

## Criteria for contributions

For new contributions to be merged, PSPBUILDs should meet the following criteria:

- For new packages:
  - Set `pkgdesc` and `license`.
  - Set `arch` to `(any)`.
  - Use `sha256sums` for downloaded files. Git sources and local patches may use `SKIP` where appropriate.
  - Prefer versioned release sources over moving development branches.
  - Install the license in `$pkgdir/psp/share/licenses/$pkgname/`.
  - Git sources should use a selected tag or commit where reproducibility requires it.
  - `pkgname` should not contain capital letters or special characters other than `-`.
  - Set `groups` to `psp-libraries` unless the package conflicts with an existing package.
  - Use SPDX license identifiers.
- For existing packages:
  - Change either `pkgver` or `pkgrel` when the package contents change.
- For all PSPBUILDs:
  - Libraries go in `$pkgdir/psp/lib/`.
  - Include files go in `$pkgdir/psp/include/`.
  - Pkg-config files go in `$pkgdir/psp/lib/pkgconfig/`.
  - License files go in `$pkgdir/psp/share/licenses/$pkgname/`.
  - User-facing scripts go in `$pkgdir/bin/`.
  - Other scripts go in `$pkgdir/psp/bin/` or `$pspdir/share/$pkgname/bin/`.
  - Documentation goes in `$pkgdir/psp/share/doc/$pkgname/` or `$pkgdir/share/doc/$pkgname/`.
