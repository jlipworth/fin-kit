/// @file risk-engine.cppm
/// @brief Risk engine interface and standard implementation
///
/// Provides pre-trade approval, post-trade checks, and active monitoring.

module;

#include <algorithm>
#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

export module finkit.risk:engine;

import :types;
import finkit.types;
import finkit.trading;

export namespace finkit::risk {

using std::map;
using std::optional;
using std::set;
using std::string;
using std::unique_ptr;
using std::vector;

using finkit::trading::Fill;
using finkit::trading::Order;
using finkit::trading::OrderSide;
using finkit::trading::Position;
using finkit::trading::Timestamp;
using finkit::types::Currency;

// ============================================================================
// FX Rate Provider Interface (for multi-currency)
// ============================================================================

class IFXRateProvider {
public:
    virtual ~IFXRateProvider() = default;

    [[nodiscard]] virtual auto rate(Currency from, Currency to) const -> double = 0;
    [[nodiscard]] virtual auto convert(double amount, Currency from,
                                       Currency to) const -> double = 0;
};

/// Simple static FX rate provider
class StaticFXProvider : public IFXRateProvider {
public:
    void set_rate(Currency from, Currency to, double rate) {
        rates_[{from, to}] = rate;
        rates_[{to, from}] = 1.0 / rate;
    }

    [[nodiscard]] auto rate(Currency from, Currency to) const -> double override {
        if (from == to)
            return 1.0;
        auto it = rates_.find({from, to});
        return it != rates_.end() ? it->second : 1.0;
    }

    [[nodiscard]] auto convert(double amount, Currency from, Currency to) const -> double override {
        return amount * rate(from, to);
    }

private:
    map<std::pair<Currency, Currency>, double> rates_;
};

// ============================================================================
// Portfolio Interface (for risk calculations)
// ============================================================================

class IPortfolio {
public:
    virtual ~IPortfolio() = default;

    [[nodiscard]] virtual auto positions() const -> const map<string, Position>& = 0;
    [[nodiscard]] virtual auto position(const string& symbol) const -> optional<Position> = 0;
    [[nodiscard]] virtual auto nav(const IFXRateProvider& fx) const -> double = 0;
    [[nodiscard]] virtual auto cash(Currency ccy) const -> double = 0;
    [[nodiscard]] virtual auto high_water_mark() const -> double = 0;
};

// ============================================================================
// Risk Engine Interface
// ============================================================================

class IRiskEngine {
public:
    virtual ~IRiskEngine() = default;

    // Mode 1: Pre/Post Trade Checks (Synchronous)
    [[nodiscard]] virtual auto check_pre_trade(const Order& order, const IPortfolio& portfolio,
                                               const IFXRateProvider& fx) -> RiskCheckResult = 0;

    [[nodiscard]] virtual auto check_post_trade(const Fill& fill, const IPortfolio& portfolio,
                                                const IFXRateProvider& fx) -> RiskCheckResult = 0;

    // Mode 2: Active Monitoring (Asynchronous)
    [[nodiscard]] virtual auto monitor(const IPortfolio& portfolio, const IFXRateProvider& fx,
                                       Timestamp now) -> vector<RiskActionEvent> = 0;

    // Configuration
    virtual void update_limits(const RiskLimits& limits) = 0;
    [[nodiscard]] virtual auto get_limits() const -> const RiskLimits& = 0;

    // Analytics
    [[nodiscard]] virtual auto get_metrics(const IPortfolio& portfolio,
                                           const IFXRateProvider& fx) const -> RiskMetrics = 0;

    // State
    [[nodiscard]] virtual auto get_state() const -> PortfolioState = 0;
    [[nodiscard]] virtual auto is_symbol_locked(const string& symbol) const -> bool = 0;
};

// ============================================================================
// Standard Risk Engine Implementation
// ============================================================================

class StandardRiskEngine : public IRiskEngine {
public:
    explicit StandardRiskEngine(RiskConfig config = {}) : config_(std::move(config)) {}

