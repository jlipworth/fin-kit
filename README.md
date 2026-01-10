# fin-kit

[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Python 3.13+](https://img.shields.io/badge/Python-3.13%2B-blue.svg)](https://www.python.org/)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS-lightgrey.svg)](docs/SETUP.md)

A modern C++20 financial analysis toolkit for quantitative research and backtesting.

## Features

- **Core**: Time series containers, date/time handling, numerical utilities
- **Data**: Market data ingestion, storage, and normalization
- **Analysis**: Technical indicators, statistical analysis, signal generation
- **Backtest**: Event-driven backtesting engine with realistic execution modeling
- **Viz**: Terminal-based visualization and reporting

## Requirements

- **LLVM Clang 21+** with libc++ (required for C++20 modules)
- CMake 3.25+
- Ninja
- Conan 2.x

## Building

See **[docs/SETUP.md](docs/SETUP.md)** for detailed setup instructions (macOS and Linux).

Quick start (after setup):
```bash
cmake --build build/build/Release
ctest --test-dir build/build/Release --output-on-failure
```

## Project Structure

```
fin-kit/
├── src/
│   ├── core/       # Foundation: time series, datetime, numerics
│   ├── data/       # Data ingestion and storage
│   ├── analysis/   # Indicators and statistics
│   ├── backtest/   # Backtesting engine
│   └── viz/        # Visualization
├── apps/           # Example applications
├── tests/          # Unit and integration tests
├── docs/           # Documentation
└── ci/             # CI/CD configuration
```

## Development

```bash
# Install pre-commit hooks
pre-commit install

# Build with debug symbols
cmake --preset conan-debug
cmake --build build --config Debug
```

## Documentation

- [docs/SETUP.md](docs/SETUP.md) - Environment setup (macOS/Linux)
- [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md) - Dependency management and Renovatebot
- [docs/architecture.md](docs/architecture.md) - Design details
- [docs/roadmap.md](docs/roadmap.md) - Planned features
