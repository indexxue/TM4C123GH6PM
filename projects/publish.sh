#!/usr/bin/env bash
# WSL wrapper: publish local release/<ver> artifacts to GitHub (tag + Release).
#
# Usage:
#   ./publish.sh 0.1.0
#   ./publish.sh 0.1.0 --build-first
#   ./publish.sh 0.1.0 --draft
#   ./publish.sh 0.1.0 --dry-run
#   ./publish.sh 0.1.0 --allow-dirty
#   ./publish.sh 0.1.0 --skip-push
#
# Prefer committing the ship commit first. Needs: gh auth login (Windows gh.exe ok).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=_common.sh
source "${SCRIPT_DIR}/_common.sh"

usage() {
    cat <<'EOF'
Usage:
  ./publish.sh <version> [options]

Options:
  --build-first   Run ./build.sh all release <ver> before publish
  --draft         Create a draft GitHub Release
  --dry-run       Print actions only
  --allow-dirty   Allow tagging with a dirty worktree
  --skip-push     Create local tag only (no git push / still needs remote tag for --verify-tag)
  --skip-tag      Do not create tag (tag must already exist on remote)
  --notes TEXT    Release notes (default: GitHub --generate-notes)
  -h, --help

Example:
  ./build.sh all release 0.1.0
  ./publish.sh 0.1.0

  ./publish.sh 0.1.0 --build-first
EOF
}

find_powershell() {
    if command -v powershell.exe >/dev/null 2>&1; then
        command -v powershell.exe
        return 0
    fi
    local c="/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"
    [[ -x "$c" ]] && { echo "$c"; return 0; }
    return 1
}

if [[ $# -lt 1 ]]; then
    usage >&2
    exit 1
fi

case "$1" in
    -h|--help|help) usage; exit 0 ;;
esac

version_raw="$1"
shift
version="$(normalize_release_version "${version_raw}")"

build_first=0
draft=0
dry_run=0
allow_dirty=0
skip_push=0
skip_tag=0
notes=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --build-first) build_first=1; shift ;;
        --draft) draft=1; shift ;;
        --dry-run) dry_run=1; shift ;;
        --allow-dirty) allow_dirty=1; shift ;;
        --skip-push) skip_push=1; shift ;;
        --skip-tag) skip_tag=1; shift ;;
        --notes)
            shift
            [[ $# -ge 1 ]] || die "--notes needs text"
            notes="$1"
            shift
            ;;
        -h|--help) usage; exit 0 ;;
        *) die "unknown option: $1" ;;
    esac
done

if [[ "${build_first}" -eq 1 ]]; then
    log "build all products release ${version}"
    "${SCRIPT_DIR}/build.sh" all release "${version}"
fi

ps="$(find_powershell)" || die "powershell.exe required"
win_root="$(wslpath -w "${REPO_ROOT}")"
ps_file="${win_root}\\scripts\\publish-release.ps1"

args=(
    -NoProfile
    -ExecutionPolicy Bypass
    -File "${ps_file}"
    -Version "${version}"
)
[[ "${draft}" -eq 1 ]] && args+=(-Draft)
[[ "${dry_run}" -eq 1 ]] && args+=(-DryRun)
[[ "${allow_dirty}" -eq 1 ]] && args+=(-AllowDirty)
[[ "${skip_push}" -eq 1 ]] && args+=(-SkipPush)
[[ "${skip_tag}" -eq 1 ]] && args+=(-SkipTag)
[[ -n "${notes}" ]] && args+=(-Notes "${notes}")

log "powershell publish-release.ps1 -Version ${version}"
"${ps}" "${args[@]}"
