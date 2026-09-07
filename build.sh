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

# Temporary tar snapshots created from checked-out component worktrees.
# makepkg consumes these as ordinary local archives, so its VCS cache/ref
# machinery is never involved in package builds.
LOCAL_SOURCE_SNAPSHOTS=()
LOCAL_SNAPSHOT_PATH=""

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
    rm -f "$snapshot"
  done
  LOCAL_SOURCE_SNAPSHOTS=()
  LOCAL_SNAPSHOT_PATH=""
}

create_local_source_snapshot() {
  local component="$1"
  local source_name="$2"
  local package_dir="$3"
  local source_index="$4"
  local archive

  archive=$(mktemp --suffix=.tar \
    "${ROOT}/${package_dir}/.psp-source-${source_index}.XXXXXX")

  # Archive exactly the checked-out worktree. Git metadata is deliberately
  # excluded: the parent repository's submodule gitlink is the source revision,
  # and package compilation must not depend on tags, ancestor history, shallow
  # boundaries, remote refs, or makepkg's own Git cache.
  (
    cd "$component"
    tar --exclude='./.git' --exclude='*/.git' \
      --transform="s|^\\./|${source_name}/|" \
      -cf "$archive" .
  )

  LOCAL_SOURCE_SNAPSHOTS+=("$archive")
  LOCAL_SNAPSHOT_PATH="$archive"
}

configure_local_git_sources() {
  local pspbuild="$1"
  local local_pspbuild="$2"
  local record source_index src entry remote source_name component mapped
  local package_dir archive cache
  local -a git_sources=()

  cleanup_local_source_snapshots

  # Keep the source-array index so the temporary PSPBUILD can replace only the
  # Git-backed entry while leaving checksums and unrelated sources aligned.
  mapfile -t git_sources < <(
    bash -c '
      source "$1"
      for i in "${!source[@]}"; do
        case "${source[$i]}" in
          *git+*) printf "%s\t%s\n" "$i" "${source[$i]}" ;;
        esac
      done
    ' _ "$pspbuild"
  )

  cp "$pspbuild" "$local_pspbuild"
  package_dir="$(dirname "$pspbuild")"

  for record in "${git_sources[@]}"; do
    source_index="${record%%$'\t'*}"
    src="${record#*$'\t'}"

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

    # Remove any historical makepkg bare VCS cache left from older versions of
    # this build script. It is no longer used, but a stale cache should not be
    # mistaken for an ordinary package source file/directory.
    cache="${ROOT}/${package_dir}/${source_name}"
    if [ -d "$cache" ] && \
       git --git-dir="$cache" rev-parse --is-bare-repository >/dev/null 2>&1; then
      rm -rf "$cache"
    fi

    create_local_source_snapshot "$component" "$source_name" "$package_dir" "$source_index"
    archive="$LOCAL_SNAPSHOT_PATH"

    # Override the evaluated source entry in the temporary PSPBUILD. The tar
    # contains the same top-level directory name makepkg would have produced for
    # the original Git source, so existing prepare/build/package functions do
    # not need to know that source acquisition changed.
    {
      echo
      printf '# Local snapshot for source[%d] from %s\n' "$source_index" "$component"
      printf 'source[%d]=%q\n' "$source_index" "$(basename "$archive")"
    } >> "$local_pspbuild"
  done
}

trap cleanup_local_source_snapshots EXIT

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
