#!/usr/bin/env bash
set -euo pipefail

# Build QuantLib with LLVM's libc++ for compatibility with fin-kit
#
# WHY THIS IS NEEDED:
# Homebrew's QuantLib is built with Apple's system libc++ (/usr/lib/libc++.1.dylib).
# fin-kit uses LLVM Clang with LLVM's libc++ (/opt/homebrew/opt/llvm/lib/c++/libc++.1.dylib)
# for C++20 modules support. These are ABI-incompatible, causing crashes at runtime.
#
# This script builds QuantLib from source using LLVM's toolchain so everything
# links against the same libc++ implementation.

QUANTLIB_VERSION="${QUANTLIB_VERSION:-1.40}"
INSTALL_PREFIX="${INSTALL_PREFIX:-$HOME/.local/quantlib-llvm}"
LLVM_PREFIX="${LLVM_PREFIX:-/opt/homebrew/opt/llvm}"
JOBS="${JOBS:-$(sysctl -n hw.ncpu)}"

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
mkdir build && cd build

CC="${LLVM_PREFIX}/bin/clang" \
CXX="${LLVM_PREFIX}/bin/clang++" \
cmake .. -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DCMAKE_CXX_STANDARD=20 \
    -DCMAKE_CXX_FLAGS="-stdlib=libc++" \
    -DCMAKE_EXE_LINKER_FLAGS="-L${LLVM_PREFIX}/lib/c++ -Wl,-rpath,${LLVM_PREFIX}/lib/c++" \
    -DCMAKE_SHARED_LINKER_FLAGS="-L${LLVM_PREFIX}/lib/c++ -Wl,-rpath,${LLVM_PREFIX}/lib/c++" \
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
otool -L "${INSTALL_PREFIX}/lib/libQuantLib.dylib" | grep -q "llvm.*libc++" && \
    echo "SUCCESS: QuantLib linked against LLVM libc++" || \
    echo "WARNING: QuantLib may not be linked against LLVM libc++"

echo ""
echo "=== Done ==="
echo "QuantLib ${QUANTLIB_VERSION} installed to ${INSTALL_PREFIX}"
echo ""
echo "To use in fin-kit, ensure CMAKE_PREFIX_PATH includes:"
echo "  ${INSTALL_PREFIX}"
