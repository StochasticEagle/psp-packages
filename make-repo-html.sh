#!/bin/bash

cd "$(dirname "$0")"
mkdir -p repo
set -a
set -e

PKGBUILD_NAME="PSPBUILD"
GITHUB_REPOSITORY_OWNER="${GITHUB_REPOSITORY_OWNER:-pspdev}"
GITHUB_REPOSITORY="${GITHUB_REPOSITORY:-pspdev/psp-packages}"
PACKAGES="$(find pspbuild -mindepth 2 -maxdepth 2 -name "${PKGBUILD_NAME}" | sort)"
PACKAGE_COUNT="$(echo ${PACKAGES} | wc -w)"
INDEX_TABLE_CONTENT=""

function createSourceLink() {
    src="${1}"
    recipe_dir="${2}"
    if [[ ${src} == git+* ]]; then
        BASE_URL="$(echo ${src} | cut -d'+' -f2- | cut -d'#' -f1)"
        if [[ ${BASE_URL} == *.git ]]; then
            BASE_URL="$(echo "${BASE_URL}" | rev | cut -d'.' -f2- | rev)"
        fi
        if [[ ${src} == *github.com/* ]] || [[ ${src} == *gilab* ]]; then
            if [[ ${src} == *#commit=* ]]; then
                COMMIT="$(echo ${src} | cut -d'=' -f2)"
                URL="${BASE_URL}/tree/${COMMIT}"
                echo "<a href=\"${URL}\">${BASE_URL}</a>"
            elif [[ ${src} == *#branch=* ]]; then
                BRANCH="$(echo ${src} | cut -d'=' -f2)"
                URL="${BASE_URL}/tree/${BRANCH}"
                echo "<a href=\"${URL}\">${BASE_URL}</a>"
            elif [[ ${src} == *#tag=* ]]; then
                TAG="$(echo ${src} | cut -d'=' -f2)"
                URL="${BASE_URL}/tree/${TAG}"
                echo "<a href=\"${URL}\">${BASE_URL}</a>"
            else
                echo "<a href=\"${BASE_URL}\">${BASE_URL}</a>"
            fi
        elif [[ ${src} == *git.code.sf.net/* ]]; then
            BASE_URL="$(echo "${BASE_URL}" | sed 's/git.code.sf/sourceforge/')"
            if [[ ${src} == *#commit=* ]]; then
                COMMIT="$(echo ${src} | cut -d'=' -f2)"
                URL="${BASE_URL}/ci/${COMMIT}/tree"
                echo "<a href=\"${URL}\">${BASE_URL}</a>"
            elif [[ ${src} == *#branch=* ]]; then
                BRANCH="$(echo ${src} | cut -d'=' -f2)"
                URL="${BASE_URL}/ci/${BRANCH}/tree"
                echo "<a href=\"${URL}\">${BASE_URL}</a>"
            elif [[ ${src} == *#tag=* ]]; then
                TAG="$(echo ${src} | cut -d'=' -f2)"
                URL="${BASE_URL}/ci/${TAG}/tree"
                echo "<a href=\"${URL}\">${BASE_URL}</a>"
            else
                echo "<a href=\"${BASE_URL}\">${BASE_URL}</a>"
            fi
        else
            echo "${src}"
        fi
    elif [[ ${src} == http?://* ]] || [[ ${src} == ftp?://* ]]; then
        echo "<a href=\"${src}\">${src}</a>"
    else
        echo "<a href=\"https://github.com/${GITHUB_REPOSITORY}/blob/master/${recipe_dir}/${src}\">${src}</a>"
    fi
}

for PKGBUILD in ${PACKAGES}; do
    unset groups license depends

    source "${PKGBUILD}"
    UPDATED=$(git log -1 --format=%cd --date=short -- "${PKGBUILD}")
    DOWNLOAD_URL="${pkgname}-${pkgver}-${pkgrel}-${arch}.pkg.tar.gz"
    RECIPE_DIR="$(dirname "${PKGBUILD}")"

    ARCH="${arch[*]}"
    LICENSE="${license[*]}"
    GROUP_LIST="${groups[*]}"

    FILENAME="repo/${DOWNLOAD_URL}"
    if [[ -f "${FILENAME}" ]]; then
        PKGSIZE="$(ls -l "${FILENAME}" | cut -d' ' -f5 | numfmt --to iec --suffix=B --format "%.1f")"
        INSTSIZE="$(gunzip -c "${FILENAME}" | grep -a '^size = ' | cut -d' ' -f3 | numfmt --to iec --suffix=B --format "%.1f")"
    fi

    if [[ ! -n "${depends[*]}" ]]; then
        DEPS="No dependencies"
    else
        DEPS="<ul>"
        for dep in "${depends[@]}"; do
            DEPS="${DEPS}<li><a href=\"${dep}.html\">${dep}</a></li>"
        done
        DEPS="${DEPS}</ul>"
    fi

    if [[ ! -n "${source[*]}" ]]; then
        SOURCES="No sources"
    else
        SOURCES="<ul>"
        for src in "${source[@]}"; do
            SOURCES="${SOURCES}<li>$(createSourceLink "${src}" "${RECIPE_DIR}")</li>"
        done
        SOURCES="${SOURCES}</ul>"
    fi

    if [[ -f "${FILENAME}" ]]; then
        CONTENT="<ul>"
        for item in $(tar -tzf "${FILENAME}" | grep -v '\.BUILDINFO\|\.MTREE\|\.PKGINFO\|/$'); do
            CONTENT="${CONTENT}<li>${item}</li>"
        done
        CONTENT="${CONTENT}</ul>"
    else
        CONTENT="Not known"
    fi

    envsubst < package.html > "repo/${pkgname}.html"
    INDEX_TABLE_CONTENT="${INDEX_TABLE_CONTENT}<tr><td><a href=\"${pkgname}.html\">${pkgname}</a></td><td>${pkgver}-${pkgrel}</td><td>${pkgdesc}</td><td>${GROUP_LIST}</td><td>${UPDATED}</td></tr>"
done

envsubst < index.html > repo/index.html
cp style.css repo/
