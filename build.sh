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
  local local_pspbuild="$2"
  local src entry remote source_name component mapped component_head
  local ref_kind ref_value escaped_ref i=0 j old_count=0
  local -a git_sources=()

  mapfile -t git_sources < <(
    bash -c 'source "$1"; printf "%s\n" "${source[@]}"' _ "$pspbuild" |
      grep -E '(^|::)git\+' || true
  )

  if [[ "${GIT_CONFIG_COUNT:-}" =~ ^[0-9]+$ ]]; then
    old_count="$GIT_CONFIG_COUNT"
  fi
  unset GIT_CONFIG_COUNT
  for ((j = 0; j < old_count; j++)); do
    unset "GIT_CONFIG_KEY_${j}" "GIT_CONFIG_VALUE_${j}"
  done

  cp "$pspbuild" "$local_pspbuild"

  for src in "${git_sources[@]}"; do
    entry="${src#*::}"
    remote="${entry#git+}"
    remote="${remote%%#*}"

    if [[ "$src" == *"::"* ]]; then
      source_name="${src%%::*}"
    else
      source_name="$(basename "$remote")"
      source_name="${source_name%.git}"
    fi

    mapped=""
    if [ -f "$SOURCE_COMPONENTS" ]; then
      mapped=$(awk -F '\t' -v remote="$remote" '$1 == remote { print $2; exit }' "$SOURCE_COMPONENTS")
    fi

    if [ -n "$mapped" ]; then
      component="${ROOT}/${mapped}"
    else
      component="${COMPONENTS}/${source_name}"
    fi

    if [ ! -e "$component" ]; then
      echo "ERROR: Git source submodule is not initialized:"
      echo "  ${component}"
      rm -f "$local_pspbuild"
      exit 1
    fi

    component_head="$(git -C "$component" rev-parse HEAD)"

    # The parent repository's gitlink is authoritative. A package recipe may
    # document an upstream branch, tag, or commit, but makepkg must consume the
    # exact revision already checked out in the shallow component. Rewrite only
    # the temporary build script, so no package build needs tags, ancestor
    # history, or remote-tracking refs to exist in the shallow clone.
    ref_kind=""
    ref_value=""
    for ref_kind in branch tag commit; do
      if [[ "$entry" == *"#${ref_kind}="* ]]; then
        ref_value="${entry##*#${ref_kind}=}"
        ref_value="${ref_value%%&*}"
        break
      fi
      ref_kind=""
    done

    if [ -n "$ref_kind" ]; then
      escaped_ref="${ref_value//&/\\&}"
      escaped_ref="${escaped_ref//|/\\|}"
      sed -i "s|#${ref_kind}=${escaped_ref}|#commit=${component_head}|g" "$local_pspbuild"
    fi

    # A depth-1 submodule can be detached at its gitlink. Give its HEAD a local
    # branch ref so makepkg's local VCS cache can fetch that object without the
    # build contacting the authoritative remote or requiring deeper history.
    git -C "$component" update-ref refs/heads/psp-packages-source "$component_head"

    export "GIT_CONFIG_KEY_${i}=url.file://${component}.insteadOf"
    export "GIT_CONFIG_VALUE_${i}=${remote}"
    i=$((i + 1))
  done

  export GIT_CONFIG_COUNT="$i"
}

for pkgdir in $PKG_LIST; do
  if [[ ! -f "$pkgdir/PSPBUILD" ]]; then
    echo "Package $pkgdir does not exist!"
    continue
  fi

  # A dependency must be installed in the PSP prefix before the dependent
  # package is configured or linked. Build/install dependencies recursively,
  # while preserving --install as the switch controlling installation of the
  # package explicitly requested by this invocation.
  for pkgdep in $(bash -c "./parse_pspbuild.sh $pkgdir/PSPBUILD depends"); do
    ./build.sh --install "$pkgdep"
  done

  pkgfile=$(bash -c "./parse_pspbuild.sh $pkgdir/PSPBUILD pkgoutput")

  if [[ ! -f "${pkgdir}/${pkgfile}" ]]; then
    echo "Building $pkgdir ..."
    local_pspbuild="${pkgdir}/.PSPBUILD.local"
    configure_local_git_sources "$pkgdir/PSPBUILD" "$local_pspbuild"
    if (cd "$pkgdir" && psp-makepkg -p .PSPBUILD.local); then
      rm -f "$local_pspbuild"
    else
      status=$?
      rm -f "$local_pspbuild"
      exit "$status"
    fi
  fi

  if [ ! -z "$doinstall" ]; then
    echo "Installing $pkgdir"
    psp-pacman -U --noconfirm "${pkgdir}/${pkgfile}" --overwrite '*'
  fi
done
