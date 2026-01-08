# Development Environment Setup

fin-kit requires **LLVM Clang** with **libc++** for C++20 modules support. This guide covers setup for macOS and Linux.

## Prerequisites

### macOS

```bash
# Install Homebrew packages
brew install llvm boost ninja

# Install uv (Python package manager)
curl -LsSf https://astral.sh/uv/install.sh | sh
```

### Linux (Debian/Ubuntu with Linuxbrew)

```bash
# Install Linuxbrew if not already installed
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Add to PATH (add to ~/.bashrc or ~/.zshrc)
eval "$(/home/linuxbrew/.linuxbrew/bin/brew shellenv)"

# Install packages
brew install llvm boost ninja

# Install uv
curl -LsSf https://astral.sh/uv/install.sh | sh
```

## Setup Steps

### 1. Python Environment and Pre-commit Hooks

```bash
cd fin-kit
uv sync
uv run pre-commit install
```

This installs Python dependencies (Conan, pre-commit) and sets up git hooks for linting/formatting.

### 2. Build QuantLib from Source

**Why?** System/Homebrew QuantLib packages link against the system's default libc++, which is ABI-incompatible with LLVM's libc++. Mixing them causes runtime crashes.

```bash
./scripts/build-quantlib.sh
```

This builds QuantLib 1.40 with LLVM's toolchain and installs to `~/.local/quantlib-llvm/`.

**Environment variables:**
- `QUANTLIB_VERSION` - QuantLib version (default: 1.40)
- `INSTALL_PREFIX` - Installation directory (default: `~/.local/quantlib-llvm`)
- `LLVM_PREFIX` - LLVM installation path (auto-detected)
- `JOBS` - Parallel build jobs (auto-detected)

### 3. Install Conan Dependencies

Create a default Conan profile (first time only):
```bash
uv run conan profile detect
```

Install dependencies with LLVM Clang:

**macOS:**
```bash
CC=/opt/homebrew/opt/llvm/bin/clang \
CXX=/opt/homebrew/opt/llvm/bin/clang++ \
LDFLAGS="-L/opt/homebrew/opt/llvm/lib/c++ -Wl,-rpath,/opt/homebrew/opt/llvm/lib/c++" \
uv run conan install . --build=missing -of=build \
  -s compiler=clang -s compiler.version=21 -s compiler.cppstd=20 -s compiler.libcxx=libc++
```

**Linux:**
```bash
LLVM_PREFIX=/home/linuxbrew/.linuxbrew/opt/llvm \
CC="${LLVM_PREFIX}/bin/clang" \
CXX="${LLVM_PREFIX}/bin/clang++" \
LDFLAGS="-L${LLVM_PREFIX}/lib -Wl,-rpath,${LLVM_PREFIX}/lib" \
uv run conan install . --build=missing -of=build \
  -s compiler=clang -s compiler.version=21 -s compiler.cppstd=20 -s compiler.libcxx=libc++
```

This builds duckdb, spdlog, and fmt from source (no pre-built binaries exist for this toolchain).

### 4. Configure CMake

**macOS:**
```bash
CC=/opt/homebrew/opt/llvm/bin/clang \
CXX=/opt/homebrew/opt/llvm/bin/clang++ \
cmake --preset conan-release
```

**Linux:**
```bash
CC=/home/linuxbrew/.linuxbrew/opt/llvm/bin/clang \
CXX=/home/linuxbrew/.linuxbrew/opt/llvm/bin/clang++ \
cmake --preset conan-release
```

### 5. Build

```bash
cmake --build build/build/Release
```

### 6. Run Tests

```bash
ctest --test-dir build/build/Release --output-on-failure
```

## Dependencies

| Dependency | Source | Notes |
|------------|--------|-------|
| Python 3.13+ | System/uv | uv manages Python automatically |
| uv | curl install | Python package manager |
| LLVM Clang 21+ | Homebrew/Linuxbrew | Required for C++20 modules |
| Boost | Homebrew/Linuxbrew | Headers required by QuantLib |
| Ninja | Homebrew/Linuxbrew | Required for C++20 module scanning |
| QuantLib 1.40 | Built from source | Must use LLVM libc++ |
| DuckDB | Conan (built) | Data storage |
| spdlog | Conan (built) | Logging |
| fmt | Conan (built) | Formatting (via spdlog) |
| nlohmann_json | Conan (header-only) | JSON parsing |
| tomlplusplus | Conan (header-only) | TOML parsing |

## Troubleshooting

### Linker errors with libc++

If you see errors like `cannot find -lc++`, ensure:
1. LLVM is installed: `brew install llvm`
2. You're using the correct library path in LDFLAGS
3. On Linux, the path is `/home/linuxbrew/.linuxbrew/opt/llvm/lib` (not `lib/c++`)

### ABI/runtime crashes

If the build succeeds but crashes at runtime, you likely have mixed libc++ versions. Verify QuantLib linkage:

**macOS:**
```bash
otool -L ~/.local/quantlib-llvm/lib/libQuantLib.dylib | grep libc++
```

**Linux:**
```bash
ldd ~/.local/quantlib-llvm/lib/libQuantLib.so | grep libc++
```

Both should show LLVM's libc++, not the system one.

### Conan cache issues

To rebuild a specific package:
```bash
uv run conan install . --build=duckdb -of=build ...
```

To clear the cache:
```bash
rm -rf ~/.conan2/p/
```
