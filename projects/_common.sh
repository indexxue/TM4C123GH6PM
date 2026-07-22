#!/usr/bin/env bash
# Shared helpers for projects/build.sh (source, do not exec).
# Sets REPO_ROOT / APT_MIRROR defaults; git semver helpers for FW_VERSION.

_COMMON_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "${_COMMON_DIR}/.." && pwd)"
PRODUCTS=(car-4wd car-2wd)

# Apt mirror: tuna | aliyun | ustc | <full ubuntu base URL>
APT_MIRROR="${APT_MIRROR:-tuna}"

die() {
    echo "error: $*" >&2
    exit 1
}

log() {
    echo "==> $*"
}

have_cmd() {
    command -v "$1" >/dev/null 2>&1
}

is_product() {
    local p
    for p in "${PRODUCTS[@]}"; do
        [[ "$1" == "$p" ]] && return 0
    done
    return 1
}

# Accept tab-complete / path forms: car-4wd/ | ./car-4wd | projects/car-4wd/
normalize_product_arg() {
    local p="${1:-}"
    while [[ "${p}" == */ ]]; do
        p="${p%/}"
    done
    [[ -n "${p}" ]] || return 1
    basename "${p}"
}

resolve_apt_mirror_uri() {
    case "${APT_MIRROR}" in
        tuna)   echo "https://mirrors.tuna.tsinghua.edu.cn/ubuntu" ;;
        aliyun) echo "https://mirrors.aliyun.com/ubuntu" ;;
        ustc)   echo "https://mirrors.ustc.edu.cn/ubuntu" ;;
        http://*|https://*) echo "${APT_MIRROR%/}" ;;
        *) die "unknown APT_MIRROR='${APT_MIRROR}' (use tuna|aliyun|ustc|https://...)" ;;
    esac
}

apt_already_on_cn_mirror() {
    local uri="$1"
    if [[ -f /etc/apt/sources.list.d/ubuntu.sources ]]; then
        grep -qF "${uri}" /etc/apt/sources.list.d/ubuntu.sources 2>/dev/null && return 0
    fi
    if [[ -f /etc/apt/sources.list ]]; then
        grep -qF "${uri}" /etc/apt/sources.list 2>/dev/null && return 0
    fi
    return 1
}

# Switch Ubuntu apt to a CN mirror (idempotent). Needs sudo once.
ensure_apt_cn_mirror() {
    local uri
    uri="$(resolve_apt_mirror_uri)"
    if apt_already_on_cn_mirror "${uri}"; then
        return 0
    fi

    have_cmd sudo || die "sudo not found; cannot switch apt mirror to ${uri}"
    log "switching apt mirror -> ${uri}"

    if [[ -f /etc/apt/sources.list.d/ubuntu.sources ]]; then
        sudo mkdir -p /var/backups/apt-sources
        sudo cp -a /etc/apt/sources.list.d/ubuntu.sources \
            "/var/backups/apt-sources/ubuntu.sources.bak.$(date +%Y%m%d%H%M%S)"
        sudo sed -i \
            -e "s|http://archive.ubuntu.com/ubuntu|${uri}|g" \
            -e "s|https://archive.ubuntu.com/ubuntu|${uri}|g" \
            -e "s|http://security.ubuntu.com/ubuntu|${uri}|g" \
            -e "s|https://security.ubuntu.com/ubuntu|${uri}|g" \
            /etc/apt/sources.list.d/ubuntu.sources
    fi

    if [[ -f /etc/apt/sources.list ]]; then
        sudo mkdir -p /var/backups/apt-sources
        sudo cp -a /etc/apt/sources.list \
            "/var/backups/apt-sources/sources.list.bak.$(date +%Y%m%d%H%M%S)"
        sudo sed -i \
            -e "s|http://archive.ubuntu.com/ubuntu|${uri}|g" \
            -e "s|https://archive.ubuntu.com/ubuntu|${uri}|g" \
            -e "s|http://security.ubuntu.com/ubuntu|${uri}|g" \
            -e "s|https://security.ubuntu.com/ubuntu|${uri}|g" \
            -e "s|http://cn.archive.ubuntu.com/ubuntu|${uri}|g" \
            /etc/apt/sources.list
    fi

    apt_already_on_cn_mirror "${uri}" \
        || log "warning: apt sources may still point elsewhere; check /etc/apt/"
}

