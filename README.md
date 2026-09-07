# PSP Packages

This repository contains the build recipes for libraries shipped with PSPDEV. Package recipes and their support files live under `pspbuild/`; checked-out upstream source components live under `components/`.

Per-package build trees are stored under `build/<package>/`. Final package archives are written flat into `packages/`.

Neither `build/` nor `packages/` is tracked by Git. To discard all local build state and built packages:

```sh
rm -rf build packages
```

## Building packages

Build one package from the repository root:

```sh
./build.sh <package>
```

Build and install it into the active PSPDEV prefix:

```sh
./build.sh --install <package>
```

Running `./build.sh` without a package name builds all non-blacklisted packages. Required package dependencies are built and installed recursively before their dependents.

When a package is rebuilt, only its own `build/<package>/` directory is cleared first. The resulting build tree remains in place after success or failure for inspection. The completed package archive is placed in `packages/`.

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
Server = https://pspdev.github.io/psp-packages/
```

For forks or alternative repositories, replace `pspdev` in the server URL with the repository owner.

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md).
