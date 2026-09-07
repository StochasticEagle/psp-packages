#!/bin/bash
# Build one package or all packages. Recipe files stay read-only under
# pspbuild/; all persistent package output is written flat into build/.

set -e

BLACKLIST="pocketpy|luasocket"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RECIPES="${ROOT}/pspbuild"
COMPONENTS="${ROOT}/components"
OUTPUT="${ROOT}/build"
SOURCE_COMPONENTS="${ROOT}/source-components.tsv"
CURRENT_WORKDIR=""

cleanup_workdir() {
  if [[ -n "${CURRENT_WORKDIR}" && -d "${CURRENT_WORKDIR}" ]]; then
    rm -rf "${CURRENT_WORKDIR}"
  fi
  CURRENT_WORKDIR=""
}
trap cleanup_workdir EXIT

mkdir -p "${OUTPUT}"

doinstall=""
if [[ "${1:-}" == "--install" ]]; then
  doinstall="true"
  shift
fi

if [[ -z "${1:-}" ]]; then
  PKG_LIST=$(find "${RECIPES}" -mindepth 2 -maxdepth 2 -type f -name PSPBUILD \
    -exec sh -c 'basename "$(dirname "$1")"' _ {} \; | LC_ALL=C sort)
  PKG_LIST=$(printf "%s\n" ${PKG_LIST} | grep -Ev "^(${BLACKLIST})$")
  echo "Will build packages: ${PKG_LIST}" | tr '\n' ' '
else
  PKG_LIST="$1"
fi

create_local_source_snapshot() {
  local component="$1"
  local source_name="$2"
  local workspace="$3"
  local source_index="$4"
  local archive

  archive=$(mktemp --suffix=.tar \
    "${workspace}/.psp-source-${source_index}.XXXXXX")

  (
    cd "${component}"
    tar --exclude='./.git' --exclude='*/.git' \
      --transform="s|^\\./|${source_name}/|" \
      -cf "${archive}" .
  )

  printf '%s\n' "${archive}"
}

configure_local_git_sources() {
  local pspbuild="$1"
  local local_pspbuild="$2"
  local workspace="$3"
  local record source_index src entry remote source_name component mapped archive
  local -a git_sources=()

  mapfile -t git_sources < <(
    bash -c '
      source "$1"
      for i in "${!source[@]}"; do
        case "${source[$i]}" in
          *git+*) printf "%s\t%s\n" "$i" "${source[$i]}" ;;
        esac
      done
    ' _ "${pspbuild}"
  )

  cp "${pspbuild}" "${local_pspbuild}"

  for record in "${git_sources[@]}"; do
    source_index="${record%%$'\t'*}"
    src="${record#*$'\t'}"

    entry="${src#*::}"
    remote="${entry#git+}"
    remote="${remote%%#*}"

    if [[ "${src}" == *"::"* ]]; then
      source_name="${src%%::*}"
    else
      source_name="$(basename "${remote}")"
      source_name="${source_name%.git}"
    fi

    mapped=""
    if [[ -f "${SOURCE_COMPONENTS}" ]]; then
      mapped=$(awk -F '\t' -v remote="${remote}" '$1 == remote { print $2; exit }' "${SOURCE_COMPONENTS}")
    fi

    if [[ -n "${mapped}" ]]; then
      component="${ROOT}/${mapped}"
    else
      component="${COMPONENTS}/${source_name}"
    fi

    if [[ ! -e "${component}" ]]; then
      echo "ERROR: Git source submodule is not initialized:"
      echo "  ${component}"
      return 1
    fi

    archive=$(create_local_source_snapshot \
      "${component}" "${source_name}" "${workspace}" "${source_index}")

    {
      echo
      printf '# Local snapshot for source[%d] from %s\n' "${source_index}" "${component}"
      printf 'source[%d]=%q\n' "${source_index}" "$(basename "${archive}")"
    } >> "${local_pspbuild}"
  done
}

for pkg in ${PKG_LIST}; do
  recipe_dir="${RECIPES}/${pkg}"
  pspbuild="${recipe_dir}/PSPBUILD"

  if [[ ! -f "${pspbuild}" ]]; then
    echo "Package ${pkg} does not exist!"
    continue
  fi

  for pkgdep in $("${ROOT}/parse_pspbuild.sh" "${pspbuild}" depends); do
    "${ROOT}/build.sh" --install "${pkgdep}"
  done

  pkgfile=$("${ROOT}/parse_pspbuild.sh" "${pspbuild}" pkgoutput)

  if [[ ! -f "${OUTPUT}/${pkgfile}" ]]; then
    echo "Building ${pkg} ..."

    CURRENT_WORKDIR=$(mktemp -d "${TMPDIR:-/tmp}/psp-packages-${pkg}.XXXXXX")
    cp -a "${recipe_dir}/." "${CURRENT_WORKDIR}/"

    local_pspbuild="${CURRENT_WORKDIR}/.PSPBUILD.local"
    configure_local_git_sources \
      "${CURRENT_WORKDIR}/PSPBUILD" "${local_pspbuild}" "${CURRENT_WORKDIR}"

    if (cd "${CURRENT_WORKDIR}" && \
      PKGDEST="${OUTPUT}" psp-makepkg -p .PSPBUILD.local); then
      :
    else
      status=$?
      cleanup_workdir
      exit "${status}"
    fi

    cleanup_workdir

    if [[ ! -f "${OUTPUT}/${pkgfile}" ]]; then
      echo "ERROR: Expected package was not produced:"
      echo "  ${OUTPUT}/${pkgfile}"
      exit 1
    fi
  fi

  if [[ -n "${doinstall}" ]]; then
    echo "Installing ${pkg}"
    psp-pacman -U --noconfirm "${OUTPUT}/${pkgfile}" --overwrite '*'
  fi
done