    [[nodiscard]] auto check_pre_trade(const Order& order, const IPortfolio& portfolio,
                                       const IFXRateProvider& fx) -> RiskCheckResult override {
        RiskCheckResult result;

        // Check if we're in liquidation mode
        if (state_ == PortfolioState::Liquidating) {
            auto pos = portfolio.position(order.symbol);
            bool is_reducing =
                pos.has_value() && ((pos->quantity > 0 && order.side == OrderSide::Sell) ||
                                    (pos->quantity < 0 && order.side == OrderSide::Buy));

            if (!is_reducing) {
                result.decision = RiskDecision::Reject;
                result.reason = "Portfolio in liquidation mode - only reducing orders allowed";
                result.conflicts_with_liquidation = true;
                return result;
            }
        }

        // Check if symbol is locked
        if (locked_symbols_.count(order.symbol) > 0) {
            result.decision = RiskDecision::Reject;
            result.reason = "Symbol " + order.symbol + " is locked for liquidation";
            result.conflicts_with_liquidation = true;
            return result;
        }

        // Check halted state
        if (state_ == PortfolioState::Halted) {
            result.decision = RiskDecision::Reject;
            result.reason = "Trading halted";
            return result;
        }

        // Check position limits
        auto pos_result = check_position_limits(order, portfolio, fx);
        if (pos_result.decision != RiskDecision::Allow) {
            return pos_result;
        }

        // Check portfolio limits
        auto port_result = check_portfolio_limits(order, portfolio, fx);
        if (port_result.decision != RiskDecision::Allow) {
            return port_result;
        }

        return result; // Allow by default
    }

    [[nodiscard]] auto check_post_trade(const Fill& /*fill*/, const IPortfolio& /*portfolio*/,
                                        const IFXRateProvider& /*fx*/
                                        ) -> RiskCheckResult override {
        // Post-trade checks are primarily for logging/alerting
        // The trade has already executed
        RiskCheckResult result;
        return result;
    }

    [[nodiscard]] auto monitor(const IPortfolio& portfolio, const IFXRateProvider& fx,
                               Timestamp now) -> vector<RiskActionEvent> override {
        vector<RiskActionEvent> actions;

        double nav = portfolio.nav(fx);
        if (nav <= 0)
            return actions;

        // Check drawdown limit
        double hwm = portfolio.high_water_mark();
        if (hwm > 0) {
            double drawdown = (hwm - nav) / hwm;

            if (drawdown > config_.limits.pnl.max_drawdown_pct) {
                state_ = PortfolioState::Liquidating;
                active_liquidation_reason_ =
                    "Drawdown " + std::to_string(drawdown * 100.0) + "% exceeds limit " +
                    std::to_string(config_.limits.pnl.max_drawdown_pct * 100.0) + "%";

                RiskActionEvent event;
                event.action = RiskActionEvent::Action::Liquidate;
                event.limit_name = "max_drawdown";
                event.reason = active_liquidation_reason_;
                event.timestamp = now;

                for (const auto& [symbol, pos] : portfolio.positions()) {
                    if (!pos.is_flat()) {
                        locked_symbols_.insert(symbol);
                        event.affected_symbols.push_back(symbol);

                        // Generate liquidation order
                        Order liq_order;
                        liq_order.symbol = symbol;
                        liq_order.side = pos.is_long() ? OrderSide::Sell : OrderSide::Buy;
                        liq_order.quantity = std::abs(pos.quantity);
                        liq_order.metadata["source"] = "risk_liquidation";
                        event.generated_orders.push_back(liq_order);
                    }
                }

                actions.push_back(std::move(event));
            }
        }

        // Check daily loss limit
        // (Would need daily P&L tracking - simplified here)

        return actions;
    }

    void update_limits(const RiskLimits& limits) override { config_.limits = limits; }

    [[nodiscard]] auto get_limits() const -> const RiskLimits& override { return config_.limits; }

    [[nodiscard]] auto get_metrics(const IPortfolio& portfolio,
                                   const IFXRateProvider& fx) const -> RiskMetrics override {
        RiskMetrics metrics;

        double nav = portfolio.nav(fx);
        Currency base = Currency::USD;

        for (const auto& [symbol, pos] : portfolio.positions()) {
            double notional = fx.convert(pos.notional(), pos.currency, base);

            metrics.gross_exposure += notional;
            if (pos.is_long()) {
                metrics.long_exposure += notional;
                metrics.net_exposure += notional;
            } else {
                metrics.short_exposure += notional;
                metrics.net_exposure -= notional;
            }
        }

        metrics.net_exposure = std::abs(metrics.net_exposure);
        metrics.leverage = nav > 0 ? metrics.gross_exposure / nav : 0.0;

        double hwm = portfolio.high_water_mark();
        if (hwm > 0) {
            metrics.drawdown_pct = (hwm - nav) / hwm;
        }

        return metrics;
    }

