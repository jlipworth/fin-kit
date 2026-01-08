# Backtest Lifecycle

This guide walks through the complete lifecycle of a backtest, from initialization to final statistics.

## Overview

The backtest engine orchestrates:
1. Data feeds (historical market data)
2. Strategy execution (user-defined trading logic)
3. Execution simulation (order matching and fills)
4. Risk management (pre-trade checks and active monitoring)
5. Portfolio tracking (positions, P&L, NAV)

## Lifecycle Stages

### 1. Initialization

```
BacktestConfig loaded
    ├── start_date, end_date
    ├── initial_capital, base_currency
    ├── execution config (slippage, commission)
    └── risk config (limits, policies)

Portfolio created
    └── initial cash deposited

Components connected
    ├── IDataFeed (historical data)
    ├── IFXRateProvider (currency conversion)
    ├── IRiskEngine (risk limits)
    └── IStrategy[] (trading strategies)

BacktestEngine.run() called
    └── Strategy.on_start() invoked for each strategy
```

### 2. Main Loop (Per Bar)

```
1. DataFeed.next() → BarEvent
   └── Returns {symbol, timestamp, open, high, low, close, volume}

2. Time synchronization
   ├── FXRateProvider updated to bar timestamp
   └── StrategyContext.current_time set

3. Portfolio.mark_to_market(bar)
   └── Position prices updated to bar.close

4. ExecutionEngine.on_bar(bar)
   └── Working orders checked for fills

5. Process fills
   ├── Portfolio.apply_fill(fill)
   ├── Update trade statistics (win/loss tracking)
   └── Strategy.on_fill(fill) callback

6. RiskEngine.monitor(portfolio, fx, timestamp) → [RiskActions]
   └── If breach detected:
       ├── Enter Liquidating state
       ├── Lock affected symbols
       ├── Generate liquidation orders
       └── Strategy.on_risk_action() callback

7. Check for new trading day
   └── Strategy.on_trading_day_start(date) if date changed

8. Strategy.on_bar(bar, ctx) → Order submissions
   └── Strategy analyzes bar, may submit orders

9. For each submitted order:
   ├── RiskEngine.check_pre_trade(order) → Allow/Reduce/Reject
   ├── If allowed: ExecutionEngine.submit_order(order)
   └── If rejected: Strategy.on_order_rejected() callback

10. Update equity curve
    ├── Calculate current NAV
    ├── Update high water mark
    ├── Track drawdown
    └── Store daily return for Sharpe calculation

11. Loop to next bar
```

### 3. Completion

```
Strategy.on_finish(ctx) called for each strategy

Calculate BacktestResult statistics:
    ├── Returns (total, annualized, CAGR)
    ├── Risk-adjusted (Sharpe, Sortino, Calmar)
    ├── Drawdown (max, avg, duration)
    ├── Trade stats (win rate, profit factor, avg P&L)
    └── Risk stats (breaches, rejections, liquidations)

Return BacktestResult
```

## Key Components

### StrategyContext

The context object passed to strategy callbacks:

```cpp
struct StrategyContext {
    const Portfolio& portfolio;        // Current portfolio state
    const IFXRateProvider& fx;         // FX rate provider
    Timestamp current_time;            // Current simulation time
    ql::Date current_date;             // Current date (QuantLib)

    // Order submission functions
    std::function<OrderId(Order)> submit_order;
    std::function<bool(OrderId)> cancel_order;
    std::function<vector<Order>()> get_open_orders;
};
```

### Strategy Callbacks

| Callback | When Called | Typical Use |
|----------|-------------|-------------|
| `on_start(ctx)` | Backtest start | Initialize state |
| `on_bar(bar, ctx)` | Each bar | Analyze, submit orders |
| `on_fill(fill, ctx)` | Order filled | Update internal state |
| `on_risk_action(event, ctx)` | Risk breach | Cancel orders, adjust |
| `on_trading_day_start(date, ctx)` | New day | Daily rebalancing |
| `on_finish(ctx)` | Backtest end | Cleanup, logging |

### Order Flow

```
Strategy                    Risk                    Execution
   │                          │                          │
   ├── submit_order(order) ──►│                          │
   │                          ├── check_pre_trade() ───►│
   │                          │◄── Allow/Reduce/Reject ──│
   │                          │                          │
   │                          │    [If allowed]          │
   │                          │────── submit() ─────────►│
   │                          │                          │
   │                          │    [On next bar]         │
   │                          │◄───── Fill event ────────│
   │◄── on_fill(fill) ────────│                          │
```

### Risk Engine Modes

The risk engine operates in two modes simultaneously:

**Mode 1: Pre-Trade Approval (Passive)**
- Trading engine asks permission for each order
- Risk engine responds: Allow, Reduce, or Reject

**Mode 2: Active Monitoring (Active)**
- Risk engine monitors portfolio on every bar
- Can autonomously trigger liquidation
- Generates orders that bypass pre-trade check
- Locks symbols during liquidation

### Liquidation Sequence

When drawdown or other limits are breached:

```
1. Risk monitoring detects breach
2. Risk engine enters Liquidating state
3. Affected symbols locked
4. Liquidation orders generated (market orders)
5. Strategy notified via on_risk_action()
6. Strategy cannot submit new orders for locked symbols
7. Liquidation fills processed
8. After all fills complete, Liquidating state exits
9. Normal trading resumes
```

## Multi-Currency Flow

For multi-currency portfolios:

```
1. Each position has a currency (Position.currency)
2. Cash held per currency (Portfolio.cash_by_currency())
3. Fills settle in instrument's currency
4. NAV calculation:
   └── Sum: fx.convert(position_value, pos.currency, base_currency)
5. Risk limits can be per-currency or in base currency
```

## Data Feed Considerations

For mixed-frequency data (e.g., 5-minute rates, daily equity):

- Events arrive ordered by timestamp across all symbols
- Strategy can check data freshness: `ctx.data_age(symbol)`
- Stale data shouldn't drive trading decisions
- Use `is_data_fresh(symbol, max_age)` before acting

## Example Strategy

```cpp
class SimpleMAStrategy : public IStrategy {
public:
    auto name() const -> string override { return "SimpleMA"; }
    auto id() const -> string override { return "simple_ma"; }

    void on_bar(const BarEvent& bar, StrategyContext& ctx) override {
        if (bar.symbol != target_symbol_) return;

        prices_.push_back(bar.close);
        if (prices_.size() < lookback_) return;

        double ma = calculate_ma();
        auto pos = ctx.portfolio.position(target_symbol_);
        double qty = pos ? pos->quantity : 0.0;

        if (bar.close > ma && qty == 0) {
            // Buy signal
            double shares = std::floor(ctx.portfolio.nav(ctx.fx) * 0.1 / bar.close);
            auto order = make_market_order(target_symbol_, OrderSide::Buy, shares, id());
            ctx.submit_order(order);
        } else if (bar.close < ma && qty > 0) {
            // Sell signal
            auto order = make_market_order(target_symbol_, OrderSide::Sell, qty, id());
            ctx.submit_order(order);
        }
    }

private:
    string target_symbol_;
    vector<double> prices_;
    size_t lookback_;
    // ...
};
```

## Performance Metrics Calculated

| Metric | Formula |
|--------|---------|
| Total Return | (final_nav - initial_nav) / initial_nav |
| Sharpe Ratio | mean(daily_returns) / std(daily_returns) * sqrt(252) |
| Max Drawdown | max((high_water_mark - nav) / high_water_mark) |
| Win Rate | winning_trades / total_trades |
| Profit Factor | sum(winners) / abs(sum(losers)) |
