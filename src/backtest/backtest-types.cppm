/// @file backtest-types.cppm
/// @brief Backtest types - configuration, results, portfolio
///
/// Core types for backtest simulation configuration and output.

module;

#include <chrono>
#include <cmath>
#include <map>
#include <optional>
#include <ql/quantlib.hpp>
#include <string>
#include <vector>

export module finkit.backtest:types;

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
using finkit::risk::RiskConfig;
using finkit::trading::Fill;
using finkit::trading::Order;
using finkit::trading::Position;
using finkit::trading::Timestamp;
using finkit::types::Currency;

// ============================================================================
// Backtest Configuration
// ============================================================================

struct BacktestConfig {
    ql::Date start_date;
    ql::Date end_date;
    double initial_capital{1000000.0};
    Currency base_currency{Currency::USD};
    map<Currency, double> initial_cash; // Multi-currency starting cash

    // Execution config
    double slippage_bps{0.5};
    double commission_per_share{0.0};
    double commission_pct{0.0};

    // Risk config
    RiskConfig risk;

    // Persistence
    bool persist_trades{true};
    bool persist_equity{true};
    bool persist_positions{true};
    int equity_snapshot_frequency_bars{1};
};

// ============================================================================
// Backtest Results
// ============================================================================

struct BacktestResult {
    string run_id;
    ql::Date start_date;
    ql::Date end_date;

    // Returns
    double total_return_pct{0.0};
    double annualized_return_pct{0.0};
    double cagr{0.0};

    // Risk-adjusted
    double sharpe_ratio{0.0};
    double sortino_ratio{0.0};
    double calmar_ratio{0.0};

    // Drawdown
    double max_drawdown_pct{0.0};
    double max_drawdown_duration_days{0.0};
    double avg_drawdown_pct{0.0};

    // Trade statistics
    int total_trades{0};
    int winning_trades{0};
    int losing_trades{0};
    double win_rate{0.0};
    double profit_factor{0.0};
    double avg_trade_pnl{0.0};
    double avg_winner{0.0};
    double avg_loser{0.0};
    double largest_winner{0.0};
    double largest_loser{0.0};
    double avg_holding_period_days{0.0};

    // Risk statistics
    int risk_breaches{0};
    int orders_rejected{0};
    int forced_liquidations{0};

    // Final state
    double final_nav{0.0};
};

// ============================================================================
// Portfolio Implementation
// ============================================================================

class Portfolio : public finkit::risk::IPortfolio {
public:
    explicit Portfolio(Currency base_ccy = Currency::USD) : base_currency_(base_ccy) {}

    // Cash management
    void deposit(double amount, Currency ccy = Currency::USD) { cash_[ccy] += amount; }

    void withdraw(double amount, Currency ccy = Currency::USD) { cash_[ccy] -= amount; }

    [[nodiscard]] auto cash(Currency ccy) const -> double override {
        auto it = cash_.find(ccy);
        return it != cash_.end() ? it->second : 0.0;
    }

    [[nodiscard]] auto cash_by_currency() const -> const map<Currency, double>& { return cash_; }

    // Position management
    [[nodiscard]] auto position(const string& symbol) const -> optional<Position> override {
        auto it = positions_.find(symbol);
        return it != positions_.end() ? optional{it->second} : std::nullopt;
    }

    [[nodiscard]] auto positions() const -> const map<string, Position>& override {
        return positions_;
    }

    void mark_to_market(const string& symbol, double price, Timestamp ts) {
        if (auto it = positions_.find(symbol); it != positions_.end()) {
            it->second.market_price = price;
            it->second.last_updated = ts;
        }
    }

    void apply_fill(const Fill& fill, Currency instrument_ccy = Currency::USD) {
        auto& pos = positions_[fill.symbol];
        pos.symbol = fill.symbol;
        pos.currency = instrument_ccy;

        double fill_value = fill.price * fill.quantity;

        if (fill.side == finkit::trading::OrderSide::Buy) {
            // Buying: increase position, decrease cash
            double old_qty = pos.quantity;
            double old_cost = pos.avg_cost * old_qty;

            pos.quantity += fill.quantity;
            if (std::abs(pos.quantity) > 1e-10) {
                pos.avg_cost = (old_cost + fill_value) / pos.quantity;
            }

            cash_[instrument_ccy] -= fill_value + fill.commission;
        } else {
            // Selling: decrease position, increase cash
            // Calculate realized P&L
            double realized = (fill.price - pos.avg_cost) * fill.quantity;
            pos.realized_pnl += realized;

            pos.quantity -= fill.quantity;
            cash_[instrument_ccy] += fill_value - fill.commission;

            // If flat, reset avg cost
            if (std::abs(pos.quantity) < 1e-10) {
                pos.quantity = 0.0;
                pos.avg_cost = 0.0;
            }
        }

        pos.market_price = fill.price;
        pos.last_updated = fill.fill_time;
    }

    // Aggregates
    [[nodiscard]] auto nav(const IFXRateProvider& fx) const -> double override {
        double total = 0.0;

        // Sum cash in base currency
        for (const auto& [ccy, amt] : cash_) {
            total += fx.convert(amt, ccy, base_currency_);
        }

        // Sum position values in base currency
        for (const auto& [symbol, pos] : positions_) {
            double pos_value = pos.quantity * pos.market_price;
            total += fx.convert(pos_value, pos.currency, base_currency_);
        }

        return total;
    }

    [[nodiscard]] auto gross_exposure(const IFXRateProvider& fx) const -> double {
        double total = 0.0;
        for (const auto& [symbol, pos] : positions_) {
            double notional = pos.notional();
            total += fx.convert(notional, pos.currency, base_currency_);
        }
        return total;
    }

    [[nodiscard]] auto net_exposure(const IFXRateProvider& fx) const -> double {
        double total = 0.0;
        for (const auto& [symbol, pos] : positions_) {
            double signed_notional = pos.quantity * pos.market_price;
            total += fx.convert(signed_notional, pos.currency, base_currency_);
        }
        return std::abs(total);
    }

    // High-water mark
    [[nodiscard]] auto high_water_mark() const -> double override { return high_water_mark_; }

    [[nodiscard]] auto drawdown(const IFXRateProvider& fx) const -> double {
        double current_nav = nav(fx);
        if (high_water_mark_ <= 0)
            return 0.0;
        return (high_water_mark_ - current_nav) / high_water_mark_;
    }

    void update_high_water_mark(double nav_value) {
        if (nav_value > high_water_mark_) {
            high_water_mark_ = nav_value;
        }
    }

private:
    Currency base_currency_;
    map<Currency, double> cash_;
    map<string, Position> positions_;
    double high_water_mark_{0.0};
};

// ============================================================================
// Equity Curve Point
// ============================================================================

struct EquityPoint {
    Timestamp timestamp;
    double nav{0.0};
    double gross_exposure{0.0};
    double net_exposure{0.0};
    double drawdown_pct{0.0};
    double daily_return{0.0};
};

} // namespace finkit::backtest
