# fin-kit

[![License: GPL-3.0](https://img.shields.io/badge/License-GPL--3.0-blue.svg)](https://www.gnu.org/licenses/gpl-3.0)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)](https://en.cppreference.com/w/cpp/20)
[![Python 3.13+](https://img.shields.io/badge/Python-3.13%2B-blue.svg)](https://www.python.org/)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20macOS-lightgrey.svg)](docs/SETUP.md)

A modern C++20 financial analysis toolkit for quantitative research and backtesting.

## Start Here

Before you build fin-kit, read the setup guide and complete the QuantLib step:

1. Follow [docs/SETUP.md](docs/SETUP.md)
2. Build QuantLib from source with `./scripts/build-quantlib.sh`
3. Use Docker Desktop + WSL integration if you are working on Redis/dashboard/streaming workflows

**Do not use Homebrew/system QuantLib for fin-kit.** The repo expects a custom build in `~/.local/quantlib-llvm/`.

## Features

- **Core**: Time series containers, date/time handling, numerical utilities
- **Data**: Market data ingestion, storage, and normalization
- **Analysis**: Technical indicators, statistical analysis, signal generation
- **Backtest**: Event-driven backtesting engine with realistic execution modeling
- **Viz**: Terminal-based visualization and reporting

## Requirements

- **LLVM Clang 21+** with libc++ (required for C++20 modules)
- CMake 3.28+
- Ninja
- uv (used to install Python tools and invoke Conan)

### Required setup steps

- Build QuantLib from source: `./scripts/build-quantlib.sh`
- Detect Conan profile once: `uv run conan profile detect`
- Install dependencies with `uv run conan install ...`

## Building

See **[docs/SETUP.md](docs/SETUP.md)** for detailed setup instructions (macOS and Linux).

Quick start (after setup):
```bash
cmake --build build/build/Release
ctest --test-dir build/build/Release --output-on-failure
```

## Local Services

Redis, Docker Desktop / WSL integration, and TimescaleDB guidance live in
[docs/LOCAL_SERVICES.md](docs/LOCAL_SERVICES.md).

In short:

- Core C++ build and tests do **not** require Docker
- Redis is required for dashboard/streaming workflows
- TimescaleDB is required for data-backed workflows that read/write shared market data

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
├── services/       # Long-running services (finkit-stream Redis consumer)
├── adapters/       # Market data publishers (mock, LSEG) → Redis Streams
├── web/            # Dashboard (React/Vite) and Bun WebSocket/tRPC server
├── tests/          # Unit and integration tests
├── docs/           # Documentation
└── ci/             # CI/CD configuration
```

## Development

```bash
# Install pre-commit hooks
uv run pre-commit install
```

For alternate build types or local service workflows, follow the same toolchain
pattern documented in [docs/SETUP.md](docs/SETUP.md) and
[docs/LOCAL_SERVICES.md](docs/LOCAL_SERVICES.md).

## Documentation

- [docs/SETUP.md](docs/SETUP.md) - Environment setup (macOS/Linux)
- [docs/LOCAL_SERVICES.md](docs/LOCAL_SERVICES.md) - Redis, Docker Desktop, WSL, TimescaleDB
- [docs/DEPENDENCIES.md](docs/DEPENDENCIES.md) - Dependency management and Renovatebot
- [docs/architecture.md](docs/architecture.md) - Design details
- [docs/roadmap.md](docs/roadmap.md) - Planned features
