# fin-kit Status

Last updated: 2026-01-08

## Current State

### Build System
- C++20 modules working with LLVM Clang 21
- Conan for most deps (DuckDB, spdlog, fmt, tomlplusplus, nlohmann_json)
- QuantLib 1.40 built from source with LLVM libc++ (ABI compatibility)
- Boost headers from Homebrew/Linuxbrew (required by QuantLib)
- All 19 tests passing

### Modules Implemented

| Module | Status | Partitions | Notes |
|--------|--------|------------|-------|
| `finkit.core` | Working | - | Path utils, logging |
| `finkit.data` | Working | - | TOML config, DuckDB, InputDataStore/OutputDataStore |
| `finkit.types` | **Complete** | - | Currency, Bond, FX, Position types |
| `finkit.bootstrap` | **Complete** | - | SOFR curve, OIS, CB cut probabilities |
| `finkit.basis` | **Complete** | bond, cip, index | Bond/CIP/Index futures basis |
| `finkit.stats` | **Complete** | rolling, covariance, signals | Rolling stats, covariance matrices, signal analysis |
| `finkit.valuation` | **Complete** | bond, swap | Bond/swap valuation with QuantLib |
| `finkit.curves` | Working | - | Rate accessors, CIP utilities |
| `finkit.analysis` | Working | - | Bond basket analysis, ranking |
| `finkit.trading` | **Complete** | types, engine | Orders, fills, execution engine |
| `finkit.risk` | **Complete** | types, engine | Limits, pre-trade checks, active monitoring |
| `finkit.backtest` | **Complete** | types, strategy, engine | Full backtest engine with strategies |
| `finkit.viz` | Placeholder | - | Terminal visualization |

### Architecture
- **6 calculation libraries**: bootstrap, basis, curves, analysis, valuation, stats
- **3 frameworks**: trading, risk, backtest
- **Proper separation**: Input data (read-only) vs Output data (written by frameworks)
- **Module partitions**: Large modules split by type for maintainability

### Documentation
- `docs/architecture.md` - Complete module structure and design principles
- `docs/roadmap.md` - Implementation phases with checkboxes
- `docs/FUTURE_WORK.md` - Comprehensive future improvements list
- `docs/concepts/backtest-lifecycle.md` - Educational guide
- `docs/guides/writing-a-strategy.md` - Strategy implementation how-to
- `docs/INPUT_REQUIREMENTS.md` - Data specs for all calculations

### Testing
- 19 tests passing (core, data, curves, analysis, backtest, viz)
- Backtest tests include: Portfolio, DataFeed, Engine, Strategy
- Precise expected value tests for P&L and drawdown calculations

## Key Design Decisions

1. **Calculation modules are stateless**: Pure functions, no framework dependencies
2. **Conventions as parameters**: Never hardcoded locale/market assumptions
3. **Multi-currency support**: Portfolio, risk limits in base currency with FX conversion
4. **Bi-directional risk**: Pre-trade approval AND active monitoring
5. **Module partitions for file splitting**: Avoid 1500+ line files

## Open Questions

### Data Pipeline
1. **Data source integration** - How does market data flow into DuckDB?
2. **Historical data backfill** - How far back? What instruments?

### Calculations
3. **Convexity adjustment** - What model for futures?
4. **FOMC handling** - Step function between meetings?
5. **Transaction costs** - Bid/ask spread parameterization

### Infrastructure
6. **Time zone handling** - UTC storage with display conversion?
7. **Config file location** - Currently checks XDG and ~/.finkit/

## Next Steps (Priority Order)

1. **Visualization module** - Implement terminal charts and reports
2. **Additional tests** - Integration tests, edge cases
3. **Concurrency** - Parallel calculations in backtest
4. **Decimal precision** - Transaction amount accuracy

## Quick Build

```bash
# Build
cmake --build build/build/Release

# Test
ctest --test-dir build/build/Release --output-on-failure
```

## Full Setup

See `docs/SETUP.md` for complete environment setup instructions.
