# PSP Packages

[![CI](https://img.shields.io/github/actions/workflow/status/StochasticEagle/psp-packages/.github/workflows/build.yml?branch=dev%2Ffork&style=for-the-badge&logo=github&label=CI)](https://github.com/StochasticEagle/psp-packages/actions/workflows/build.yml)

This repository contains the build recipes for libraries shipped with PSPDEV. Package recipes and their support files live under `pspbuild/`; checked-out upstream source components live under `components/`.

Generated state is kept separate from recipes:

- `build/<package>/` contains makepkg's temporary build tree (`src/`, `pkg/`, source snapshots, and downloaded sources).
- `packages/` contains only final `.pkg.tar.gz` archives and is flat.

Neither `build/` nor `packages/` is tracked by Git. To discard all local build state and built packages:

```sh
rm -rf build packages
```

## Building packages

A fresh clone can be built directly; `build.sh` initializes the source submodules at the revisions selected by this repository.

Build one package from the repository root:

```sh
./build.sh <package>
```

The Mbed TLS 4.x generator requires the host Python modules `jinja2` and
`jsonschema`. On Debian/Ubuntu these are provided by `python3-jinja2` and
`python3-jsonschema`.

Build and install it into the active PSPDEV prefix:

```sh
./build.sh --install <package>
```

Running `./build.sh` without a package name builds all non-blacklisted packages. Required package dependencies are built and installed recursively before their dependents.

When a package is rebuilt, only its own `build/<package>/` tree is cleared first. Build trees remain available after success or failure for inspection. Completed package archives are written to `packages/`.

## Source components and reproducibility

Every Git-backed upstream source is represented by a shallow submodule under `components/`. A Git source in a `PSPBUILD` must use an exact 40-character `#commit=` selector that matches the component gitlink selected by this repository.

`source-components.tsv` maps each unique upstream URL to its default component. Recipes that intentionally use a different component for the same upstream repository, such as historical SDL 1.2/2.x sources alongside SDL3, use `psp_source_components` to disambiguate the source index.

The invariant checker verifies the recipe URL and commit, `source-components.tsv`, `.gitmodules`, and the component gitlink together:

```sh
python3 scripts/check-source-components.py
```

`build.sh` runs this check before source acquisition. Git-backed package builds are then fed tar snapshots of the already-selected local components; makepkg does not clone those Git sources during the package build. This keeps source selection in the repository/component layer and is the basis for eventually prohibiting package-build network access entirely.

## Installing libraries from the repository

Installing libraries from the published repository can be done with `psp-pacman`:

```sh
psp-pacman -Sy library
```

An overview of available libraries can be viewed with:

```sh
psp-pacman -Ss
```

Installing all available libraries:

```sh
psp-pacman -Sy
psp-pacman -S psp-libraries
```

Updating libraries:

```sh
psp-pacman -Syu
```

## Pacman Repository Hosting

The pacman repository produced from this repository is published to GitHub Pages by GitHub Actions. The repository should already be configured in `${PSPDEV}/etc/pacman.conf`:

```ini
[pspdev]
SigLevel = Optional TrustAll
Server = https://stochasticeagle.github.io/psp-packages/
```

This fork publishes its package repository from the `dev/fork` branch to `https://stochasticeagle.github.io/psp-packages/`.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).
