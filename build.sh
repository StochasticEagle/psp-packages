#!/bin/bash
# Build one package or all packages. Recipes remain under pspbuild/;
# makepkg build trees live under build/ and final package archives are written
# flat into packages/.

set -e

BLACKLIST="pocketpy|luasocket"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
RECIPES="${ROOT}/pspbuild"
COMPONENTS="${ROOT}/components"
BUILD_ROOT="${ROOT}/build"
PACKAGES="${ROOT}/packages"
SOURCE_COMPONENTS="${ROOT}/source-components.tsv"
CURRENT_LOCAL_BUILD_FILE=""

cleanup_local_buildfile() {
  if [[ -n "${CURRENT_LOCAL_BUILD_FILE}" ]]; then
    rm -f "${CURRENT_LOCAL_BUILD_FILE}"
    CURRENT_LOCAL_BUILD_FILE=""
  fi
}
trap cleanup_local_buildfile EXIT

mkdir -p "${BUILD_ROOT}" "${PACKAGES}"

# A fresh clone must be buildable directly. Package source submodules follow
# the gitlinks selected by psp-packages; do not float them with --remote.
if [[ -z "${PSP_PACKAGES_SUBMODULES_READY:-}" ]]; then
  git -C "${ROOT}" submodule update --init --recursive --depth 1 --quiet
  export PSP_PACKAGES_SUBMODULES_READY=1
fi

# Recipes used to live at <repo>/<package>. They now live at
# <repo>/pspbuild/<package>, so a recipe must not escape through startdir/..
# to reach repository files. Git-backed source components are supplied by the
# snapshot mechanism below; local patches/support files stay beside PSPBUILD.
legacy_startdir_paths=$(grep -RInE --include=PSPBUILD \
  '\$startdir/(\.\./)+|\$\{startdir\}/(\.\./)+' "${RECIPES}" || true)
if [[ -n "${legacy_startdir_paths}" ]]; then
  echo "ERROR: PSPBUILD recipes contain repository-relative startdir paths:"
  printf '%s\n' "${legacy_startdir_paths}"
  echo "Use source components or package-local support files instead."
  exit 1
fi

doinstall=""
if [[ "${1:-}" == "--install" ]]; then
  doinstall="true"
  shift
fi

if [[ -z "${1:-}" ]]; then
  PKG_LIST=$(find "${RECIPES}" -mindepth 2 -maxdepth 2 -type f -name PSPBUILD \
    -exec sh -c 'basename "$(dirname "$1")"' _ {} \; | LC_ALL=C sort)
  PKG_LIST=$(printf "%s\n" ${PKG_LIST} | grep -Ev "^(${BLACKLIST})$")
  printf 'Will build packages:'
  while IFS= read -r pkg; do
    [[ -n "${pkg}" ]] && printf ' %s' "${pkg}"
  done <<< "${PKG_LIST}"
  printf '\n'
else
  PKG_LIST="$1"
fi

create_local_source_snapshot() {
  local component="$1"
  local source_name="$2"
  local source_cache="$3"
  local source_index="$4"
  local archive

  mkdir -p "${source_cache}"
  archive=$(mktemp --suffix=.tar \
    "${source_cache}/.psp-source-${source_index}.XXXXXX")

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
  local source_cache="$3"
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

  if (( ${#git_sources[@]} == 0 )); then
    return 1
  fi

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

    # A recipe can override URL-based source mapping when two intentionally
    # distinct components use the same upstream repository (for example the
    # current SDL component and a historical SDL 1.2 component).
    mapped=$(bash -c '
      source "$1"
      if declare -p psp_source_components >/dev/null 2>&1; then
        printf "%s" "${psp_source_components[$2]-}"
      fi
    ' _ "${pspbuild}" "${source_index}")

    if [[ -n "${mapped}" ]]; then
      if [[ "${mapped}" != components/* ]]; then
        echo "ERROR: Invalid psp_source_components[${source_index}] in ${pspbuild}:"
        echo "  ${mapped}"
        return 2
      fi
    elif [[ -f "${SOURCE_COMPONENTS}" ]]; then
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
      return 2
    fi

    archive=$(create_local_source_snapshot \
      "${component}" "${source_name}" "${source_cache}" "${source_index}")

    {
      echo
      printf '# Local snapshot for source[%d] from %s\n' "${source_index}" "${component}"
      printf 'source[%d]=%q\n' "${source_index}" "${archive}"
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
  pkgbase=$(bash -c 'source "$1"; printf "%s\n" "${pkgbase:-${pkgname[0]}}"' _ "${pspbuild}")
  package_path="${PACKAGES}/${pkgfile}"
  workdir="${BUILD_ROOT}/${pkgbase}"
  source_cache="${workdir}/sources"

  if [[ ! -f "${package_path}" ]]; then
    echo "Building ${pkg} ..."

    # Native makepkg BUILDDIR handling creates build/<pkgbase>/src and
    # build/<pkgbase>/pkg while keeping startdir at pspbuild/<package>.
    rm -rf "${workdir}"
    mkdir -p "${source_cache}"

    cleanup_local_buildfile
    CURRENT_LOCAL_BUILD_FILE=$(mktemp "${recipe_dir}/.PSPBUILD.local.XXXXXX")

    makepkg_args=()
    set +e
    configure_local_git_sources \
      "${pspbuild}" "${CURRENT_LOCAL_BUILD_FILE}" "${source_cache}"
    source_status=$?
    set -e

    if (( source_status == 1 )); then
      # No Git-backed sources: use the tracked PSPBUILD directly.
      rm -f "${CURRENT_LOCAL_BUILD_FILE}"
      CURRENT_LOCAL_BUILD_FILE=""
    elif (( source_status != 0 )); then
      cleanup_local_buildfile
      exit "${source_status}"
    else
      makepkg_args=(-p "$(basename "${CURRENT_LOCAL_BUILD_FILE}")")
    fi

    if (cd "${recipe_dir}" && \
      psp-makepkg "${makepkg_args[@]}" \
        "BUILDDIR=${BUILD_ROOT}" \
        "SRCDEST=${source_cache}" \
        "PKGDEST=${PACKAGES}"); then
      :
    else
      status=$?
      cleanup_local_buildfile
      echo "ERROR: Build failed for ${pkg}. Build tree preserved at:"
      echo "  ${workdir}"
      exit "${status}"
    fi

    cleanup_local_buildfile

    if [[ ! -f "${package_path}" ]]; then
      echo "ERROR: Expected package was not produced:"
      echo "  ${package_path}"
      exit 1
    fi
  fi

  if [[ -n "${doinstall}" ]]; then
    echo "Installing ${pkg}"
    psp-pacman -U --noconfirm "${package_path}" --overwrite '*'
  fi
done
