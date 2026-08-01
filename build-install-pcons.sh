#!/usr/bin/env bash

set -Eeuo pipefail

readonly REPO_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
readonly BUILD_DIR="$REPO_DIR/build/pcons"

show_usage() {
    cat <<'EOF'
Usage: ./build-install-pcons.sh [PCONS_VAR=value ...]

Build and install MXVK with pcons. The default build uses GCC and C++20,
builds the core libraries, mxmod2obj, and all standard examples, and enables
AUTO-detected mixer, Eigen, MXWrite/FFmpeg, and CUDA support.

Examples:
  ./build-install-pcons.sh
  ./build-install-pcons.sh EXAMPLES=0
  ./build-install-pcons.sh WITH_CUDA=OFF WITH_MXWRITE=OFF
  ./build-install-pcons.sh CV=1 JPEG=1

Environment:
  CC, CXX                 compilers (defaults: gcc and g++)
  VARIANT                 release or debug (default: release)
  MXVK_INSTALL_PREFIX     install prefix (default: /usr/local)
  MXVK_DESTDIR            optional package root prepended to the prefix
  MXVK_JOBS               optional positive parallel job count
  MXVK_PCONS              pcons command (default: uvx --from pcons>=0.24 pcons)
  UV_CACHE_DIR            uv cache (default: build/pcons/.uv-cache)
  UV_TOOL_DIR             uv tool directory (default: build/pcons/.uv-tools)
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
    show_usage
    exit 0
fi

compiler_c="${CC:-gcc}"
compiler_cxx="${CXX:-g++}"
variant="${VARIANT:-release}"
install_prefix="${MXVK_INSTALL_PREFIX:-/usr/local}"
destination_root="${MXVK_DESTDIR:-}"
jobs="${MXVK_JOBS:-}"

if [[ "$install_prefix" != /* ]]; then
    echo "MXVK_INSTALL_PREFIX must be absolute: $install_prefix" >&2
    exit 2
fi
if [[ -n "$destination_root" && "$destination_root" != /* ]]; then
    echo "MXVK_DESTDIR must be absolute: $destination_root" >&2
    exit 2
fi
if [[ -n "$jobs" && ! "$jobs" =~ ^[1-9][0-9]*$ ]]; then
    echo "MXVK_JOBS must be a positive integer: $jobs" >&2
    exit 2
fi
for command_name in "$compiler_c" "$compiler_cxx" uvx install cp mktemp; do
    command -v "$command_name" >/dev/null 2>&1 || {
        echo "Required command not found: $command_name" >&2
        exit 3
    }
done

mkdir -p "$BUILD_DIR"
stage_dir="$(mktemp -d "$BUILD_DIR/install-stage.XXXXXX")"
cleanup_stage() {
    if [[ "$stage_dir" == "$BUILD_DIR"/install-stage.* ]]; then
        rm -rf -- "$stage_dir"
    fi
}
trap cleanup_stage EXIT

pcons_command=(uvx --from 'pcons>=0.24' pcons)
if [[ -n "${MXVK_PCONS:-}" ]]; then
    read -r -a pcons_command <<<"$MXVK_PCONS"
fi
pcons_options=(-B "$BUILD_DIR" --reconfigure)
if [[ -n "$jobs" ]]; then
    pcons_options+=(-j "$jobs")
fi

echo "Building and staging MXVK..."
(
    cd "$REPO_DIR"
    CC="$compiler_c" CXX="$compiler_cxx" \
        UV_CACHE_DIR="${UV_CACHE_DIR:-$BUILD_DIR/.uv-cache}" \
        UV_TOOL_DIR="${UV_TOOL_DIR:-$BUILD_DIR/.uv-tools}" \
        "${pcons_command[@]}" "${pcons_options[@]}" \
        "VARIANT=$variant" \
        "PCONS_INSTALL_PREFIX=$stage_dir" \
        "PCONS_FINAL_PREFIX=$install_prefix" \
        "$@" all install
)

required_files=(
    "$stage_dir/include/mxvk/mxvk.hpp"
    "$stage_dir/include/mxvk/mxvk_version.hpp"
    "$stage_dir/include/mxnetwork/socket.hpp"
    "$stage_dir/lib/libmxvk.a"
    "$stage_dir/lib/libvolk.a"
    "$stage_dir/lib/libmxnetwork.a"
    "$stage_dir/lib/pkgconfig/mxvk.pc"
    "$stage_dir/bin/mxmod2obj"
)
for required_file in "${required_files[@]}"; do
    [[ -f "$required_file" ]] || {
        echo "Expected staged file was not produced: $required_file" >&2
        exit 4
    }
done

install_prefix="${install_prefix%/}"
readonly INSTALL_ROOT="${destination_root}${install_prefix}"
install_parent="$INSTALL_ROOT"
while [[ ! -e "$install_parent" && "$install_parent" != / ]]; do
    install_parent="$(dirname -- "$install_parent")"
done

privilege_command=()
needs_privilege=0
if ((EUID != 0)); then
    if [[ -z "$destination_root" && ("$install_prefix" == /usr || "$install_prefix" == /usr/local) ]]; then
        needs_privilege=1
    elif [[ ! -w "$install_parent" ]]; then
        needs_privilege=1
    fi
fi
if ((needs_privilege)); then
    command -v sudo >/dev/null 2>&1 || {
        echo "Installing to $INSTALL_ROOT requires root privileges, but sudo was not found." >&2
        exit 5
    }
    privilege_command=(sudo)
fi

echo "Installing into $INSTALL_ROOT..."
"${privilege_command[@]}" install -d "$INSTALL_ROOT"
"${privilege_command[@]}" cp -a "$stage_dir/." "$INSTALL_ROOT/"
if [[ -z "$destination_root" && ("$install_prefix" == /usr || "$install_prefix" == /usr/local) ]] && command -v ldconfig >/dev/null 2>&1; then
    "${privilege_command[@]}" ldconfig
fi

echo
echo "MXVK installation complete."
echo "  Headers:    $INSTALL_ROOT/include/mxvk"
echo "  Libraries:  $INSTALL_ROOT/lib"
echo "  Tool:       $INSTALL_ROOT/bin/mxmod2obj"
echo "  Examples:   $INSTALL_ROOT/libexec/mxvk"
