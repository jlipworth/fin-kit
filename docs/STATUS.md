# fin-kit Status

Last updated: 2026-01-06

## Current State

### Build System
- C++20 modules working with LLVM Clang 21 (Homebrew)
- Conan for most deps (DuckDB, spdlog, fmt, tomlplusplus, nlohmann_json)
- QuantLib 1.40 from Homebrew (Conan's 1.30 has consteval issues with Clang 21)
- Boost headers from Homebrew (required by QuantLib)
- All tests passing

### Modules Implemented

| Module | Status | Notes |
|--------|--------|-------|
| `finkit.core` | Skeleton | Path utils, logging |
| `finkit.data` | Working | TOML config, DuckDB wrapper, run management |
| `finkit.curves` | Skeleton | SOFR bootstrap stub, CIP basis **implemented** |
| `finkit.analysis` | Working | Bond basis, CF calculation, CTD identification |
| `finkit.backtest` | Placeholder | - |
| `finkit.viz` | Placeholder | - |

### Key Files
- `docs/INPUT_REQUIREMENTS.md` - Complete data specs for all calculations
- `src/curves/curves.cppm` - SOFR + CIP/CCY basis types and functions
- `src/analysis/analysis.cppm` - Bond basis types and functions

## Open Questions

### Data Pipeline
1. **Refinitiv integration** - How does data flow from Refinitiv pipeline toolkit into DuckDB?
   - Need to define ingestion format/schema
   - Real-time vs batch?

2. **Historical data backfill** - How far back? What instruments?
   - SOFR fixings (2018+)
   - Treasury prices/yields
   - FX spots/forwards

### SOFR Curve
3. **Convexity adjustment** - What model for SR3 futures?
   - Hull-White?
   - Market-implied from options?

4. **FOMC handling** - How to incorporate meeting dates?
   - Step function between meetings?
   - Fed funds futures for implied moves?

### CIP Basis
5. **Rate source hierarchy** - Which rates for CIP calculation?
   - OIS preferred, but need consistent tenors across G10
   - Fallback to repo/deposit rates?

6. **Transaction costs** - What bid/ask spreads are realistic?
   - Depends on notional size
   - Need to parameterize

### Bond Basis
7. **Delivery options** - Priority for implementation?
   - Quality/switch option most valuable
   - Need yield vol source

8. **Interim coupons** - How to handle reinvestment?
   - Assume repo rate reinvestment?

### General
9. **Time zone handling** - How to reconcile NY close vs London vs Tokyo?
   - Store everything in UTC?
   - Convert on display?

10. **Config file location** - Finalize paths
    - `~/.config/finkit/config.toml` (XDG)
    - `~/.finkit/config.toml` (simpler)
    - Currently checks both

## Next Steps (Priority Order)

1. **Implement `bootstrap_sofr_curve()`** - Use QuantLib's OISRateHelper
2. **Add real tests** - Bond basis with known values, CIP with fixture data
3. **Data ingestion** - Define schema for Refinitiv data
4. **Backtest engine** - Event loop, position tracking

## Dependencies (Homebrew)

```bash
brew install llvm quantlib boost ninja
```

## Quick Build

```bash
uv sync
CC=/opt/homebrew/opt/llvm/bin/clang CXX=/opt/homebrew/opt/llvm/bin/clang++ \
  uv run conan install . --build=missing -of=build \
  -s compiler=clang -s compiler.version=21 -s compiler.cppstd=20 -s compiler.libcxx=libc++

CC=/opt/homebrew/opt/llvm/bin/clang CXX=/opt/homebrew/opt/llvm/bin/clang++ \
  cmake --preset conan-release

cmake --build build/build/Release
ctest --test-dir build/build/Release --output-on-failure
```
