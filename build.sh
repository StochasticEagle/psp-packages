#!/bin/bash
# Build one package or all packages. Recipes remain under pspbuild/;
# makepkg build trees live under build/ and final package archives are written
# flat into packages/.

set -e

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
source "${ROOT}/install-permissions.sh"
RECIPES="${ROOT}/pspbuild"
BUILD_ROOT="${ROOT}/build"
PACKAGES="${ROOT}/packages"
SOURCE_COMPONENTS="${ROOT}/source-components.tsv"
CURRENT_LOCAL_BUILD_FILE=""
PACKAGE_INPUT_STAMP=".psp-package-input.sha256"

cleanup_local_buildfile() {
  if [[ -n "${CURRENT_LOCAL_BUILD_FILE}" ]]; then
    rm -f "${CURRENT_LOCAL_BUILD_FILE}"
    CURRENT_LOCAL_BUILD_FILE=""
  fi
}
trap cleanup_local_buildfile EXIT

mkdir -p "${BUILD_ROOT}" "${PACKAGES}"

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

archive_package_name() {
  local archive="$1"
  local base stem rest

  base="$(basename "${archive}")"
  stem="${base%%.pkg.tar.*}"
  rest="${stem%-*}"
  rest="${rest%-*}"
  rest="${rest%-*}"
  printf '%s\n' "${rest}"
}

prune_package_archives() {
  local pkgname="$1"
  local keep="${2:-}"
  local archive archive_pkgname
  local -a archives=()

  shopt -s nullglob
  archives=("${PACKAGES}"/*.pkg.tar.*)
  shopt -u nullglob

  for archive in "${archives[@]}"; do
    [[ -n "${keep}" && "${archive}" == "${keep}" ]] && continue
    archive_pkgname="$(archive_package_name "${archive}")"
    if [[ "${archive_pkgname}" == "${pkgname}" ]]; then
      echo "Removing stale package archive: $(basename "${archive}")"
      rm -f "${archive}"
    fi
  done
}
doinstall=""
doclean=""
requested_package=""

while (( $# > 0 )); do
  case "$1" in
    --install)
      doinstall="true"
      ;;
    --clean)
      doclean="true"
      ;;
    -*)
      echo "ERROR: Unknown option: $1"
      exit 1
      ;;
    *)
      if [[ -n "${requested_package}" ]]; then
        echo "ERROR: Only one package may be specified."
        exit 1
      fi
      requested_package="$1"
      ;;
  esac
  shift
done

if [[ -n "${doclean}" && -n "${doinstall}" ]]; then
  echo "ERROR: --clean and --install cannot be used together."
  exit 1
fi

if [[ -z "${requested_package}" ]]; then
  PKG_LIST=$(find "${RECIPES}" -mindepth 2 -maxdepth 2 -type f -name PSPBUILD \
    -exec sh -c 'basename "$(dirname "$1")"' _ {} \; | LC_ALL=C sort)
  if [[ -z "${doclean}" ]]; then
      printf 'Will build packages:'
    while IFS= read -r pkg; do
      [[ -n "${pkg}" ]] && printf ' %s' "${pkg}"
    done <<< "${PKG_LIST}"
    printf '\n'
  fi
else
  PKG_LIST="${requested_package}"
fi

if [[ -n "${doclean}" ]]; then
  for pkg in ${PKG_LIST}; do
    pspbuild="${RECIPES}/${pkg}/PSPBUILD"

    if [[ ! -f "${pspbuild}" ]]; then
      echo "Package ${pkg} does not exist!"
      continue
    fi

    pkgbase=$(bash -c 'source "$1"; printf "%s\n" "${pkgbase:-${pkgname[0]}}"' _ "${pspbuild}")
    pkgname=$("${ROOT}/parse_pspbuild.sh" "${pspbuild}" pkgname)

    echo "Cleaning ${pkg} ..."
    rm -rf "${BUILD_ROOT}/${pkgbase}"
    prune_package_archives "${pkgname}"
  done
  exit 0
fi

# Fail before source acquisition if a recipe, source map, submodule URL, or
# gitlink has drifted out of sync. Recursive dependency builds inherit the
# validation result.
if [[ -z "${PSP_PACKAGES_INVARIANTS_VALIDATED:-}" ]]; then
  python3 "${ROOT}/scripts/check-source-components.py"
  export PSP_PACKAGES_INVARIANTS_VALIDATED=1
fi

# A fresh clone must be buildable directly. Package source submodules follow
# the gitlinks selected by psp-packages; do not float them with --remote.
# Cleaning is intentionally local and does not initialize or fetch submodules.
if [[ -z "${PSP_PACKAGES_SUBMODULES_READY:-}" ]]; then
  git -C "${ROOT}" -c remote.origin.tagOpt=--no-tags submodule update --init --recursive --depth 1 --quiet
  export PSP_PACKAGES_SUBMODULES_READY=1
fi

create_local_source_snapshot() {
  local component="$1"
  local source_name="$2"
  local source_cache="$3"
  local source_index="$4"
  local component_sha="$5"
  local archive actual_sha dirty

  mkdir -p "${source_cache}"
  archive="${source_cache}/psp-source-${source_index}-${component_sha}.tar"

  actual_sha=$(git -C "${component}" rev-parse HEAD)
  if [[ "${actual_sha}" != "${component_sha}" ]]; then
    echo "ERROR: Source component HEAD does not match selected gitlink: ${component}" >&2
    return 2
  fi

  dirty=$(git -C "${component}" status --porcelain --untracked-files=all)
  if [[ -n "${dirty}" ]]; then
    echo "ERROR: Source component contains local changes: ${component}" >&2
    git -C "${component}" status --short >&2
    return 2
  fi

  if [[ ! -f "${archive}" ]]; then
    rm -f "${source_cache}/psp-source-${source_index}-"*.tar
    (
      cd "${component}"
      tar --exclude='./.git' --exclude='*/.git' \
        --transform="s|^\\./|${source_name}/|" \
        -cf "${archive}" .
    )
  fi

  printf '%s\n' "${archive}"
}

