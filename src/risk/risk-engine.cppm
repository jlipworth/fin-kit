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
using finkit::types::currency_to_string;

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

    /// Re-arm after a completed liquidation: clears the liquidation state and
    /// any symbol locks. On the interface so callers that own the engine via
    /// IRiskEngine (e.g. the backtest engine) can actually reach it — without
    /// this, locks leak for the remainder of the run. Note: if the portfolio's
    /// high-water mark still implies a breached drawdown, the next monitor()
    /// re-triggers liquidation.
    virtual void reset_liquidation() {}
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
            // "Reducing" means moving |position| toward zero WITHOUT crossing:
            // the order must be opposite-side and no larger than the open
            // quantity (a buy of 1000 against a 10-share short is not reducing,
            // it flips the book long).
            bool is_reducing =
                pos.has_value() && ((pos->quantity > 0 && order.side == OrderSide::Sell) ||
                                    (pos->quantity < 0 && order.side == OrderSide::Buy)) &&
                order.quantity <= std::abs(pos->quantity) + 1e-9;

            if (!is_reducing) {
                result.decision = RiskDecision::Reject;
                result.reason = "Portfolio in liquidation mode - only reducing orders allowed";
                result.conflicts_with_liquidation = true;
                return result;
            }
        }

        // The engine's own liquidation orders are tagged at generation time.
        // They must bypass the symbol locks (which exist to keep OTHER order
        // flow away from positions being liquidated) and the remaining limit
        // checks — otherwise forced liquidation deadlocks on its own lock and
        // can never execute.
        if (auto md = order.metadata.find("source");
            md != order.metadata.end() && md->second == "risk_liquidation") {
            return result; // Allow
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

        // Check liquidity limits (ADV-based sizing)
        auto liq_result = check_liquidity_limits(order, portfolio);
        if (liq_result.decision != RiskDecision::Allow) {
            return liq_result;
        }

        // Check concentration limits (sector / currency gross exposure)
        auto conc_result = check_concentration_limits(order, portfolio, fx);
        if (conc_result.decision != RiskDecision::Allow) {
            return conc_result;
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

        // Check drawdown limit. NOTE: nav <= 0 must NOT short-circuit here —
        // a wiped-out (or negative) NAV is a drawdown of >= 100% and is exactly
        // when liquidation matters most. The state latch below ensures the
        // Liquidate event fires once per breach, not on every monitor() call.
        double hwm = portfolio.high_water_mark();
        if (hwm > 0 && state_ != PortfolioState::Liquidating) {
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
    void reset_liquidation() override {
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

            // Adjusted quantity for Reduce. The headroom is SIGNED: a buy may
            // add up to (max - current), a sell up to (max + current) — e.g. a
            // 50-share short with max 100 leaves a buy headroom of 150 (final
            // position +100), not 50.
            double max_change = (order.side == OrderSide::Buy)
                                    ? limits.max_quantity - current_qty
                                    : limits.max_quantity + current_qty;
            if (max_change > 0) {
                result.decision = RiskDecision::Reduce;
                result.adjusted_quantity = max_change;
            }
        }

        // Check concentration limit
        if (limits.max_concentration_pct < 1.0) {
            double nav = portfolio.nav(fx);
            if (nav > 0) {
                double price = mark_price(order, current_pos);
                if (price > 0) {
                    // NAV is base-currency (USD): the position notional must be
                    // FX-converted before dividing, or non-USD instruments are
                    // measured on the wrong scale.
                    Currency ccy = instrument_currency(order.symbol, current_pos);
                    double new_notional =
                        fx.convert(std::abs(new_qty) * price, ccy, Currency::USD);
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

    /// Best available mark for pre-trade projections: the position's market
    /// price when a position exists, else the order's own limit price. Market
    /// orders in a brand-new symbol still have no price (returns 0), in which
    /// case notional-based checks cannot be evaluated and are skipped.
    [[nodiscard]] static auto mark_price(const Order& order,
                                         const optional<Position>& pos) -> double {
        if (pos && pos->market_price > 0.0) {
            return pos->market_price;
        }
        return order.limit_price.value_or(0.0);
    }

    /// Denomination currency for a symbol: instrument reference data when
    /// available, else the live position's currency, else USD.
    [[nodiscard]] auto instrument_currency(const string& symbol,
                                           const optional<Position>& pos) const -> Currency {
        if (auto it = config_.instrument_info.find(symbol); it != config_.instrument_info.end()) {
            return it->second.currency;
        }
        return pos ? pos->currency : Currency::USD;
    }

    [[nodiscard]] auto check_portfolio_limits(const Order& order, const IPortfolio& portfolio,
                                              const IFXRateProvider& fx) -> RiskCheckResult {
        RiskCheckResult result;

        auto metrics = get_metrics(portfolio, fx);
        double nav = portfolio.nav(fx);

        // Estimate the order's impact as the SIGNED change in |position|
        // notional, FX-converted to the base currency (metrics.gross_exposure
        // is USD). Adding the raw order notional was doubly wrong: sells that
        // de-risk the book inflated projected gross, and non-USD notionals
        // were mixed into a USD aggregate unconverted.
        auto current_pos = portfolio.position(order.symbol);
        double price = mark_price(order, current_pos);
        double current_qty = current_pos ? current_pos->quantity : 0.0;
        double new_qty =
            current_qty + (order.side == OrderSide::Buy ? order.quantity : -order.quantity);
        Currency ccy = instrument_currency(order.symbol, current_pos);
        double exposure_delta =
            fx.convert((std::abs(new_qty) - std::abs(current_qty)) * price, ccy, Currency::USD);

        // Check gross exposure
        if (config_.limits.portfolio.max_gross_exposure > 0) {
            double new_gross = metrics.gross_exposure + exposure_delta;
            if (new_gross > config_.limits.portfolio.max_gross_exposure) {
                result.decision = RiskDecision::Reject;
                result.reason = "Exceeds max gross exposure";
                result.violated_limits.push_back("portfolio.max_gross_exposure");
            }
        }

        // Check leverage
        if (config_.limits.portfolio.max_leverage > 0 && nav > 0) {
            double new_leverage = (metrics.gross_exposure + exposure_delta) / nav;
            if (new_leverage > config_.limits.portfolio.max_leverage) {
                result.decision = RiskDecision::Reject;
                result.reason = "Exceeds max leverage";
                result.violated_limits.push_back("portfolio.max_leverage");
            }
        }

        return result;
    }

    [[nodiscard]] auto lookup_info(const string& symbol) const -> InstrumentRiskInfo {
        if (auto it = config_.instrument_info.find(symbol);
            it != config_.instrument_info.end()) {
            return it->second;
        }
        return InstrumentRiskInfo{};
    }

    [[nodiscard]] auto check_liquidity_limits(const Order& order,
                                              const IPortfolio& portfolio) -> RiskCheckResult {
        RiskCheckResult result;

        const auto& lim = config_.limits.liquidity;
        if (lim.max_adv_pct <= 0.0 && lim.max_order_adv_pct <= 0.0) {
            return result;
        }
        double adv = lookup_info(order.symbol).adv;
        if (adv <= 0.0) {
            return result; // unknown liquidity -> not enforced
        }

        auto current_pos = portfolio.position(order.symbol);
        double current_qty = current_pos ? current_pos->quantity : 0.0;
        double new_qty =
            current_qty + (order.side == OrderSide::Buy ? order.quantity : -order.quantity);
        double allowed = order.quantity;
        vector<string> violated;

        // (a) single-order participation cap (applies to buys AND sells - market impact)
        if (lim.max_order_adv_pct > 0.0) {
            double order_cap = lim.max_order_adv_pct * adv;
            if (order.quantity > order_cap) {
                violated.push_back("liquidity.max_order_adv_pct");
                allowed = std::min(allowed, order_cap);
            }
        }

        // (b) resulting-position cap; skipped when the order reduces |position|
        if (lim.max_adv_pct > 0.0 && std::abs(new_qty) > std::abs(current_qty)) {
            double pos_cap = lim.max_adv_pct * adv;
            if (std::abs(new_qty) > pos_cap) {
                violated.push_back("liquidity.max_adv_pct");
                double headroom = pos_cap - std::abs(current_qty);
                allowed = std::min(allowed, std::max(headroom, 0.0));
            }
        }

        if (violated.empty()) {
            return result;
        }
        result.violated_limits = violated;
        if (allowed > 0.0) {
            result.decision = RiskDecision::Reduce;
            result.adjusted_quantity = allowed;
            result.reason = "Order reduced to satisfy liquidity limits";
        } else {
            result.decision = RiskDecision::Reject;
            result.reason = "Exceeds liquidity (ADV) limits";
        }
        return result;
    }

    [[nodiscard]] auto check_concentration_limits(const Order& order, const IPortfolio& portfolio,
                                                  const IFXRateProvider& fx) -> RiskCheckResult {
        RiskCheckResult result;

        const auto& cl = config_.limits.concentration;
        bool sector_enforced =
            !cl.max_sector_pct.empty() || cl.default_max_sector_pct < 1.0;
        bool currency_enforced =
            !cl.max_currency_pct.empty() || cl.default_max_currency_pct < 1.0;
        if (!sector_enforced && !currency_enforced) {
            return result;
        }

        double nav = portfolio.nav(fx);
        if (nav <= 0.0) {
            return result;
        }
        Currency base = Currency::USD;

        auto current_pos = portfolio.position(order.symbol);
        double price = current_pos ? current_pos->market_price : 0.0;
        if (price <= 0.0) {
            return result; // no mark yet -> cannot evaluate
        }
        double current_qty = current_pos ? current_pos->quantity : 0.0;
        double new_qty =
            current_qty + (order.side == OrderSide::Buy ? order.quantity : -order.quantity);
        if (std::abs(new_qty) <= std::abs(current_qty)) {
            return result; // never block de-risking
        }

        InstrumentRiskInfo info_o = lookup_info(order.symbol);
        Currency ccy_o = config_.instrument_info.count(order.symbol) > 0
                             ? info_o.currency
                             : (current_pos ? current_pos->currency : Currency::USD);
        double new_sym_notional = fx.convert(std::abs(new_qty) * price, ccy_o, base);

        // --- sector ---
        if (sector_enforced) {
            const string& sector = info_o.sector; // may be ""
            double limit = cl.max_sector_pct.count(sector) > 0 ? cl.max_sector_pct.at(sector)
                                                               : cl.default_max_sector_pct;
            if (limit < 1.0) {
                double gross = new_sym_notional;
                for (const auto& [sym, p] : portfolio.positions()) {
                    if (sym == order.symbol || p.is_flat()) {
                        continue;
                    }
                    if (lookup_info(sym).sector == sector) {
                        gross += fx.convert(p.notional(), p.currency, base);
                    }
                }
                if (gross / nav > limit) {
                    result.decision = RiskDecision::Reject;
                    result.reason = "Exceeds sector concentration limit";
                    result.violated_limits.push_back(
                        "concentration.sector." + (sector.empty() ? string("UNCLASSIFIED") : sector));
                    return result;
                }
            }
        }

        // --- currency --- (same shape; keyed on denomination currency)
        if (currency_enforced) {
            double limit = cl.max_currency_pct.count(ccy_o) > 0 ? cl.max_currency_pct.at(ccy_o)
                                                                : cl.default_max_currency_pct;
            if (limit < 1.0) {
                double gross = new_sym_notional;
                for (const auto& [sym, p] : portfolio.positions()) {
                    if (sym == order.symbol || p.is_flat()) {
                        continue;
                    }
                    Currency p_ccy = config_.instrument_info.count(sym) > 0
                                         ? lookup_info(sym).currency
                                         : p.currency;
                    if (p_ccy == ccy_o) {
                        gross += fx.convert(p.notional(), p.currency, base);
                    }
                }
                if (gross / nav > limit) {
                    result.decision = RiskDecision::Reject;
                    result.reason = "Exceeds currency concentration limit";
                    result.violated_limits.push_back("concentration.currency." +
                                                     currency_to_string(ccy_o));
                    return result;
                }
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
