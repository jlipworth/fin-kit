#!/usr/bin/env bash
set -euo pipefail

# Build dependencies for fin-kit CI on Debian-based systems
# Usage: ./build-deps.sh [--quantlib-version 1.36]

QUANTLIB_VERSION="${QUANTLIB_VERSION:-1.40}"
BOOST_VERSION="${BOOST_VERSION:-1.83.0}"
INSTALL_PREFIX="${INSTALL_PREFIX:-/usr/local}"
JOBS="${JOBS:-$(nproc)}"

echo "=== Building fin-kit CI dependencies ==="
echo "QuantLib: ${QUANTLIB_VERSION}"
echo "Boost: ${BOOST_VERSION}"
echo "Install prefix: ${INSTALL_PREFIX}"
echo "Parallel jobs: ${JOBS}"

# Install system packages
apt-get update && apt-get install -y --no-install-recommends \
    build-essential \
    cmake \
    ninja-build \
    python3 \
    python3-pip \
    python3-venv \
    git \
    curl \
    wget \
    pkg-config \
    ca-certificates \
    libboost-all-dev

# Install LLVM/Clang for C++20 modules support
# GCC's module support differs from Clang - use Clang for consistency with local dev
wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc
echo "deb http://apt.llvm.org/bookworm/ llvm-toolchain-bookworm-19 main" >> /etc/apt/sources.list.d/llvm.list
apt-get update && apt-get install -y --no-install-recommends \
    clang-19 \
    libc++-19-dev \
    libc++abi-19-dev \
    lld-19

# Set up alternatives for clang
update-alternatives --install /usr/bin/clang clang /usr/bin/clang-19 100
update-alternatives --install /usr/bin/clang++ clang++ /usr/bin/clang++-19 100
update-alternatives --install /usr/bin/cc cc /usr/bin/clang-19 100
update-alternatives --install /usr/bin/c++ c++ /usr/bin/clang++-19 100

# Build QuantLib from source
# Note: Using 1.40 to match local dev (Homebrew); our code uses 1.40 Bond::Price API
cd /tmp
wget -q "https://github.com/lballabio/QuantLib/releases/download/v${QUANTLIB_VERSION}/QuantLib-${QUANTLIB_VERSION}.tar.gz"
tar xzf "QuantLib-${QUANTLIB_VERSION}.tar.gz"
cd "QuantLib-${QUANTLIB_VERSION}"

mkdir build && cd build
cmake .. \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_INSTALL_PREFIX="${INSTALL_PREFIX}" \
    -DCMAKE_CXX_STANDARD=20 \
    -DQL_BUILD_EXAMPLES=OFF \
    -DQL_BUILD_TEST_SUITE=OFF \
    -DQL_BUILD_BENCHMARK=OFF

ninja -j"${JOBS}"
ninja install

# Clean up build artifacts
cd /tmp && rm -rf "QuantLib-${QUANTLIB_VERSION}" "QuantLib-${QUANTLIB_VERSION}.tar.gz"

# Verify installation
echo "=== Verifying installation ==="
pkg-config --modversion QuantLib || echo "Warning: pkg-config not configured for QuantLib"
ls -la "${INSTALL_PREFIX}/lib/"*QuantLib* || true

echo "=== Done ==="
