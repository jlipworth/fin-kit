/// @file backtest-strategy.cppm
/// @brief Strategy interface for backtesting
///
/// Defines the IStrategy interface that users implement to create
/// trading strategies for backtesting.

module;

#include <chrono>
#include <functional>
#include <map>
#include <optional>
#include <ql/quantlib.hpp>
#include <string>
#include <vector>

export module finkit.backtest:strategy;

import :types;
import finkit.types;
import finkit.trading;
import finkit.risk;

export namespace finkit::backtest {

using std::map;
using std::optional;
using std::string;
using std::vector;

namespace ql = QuantLib;

using finkit::risk::IFXRateProvider;
using finkit::risk::RiskActionEvent;
using finkit::risk::RiskBreachEvent;
using finkit::trading::BarEvent;
using finkit::trading::Fill;
using finkit::trading::Order;
using finkit::trading::OrderId;
using finkit::trading::TickEvent;
using finkit::trading::Timestamp;

// ============================================================================
// Strategy Context
// ============================================================================

/// Context provided to strategy callbacks
struct StrategyContext {
    const Portfolio& portfolio;
    const IFXRateProvider& fx;
    Timestamp current_time;
    ql::Date current_date;

    // Order submission (goes through risk first)
    std::function<OrderId(Order)> submit_order;
    std::function<bool(OrderId)> cancel_order;
    std::function<vector<Order>()> get_open_orders;
};

// ============================================================================
// Strategy Interface
// ============================================================================

/// Abstract strategy interface - users implement this
class IStrategy {
public:
    virtual ~IStrategy() = default;

    // Identity
    [[nodiscard]] virtual auto name() const -> string = 0;
    [[nodiscard]] virtual auto id() const -> string = 0;
    [[nodiscard]] virtual auto parameters() const -> map<string, string> { return {}; }

    // Lifecycle
    virtual void on_init(StrategyContext& /*ctx*/) {}
    virtual void on_start(StrategyContext& /*ctx*/) {}
    virtual void on_finish(StrategyContext& /*ctx*/) {}

    // Market data - on_bar is required
    virtual void on_bar(const BarEvent& bar, StrategyContext& ctx) = 0;
    virtual void on_tick(const TickEvent& /*tick*/, StrategyContext& /*ctx*/) {}

    // Order events
    virtual void on_fill(const Fill& /*fill*/, StrategyContext& /*ctx*/) {}
    virtual void on_order_rejected(const Order& /*order*/, const string& /*reason*/,
                                   StrategyContext& /*ctx*/) {}
    virtual void on_order_cancelled(const Order& /*order*/, const string& /*reason*/,
                                    StrategyContext& /*ctx*/) {}

    // Risk events
    virtual void on_risk_breach(const RiskBreachEvent& /*event*/, StrategyContext& /*ctx*/) {}
    virtual void on_risk_action(const RiskActionEvent& /*event*/, StrategyContext& /*ctx*/) {}

    // Time events
    virtual void on_trading_day_start(const ql::Date& /*date*/, StrategyContext& /*ctx*/) {}
    virtual void on_trading_day_end(const ql::Date& /*date*/, StrategyContext& /*ctx*/) {}
};

// ============================================================================
// Example Strategy: Buy and Hold
// ============================================================================

/// Simple buy-and-hold strategy for testing
class BuyAndHoldStrategy : public IStrategy {
public:
    explicit BuyAndHoldStrategy(string symbol, double target_weight = 1.0)
        : symbol_(std::move(symbol)), target_weight_(target_weight) {}

    [[nodiscard]] auto name() const -> string override { return "BuyAndHold"; }

    [[nodiscard]] auto id() const -> string override { return "buy_and_hold_" + symbol_; }

    [[nodiscard]] auto parameters() const -> map<string, string> override {
        return {{"symbol", symbol_}, {"target_weight", std::to_string(target_weight_)}};
    }

    void on_start(StrategyContext& /*ctx*/) override {
        // Buy on first bar
        first_bar_ = true;
    }

    void on_bar(const BarEvent& bar, StrategyContext& ctx) override {
        if (bar.symbol != symbol_)
            return;

        if (first_bar_) {
            first_bar_ = false;

            // Calculate position size based on target weight
            double nav = ctx.portfolio.nav(ctx.fx);
            double target_value = nav * target_weight_;
            double shares = std::floor(target_value / bar.close);

            if (shares > 0) {
                auto order = finkit::trading::make_market_order(
                    symbol_, finkit::trading::OrderSide::Buy, shares, id());
                ctx.submit_order(order);
            }
        }
    }

private:
    string symbol_;
    double target_weight_;
    bool first_bar_{false};
};

// ============================================================================
// Example Strategy: Moving Average Crossover
// ============================================================================

/// Simple moving average crossover strategy
class MACrossoverStrategy : public IStrategy {
public:
    MACrossoverStrategy(string symbol, int fast_period, int slow_period)
        : symbol_(std::move(symbol)), fast_period_(fast_period), slow_period_(slow_period) {}

    [[nodiscard]] auto name() const -> string override { return "MACrossover"; }

    [[nodiscard]] auto id() const -> string override { return "ma_crossover_" + symbol_; }

    void on_bar(const BarEvent& bar, StrategyContext& ctx) override {
        if (bar.symbol != symbol_)
            return;

        prices_.push_back(bar.close);

        // Need enough data for slow MA
        if (prices_.size() < static_cast<size_t>(slow_period_))
            return;

        double fast_ma = calculate_ma(fast_period_);
        double slow_ma = calculate_ma(slow_period_);

        auto pos = ctx.portfolio.position(symbol_);
        double current_qty = pos ? pos->quantity : 0.0;

        // Cross above: buy signal
        if (fast_ma > slow_ma && !is_long_) {
            // Close short if any
            if (current_qty < 0) {
                auto cover = finkit::trading::make_market_order(
                    symbol_, finkit::trading::OrderSide::Buy, std::abs(current_qty), id());
                ctx.submit_order(cover);
            }

            // Go long
            double nav = ctx.portfolio.nav(ctx.fx);
            double shares = std::floor(nav * 0.5 / bar.close);
            if (shares > 0) {
                auto buy = finkit::trading::make_market_order(
                    symbol_, finkit::trading::OrderSide::Buy, shares, id());
                ctx.submit_order(buy);
            }
            is_long_ = true;
        }
        // Cross below: sell signal
        else if (fast_ma < slow_ma && is_long_) {
            // Close long if any
            if (current_qty > 0) {
                auto sell = finkit::trading::make_market_order(
                    symbol_, finkit::trading::OrderSide::Sell, current_qty, id());
                ctx.submit_order(sell);
            }
            is_long_ = false;
        }
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

} // namespace finkit::backtest
