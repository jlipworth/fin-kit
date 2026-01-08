# Running a Backtest

This guide covers configuring and running backtests with the fin-kit backtest engine.

## Prerequisites

- Strategy implemented (see [Writing a Strategy](writing-a-strategy.md))
- Market data loaded (see [Loading Market Data](loading-market-data.md))

## Quick Start

```cpp
#include <memory>
#include <ql/quantlib.hpp>

import finkit.backtest;
import finkit.types;

namespace ql = QuantLib;
using namespace finkit::backtest;
using namespace finkit::types;

auto run() -> BacktestResult {
    // 1. Configure
    BacktestConfig config{
        .start_date = ql::Date(1, ql::January, 2024),
        .end_date = ql::Date(31, ql::December, 2024),
        .initial_capital = 1000000.0,
        .base_currency = Currency::USD
    };

    // 2. Create engine
    BacktestEngine engine(config);

    // 3. Set data feed
    auto feed = std::make_unique<InMemoryDataFeed>();
    // ... populate feed
    engine.set_data_feed(std::move(feed));

    // 4. Add strategy
    auto strategy = std::make_unique<MACrossoverStrategy>("SPY", 10, 50);
    engine.add_strategy(std::move(strategy));

    // 5. Run
    return engine.run();
}
```

## BacktestConfig Options

```cpp
struct BacktestConfig {
    // Time range
    ql::Date start_date;
    ql::Date end_date;

    // Capital
    double initial_capital{1000000.0};
    Currency base_currency{Currency::USD};
    map<Currency, double> initial_cash;  // For multi-currency

    // Execution costs
    double slippage_bps{0.5};            // Slippage in basis points
    double commission_per_share{0.0};     // Per-share commission
    double commission_pct{0.0};           // Percentage commission

    // Risk config (see risk section below)
    RiskConfig risk;

    // Persistence
    bool persist_trades{true};
    bool persist_equity{true};
    bool persist_positions{true};
    int equity_snapshot_frequency_bars{1};
};
```

### Multi-Currency Setup

```cpp
BacktestConfig config{
    .start_date = ql::Date(1, ql::January, 2024),
    .end_date = ql::Date(31, ql::December, 2024),
    .base_currency = Currency::USD,
    .initial_cash = {
        {Currency::USD, 500000.0},
        {Currency::EUR, 200000.0},
        {Currency::GBP, 100000.0}
    }
};
```

## Setting Up the Data Feed

### InMemoryDataFeed

For testing and small datasets:

```cpp
auto feed = std::make_unique<InMemoryDataFeed>();

// Add individual bars
feed->add_bar(BarEvent{
    .symbol = "SPY",
    .timestamp = make_timestamp(2024, 1, 2),
    .open = 470.0,
    .high = 475.0,
    .low = 469.0,
    .close = 474.0,
    .volume = 50000000.0
});

// Or add multiple bars at once
std::vector<BarEvent> bars = load_bars_from_database();
feed->add_bars(bars);

engine.set_data_feed(std::move(feed));
```

### Custom Data Feed

Implement `IDataFeed` for database-backed feeds:

```cpp
class DatabaseDataFeed : public IDataFeed {
public:
    explicit DatabaseDataFeed(InputDataStore& db,
                              const std::string& symbol,
                              ql::Date start, ql::Date end)
        : db_(db), symbol_(symbol), start_(start), end_(end) {
        load_bars();
    }

    auto next() -> std::optional<BarEvent> override {
        if (index_ >= bars_.size()) return std::nullopt;
        return bars_[index_++];
    }

    auto has_more() const -> bool override {
        return index_ < bars_.size();
    }

    auto current_time() const -> Timestamp override {
        return index_ > 0 ? bars_[index_ - 1].timestamp : Timestamp{};
    }

    void reset() override { index_ = 0; }

private:
    void load_bars() {
        auto result = db_.query(/* SQL to load bars */);
        // Convert result to bars_
    }

    InputDataStore& db_;
    std::string symbol_;
    ql::Date start_, end_;
    std::vector<BarEvent> bars_;
    size_t index_{0};
};
```

## Risk Configuration

```cpp
BacktestConfig config;

// Pre-trade limits
config.risk.max_position_pct = 0.10;        // 10% max per position
config.risk.max_order_pct = 0.05;           // 5% max per order
config.risk.max_gross_leverage = 2.0;       // 200% gross exposure

// Active monitoring
config.risk.max_drawdown_pct = 0.20;        // 20% max drawdown
config.risk.drawdown_policy = BreachPolicy::Liquidate;
```

