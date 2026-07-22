#!/usr/bin/env bash
# WSL / Linux build entry for TM4C123GH6PM multi-product firmware.
#
# Compiles via Windows PowerShell scripts/build.ps1 (TivaWare + tools/ on the
# Windows side). This script owns CLI UX, version resolution, and detect.
#
# Usage:
#   ./build.sh <product> [build]         # debug incremental (default)
#   ./build.sh <product> rebuild         # clean + debug
#   ./build.sh <product> clean
#   ./build.sh <product> release         # LOG_ENABLE=0 + release/ package
#   ./build.sh <product> release <ver>
#   ./build.sh all [build|clean|rebuild|release [ver]]
#   ./build.sh detect
#   ./build.sh help
#
# Products: car-4wd | car-2wd
#
# Env:
#   IMAGE_TARGET   standalone|bootloader|app|factory|all  (default: standalone)
#   FW_VERSION     override version string
#   LOG_ENABLE     1 (debug default) | 0
#   FW_MCU_NAME    release filename MCU tag (default TM4C123GH6PM)
#   FW_SIGNED=1    release stem uses _sign instead of _unsigned
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# shellcheck source=_common.sh
source "${SCRIPT_DIR}/_common.sh"
PRODUCTS=(car-4wd car-2wd)

FW_MCU_NAME="${FW_MCU_NAME:-TM4C123GH6PM}"
IMAGE_TARGET="${IMAGE_TARGET:-standalone}"

usage() {
    cat <<'EOF'
Usage:
  ./build.sh <product> [build]              Debug build → elf/hex/bin
  ./build.sh <product> rebuild              Clean then debug build
  ./build.sh <product> clean                Remove projects/<product>/build/
  ./build.sh <product> release [version]    Release build + package to release/
  ./build.sh all [build|clean|rebuild|release [version]]
  ./build.sh detect                        Check powershell / gcc / SDK
  ./build.sh help

Products: car-4wd | car-2wd
  Tab-complete / path ok: ./build.sh car-4wd/  |  ./build.sh ./car-2wd

IMAGE_TARGET (env, default standalone):
  standalone | bootloader | app | factory | all

Examples:
  ./build.sh car-4wd
  ./build.sh car-2wd build
  ./build.sh car-4wd rebuild
  ./build.sh car-4wd release 0.1.0
  IMAGE_TARGET=app ./build.sh car-4wd
  FW_VERSION=1.2.3 ./build.sh car-4wd
  ./build.sh all release 0.1.0

Debug artifacts:   projects/<product>/build/<name>.{elf,hex,bin}
  (standalone name = product, e.g. car-4wd.elf)
Release artifacts: release/<ver>/{MCU}_{YYYYMMDD}_{product}_{ver}_unsigned.{elf,hex,bin}

Compile still runs scripts/build.ps1 via powershell.exe (Windows host toolchain).
EOF
}

release_sign_tag() {
    case "${FW_SIGNED:-0}" in
        1|true|TRUE|yes|YES|on|ON) echo "sign" ;;
        *) echo "unsigned" ;;
    esac
}

release_artifact_stem() {
    local product="$1"
    local version="$2"
    local date_ymd="$3"
    echo "${FW_MCU_NAME}_${date_ymd}_${product}_${version}_$(release_sign_tag)"
}

find_powershell() {
    if have_cmd powershell.exe; then
        command -v powershell.exe
        return 0
    fi
    local candidate
    for candidate in \
        "/mnt/c/Windows/System32/WindowsPowerShell/v1.0/powershell.exe" \
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
        # Fallback: already a Windows path
        echo "${REPO_ROOT}"
    fi
}

