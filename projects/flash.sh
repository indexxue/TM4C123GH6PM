#!/usr/bin/env bash
# WSL / Linux flash entry for TM4C123GH6PM (J-Link via Windows host).
#
# Calls scripts/flash-jlink.ps1 through powershell.exe. J-Link must be installed
# on Windows (path in flash-jlink.ps1).
#
# Usage:
#   ./flash.sh                              # car-4wd standalone
#   ./flash.sh <product>                    # product default image
#   ./flash.sh <product> <slot>             # slot = standalone|bootloader|app|factory
#   ./flash.sh <product> <slot> --erase-all
#   ./flash.sh <product> <slot> --erase-apps
#   ./flash.sh help
#
# Products: factory | car-4wd | car-2wd
# Env:
#   JLINK_SPEED   SWD speed kHz (default 400)
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=_common.sh
source "${SCRIPT_DIR}/_common.sh"
PRODUCTS=(factory car-4wd car-2wd)

usage() {
    cat <<'EOF'
Usage:
  ./flash.sh                              Flash car-4wd standalone
  ./flash.sh <product>                    Default slot for product
  ./flash.sh <product> <slot>             Flash named slot
  ./flash.sh <product> <slot> --erase-all
  ./flash.sh <product> <slot> --erase-apps
  ./flash.sh help

Products: factory | car-4wd | car-2wd
  Tab-complete / path ok: ./flash.sh factory/  |  ./flash.sh ./car-4wd

Slots:
  standalone   whole-image @ 0x0     (car default)
  bootloader   Boot @ 0x0
  app          APP_A @ 0x4000
  factory      APP_B @ 0x21000       (factory product default; always projects/factory/build/)

Examples:
  ./flash.sh
  ./flash.sh car-4wd
  ./flash.sh factory
  ./flash.sh car-4wd app
  ./flash.sh car-4wd bootloader --erase-all
  ./flash.sh car-4wd app --erase-apps
  JLINK_SPEED=1000 ./flash.sh car-2wd

Requires: WSL2 on Windows + powershell.exe + J-Link (Windows).
Build first: ./build.sh <product>
EOF
}

find_powershell() {
    if have_cmd powershell.exe; then
        command -v powershell.exe
        return 0
    fi
    local candidate
    for candidate in \
        "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe"
    do
        if [[ -x "${candidate}" ]]; then
            echo "${candidate}"
            return 0
        fi
    done
    return 1
}

repo_win_path() {
    if have_cmd wslpath; then
        wslpath -w "${REPO_ROOT}"
    else
        echo "${REPO_ROOT}"
    fi
}

default_slot_for_product() {
    case "$1" in
        factory) echo "factory" ;;
        *)       echo "standalone" ;;
    esac
}

# --- main ---
if [[ $# -ge 1 ]]; then
    case "$1" in
        help|-h|--help)
            usage
            exit 0
            ;;
    esac
fi

product="car-4wd"
slot=""
erase_all=0
erase_apps=0
speed="${JLINK_SPEED:-400}"

if [[ $# -ge 1 ]]; then
    product="$(normalize_product_arg "$1" || true)"
    shift
    [[ -n "${product}" ]] || die "empty product (expected: ${PRODUCTS[*]})"
    is_product "${product}" || die "unknown product '${product}' (expected: ${PRODUCTS[*]})"
fi

if [[ $# -ge 1 ]]; then
    case "$1" in
        standalone|bootloader|app|factory)
            slot="$1"
            shift
            ;;
        --erase-all|--erase-apps)
            ;;
        *)
            die "unknown slot '$1' (standalone|bootloader|app|factory)"
            ;;
    esac
fi

while [[ $# -ge 1 ]]; do
    case "$1" in
        --erase-all)  erase_all=1; shift ;;
        --erase-apps) erase_apps=1; shift ;;
        *) die "unexpected arg: $1" ;;
    esac
done

if [[ -z "${slot}" ]]; then
    slot="$(default_slot_for_product "${product}")"
fi

# factory product / factory slot → always projects/factory
if [[ "${product}" == "factory" || "${slot}" == "factory" ]]; then
    product="factory"
    slot="factory"
fi

if [[ "${erase_all}" -eq 1 && "${erase_apps}" -eq 1 ]]; then
    die "use either --erase-all or --erase-apps, not both"
fi

case "${speed}" in
    ''|*[!0-9]*) die "bad JLINK_SPEED='${speed}' (need integer kHz)" ;;
esac

ps="$(find_powershell)" || die "powershell.exe not found — run from WSL2 on Windows, or use .\\flash-jlink.cmd"
win_root="$(repo_win_path)"
ps_file="${win_root}\\scripts\\flash-jlink.ps1"

args=(
    -NoProfile
    -ExecutionPolicy Bypass
    -File "${ps_file}"
    -CarProject "${product}"
    -Target "${slot}"
    -Speed "${speed}"
)
if [[ "${erase_all}" -eq 1 ]]; then
    args+=(-EraseAll)
fi
if [[ "${erase_apps}" -eq 1 ]]; then
    args+=(-EraseApps)
fi

extra=""
[[ "${erase_all}" -eq 1 ]] && extra+=" erase-all"
[[ "${erase_apps}" -eq 1 ]] && extra+=" erase-apps"
log "flash ${product} slot=${slot} speed=${speed} kHz${extra}"
"${ps}" "${args[@]}"