See [Backtest Lifecycle](../concepts/backtest-lifecycle.md#risk-engine-modes) for details on risk engine behavior.

## Running Multiple Strategies

```cpp
BacktestEngine engine(config);
engine.set_data_feed(std::move(feed));

// Add multiple strategies
engine.add_strategy(std::make_unique<MACrossoverStrategy>("SPY", 10, 50));
engine.add_strategy(std::make_unique<MACrossoverStrategy>("QQQ", 10, 50));
engine.add_strategy(std::make_unique<BuyAndHoldStrategy>("TLT", 0.2));

auto result = engine.run();
```

## Interpreting Results

```cpp
auto result = engine.run();

// Returns
std::cout << "Total Return: " << result.total_return_pct * 100 << "%\n";
std::cout << "CAGR: " << result.cagr * 100 << "%\n";

// Risk-adjusted metrics
std::cout << "Sharpe Ratio: " << result.sharpe_ratio << "\n";
std::cout << "Sortino Ratio: " << result.sortino_ratio << "\n";
std::cout << "Calmar Ratio: " << result.calmar_ratio << "\n";

// Drawdown
std::cout << "Max Drawdown: " << result.max_drawdown_pct * 100 << "%\n";

// Trade statistics
std::cout << "Total Trades: " << result.total_trades << "\n";
std::cout << "Win Rate: " << result.win_rate * 100 << "%\n";
std::cout << "Profit Factor: " << result.profit_factor << "\n";

// Risk events
std::cout << "Orders Rejected: " << result.orders_rejected << "\n";
std::cout << "Forced Liquidations: " << result.forced_liquidations << "\n";
```

### BacktestResult Fields

| Field | Description |
|-------|-------------|
| `total_return_pct` | (final_nav - initial_nav) / initial_nav |
| `sharpe_ratio` | Annualized risk-adjusted return |
| `max_drawdown_pct` | Largest peak-to-trough decline |
| `win_rate` | winning_trades / total_trades |
| `profit_factor` | sum(winners) / abs(sum(losers)) |

## Custom FX Provider

For multi-currency backtests with dynamic rates:

```cpp
class HistoricalFXProvider : public IFXRateProvider {
public:
    void set_time(Timestamp ts) { current_time_ = ts; }

    auto convert(double amount, Currency from, Currency to) const -> double override {
        if (from == to) return amount;
        double rate = get_rate(from, to, current_time_);
        return amount * rate;
    }

private:
    double get_rate(Currency from, Currency to, Timestamp ts) const;
    Timestamp current_time_;
};

auto fx = std::make_unique<HistoricalFXProvider>();
engine.set_fx_provider(std::move(fx));
```

## Complete Example

```cpp
#include <iostream>
#include <memory>
#include <ql/quantlib.hpp>

import finkit.backtest;
import finkit.data;
import finkit.types;

int main() {
    namespace ql = QuantLib;
    using namespace finkit::backtest;
    using namespace finkit::data;
    using namespace finkit::types;

    // Load config and create stores
    auto config = load_config();
    auto stores = create_data_stores(config);

    // Configure backtest
    BacktestConfig bt_config{
        .start_date = ql::Date(1, ql::January, 2023),
        .end_date = ql::Date(31, ql::December, 2023),
        .initial_capital = 1000000.0,
        .base_currency = Currency::USD,
        .slippage_bps = 1.0,
        .commission_per_share = 0.005
    };

    BacktestEngine engine(bt_config);

    // Load data from database into feed
    auto feed = std::make_unique<InMemoryDataFeed>();
    // ... populate from stores.input
    engine.set_data_feed(std::move(feed));

    // Add strategy
    engine.add_strategy(std::make_unique<MACrossoverStrategy>("SPY", 20, 50));

    // Run backtest
    auto result = engine.run();

    // Print summary
    std::cout << "=== Backtest Results ===\n";
    std::cout << "Run ID: " << result.run_id << "\n";
    std::cout << "Total Return: " << result.total_return_pct * 100 << "%\n";
    std::cout << "Sharpe Ratio: " << result.sharpe_ratio << "\n";
    std::cout << "Max Drawdown: " << result.max_drawdown_pct * 100 << "%\n";
    std::cout << "Win Rate: " << result.win_rate * 100 << "%\n";

    return 0;
}
```

## Next Steps

- [Backtest Lifecycle](../concepts/backtest-lifecycle.md) - Understand the event loop
- [Writing a Strategy](writing-a-strategy.md) - Create custom strategies
- [Architecture](../architecture.md) - Module dependencies
