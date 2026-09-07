#!/bin/bash
# build.sh by davidgfnet

# Will build the specified package or all of them if none are specified.
# Git-backed package sources are satisfied from the checked-out shallow
# submodules under components/; package builds do not acquire source remotely.

set -e

BLACKLIST="pocketpy|luasocket"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPONENTS="${ROOT}/components"
SOURCE_COMPONENTS="${ROOT}/source-components.tsv"

# Temporary one-commit repositories created from checked-out component
# worktrees. They are removed after psp-makepkg finishes.
LOCAL_SOURCE_SNAPSHOTS=()

doinstall=""
if [ "$1" == "--install" ]; then
  doinstall="true"
  shift
fi

if [ -z "$1" ]; then
  # Package recipes live exactly one directory below the repository root.
  # Sort explicitly: find(1) traversal order is filesystem-dependent.
  PKG_LIST=$(find . -mindepth 2 -maxdepth 2 -type f -name "PSPBUILD" \
    -exec sh -c 'basename "$(dirname "$1")"' _ {} \; | LC_ALL=C sort)
  PKG_LIST=$(printf "%s\n" $PKG_LIST | grep -Ev "^($BLACKLIST)$")
  echo "Will build packages: ${PKG_LIST}" | tr '\n' ' '
else
  PKG_LIST=$1
fi

cleanup_local_source_snapshots() {
  local snapshot
  for snapshot in "${LOCAL_SOURCE_SNAPSHOTS[@]}"; do
    rm -rf "$snapshot"
  done
  LOCAL_SOURCE_SNAPSHOTS=()
}

create_local_source_snapshot() {
  local component="$1"
  local source_name="$2"
  local snapshot tree snapshot_commit

  snapshot=$(mktemp -d "${TMPDIR:-/tmp}/psp-source-${source_name}.XXXXXX")

  # Materialize the checked-out source tree, including initialized nested
  # submodules, but never copy Git metadata. This makes the component worktree
  # itself authoritative and avoids depending on tag refs, ancestor history,
  # shallow boundaries, alternates, or promisor objects in its Git database.
  (
    cd "$component"
    tar --exclude='./.git' --exclude='*/.git' -cf - .
  ) | (
    cd "$snapshot"
    tar -xf -
  )

  git -C "$snapshot" init -q
  git -C "$snapshot" add -A
  tree=$(git -C "$snapshot" write-tree)
  snapshot_commit=$(
    GIT_AUTHOR_NAME=psp-packages \
    GIT_AUTHOR_EMAIL=psp-packages@localhost \
    GIT_COMMITTER_NAME=psp-packages \
    GIT_COMMITTER_EMAIL=psp-packages@localhost \
      git -C "$snapshot" commit-tree "$tree" -m "Local source snapshot"
  )
  git -C "$snapshot" update-ref refs/heads/psp-packages-source "$snapshot_commit"
  git -C "$snapshot" symbolic-ref HEAD refs/heads/psp-packages-source

  LOCAL_SOURCE_SNAPSHOTS+=("$snapshot")
  printf '%s\t%s\n' "$snapshot" "$snapshot_commit"
}

configure_local_git_sources() {
  local pspbuild="$1"
  local local_pspbuild="$2"
  local src entry remote source_name component mapped
  local ref_kind ref_value escaped_ref i=0 j old_count=0
  local snapshot snapshot_commit snapshot_info package_dir
  local -a git_sources=()

  cleanup_local_source_snapshots

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
  package_dir="$(dirname "$pspbuild")"

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
      cleanup_local_source_snapshots
      exit 1
    fi

    # Do not let makepkg reuse a historical/broken bare VCS cache. The checked
    # out component is the cache; this package-local repository is disposable.
    rm -rf "${ROOT}/${package_dir}/${source_name}"

    snapshot_info="$(create_local_source_snapshot "$component" "$source_name")"
    snapshot="${snapshot_info%%$'\t'*}"
    snapshot_commit="${snapshot_info#*$'\t'}"

    # The parent repository's gitlink/worktree is authoritative. Rewrite any
    # documented branch/tag/commit selector in the temporary PSPBUILD to the
    # synthetic one-commit snapshot. A plain Git source needs no selector: the
    # snapshot's HEAD already names psp-packages-source.
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
      sed -i "s|#${ref_kind}=${escaped_ref}|#commit=${snapshot_commit}|g" "$local_pspbuild"
    fi

    export "GIT_CONFIG_KEY_${i}=url.file://${snapshot}.insteadOf"
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
  # package is configured or linked. Dependency recursion can therefore appear
  # before its alphabetically selected parent package.
  for pkgdep in $(bash -c "./parse_pspbuild.sh $pkgdir/PSPBUILD depends"); do
    ./build.sh --install "$pkgdep"
  done

  pkgfile=$(bash -c "./parse_pspbuild.sh $pkgdir/PSPBUILD pkgoutput")

  if [[ ! -f "${pkgdir}/${pkgfile}" ]]; then
    echo "Building $pkgdir ..."

    # Many CMake recipes use ${srcdir}/build. psp-makepkg preserves src/ across
    # invocations, so a version bump can otherwise reuse a cache whose source
    # directory points at the previous release. Remove only this generated
    # top-level CMake build tree before starting a package rebuild.
    rm -rf "${pkgdir}/src/build"

    local_pspbuild="${pkgdir}/.PSPBUILD.local"
    configure_local_git_sources "$pkgdir/PSPBUILD" "$local_pspbuild"
    if (cd "$pkgdir" && psp-makepkg -p .PSPBUILD.local); then
      rm -f "$local_pspbuild"
      cleanup_local_source_snapshots
    else
      status=$?
      rm -f "$local_pspbuild"
      cleanup_local_source_snapshots
      exit "$status"
    fi
  fi

  if [ ! -z "$doinstall" ]; then
    echo "Installing $pkgdir"
    psp-pacman -U --noconfirm "${pkgdir}/${pkgfile}" --overwrite '*'
  fi
done
