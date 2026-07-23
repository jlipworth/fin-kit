# fin-kit Status

Last updated: 2026-07-23

> **Onboarding note:** this file is a project snapshot, not the authoritative
> setup guide. For first-time setup, use [docs/SETUP.md](SETUP.md). For Redis,
> Docker Desktop / WSL, and TimescaleDB-backed workflows, use
> [docs/LOCAL_SERVICES.md](LOCAL_SERVICES.md).

## Current State

### Build System
- C++20 modules working with LLVM Clang 21
- Conan for most deps (libpqxx, spdlog, nlohmann_json, tomlplusplus, eigen)
- QuantLib 1.40 built from source with LLVM libc++ (ABI compatibility)
- Boost headers from Homebrew/Linuxbrew (required by QuantLib)
- Repo workflow uses `uv run conan ...`
- Redis/Docker Desktop is required for dashboard and streaming workflows
- 136 tests passing (10 DataStoreTest cases skipped without a live TimescaleDB)

### Modules Implemented

| Module | Status | Partitions | Notes |
|--------|--------|------------|-------|
| `finkit.core` | Working | - | Path utils, logging |
| `finkit.data` | Working | - | TOML config, TimescaleDB via libpqxx, DataStore |
| `finkit.types` | **Complete** | - | Currency, Bond, FX, Position types |
| `finkit.bootstrap` | **Complete** | - | SOFR curve, OIS, CB cut probabilities |
| `finkit.basis` | **Complete** | bond, cip, index | Bond/CIP/Index futures basis |
| `finkit.stats` | **Complete** | rolling, covariance, signals | Rolling stats, covariance matrices, signal analysis |
| `finkit.valuation` | **Complete** | bond, swap | Bond/swap valuation with QuantLib |
| `finkit.curves` | Working | - | Rate accessors, CIP utilities |
| `finkit.analysis` | Working | - | Bond basket analysis, ranking |
| `finkit.trading` | **Complete** | types, engine | Orders, fills, execution engine, bid/ask half-spread |
| `finkit.risk` | **Complete** | types, engine, var, stress | Limits, pre-trade checks, active monitoring, VaR (historical/parametric/Monte Carlo) + expected shortfall, scenario stress testing, ADV liquidity limits, sector/currency concentration limits |
| `finkit.backtest` | **Complete** | types, strategy, engine | Full backtest engine with strategies; look-ahead guard, listing/delisting universe with forced liquidation, borrow costs + hard-to-borrow |
| `finkit.viz` | Placeholder | - | Terminal visualization |

### Architecture
- **6 calculation libraries**: bootstrap, basis, curves, analysis, valuation, stats
- **3 frameworks**: trading, risk, backtest
- **Proper separation**: Input data (read-only) vs Output data (written by frameworks)
- **Module partitions**: Large modules split by type for maintainability

### Documentation
- `docs/architecture.md` - Complete module structure and design principles
- `docs/roadmap.md` - Development roadmap and planned features
- `docs/concepts/backtest-lifecycle.md` - Educational guide
- `docs/guides/writing-a-strategy.md` - Strategy implementation how-to
- `docs/INPUT_REQUIREMENTS.md` - Data specs for all calculations

### Testing
- 136 tests passing (core, data, curves, analysis, trading, risk, backtest, viz)
- Backtest tests include: Portfolio, DataFeed, Engine, Strategy, full-engine
  integration (known P&L, order rejection, no-strategy runs), and realism
  behavior (look-ahead guard, listing/delisting forced liquidation, borrow
  cost accrual, hard-to-borrow, bid/ask half-spread)
- Trading tests cover orders, positions, the order book, execution engine
  fill models, commission schedules, and half-spread pricing
- Risk tests cover limits/pre-trade/post-trade/monitoring, VaR (historical,
  parametric, Monte Carlo) and expected shortfall, stress scenarios, ADV
  liquidity limits, and sector/currency concentration limits
- Precise expected value tests for P&L and drawdown calculations

## Key Design Decisions

1. **Calculation modules are stateless**: Pure functions, no framework dependencies
2. **Conventions as parameters**: Never hardcoded locale/market assumptions
3. **Multi-currency support**: Portfolio, risk limits in base currency with FX conversion
4. **Bi-directional risk**: Pre-trade approval AND active monitoring
5. **Module partitions for file splitting**: Avoid 1500+ line files

## Open Questions

### Data Pipeline
1. **Data source integration** - Market data flows from Python repo into shared TimescaleDB
2. **Historical data backfill** - How far back? What instruments?

### Calculations
3. **Convexity adjustment** - What model for futures?
4. **FOMC handling** - Step function between meetings?
5. **Transaction costs** - Bid/ask spread parameterization

### Infrastructure
6. **Time zone handling** - UTC storage with display conversion?
7. **Config file location** - Currently checks XDG and ~/.finkit/

## What's Next

See [roadmap.md](roadmap.md) for current priorities and planned phases.

## Quick Build

After completing `docs/SETUP.md`:

```bash
# Build
cmake --build build/build/Release

# Test
ctest --test-dir build/build/Release --output-on-failure
```

## Full Setup

See `docs/SETUP.md` for complete environment setup instructions and
`docs/LOCAL_SERVICES.md` for Redis/Docker/TimescaleDB workflows.
