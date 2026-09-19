#!/usr/bin/env bash
# Prepare the psp-packages working tree after clone/pull.
#
# This script intentionally follows the parent repository's recorded gitlinks.
# It never uses `git submodule update --remote`, so package source components
# cannot float to newer upstream revisions unexpectedly.

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd -P)"
JOBS="${GITPREP_JOBS:-8}"

if (( EUID == 0 )); then
    echo "ERROR: Do not run gitprep.sh as root." >&2
    exit 1
fi

cd "${ROOT}"

if ! git rev-parse --is-inside-work-tree >/dev/null 2>&1; then
    echo "ERROR: ${ROOT} is not a Git working tree." >&2
    exit 1
fi

TOPLEVEL="$(git rev-parse --show-toplevel)"
if [[ "${TOPLEVEL}" != "${ROOT}" ]]; then
    echo "ERROR: gitprep.sh must live at the repository root." >&2
    echo "  repository: ${TOPLEVEL}" >&2
    echo "  script:     ${ROOT}" >&2
    exit 1
fi

if [[ ! -f .gitmodules ]]; then
    echo "ERROR: .gitmodules is missing." >&2
    exit 1
fi

# Do not overwrite staged component gitlink changes. This protects an
# in-progress package/component update that has already been staged.
if ! git diff --cached --quiet --ignore-submodules=none -- components; then
    echo "ERROR: staged component changes are present." >&2
    echo "Commit or unstage them before running gitprep.sh." >&2
    exit 1
fi

# Do not overwrite actual file changes inside already-initialized submodules.
# A clean submodule whose HEAD merely differs from the parent gitlink is okay:
# updating that HEAD is one of this script's jobs after a pull.
if ! git submodule foreach --quiet --recursive \
    'git diff --quiet && git diff --cached --quiet'
then
    echo "ERROR: a component contains uncommitted file changes." >&2
    echo "Commit, stash, or discard those changes before running gitprep.sh." >&2
    exit 1
fi

echo "Synchronizing submodule URLs..."
git submodule sync --recursive

echo "Initializing/updating shallow submodules..."
git -c remote.origin.tagOpt=--no-tags submodule update \
    --init \
    --recursive \
    --depth 1 \
    --jobs "${JOBS}"

# Keep subsequent fetches inside initialized components commit-focused too.
# Package source selection is controlled by gitlinks, not remote tag names.
git submodule foreach --quiet --recursive \
    'git config remote.origin.tagOpt --no-tags'

echo "Verifying submodule state..."
bad=0
while IFS= read -r line; do
    [[ -z "${line}" ]] && continue

    state="${line:0:1}"
    case "${state}" in
        " ")
            ;;
        "-")
            echo "ERROR: uninitialized submodule: ${line}" >&2
            bad=1
            ;;
        "+")
            echo "ERROR: submodule is not at the recorded gitlink: ${line}" >&2
            bad=1
            ;;
        "U")
            echo "ERROR: submodule has unresolved conflicts: ${line}" >&2
            bad=1
            ;;
        *)
            echo "ERROR: unexpected submodule state: ${line}" >&2
            bad=1
            ;;
    esac
done < <(git submodule status --recursive)

if (( bad != 0 )); then
    exit 1
fi

# Catch the specific failure mode where `git -C components/foo ...` would
# silently walk up and operate on the parent repository because the submodule
# was never initialized.
while read -r _ path; do
    [[ -z "${path}" ]] && continue

    component_root="$(git -C "${path}" rev-parse --show-toplevel 2>/dev/null || true)"
    expected_root="${ROOT}/${path}"

    if [[ "${component_root}" != "${expected_root}" ]]; then
        echo "ERROR: ${path} is not an initialized submodule working tree." >&2
        echo "  resolved Git root: ${component_root:-<none>}" >&2
        exit 1
    fi
done < <(git config -f .gitmodules --get-regexp '^submodule\..*\.path$' || true)

# New clones are shallow. Existing full clones are deliberately not destroyed
# here; doing so automatically could discard useful local history.
nonshallow=0
while IFS= read -r line; do
    [[ -z "${line}" ]] && continue
    entry="${line:1}"
    path="${entry#* }"
    path="${path%% *}"

    if [[ "$(git -C "${path}" rev-parse --is-shallow-repository 2>/dev/null || echo false)" != "true" ]]; then
        echo "WARNING: ${path} is not shallow." >&2
        nonshallow=$((nonshallow + 1))
    fi
done < <(git submodule status --recursive)

if (( nonshallow > 0 )); then
    echo "Prepared repository successfully; ${nonshallow} existing submodule(s) retain full history."
else
    echo "Prepared repository successfully; all submodules are initialized and shallow."
fi
