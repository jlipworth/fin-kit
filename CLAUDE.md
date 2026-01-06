# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with this repository.

## Build Commands

```bash
# Install dependencies
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

fin-kit is a modular C++20 financial toolkit organized as:

- **core**: Foundation library with time series containers, datetime handling, and numerical utilities
- **data**: Market data ingestion, normalization, and storage layer
- **analysis**: Technical indicators, statistical functions, and signal generation
- **backtest**: Event-driven backtesting engine with order management and execution simulation
- **viz**: Terminal-based charts and reporting

Each module is a separate CMake target with explicit dependencies. The dependency graph flows: core → data → analysis → backtest → viz.

## Code Style

- Follow `.clang-format` (Google style base with project modifications)
- Use `.clang-tidy` checks - fix all warnings before committing
- Naming: `snake_case` for functions/variables, `PascalCase` for types, `SCREAMING_CASE` for constants
- Headers use `#pragma once`
- All public APIs require documentation comments

## Testing

- Tests live in `tests/` mirroring `src/` structure
- Use Catch2 for unit tests
- Name test files `*_test.cpp`
- Run tests before committing: `ctest --test-dir build --output-on-failure`