cmd_detect() {
    local ok=0
    echo "Repo: ${REPO_ROOT}"
    echo "Products: ${PRODUCTS[*]}"
    echo "IMAGE_TARGET=${IMAGE_TARGET}"
    echo "FW_MCU_NAME=${FW_MCU_NAME}"
    echo "apt mirror: ${APT_MIRROR} -> $(resolve_apt_mirror_uri)"

    local ps
    if ps="$(find_powershell)"; then
        echo "powershell: ${ps}"
    else
        echo "powershell: NOT FOUND (required — build.ps1 runs on Windows host)"
        ok=1
    fi

    if have_cmd python3; then
        echo "python3: $(command -v python3) ($(python3 --version 2>&1))"
    elif have_cmd python; then
        echo "python: $(command -v python)"
    else
        echo "python: NOT FOUND (gen_config.py / check-image-size.py need it)"
        ok=1
    fi

    local win_gcc="${REPO_ROOT}/tools/bin/arm-none-eabi-gcc.exe"
    if [[ -x "${win_gcc}" ]]; then
        echo "win gcc: ${win_gcc}"
    else
        echo "win gcc: NOT FOUND at tools/bin/ (first build.ps1 will install)"
    fi

    if have_cmd arm-none-eabi-gcc; then
        echo "wsl gcc: $(command -v arm-none-eabi-gcc)"
        arm-none-eabi-gcc --version | head -n1
    else
        echo "wsl gcc: optional (compile uses Windows tools/ via build.ps1)"
    fi

    local tw_lib="${REPO_ROOT}/sdk/TivaWare_C_Series-2.2.0.295/driverlib/gcc/libdriver.a"
    if [[ -f "${tw_lib}" ]]; then
        echo "TivaWare: ${tw_lib}"
    else
        echo "TivaWare: NOT FOUND (first build.ps1 will install to sdk/)"
    fi

    return "${ok}"
}

invoke_build_ps1() {
    local product="$1"
    local action="$2"
    local fw_version="${3:-}"
    local log_enable="${4:-1}"
    local ps ps_file win_root
    local -a args

    ps="$(find_powershell)" || die "powershell.exe not found — run from WSL2 on Windows, or use .\\build.cmd"
    win_root="$(repo_win_path)"
    ps_file="${win_root}\\scripts\\build.ps1"

    args=(
        -NoProfile
        -ExecutionPolicy Bypass
        -File "${ps_file}"
        -CarProject "${product}"
        -Target "${IMAGE_TARGET}"
        -Action "${action}"
        -LogEnable "${log_enable}"
    )
    if [[ -n "${fw_version}" ]]; then
        args+=(-FwVersion "${fw_version}")
    fi

    log "powershell build.ps1 -CarProject ${product} -Target ${IMAGE_TARGET} -Action ${action} -LogEnable ${log_enable}${fw_version:+ -FwVersion ${fw_version}}"
    # Convert WSL path args: -File already Windows. CarProject etc. are plain strings.
    "${ps}" "${args[@]}"
}

resolve_debug_version() {
    local ve
    if [[ -n "${FW_VERSION:-}" ]]; then
        echo "${FW_VERSION}"
        return 0
    fi
    ver="$(git_semver_for_build || true)"
    if [[ -n "${ver}" ]]; then
        echo "${ver}"
        return 0
    fi
    echo "0.0.0-dev"
}

artifact_stem_for_image() {
    local product="$1"
    case "${IMAGE_TARGET}" in
        standalone) echo "${product}" ;;
        bootloader) echo "bootloader" ;;
        app)        echo "app" ;;
        factory)    echo "factory" ;;
        all)        echo "${product}" ;; # release packages standalone name; all is multi
        *)          echo "${product}" ;;
    esac
}

build_debug() {
    local product="$1"
    local ve
    ver="$(resolve_debug_version)"
    log "build ${product} (debug) ver=${ver} LOG_ENABLE=${LOG_ENABLE:-1} IMAGE_TARGET=${IMAGE_TARGET}"
    invoke_build_ps1 "${product}" "build" "${ver}" "${LOG_ENABLE:-1}"

    local name out
    name="$(artifact_stem_for_image "${product}")"
    out="${REPO_ROOT}/projects/${product}/build/${name}"
    if [[ "${IMAGE_TARGET}" == "all" ]]; then
        echo "OK  product=${product} version=${ver} IMAGE_TARGET=all"
        return 0
    fi
    [[ -f "${out}.elf" ]] || die "ELF not produced: ${out}.elf"
    echo "OK  ELF: ${out}.elf"
    echo "    HEX: ${out}.hex"
    echo "    BIN: ${out}.bin"
    echo "    product=${product} version=${ver}"
}