    [[nodiscard]] auto get_state() const -> PortfolioState override { return state_; }

    [[nodiscard]] auto is_symbol_locked(const string& symbol) const -> bool override {
        return locked_symbols_.count(symbol) > 0;
    }

    // Reset liquidation state (after liquidation completes)
    void reset_liquidation() {
        state_ = PortfolioState::Normal;
        locked_symbols_.clear();
        active_liquidation_reason_.clear();
    }

private:
    [[nodiscard]] auto check_position_limits(const Order& order, const IPortfolio& portfolio,
                                             const IFXRateProvider& fx) -> RiskCheckResult {
        RiskCheckResult result;

        // Get limits for this symbol (or default)
        PositionLimits limits;
        if (auto it = config_.limits.position_limits.find(order.symbol);
            it != config_.limits.position_limits.end()) {
            limits = it->second;
        } else {
            limits = config_.limits.default_position_limits;
        }

        // Get current position
        auto current_pos = portfolio.position(order.symbol);
        double current_qty = current_pos ? current_pos->quantity : 0.0;

        // Calculate new position quantity
        double new_qty = current_qty;
        if (order.side == OrderSide::Buy) {
            new_qty += order.quantity;
        } else {
            new_qty -= order.quantity;
        }

        // Check max quantity
        if (limits.max_quantity > 0 && std::abs(new_qty) > limits.max_quantity) {
            result.decision = RiskDecision::Reject;
            result.reason = "Exceeds max quantity limit";
            result.violated_limits.push_back("position.max_quantity");

            // Calculate adjusted quantity for reduce
            double max_change = limits.max_quantity - std::abs(current_qty);
            if (max_change > 0) {
                result.decision = RiskDecision::Reduce;
                result.adjusted_quantity = max_change;
            }
        }

        // Check concentration limit
        if (limits.max_concentration_pct < 1.0) {
            double nav = portfolio.nav(fx);
            if (nav > 0) {
                double price = current_pos ? current_pos->market_price : 0.0;
                if (price > 0) {
                    double new_notional = std::abs(new_qty) * price;
                    double concentration = new_notional / nav;

                    if (concentration > limits.max_concentration_pct) {
                        result.decision = RiskDecision::Reject;
                        result.reason = "Exceeds concentration limit";
                        result.violated_limits.push_back("position.max_concentration");
                    }
                }
            }
        }

        return result;
    }

    [[nodiscard]] auto check_portfolio_limits(const Order& order, const IPortfolio& portfolio,
                                              const IFXRateProvider& fx) -> RiskCheckResult {
        RiskCheckResult result;

        auto metrics = get_metrics(portfolio, fx);
        double nav = portfolio.nav(fx);

        // Estimate impact of order
        auto current_pos = portfolio.position(order.symbol);
        double price = current_pos ? current_pos->market_price : 0.0;
        double order_notional = order.quantity * price;

        // Check gross exposure
        if (config_.limits.portfolio.max_gross_exposure > 0) {
            double new_gross = metrics.gross_exposure + order_notional;
            if (new_gross > config_.limits.portfolio.max_gross_exposure) {
                result.decision = RiskDecision::Reject;
                result.reason = "Exceeds max gross exposure";
                result.violated_limits.push_back("portfolio.max_gross_exposure");
            }
        }

        // Check leverage
        if (config_.limits.portfolio.max_leverage > 0 && nav > 0) {
            double new_leverage = (metrics.gross_exposure + order_notional) / nav;
            if (new_leverage > config_.limits.portfolio.max_leverage) {
                result.decision = RiskDecision::Reject;
                result.reason = "Exceeds max leverage";
                result.violated_limits.push_back("portfolio.max_leverage");
            }
        }

        return result;
    }

    RiskConfig config_;
    PortfolioState state_{PortfolioState::Normal};
    set<string> locked_symbols_;
    string active_liquidation_reason_;
};

} // namespace finkit::risk
