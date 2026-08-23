#!/usr/bin/env bash
# Attach SEGGER J-Link into WSL via Windows usbipd.
#
# One-time (Windows Admin PowerShell) — device must be Shared first:
#   usbipd list
#   usbipd bind --busid <BUSID> --force
#
# Then either:
#   source this from ~/.bashrc   → attach on every interactive WSL login
#   or run: ./scripts/wsl-jlink-attach.sh
#   or just ./flash.sh …         → flash also auto-attaches
#
# Env:
#   USBIPD_HARDWARE_ID=1366:0105   # override VID:PID
#   USBIPD_BUSID=2-4               # override BUSID (port-sensitive)

_sourced=0
[[ "${BASH_SOURCE[0]}" != "${0}" ]] && _sourced=1
# Do not enable errexit in the caller's interactive shell when sourced.
if [[ "${_sourced}" -eq 0 ]]; then
    set -euo pipefail
fi

find_usbipd() {
    if [[ -n "${USBIPD_EXE:-}" && -x "${USBIPD_EXE}" ]]; then
        printf '%s\n' "${USBIPD_EXE}"
        return 0
    fi
    if command -v usbipd.exe >/dev/null 2>&1; then
        command -v usbipd.exe
        return 0
    fi
    local c
    for c in \
        "/mnt/c/Program Files/usbipd-win/usbipd.exe" \
        "/mnt/c/Program Files (x86)/usbipd-win/usbipd.exe"
    do
        [[ -x "${c}" ]] && { printf '%s\n' "${c}"; return 0; }
    done
    return 1
}

probe_in_wsl() {
    if command -v lsusb >/dev/null 2>&1; then
        lsusb 2>/dev/null | grep -Ei 'SEGGER|J-Link|1366:' >/dev/null
        return $?
    fi
    local d vid
    for d in /sys/bus/usb/devices/*/idVendor; do
        [[ -f "${d}" ]] || continue
        vid="$(tr '[:upper:]' '[:lower:]' <"${d}" | tr -d '[:space:]')"
        [[ "${vid}" == "1366" ]] && return 0
    done
    return 1
}

_qlog() {
    [[ "${USBIPD_ATTACH_QUIET:-0}" == "1" ]] && return 0
    echo "==> $*"
}

main() {
    local exe hwid rc=1
    local -a hwids=()

    if [[ "${_sourced}" -eq 0 ]]; then
        set -euo pipefail
    fi

    if probe_in_wsl; then
        _qlog "J-Link already visible in WSL"
        return 0
    fi

    exe="$(find_usbipd)" || {
        echo "error: usbipd.exe not found (install usbipd-win on Windows)" >&2
        return 1
    }

    if [[ -n "${USBIPD_BUSID:-}" ]]; then
        _qlog "usbipd attach --wsl --busid ${USBIPD_BUSID}"
        "${exe}" attach --wsl --busid "${USBIPD_BUSID}" || rc=$?
    else
        if [[ -n "${USBIPD_HARDWARE_ID:-}" ]]; then
            hwids=("${USBIPD_HARDWARE_ID}")
        else
            hwids=("1366:0105" "1366:0101" "1366:1020" "1366:0102" "1366:0103")
        fi
        for hwid in "${hwids[@]}"; do
            if "${exe}" attach --wsl --hardware-id "${hwid}" >/dev/null 2>&1; then
                _qlog "attached ${hwid}"
                rc=0
                break
            fi
        done
    fi

    sleep 0.4
    if probe_in_wsl; then
        _qlog "OK — J-Link in WSL (lsusb)"
        return 0
    fi

    echo "error: attach failed — device must be Shared first (Windows Admin once):" >&2
    echo "  usbipd list" >&2
    echo "  usbipd bind --busid <BUSID> --force   # J-Link often 1366:0105" >&2
    echo "  Then re-run: ${BASH_SOURCE[0]}" >&2
    return 1
}

if [[ "${_sourced}" -eq 1 ]]; then
    # Never enable errexit in the interactive login shell.
    set +e
    USBIPD_ATTACH_QUIET="${USBIPD_ATTACH_QUIET:-1}"
    main || true
else
    main
fi