configure_local_git_sources() {
  local pspbuild="$1"
  local local_pspbuild="$2"
  local source_cache="$3"
  local record source_index src entry remote source_name component mapped component_sha archive
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
      mapped=$(awk -F '\t' -v remote="${remote}" '
        function normalize(url) {
          sub(/\/$/, "", url)
          sub(/\.git$/, "", url)
          return url
        }
        normalize($1) == normalize(remote) { print $2; exit }
      ' "${SOURCE_COMPONENTS}")
    fi

    if [[ -z "${mapped}" ]]; then
      echo "ERROR: Git source has no source-component mapping:"
      echo "  ${remote}"
      return 2
    fi

    component="${ROOT}/${mapped}"

    if [[ ! -e "${component}" ]]; then
      echo "ERROR: Git source submodule is not initialized:"
      echo "  ${component}"
      return 2
    fi

    component_sha=$(git -C "${ROOT}" ls-files --stage -- "${mapped}" | awk '$1 == "160000" { print $2; exit }')
    if [[ -z "${component_sha}" ]]; then
      echo "ERROR: Source component is not a gitlink: ${mapped}" >&2
      return 2
    fi

    archive=$(create_local_source_snapshot \
      "${component}" "${source_name}" "${source_cache}" "${source_index}" "${component_sha}") || return $?

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
  pkgname=$("${ROOT}/parse_pspbuild.sh" "${pspbuild}" pkgname)
  package_path="${PACKAGES}/${pkgfile}"
  workdir="${BUILD_ROOT}/${pkgbase}"
  source_cache="${workdir}/sources"
  input_stamp="${workdir}/${PACKAGE_INPUT_STAMP}"

  prune_package_archives "${pkgname}" "${package_path}"

  input_fingerprint=$(python3 "${ROOT}/scripts/package-input-fingerprint.py" \
    "${pkg}" "${pspbuild}" "${recipe_dir}" "${PACKAGES}")
  stored_fingerprint=""
  [[ -f "${input_stamp}" ]] && stored_fingerprint=$(<"${input_stamp}")

  if [[ "${stored_fingerprint}" != "${input_fingerprint}" ]]; then
    if [[ -f "${package_path}" || -d "${workdir}" ]]; then
      echo "Package inputs changed for ${pkg}; invalidating cached build state."
    fi
    rm -f "${package_path}"
    rm -rf "${workdir}"
  fi

  if [[ ! -f "${package_path}" ]]; then
    echo "Building ${pkg} ..."

    mkdir -p "${source_cache}"
    printf '%s\n' "${input_fingerprint}" > "${input_stamp}"

    cleanup_local_buildfile
    CURRENT_LOCAL_BUILD_FILE=$(mktemp "${recipe_dir}/.PSPBUILD.local.XXXXXX")

    makepkg_args=()
    if [[ -d "${workdir}/src" ]] &&
       find "${workdir}/src" -mindepth 1 -print -quit | grep -q .; then
      echo "Reusing existing source/build tree for ${pkg}."
      echo "Run ./build.sh --clean ${pkg} for a fresh build."
      makepkg_args+=(--noextract)
    fi

    set +e
    configure_local_git_sources \
      "${pspbuild}" "${CURRENT_LOCAL_BUILD_FILE}" "${source_cache}"
    source_status=$?
    set -e

    if (( source_status == 1 )); then
      rm -f "${CURRENT_LOCAL_BUILD_FILE}"
      CURRENT_LOCAL_BUILD_FILE=""
    elif (( source_status != 0 )); then
      cleanup_local_buildfile
      exit "${source_status}"
    else
      makepkg_args+=(-p "$(basename "${CURRENT_LOCAL_BUILD_FILE}")")
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
    pspdev_run_install psp-pacman -U --noconfirm "${package_path}" --overwrite '*'
  fi
done
