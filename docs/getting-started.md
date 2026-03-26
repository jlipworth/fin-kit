# Getting Started

This guide walks you through running your first fin-kit calculation.

## Prerequisites

Complete the environment setup in [SETUP.md](SETUP.md) before proceeding.

> **Start here first:** fin-kit expects QuantLib to be built from source with
> `./scripts/build-quantlib.sh`. Do **not** use Homebrew/system QuantLib for
> this repo. Run Conan via `uv run conan ...`, not a separate ad hoc Python
> environment. If you are working on Redis/dashboard/streaming flows, also read
> [LOCAL_SERVICES.md](LOCAL_SERVICES.md) before proceeding.

## Quick Example: Buy-and-Hold Backtest

Here's a minimal example that runs a backtest with a buy-and-hold strategy:

```cpp
#include <chrono>
#include <iostream>
#include <memory>
#include <ql/quantlib.hpp>

import finkit.backtest;
import finkit.types;

namespace ql = QuantLib;

using namespace finkit::backtest;
using namespace finkit::types;

// Helper to create timestamps
auto make_timestamp(int year, int month, int day) -> Timestamp {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    return std::chrono::time_point_cast<std::chrono::nanoseconds>(tp);
}

int main() {
    // 1. Configure the backtest
    BacktestConfig config{
        .start_date = ql::Date(2, ql::January, 2024),
        .end_date = ql::Date(31, ql::January, 2024),
        .initial_capital = 100000.0,
        .base_currency = Currency::USD,
        .slippage_bps = 0.5,
        .commission_per_share = 0.01
    };

    BacktestEngine engine(config);

    // 2. Create and populate a data feed
    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar({
        .symbol = "SPY",
        .timestamp = make_timestamp(2024, 1, 2),
        .open = 470.0, .high = 475.0, .low = 469.0, .close = 474.0,
        .volume = 50000000.0
    });
    feed->add_bar({
        .symbol = "SPY",
        .timestamp = make_timestamp(2024, 1, 3),
        .open = 474.0, .high = 478.0, .low = 473.0, .close = 477.0,
        .volume = 45000000.0
    });
    // ... add more bars
    engine.set_data_feed(std::move(feed));

    // 3. Add a strategy
    auto strategy = std::make_unique<BuyAndHoldStrategy>("SPY", 1.0);
    engine.add_strategy(std::move(strategy));

    // 4. Run and inspect results
    auto result = engine.run();

    std::cout << "Total Return: " << result.total_return_pct * 100 << "%\n";
    std::cout << "Final NAV: $" << result.final_nav << "\n";
    std::cout << "Total Trades: " << result.total_trades << "\n";
    std::cout << "Sharpe Ratio: " << result.sharpe_ratio << "\n";

    return 0;
}
```

## Expected Output

```
Total Return: 0.633%
Final NAV: $100633.12
Total Trades: 1
Sharpe Ratio: 1.42
```

## Building and Running

```bash
# Build the project
cmake --build build/build/Release

# Run tests to verify your setup
ctest --test-dir build/build/Release --output-on-failure
```

## Example Application: Bond Basis Analyzer

For a more complete example, see the [bond_basis application](../apps/bond_basis/README.md) which demonstrates:

- Loading market data from TimescaleDB
- Running financial calculations
- Using the analysis module

## Next Steps

1. **[Loading Market Data](guides/loading-market-data.md)** - How to query market data from TimescaleDB
2. **[Writing a Strategy](guides/writing-a-strategy.md)** - Create custom trading strategies
3. **[Running a Backtest](guides/running-a-backtest.md)** - Full backtest configuration guide
4. **[Architecture](architecture.md)** - Understand the module structure

## Module Overview

| Module | Purpose |
|--------|---------|
| `finkit.core` | Foundation: DateTime, TimeSeries, Result<T,E> |
| `finkit.data` | TimescaleDB data store, config loading |
| `finkit.types` | Shared types: Currency, Bond, FXSpot |
| `finkit.backtest` | Simulation engine, portfolio, strategies |
| `finkit.trading` | Order types, execution models |
| `finkit.risk` | Pre-trade checks, active monitoring |
| `finkit.analysis` | Bond analysis, CTD identification |
| `finkit.curves` | Rate curve bootstrapping, CIP basis |