apt_install() {
    local pkgs=("$@")
    have_cmd sudo || die "sudo not found; install manually: ${pkgs[*]}"
    ensure_apt_cn_mirro
    log "installing via apt: ${pkgs[*]}"
    sudo apt-get update || return 1
    sudo DEBIAN_FRONTEND=noninteractive apt-get install -y "${pkgs[@]}" || return 1
}

# --- git semver ---
# Tag forms: v1.2.3 | V1.2.3 | 1.2.3. Normalized output is always X.Y.Z.

parse_semver_tag() {
    local raw="${1:-}"
    raw="${raw#"${raw%%[![:space:]]*}"}"
    raw="${raw%"${raw##*[![:space:]]}"}"
    [[ "${raw}" =~ ^[vV]?([0-9]+)\.([0-9]+)\.([0-9]+)$ ]] || return 1
    echo "${BASH_REMATCH[1]}.${BASH_REMATCH[2]}.${BASH_REMATCH[3]}"
}

semver_lt() {
    local a="$1" b="$2"
    local a1 a2 a3 b1 b2 b3
    IFS=. read -r a1 a2 a3 <<< "${a}"
    IFS=. read -r b1 b2 b3 <<< "${b}"
    ((10#${a1} < 10#${b1})) && return 0
    ((10#${a1} > 10#${b1})) && return 1
    ((10#${a2} < 10#${b2})) && return 0
    ((10#${a2} > 10#${b2})) && return 1
    ((10#${a3} < 10#${b3})) && return 0
    return 1
}

git_max_semver_tag() {
    local line ve
    have_cmd git || return 0
    while IFS= read -r line; do
        [[ -z "${line}" ]] && continue
        if ver="$(parse_semver_tag "${line}")"; then
            echo "${ver}"
            return 0
        fi
    done < <(git -C "${REPO_ROOT}" tag -l --sort=-version:refname 2>/dev/null || true)
}

git_semver_for_build() {
    local raw ve
    have_cmd git || return 0
    raw="$(git -C "${REPO_ROOT}" describe --exact-match --tags HEAD 2>/dev/null || true)"
    if [[ -n "${raw}" ]]; then
        if ver="$(parse_semver_tag "${raw}")"; then
            echo "${ver}"
            return 0
        fi
    fi
    raw="$(git -C "${REPO_ROOT}" describe --tags --abbrev=0 2>/dev/null || true)"
    if [[ -n "${raw}" ]]; then
        if ver="$(parse_semver_tag "${raw}")"; then
            echo "${ver}"
            return 0
        fi
    fi
}

normalize_release_version() {
    local ve
    if ! ver="$(parse_semver_tag "${1:-}")"; then
        die "invalid version '${1:-}' — use three-part semver, e.g. 1.2.3 or V1.2.3"
    fi
    echo "${ver}"
}

clamp_release_version() {
    local requested="$1"
    local floo
    floor="$(git_max_semver_tag || true)"
    if [[ -n "${floor}" ]] && semver_lt "${requested}" "${floor}"; then
        echo "==> release ${requested} below git tag v${floor} — using ${floor}" >&2
        echo "${floor}"
        return 0
    fi
    echo "${requested}"
}

resolve_default_release_version() {
    local ve
    if [[ -n "${FW_VERSION:-}" ]]; then
        normalize_release_version "${FW_VERSION}"
        return 0
    fi
    ver="$(git_semver_for_build || true)"
    if [[ -n "${ver}" ]]; then
        echo "${ver}"
        return 0
    fi
    ver="$(git_max_semver_tag || true)"
    if [[ -n "${ver}" ]]; then
        echo "${ver}"
        return 0
    fi
    die "release needs a version: no git semver tag found — pass e.g. release 1.0.0 or set FW_VERSION"
}
