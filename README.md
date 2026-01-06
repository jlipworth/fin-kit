# fin-kit

A modern C++20 financial analysis toolkit for quantitative research and backtesting.

## Features

- **Core**: Time series containers, date/time handling, numerical utilities
- **Data**: Market data ingestion, storage, and normalization
- **Analysis**: Technical indicators, statistical analysis, signal generation
- **Backtest**: Event-driven backtesting engine with realistic execution modeling
- **Viz**: Terminal-based visualization and reporting

## Requirements

- C++20 compatible compiler (GCC 12+, Clang 15+, MSVC 2022+)
- CMake 3.25+
- Conan 2.x

## Building

```bash
# Install dependencies
conan install . --build=missing -of=build

# Configure and build
cmake --preset conan-release
cmake --build build --config Release

# Run tests
ctest --test-dir build --output-on-failure
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

See [docs/architecture.md](docs/architecture.md) for design details and [docs/roadmap.md](docs/roadmap.md) for planned features.
