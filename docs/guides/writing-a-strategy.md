# Writing a Strategy

This guide walks through implementing a trading strategy for the fin-kit backtest engine.

## Strategy Interface

All strategies implement the `IStrategy` interface:

```cpp
class IStrategy {
public:
    virtual ~IStrategy() = default;

    // Identity
    [[nodiscard]] virtual auto name() const -> string = 0;
    [[nodiscard]] virtual auto id() const -> string = 0;
    [[nodiscard]] virtual auto parameters() const -> map<string, string> { return {}; }

    // Lifecycle
    virtual void on_init(StrategyContext& ctx) {}
    virtual void on_start(StrategyContext& ctx) {}
    virtual void on_finish(StrategyContext& ctx) {}

    // Market data - on_bar is required
    virtual void on_bar(const BarEvent& bar, StrategyContext& ctx) = 0;
    virtual void on_tick(const TickEvent& tick, StrategyContext& ctx) {}

    // Order events
    virtual void on_fill(const Fill& fill, StrategyContext& ctx) {}
    virtual void on_order_rejected(const Order& order, const string& reason, StrategyContext& ctx) {}
    virtual void on_order_cancelled(const Order& order, const string& reason, StrategyContext& ctx) {}

    // Risk events
    virtual void on_risk_breach(const RiskBreachEvent& event, StrategyContext& ctx) {}
    virtual void on_risk_action(const RiskActionEvent& event, StrategyContext& ctx) {}

    // Time events
    virtual void on_trading_day_start(const ql::Date& date, StrategyContext& ctx) {}
    virtual void on_trading_day_end(const ql::Date& date, StrategyContext& ctx) {}
};
```

## Minimal Strategy

The simplest valid strategy:

```cpp
#include <string>

import finkit.backtest;

using namespace finkit::backtest;

class MinimalStrategy : public IStrategy {
public:
    [[nodiscard]] auto name() const -> string override {
        return "Minimal";
    }

    [[nodiscard]] auto id() const -> string override {
        return "minimal_strategy";
    }

    void on_bar(const BarEvent& bar, StrategyContext& ctx) override {
        // Required but can be empty
    }
};
```

## Complete Example: Moving Average Crossover

```cpp
class MACrossoverStrategy : public IStrategy {
public:
    MACrossoverStrategy(string symbol, int fast_period, int slow_period)
        : symbol_(std::move(symbol)),
          fast_period_(fast_period),
          slow_period_(slow_period) {}

    [[nodiscard]] auto name() const -> string override {
        return "MACrossover";
    }

    [[nodiscard]] auto id() const -> string override {
        return "ma_crossover_" + symbol_;
    }

    [[nodiscard]] auto parameters() const -> map<string, string> override {
        return {
            {"symbol", symbol_},
            {"fast_period", std::to_string(fast_period_)},
            {"slow_period", std::to_string(slow_period_)}
        };
    }

    void on_bar(const BarEvent& bar, StrategyContext& ctx) override {
        // Only process bars for our target symbol
        if (bar.symbol != symbol_) return;

        // Accumulate prices
        prices_.push_back(bar.close);

        // Need enough data for slow MA
        if (prices_.size() < static_cast<size_t>(slow_period_)) return;

        // Calculate moving averages
        double fast_ma = calculate_ma(fast_period_);
        double slow_ma = calculate_ma(slow_period_);

        // Get current position
        auto pos = ctx.portfolio.position(symbol_);
        double current_qty = pos ? pos->quantity : 0.0;

        // Cross above: buy signal
        if (fast_ma > slow_ma && !is_long_) {
            // Close short if any
            if (current_qty < 0) {
                auto cover = finkit::trading::make_market_order(
                    symbol_, OrderSide::Buy, std::abs(current_qty), id());
                ctx.submit_order(cover);
            }

            // Go long with 50% of NAV
            double nav = ctx.portfolio.nav(ctx.fx);
            double shares = std::floor(nav * 0.5 / bar.close);
            if (shares > 0) {
                auto buy = finkit::trading::make_market_order(
                    symbol_, OrderSide::Buy, shares, id());
                ctx.submit_order(buy);
            }
            is_long_ = true;
        }
        // Cross below: sell signal
        else if (fast_ma < slow_ma && is_long_) {
            if (current_qty > 0) {
                auto sell = finkit::trading::make_market_order(
                    symbol_, OrderSide::Sell, current_qty, id());
                ctx.submit_order(sell);
            }
            is_long_ = false;
        }
    }

    void on_fill(const Fill& fill, StrategyContext& ctx) override {
        // Optional: track fills for logging/analysis
        spdlog::info("Fill: {} {} {} @ {}",
                     fill.side == OrderSide::Buy ? "BUY" : "SELL",
                     fill.quantity, fill.symbol, fill.price);
    }

private:
    [[nodiscard]] auto calculate_ma(int period) const -> double {
        double sum = 0.0;
        size_t start = prices_.size() - static_cast<size_t>(period);
        for (size_t i = start; i < prices_.size(); ++i) {
            sum += prices_[i];
        }
        return sum / static_cast<double>(period);
    }

    string symbol_;
    int fast_period_;
    int slow_period_;
    vector<double> prices_;
    bool is_long_{false};
};
```

