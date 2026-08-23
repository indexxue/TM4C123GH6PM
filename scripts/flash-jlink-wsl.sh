#!/usr/bin/env bash
# WSL-native J-Link flash (device attached via usbipd; uses Linux JLinkExe).
#
# Called from projects/flash.sh. Do not use Windows JLink.exe while the probe
# is attached to WSL — they cannot share the same USB device.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"
PROJECTS_DIR="${REPO_ROOT}/projects"

CarProject="car-4wd"
Target="standalone"
Speed=400
EraseAll=0
EraseApps=0
Recover=0
Image=""

usage() {
    cat <<'EOF'
Usage: flash-jlink-wsl.sh [options]

Options:
  -CarProject <factory|car-4wd|car-2wd|rc-controller>
  -Target <standalone|bootloader|app|factory|full>  (rc-controller: standalone only)
  -Speed <kHz>
  -Image <path>
  -EraseAll
  -EraseApps
  -Recover
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -CarProject) CarProject="${2:?}"; shift 2 ;;
        -Target)     Target="${2:?}"; shift 2 ;;
        -Speed)      Speed="${2:?}"; shift 2 ;;
        -Image)      Image="${2:?}"; shift 2 ;;
        -EraseAll)   EraseAll=1; shift ;;
        -EraseApps)  EraseApps=1; shift ;;
        -Recover)    Recover=1; shift ;;
        -h|--help)   usage; exit 0 ;;
        *) echo "error: unknown arg: $1" >&2; exit 1 ;;
    esac
done

if [[ "${EraseAll}" -eq 1 && "${EraseApps}" -eq 1 ]]; then
    echo "error: use -EraseAll or -EraseApps, not both" >&2
    exit 1
fi

if [[ "${Recover}" -eq 1 ]]; then
    EraseAll=1
    if [[ "${Speed}" -gt 100 ]]; then
        Speed=50
    fi
fi

if [[ "${Target}" == "full" && "${CarProject}" == "factory" ]]; then
    echo "error: Target 'full' is for car products only" >&2
    exit 1
fi

if [[ "${CarProject}" == "rc-controller" && "${Target}" != "standalone" ]]; then
    echo "error: rc-controller only supports Target 'standalone'" >&2
    exit 1
fi

BuildDir="${PROJECTS_DIR}/${CarProject}/build"

resolve_image() {
    local flash_target="$1"
    local base preset

    if [[ "${flash_target}" == "full" ]]; then
        [[ -f "${BuildDir}/${CarProject}-full.hex" ]] && { echo "${BuildDir}/${CarProject}-full.hex"; return 0; }
        [[ -f "${BuildDir}/${CarProject}-full.bin" ]] && { echo "${BuildDir}/${CarProject}-full.bin"; return 0; }
        return 1
    fi

    case "${flash_target}" in
        standalone) preset="${CarProject}" ;;
        bootloader) preset="bootloader" ;;
        app)        preset="app" ;;
        factory)    preset="factory" ;;
        *) echo "error: unknown target ${flash_target}" >&2; exit 1 ;;
    esac

    [[ -f "${BuildDir}/${preset}.elf" ]] && { echo "${BuildDir}/${preset}.elf"; return 0; }
    [[ -f "${BuildDir}/${preset}.bin" ]] && { echo "${BuildDir}/${preset}.bin"; return 0; }
    return 1
}

auto_build() {
    local build_target
    build_target="${Target}"
    [[ "${build_target}" == "full" ]] && build_target="all"
    echo "==> image missing for ${Target}; building ${CarProject} (${build_target})..."
    LOG_ENABLE="${LOG_ENABLE:-1}" IMAGE_TARGET="${build_target}" \
        "${PROJECTS_DIR}/build.sh" "${CarProject}"
}

if [[ -z "${Image}" ]]; then
    Image="$(resolve_image "${Target}")" || true
    if [[ -z "${Image}" ]]; then
        auto_build
        Image="$(resolve_image "${Target}")" || {
            echo "error: no image for ${Target} in ${BuildDir}" >&2
            exit 1
        }
    fi
fi

[[ -f "${Image}" ]] || { echo "error: image not found: ${Image}" >&2; exit 1; }

