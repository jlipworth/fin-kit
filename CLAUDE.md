# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with this repository.

## Build Commands

```bash
# Prerequisites (macOS)
brew install llvm quantlib boost ninja

# Set up Python environment (first time)
uv sync

# Install Conan dependencies (use LLVM Clang for C++20 modules)
CC=/opt/homebrew/opt/llvm/bin/clang \
CXX=/opt/homebrew/opt/llvm/bin/clang++ \
LDFLAGS="-L/opt/homebrew/opt/llvm/lib/c++ -Wl,-rpath,/opt/homebrew/opt/llvm/lib/c++" \
uv run conan install . --build=missing -of=build \
  -s compiler=clang -s compiler.version=21 -s compiler.cppstd=20 -s compiler.libcxx=libc++

# Configure
CC=/opt/homebrew/opt/llvm/bin/clang \
CXX=/opt/homebrew/opt/llvm/bin/clang++ \
cmake --preset conan-release

# Build
cmake --build build/build/Release

# Run all tests
ctest --test-dir build/build/Release --output-on-failure

# Run specific test
ctest --test-dir build/build/Release -R <test_name> --output-on-failure
```

## Architecture

fin-kit is a **C++20-first** financial toolkit. Use C++20 features throughout:
- **C++20 modules** (not headers) for all project code
- Concepts, ranges, coroutines, std::format where appropriate
- Only fall back to older patterns when external dependencies require it

### Modules

- **finkit.core**: Foundation library with time series containers, datetime handling, and numerical utilities
- **finkit.data**: Market data ingestion, normalization, and storage layer (DuckDB/Parquet)
- **finkit.curves**: Rate curve bootstrapping (SOFR OIS), CIP/CCY basis calculations
- **finkit.analysis**: Technical indicators, statistical functions, bond basis calculations (QuantLib)
- **finkit.backtest**: Event-driven backtesting engine with order management and execution simulation
- **finkit.viz**: Terminal-based charts and reporting

### Dependency Graph

```
finkit.core → finkit.data → finkit.curves ─┬→ finkit.analysis → finkit.backtest → finkit.viz
                           └───────────────┘
```

### Module Files

Module interface units are `.cppm` files in each module's directory:
- `src/core/core.cppm`
- `src/data/data.cppm`
- `src/curves/curves.cppm`
- `src/analysis/analysis.cppm`
- etc.

## Code Style

- Follow `.clang-format` (Google style base with project modifications)
- Use `.clang-tidy` checks - fix all warnings before committing
- Naming: `snake_case` for functions/variables, `PascalCase` for types, `SCREAMING_CASE` for constants
- Use `export module` and `import` (C++20 modules), not `#include` for project code
- Non-module headers (std library, external deps) go in the global module fragment
- All public APIs require documentation comments

## Testing

- Tests live in `tests/` mirroring `src/` structure
- Use GoogleTest for unit tests (via FetchContent)
- Name test files `*_test.cpp`
- Import modules with `import finkit.module_name;`
- Run tests before committing: `ctest --test-dir build --output-on-failure`

## Dependencies

### Homebrew (macOS)
- **llvm** - LLVM Clang 21+ (required for C++20 modules)
- **quantlib** - Bond pricing (Conan's version has consteval issues with Clang 21)
- **boost** - Headers required by QuantLib
- **ninja** - Build system (required for C++20 module scanning)

### Conan
- DuckDB (data storage)
- spdlog (logging)
- fmt (formatting, pulled in by spdlog)
- nlohmann_json (JSON)

### Python (via uv)
- conan
- pre-commit
