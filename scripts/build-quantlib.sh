#!/usr/bin/env bash
set -euo pipefail

# Build QuantLib with LLVM's libc++ for compatibility with fin-kit
#
# WHY THIS IS NEEDED:
# System/Homebrew QuantLib packages are typically built with the system's default libc++.
# fin-kit uses LLVM Clang with LLVM's libc++ for C++20 modules support.
# These are ABI-incompatible, causing crashes at runtime.
#
# This script builds QuantLib from source using LLVM's toolchain so everything
# links against the same libc++ implementation.
#
# USAGE:
#   ./scripts/build-quantlib.sh                    # Use auto-detected defaults
#   QUANTLIB_VERSION=1.41 ./scripts/build-quantlib.sh  # Build specific version
#   LLVM_PREFIX=/custom/llvm ./scripts/build-quantlib.sh  # Use custom LLVM path

# Detect platform
OS="$(uname -s)"
case "${OS}" in
    Darwin)
        DEFAULT_LLVM_PREFIX="/opt/homebrew/opt/llvm"
        LLVM_LIBCXX_DIR="lib/c++"
        LIB_EXT="dylib"
        get_cpu_count() { sysctl -n hw.ncpu; }
        verify_linkage() {
            otool -L "$1" | grep -q "llvm.*libc++" && \
                echo "SUCCESS: QuantLib linked against LLVM libc++" || \
                echo "WARNING: QuantLib may not be linked against LLVM libc++"
        }
        ;;
    Linux)
        DEFAULT_LLVM_PREFIX="/home/linuxbrew/.linuxbrew/opt/llvm"
        LLVM_LIBCXX_DIR="lib"
        LIB_EXT="so"
        get_cpu_count() { nproc; }
        verify_linkage() {
            ldd "$1" | grep -q "libc++" && \
                echo "SUCCESS: QuantLib linked against libc++" || \
                echo "WARNING: QuantLib may not be linked against libc++"
        }
        ;;
    *)
        echo "Error: Unsupported platform: ${OS}"
        exit 1
        ;;
esac

QUANTLIB_VERSION="${QUANTLIB_VERSION:-1.40}"
INSTALL_PREFIX="${INSTALL_PREFIX:-$HOME/.local/quantlib-llvm}"
LLVM_PREFIX="${LLVM_PREFIX:-${DEFAULT_LLVM_PREFIX}}"
JOBS="${JOBS:-$(get_cpu_count)}"

echo "=== Building QuantLib ${QUANTLIB_VERSION} with LLVM libc++ ==="
echo "Install prefix: ${INSTALL_PREFIX}"
echo "LLVM prefix: ${LLVM_PREFIX}"
echo "Parallel jobs: ${JOBS}"

# Check LLVM is installed
if [[ ! -x "${LLVM_PREFIX}/bin/clang++" ]]; then
    echo "Error: LLVM not found at ${LLVM_PREFIX}"
    echo "Install with: brew install llvm"
    exit 1
fi

# Create temp directory
WORK_DIR=$(mktemp -d)
trap 'rm -rf "${WORK_DIR}"' EXIT

cd "${WORK_DIR}"

# Download QuantLib
echo "=== Downloading QuantLib ${QUANTLIB_VERSION} ==="
curl -LO "https://github.com/lballabio/QuantLib/releases/download/v${QUANTLIB_VERSION}/QuantLib-${QUANTLIB_VERSION}.tar.gz"
tar xzf "QuantLib-${QUANTLIB_VERSION}.tar.gz"
cd "QuantLib-${QUANTLIB_VERSION}"

# Configure with LLVM
echo "=== Configuring ==="
rm -rf build && mkdir build && cd build

LLVM_LIBCXX_PATH="${LLVM_PREFIX}/${LLVM_LIBCXX_DIR}"

CC="${LLVM_PREFIX}/bin/clang" \
CXX="${LLVM_PREFIX}/bin/clang++" \
cmake .. -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DCMAKE_CXX_STANDARD=20 \
    -DCMAKE_CXX_FLAGS="-stdlib=libc++" \
    -DCMAKE_EXE_LINKER_FLAGS="-L${LLVM_LIBCXX_PATH} -Wl,-rpath,${LLVM_LIBCXX_PATH}" \
    -DCMAKE_SHARED_LINKER_FLAGS="-L${LLVM_LIBCXX_PATH} -Wl,-rpath,${LLVM_LIBCXX_PATH}" \
    -DQL_BUILD_EXAMPLES=OFF \
    -DQL_BUILD_TEST_SUITE=OFF \
    -DQL_BUILD_BENCHMARK=OFF

# Build
echo "=== Building (${JOBS} jobs) ==="
ninja -j"${JOBS}"

# Install
echo "=== Installing to ${INSTALL_PREFIX} ==="
ninja install

# Verify
echo "=== Verifying ==="
verify_linkage "${INSTALL_PREFIX}/lib/libQuantLib.${LIB_EXT}"

echo ""
echo "=== Done ==="
echo "QuantLib ${QUANTLIB_VERSION} installed to ${INSTALL_PREFIX}"
echo ""
echo "To use in fin-kit, ensure CMAKE_PREFIX_PATH includes:"
echo "  ${INSTALL_PREFIX}"