find_jlink_exe() {
    if [[ -n "${JLINK_EXE:-}" && -x "${JLINK_EXE}" ]]; then
        printf '%s\n' "${JLINK_EXE}"
        return 0
    fi
    local c
    for c in \
        /opt/SEGGER/JLink/JLinkExe \
        /usr/local/bin/JLinkExe \
        /usr/bin/JLinkExe
    do
        [[ -x "${c}" ]] && { printf '%s\n' "${c}"; return 0; }
    done
    return 1
}

# shellcheck source=wsl-jlink-attach.sh
source "${SCRIPT_DIR}/wsl-jlink-attach.sh"
main || {
    echo "" >&2
    echo "WSL J-Link attach failed. One-time (Windows Admin):" >&2
    echo "  usbipd list" >&2
    echo "  usbipd bind --busid <BUSID> --force" >&2
    exit 1
}

JLinkExe="$(find_jlink_exe)" || {
    cat >&2 <<'EOF'
error: Linux JLinkExe not found in WSL.

Install SEGGER J-Link Software and Documentation Pack for Linux, e.g.:
  https://www.segger.com/downloads/jlink/
  sudo dpkg -i JLink_Linux_V*_x86_64.deb

Default path: /opt/SEGGER/JLink/JLinkExe
Or set: export JLINK_EXE=/path/to/JLinkExe
EOF
    exit 1
}

offsets_standalone="0x0"
offsets_bootloader="0x0"
offsets_app="0x4000"
offsets_factory="0x21000"
offsets_full="0x0"

use_loadfile=0
case "${Image}" in
    *.elf|*.hex) use_loadfile=1 ;;
esac

if [[ "${use_loadfile}" -eq 1 ]]; then
    load_cmd="loadfile \"${Image}\""
else
    off_var="offsets_${Target}"
    off="${!off_var:-0x0}"
    load_cmd="loadbin \"${Image}\", ${off}"
fi

erase_cmd=""
if [[ "${EraseAll}" -eq 1 ]]; then
    erase_cmd="erase"
elif [[ "${EraseApps}" -eq 1 ]]; then
    erase_cmd="erase 0x4000 0x40000"
fi

tmp_dir="${REPO_ROOT}/tmp"
mkdir -p "${tmp_dir}"
cmd_file="${tmp_dir}/flash.jlink"
log_file="${tmp_dir}/jlink-flash.log"

{
    echo "si SWD"
    echo "speed ${Speed}"
    echo "device TM4C123GH6PM"
    echo "rsettype 2"
    echo "r0"
    echo "sleep 200"
    echo "connect"
    echo "halt"
    [[ -n "${erase_cmd}" ]] && echo "${erase_cmd}"
    echo "${load_cmd}"
    echo "r1"
    echo "r"
    echo "g"
    echo "exit"
} >"${cmd_file}"

echo "Flashing ${Image} via J-Link (WSL)..."
echo "  Car:    ${CarProject}"
echo "  Target: ${Target}"
echo "  Image:  ${Image}"
echo "  SWD:    ${Speed} kHz"
echo "  Probe:  ${JLinkExe}"
echo ""
echo "===== BEFORE FLASH: hold board RESET now ====="
echo "  Keep holding until you see Erasing/Downloading (not only Connecting)."
echo "  Unplug HC-SR04 Echo (PC1/SWDIO) if plugged."
echo "=============================================="
echo ""
sleep 2

set +e
"${JLinkExe}" -AutoConnect 1 -CommanderScript "${cmd_file}" | tee "${log_file}"
rc=${PIPESTATUS[0]}
set -e

if grep -qE 'Error occurred:|Could not connect|Failed to power up DAP|Failed to halt CPU' "${log_file}"; then
    echo ""
    echo "J-Link connect/flash failed. See ${log_file}"
    echo "Try: ./flash.sh ${CarProject} ${Target}  (with board RESET held)"
    exit 1
fi

if [[ "${rc}" -ne 0 ]]; then
    echo "J-Link failed (exit ${rc}). See ${log_file}"
    exit "${rc}"
fi

echo "Done. See ${log_file} for details."
