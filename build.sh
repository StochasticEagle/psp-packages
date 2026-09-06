#!/bin/bash
# build.sh by davidgfnet

# Will build the specified package or all of them if none are specified.
# Git-backed package sources are satisfied from the checked-out shallow
# submodules under components/; package builds do not acquire source.

set -e

BLACKLIST="pocketpy|luasocket"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPONENTS="${ROOT}/components"
SOURCE_COMPONENTS="${ROOT}/source-components.tsv"

doinstall=""
if [ "$1" == "--install" ]; then
  doinstall="true"
  shift
fi

if [ -z "$1" ]; then
  # Package recipes live exactly one directory below the repository root.
  # Do not recurse into generated src/pkg trees or checked-out components.
  PKG_LIST=$(find . -mindepth 2 -maxdepth 2 -type f -name "PSPBUILD" -exec sh -c 'basename "$(dirname "$1")"' _ {} \;)
  PKG_LIST=$(printf "%s\n" $PKG_LIST | grep -Ev "^($BLACKLIST)$")
  echo "Will build packages: ${PKG_LIST}" | tr '\n' ' '
else
  PKG_LIST=$1
fi

configure_local_git_sources() {
  local pspbuild="$1"
  local src entry remote alias component mapped i=0
  local -a git_sources=()

  mapfile -t git_sources < <(
    bash -c 'source "$1"; printf "%s\n" "${source[@]}"' _ "$pspbuild" |
      grep -E '(^|::)git\+' || true
  )

  export GIT_CONFIG_COUNT="${#git_sources[@]}"

  for src in "${git_sources[@]}"; do
    entry="${src#*::}"
    remote="${entry#git+}"
    remote="${remote%%#*}"

    mapped=""
    if [ -f "$SOURCE_COMPONENTS" ]; then
      mapped=$(awk -F '\t' -v remote="$remote" '$1 == remote { print $2; exit }' "$SOURCE_COMPONENTS")
    fi

    if [ -n "$mapped" ]; then
      component="${ROOT}/${mapped}"
    else
      if [[ "$src" == *"::"* ]]; then
        alias="${src%%::*}"
      else
        alias="$(basename "$remote")"
        alias="${alias%.git}"
      fi
      component="${COMPONENTS}/${alias}"
    fi

    if [ ! -e "$component" ]; then
      echo "ERROR: Git source submodule is not initialized:"
      echo "  ${component}"
      exit 1
    fi

    export "GIT_CONFIG_KEY_${i}=url.file://${component}.insteadOf"
    export "GIT_CONFIG_VALUE_${i}=${remote}"
    i=$((i + 1))
  done
}

for pkgdir in $PKG_LIST; do
  if [[ ! -f "$pkgdir/PSPBUILD" ]]; then
    echo "Package $pkgdir does not exist!"
    continue
  fi

  for pkgdep in $(bash -c "./parse_pspbuild.sh $pkgdir/PSPBUILD depends"); do
    if [ -z "$doinstall" ]; then
      ./build.sh "$pkgdep"
    else
      ./build.sh --install "$pkgdep"
    fi
  done

  pkgfile=$(bash -c "./parse_pspbuild.sh $pkgdir/PSPBUILD pkgoutput")

  if [[ ! -f "${pkgdir}/${pkgfile}" ]]; then
    echo "Building $pkgdir ..."
    configure_local_git_sources "$pkgdir/PSPBUILD"
    (cd "$pkgdir" && psp-makepkg)
  fi

  if [ ! -z "$doinstall" ]; then
    echo "Installing $pkgdir"
    psp-pacman -U --noconfirm "${pkgdir}/${pkgfile}" --overwrite '*'
  fi
done
