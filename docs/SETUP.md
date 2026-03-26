# Development Environment Setup

fin-kit requires **LLVM Clang** with **libc++** for C++20 modules support.
This guide covers the core build toolchain. For Redis/Docker/TimescaleDB
workflows, see [LOCAL_SERVICES.md](LOCAL_SERVICES.md).

> **Critical:** Do not use Homebrew/system QuantLib for fin-kit.
> Build QuantLib from source with `./scripts/build-quantlib.sh` so it lands in
> `~/.local/quantlib-llvm/` and matches the LLVM libc++ toolchain.

## 0. What is required?

### Core build prerequisites

- LLVM Clang 21+ with libc++
- CMake 3.28+
- Ninja
- uv
- Boost headers

### QuantLib

- Must be built from source with `./scripts/build-quantlib.sh`
- Install prefix: `~/.local/quantlib-llvm/`
- The build will fail or be unstable if you use a Homebrew/system QuantLib build

### Local services for streaming/dashboard/data workflows

- Redis via Docker Compose
- Docker Desktop with WSL integration enabled if you are on WSL2
- TimescaleDB credentials for data-backed workflows

See [LOCAL_SERVICES.md](LOCAL_SERVICES.md) for the exact commands and checks.

## 1. Prerequisites

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

## 2. Setup Steps

### 2.1 Python Environment and Pre-commit Hooks

```bash
cd fin-kit
uv sync
uv run pre-commit install
```

This installs Python dependencies and sets up git hooks for linting/formatting.
The repo invokes Conan through `uv run conan`; you do not need to manage a
separate Python environment for it.

### 2.2 Build QuantLib from Source

**Why?** System/Homebrew QuantLib packages link against the system's default
libc++, which is ABI-incompatible with LLVM's libc++. Mixing them causes runtime
crashes.

If you already have a Homebrew QuantLib installed, keep it out of the way and
use the source build below instead.

```bash
./scripts/build-quantlib.sh
```

This builds QuantLib 1.40 with LLVM's toolchain and installs to `~/.local/quantlib-llvm/`.

**Environment variables:**
- `QUANTLIB_VERSION` - QuantLib version (default: 1.40)
- `INSTALL_PREFIX` - Installation directory (default: `~/.local/quantlib-llvm`)
- `LLVM_PREFIX` - LLVM installation path (auto-detected)
- `JOBS` - Parallel build jobs (auto-detected)

### 2.3 Install Conan Dependencies

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

This builds libpqxx, spdlog, and fmt from source (no pre-built binaries exist for this toolchain).

### 2.4 Configure CMake

**macOS:**
```bash
CC=/opt/homebrew/opt/llvm/bin/clang \
CXX=/opt/homebrew/opt/llvm/bin/clang++ \
cmake -S . -B build/build/Release -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=build/build/Release/generators/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release
```

**Linux:**
```bash
CC=/home/linuxbrew/.linuxbrew/opt/llvm/bin/clang \
CXX=/home/linuxbrew/.linuxbrew/opt/llvm/bin/clang++ \
cmake -S . -B build/build/Release -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE=build/build/Release/generators/conan_toolchain.cmake \
  -DCMAKE_BUILD_TYPE=Release
```

`uv run conan install ... -of=build` generates the Conan toolchain under
`build/build/Release/generators/`. These explicit `cmake -S/-B` commands work
in a fresh clone and do not depend on any local, ignored preset files.

### 2.5 Build

```bash
cmake --build build/build/Release
```

### 2.6 Run Tests

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
| libpqxx | Conan (built) | PostgreSQL/TimescaleDB client |
| spdlog | Conan (built) | Logging (fmt is transitive) |
| nlohmann_json | Conan (header-only) | JSON parsing |
| tomlplusplus | Conan (header-only) | TOML parsing |
| eigen | Conan (header-only) | Linear algebra |

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
uv run conan install . --build=libpqxx -of=build ...
```

To clear the cache:
```bash
rm -rf ~/.conan2/p/
```

### TimescaleDB connection

fin-kit connects to TimescaleDB for data storage. Set credentials via environment
variables:

```bash
# Via Infisical (recommended)
infisical run --env=dev --path="/kubernetes/infrastructure/timescaledb" -- \
  cmake --build build/build/Release

# Or set manually
export TSDB_HOST=localhost
export TSDB_PORT=5432
export TSDB_DATABASE=finkit
export TSDB_USER=finkit
export TSDB_PASSWORD=secret
```

Alternatively, configure in `~/.config/finkit/config.toml`:
```toml
[database]
host = "localhost"
port = 5432
database = "finkit"
user = "finkit"
password = "secret"
```

For Redis, Docker Desktop, and WSL-specific local service setup, see
[LOCAL_SERVICES.md](LOCAL_SERVICES.md).
