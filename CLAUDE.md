# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with this repository.

## Build Commands

```bash
# Set up Python environment (first time)
uv sync

# Activate venv and install dependencies
source .venv/bin/activate
conan install . --build=missing -of=build

# Configure (choose one)
cmake --preset conan-release    # Release build
cmake --preset conan-debug      # Debug build

# Build
cmake --build build --config Release

# Run all tests
ctest --test-dir build --output-on-failure

# Run specific test
ctest --test-dir build -R <test_name> --output-on-failure
```

## Architecture

fin-kit is a **C++20-first** financial toolkit. Use C++20 features throughout:
- **C++20 modules** (not headers) for all project code
- Concepts, ranges, coroutines, std::format where appropriate
- Only fall back to older patterns when external dependencies require it

### Modules

- **finkit.core**: Foundation library with time series containers, datetime handling, and numerical utilities
- **finkit.data**: Market data ingestion, normalization, and storage layer (DuckDB/Parquet)
- **finkit.analysis**: Technical indicators, statistical functions, bond basis calculations (QuantLib)
- **finkit.backtest**: Event-driven backtesting engine with order management and execution simulation
- **finkit.viz**: Terminal-based charts and reporting

### Dependency Graph

```
finkit.core → finkit.data → finkit.analysis → finkit.backtest → finkit.viz
```

### Module Files

Module interface units are `.cppm` files in each module's `src/` directory:
- `src/core/src/core.cppm`
- `src/data/src/data.cppm`
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

C++ dependencies managed by Conan:
- QuantLib (bond pricing)
- DuckDB (data storage)
- spdlog (logging)
- fmt (formatting)
- nlohmann_json (JSON)

Python dev dependencies in `pyproject.toml`:
- conan
- pre-commit