## Using the Strategy

```cpp
#include <memory>

import finkit.backtest;

using namespace finkit::backtest;

auto run_backtest() -> BacktestResult {
    // Configure backtest
    BacktestConfig config{
        .start_date = ql::Date(1, ql::January, 2020),
        .end_date = ql::Date(31, ql::December, 2023),
        .initial_capital = 1000000.0,
        .base_currency = Currency::USD,
        .slippage_bps = 0.5,
        .commission_per_share = 0.01
    };

    // Create engine
    BacktestEngine engine(config);

    // Add data feed
    auto feed = std::make_unique<InMemoryDataFeed>();
    // ... populate feed with bars
    engine.set_data_feed(std::move(feed));

    // Add strategy
    auto strategy = std::make_unique<MACrossoverStrategy>("SPY", 10, 50);
    engine.add_strategy(std::move(strategy));

    // Run
    return engine.run();
}
```

## Order Helpers

Use the helper functions for creating orders:

```cpp
// Market order
auto order = finkit::trading::make_market_order(
    symbol,           // string
    OrderSide::Buy,   // Buy or Sell
    quantity,         // double
    strategy_id       // string
);

// Limit order
auto order = finkit::trading::make_limit_order(
    symbol,
    OrderSide::Sell,
    quantity,
    limit_price,      // double
    strategy_id
);

// Submit via context
OrderId id = ctx.submit_order(order);
```

## Position Sizing

Access portfolio for position sizing:

```cpp
void on_bar(const BarEvent& bar, StrategyContext& ctx) override {
    // Get current NAV in base currency
    double nav = ctx.portfolio.nav(ctx.fx);

    // Target 10% of NAV per position
    double target_value = nav * 0.10;
    double shares = std::floor(target_value / bar.close);

    // Check current position
    auto pos = ctx.portfolio.position(bar.symbol);
    double current_qty = pos ? pos->quantity : 0.0;

    // Only trade if we need to rebalance
    double delta = shares - current_qty;
    if (std::abs(delta) > 10) {  // Threshold to avoid tiny trades
        OrderSide side = delta > 0 ? OrderSide::Buy : OrderSide::Sell;
        auto order = make_market_order(bar.symbol, side, std::abs(delta), id());
        ctx.submit_order(order);
    }
}
```

## Handling Multiple Symbols

```cpp
class MultiSymbolStrategy : public IStrategy {
    vector<string> symbols_;
    map<string, vector<double>> prices_;

public:
    void on_bar(const BarEvent& bar, StrategyContext& ctx) override {
        // Store price for this symbol
        prices_[bar.symbol].push_back(bar.close);

        // Check if we have data for all symbols
        bool all_ready = true;
        for (const auto& sym : symbols_) {
            if (prices_[sym].size() < required_history_) {
                all_ready = false;
                break;
            }
        }

        if (!all_ready) return;

        // Now process with all symbols' data available
        generate_signals(ctx);
    }
};
```

## Risk Event Handling

Handle risk engine actions:

```cpp
void on_risk_action(const RiskActionEvent& event, StrategyContext& ctx) override {
    if (event.action == RiskActionEvent::Action::Liquidate) {
        spdlog::warn("Risk liquidation: {}", event.reason);

        // Cancel all pending orders
        for (const auto& order : ctx.get_open_orders()) {
            ctx.cancel_order(order.id);
        }

        // Reset internal state
        is_long_ = false;
        prices_.clear();
    }
}

void on_order_rejected(const Order& order, const string& reason, StrategyContext& ctx) override {
    spdlog::warn("Order rejected: {} - {}", order.symbol, reason);
    // Handle rejection (e.g., adjust position sizing)
}
```

## Daily Rebalancing

Use day change callback:

```cpp
void on_trading_day_start(const ql::Date& date, StrategyContext& ctx) override {
    // Daily rebalancing logic
    if (is_rebalance_day(date)) {
        rebalance_portfolio(ctx);
    }
}

void on_trading_day_end(const ql::Date& date, StrategyContext& ctx) override {
    // End-of-day processing
    log_daily_pnl(ctx);
}
```

## Tips

1. **Filter by symbol**: Always check `bar.symbol` if your strategy targets specific symbols
2. **Check data sufficiency**: Ensure you have enough historical bars before calculating indicators
3. **Use strategy_id**: Pass your `id()` when creating orders for tracking
4. **Handle rejections**: Implement `on_order_rejected` to handle risk rejections gracefully
5. **Clean state on finish**: Use `on_finish` to log results or cleanup resources
