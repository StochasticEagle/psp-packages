# PSP Packages

This repository contains the build recipes for libraries shipped with PSPDEV. Package recipes and their support files live under `pspbuild/`; checked-out upstream source components live under `components/`.

Generated package archives are written flat into `build/`. Package compilation itself runs in disposable temporary workspaces, so recipe directories never accumulate `src/`, `pkg/`, downloaded source archives, or package files.

To discard all repository-side build output and start clean:

```sh
rm -rf build
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