clean_product() {
    local product="$1"
    log "clean ${product}"
    invoke_build_ps1 "${product}" "clean" "" "1"
}

build_release() {
    local product="$1"
    local version="$2"

    log "release ${product} ${version} (LOG_ENABLE=0)"
    invoke_build_ps1 "${product}" "release" "${version}" "0"

    local out_dir="${REPO_ROOT}/release/${version}"
    local name stem date_ymd
    name="$(artifact_stem_for_image "${product}")"
    date_ymd="$(date +%Y%m%d)"
    stem="$(release_artifact_stem "${product}" "${version}" "${date_ymd}")"

    # build.ps1 already packages; print confirmation if present
    if [[ -d "${out_dir}" ]]; then
        echo "OK  packaged -> ${out_dir}/"
        ls -1 "${out_dir}"/"${FW_MCU_NAME}"_*_"${product}"_"${version}"_* 2>/dev/null || \
            ls -1 "${out_dir}" 2>/dev/null || true
        echo "    product=${product} version=${version} sign=$(release_sign_tag) image=${name}"
    else
        die "release dir missing: ${out_dir}"
    fi
}

run_for_products() {
    local action="$1"
    local version="${2:-}"
    shift 2 || true
    local list=("$@")
    local p

    for p in "${list[@]}"; do
        case "${action}" in
            build)   build_debug "${p}" ;;
            rebuild) clean_product "${p}"; build_debug "${p}" ;;
            clean)   clean_product "${p}" ;;
            release) build_release "${p}" "${version}" ;;
            *)       die "internal: bad action '${action}'" ;;
        esac
    done
}

# --- main ---
if [[ $# -lt 1 ]]; then
    usage >&2
    exit 1
fi

case "$1" in
    help|-h|--help)
        usage
        exit 0
        ;;
    detect)
        cmd_detect
        exit $?
        ;;
esac

target="$(normalize_product_arg "$1" || true)"
shift
[[ -n "${target}" ]] || die "empty product (expected: ${PRODUCTS[*]} | all | detect | help)"

products=()
if [[ "${target}" == "all" ]]; then
    products=("${PRODUCTS[@]}")
elif is_product "${target}"; then
    products=("${target}")
else
    die "unknown product '${target}' (expected: ${PRODUCTS[*]} | all | detect | help)"
fi

action="build"
version=""

if [[ $# -ge 1 ]]; then
    case "$1" in
        build)
            action="build"
            shift
            [[ $# -eq 0 ]] || die "unexpected args after build: $*"
            ;;
        clean|rebuild)
            action="$1"
            shift
            [[ $# -eq 0 ]] || die "unexpected args after ${action}: $*"
            ;;
        release)
            action="release"
            shift
            if [[ $# -ge 1 ]]; then
                version="$1"
                shift
                [[ $# -eq 0 ]] || die "unexpected args after version: $*"
                [[ -n "${version}" ]] || die "empty version"
                version="$(normalize_release_version "${version}")"
            fi
            ;;
        *)
            die "unknown action '$1' (expected: build | clean | rebuild | release [ver])"
            ;;
    esac
fi

case "${IMAGE_TARGET}" in
    standalone|bootloader|app|factory|all) ;;
    *) die "bad IMAGE_TARGET='${IMAGE_TARGET}' (standalone|bootloader|app|factory|all)" ;;
esac

if [[ "${action}" == "release" ]]; then
    if [[ -z "${version}" ]]; then
        version="$(resolve_default_release_version)"
        log "release version defaulted from git/FW_VERSION: ${version}"
    fi
    tag_floor="$(git_max_semver_tag || true)"
    if [[ -n "${tag_floor}" ]]; then
        log "git tag floor: v${tag_floor}"
    fi
    version="$(clamp_release_version "${version}")"
    log "using release version ${version}"
fi

# Ensure Windows toolchain/SDK via detect-only path is optional; first build installs.
if ! find_powershell >/dev/null; then
    die "powershell.exe required for compile (WSL2 on Windows). Or use .\\build.cmd on Windows."
fi

run_for_products "${action}" "${version}" "${products[@]}"
