# CLAUDE.md

Guidance for Claude Code when working with this repository.

## Quick Reference

- **Setup**: See [docs/SETUP.md](docs/SETUP.md) for environment setup
- **Architecture**: See [docs/architecture.md](docs/architecture.md) for design details

## Build Commands

```bash
# Build (after setup)
cmake --build build/build/Release

# Run tests
ctest --test-dir build/build/Release --output-on-failure

# Run specific test
ctest --test-dir build/build/Release -R <test_name> --output-on-failure
```

## Code Style

- **Naming**: `snake_case` for functions/variables, `PascalCase` for types, `SCREAMING_CASE` for constants
- **Modules**: Use `export module` and `import` (C++20 modules), not `#include` for project code
- **Format**: Follow `.clang-format` and `.clang-tidy`

### Standard Aliases

```cpp
namespace ql = QuantLib;  // Always alias QuantLib as ql

using std::optional;
using std::string;
using std::vector;
```

## Module Structure

Module interface units are `.cppm` files:
- `src/core/core.cppm` - Time series, datetime, numerics
- `src/data/data.cppm` - Market data ingestion and storage (DuckDB)
- `src/curves/curves.cppm` - Rate curve bootstrapping, CIP/CCY basis
- `src/analysis/analysis.cppm` - Indicators, statistics, bond basis (QuantLib)

## Testing

- Tests in `tests/` mirror `src/` structure
- Use GoogleTest, name files `*_test.cpp`
- Import modules with `import finkit.module_name;`
