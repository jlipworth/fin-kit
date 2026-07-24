/// @file backtest-engine.cppm
/// @brief Backtest engine implementation
///
/// The main backtest engine that orchestrates data feed, strategy,
/// execution, and risk management.

module;

#include <chrono>
#include <cmath>
#include <map>
#include <memory>
#include <ql/quantlib.hpp>
#include <set>
#include <span>
#include <string>
#include <uuid/uuid.h>
#include <vector>

export module finkit.backtest:engine;

import :types;
import :strategy;
import finkit.types;
import finkit.trading;
import finkit.risk;
import finkit.stats;

export namespace finkit::backtest {

using std::make_unique;
using std::set;
using std::string;
using std::unique_ptr;
using std::vector;

namespace ql = QuantLib;
namespace stats = finkit::stats;

using finkit::risk::IFXRateProvider;
using finkit::risk::IRiskEngine;
using finkit::risk::RiskDecision;
using finkit::risk::StandardRiskEngine;
using finkit::risk::StaticFXProvider;
using finkit::trading::BacktestExecutionConfig;
using finkit::trading::BacktestExecutionEngine;
using finkit::trading::BarEvent;
using finkit::trading::Fill;
using finkit::trading::Order;
using finkit::trading::OrderId;
using finkit::trading::Timestamp;
using finkit::types::Currency;

// ============================================================================
// Data Feed Interface
// ============================================================================

class IDataFeed {
public:
    virtual ~IDataFeed() = default;

    [[nodiscard]] virtual auto next() -> std::optional<BarEvent> = 0;
    [[nodiscard]] virtual auto has_more() const -> bool = 0;
    [[nodiscard]] virtual auto current_time() const -> Timestamp = 0;
    virtual void reset() = 0;
};

/// Simple in-memory data feed for testing
class InMemoryDataFeed : public IDataFeed {
public:
    void add_bar(const BarEvent& bar) { bars_.push_back(bar); }

    void add_bars(const vector<BarEvent>& bars) {
        bars_.insert(bars_.end(), bars.begin(), bars.end());
    }

    [[nodiscard]] auto next() -> std::optional<BarEvent> override {
        if (index_ >= bars_.size())
            return std::nullopt;
        return bars_[index_++];
    }

    [[nodiscard]] auto has_more() const -> bool override { return index_ < bars_.size(); }

    [[nodiscard]] auto current_time() const -> Timestamp override {
        if (index_ > 0 && index_ <= bars_.size()) {
            return bars_[index_ - 1].timestamp;
        }
        return Timestamp{};
    }

    void reset() override { index_ = 0; }

private:
    vector<BarEvent> bars_;
    size_t index_{0};
};

// ============================================================================
// Backtest Engine
// ============================================================================

class BacktestEngine {
public:
    explicit BacktestEngine(BacktestConfig config)
        : config_(std::move(config)), portfolio_(config_.base_currency),
          fx_provider_(make_unique<StaticFXProvider>()),
          risk_engine_(make_unique<StandardRiskEngine>(config_.risk)),
          execution_engine_(
              BacktestExecutionConfig{.default_slippage_bps = config_.slippage_bps,
                                      .commission_per_share = config_.commission_per_share,
                                      .commission_pct = config_.commission_pct,
                                      .half_spread_bps = config_.half_spread_bps}) {
        // Initialize portfolio with starting capital
        if (config_.initial_cash.empty()) {
            portfolio_.deposit(config_.initial_capital, config_.base_currency);
        } else {
            for (const auto& [ccy, amount] : config_.initial_cash) {
                portfolio_.deposit(amount, ccy);
            }
        }
    }

    // Setup
    void set_data_feed(unique_ptr<IDataFeed> feed) { data_feed_ = std::move(feed); }

    void add_strategy(unique_ptr<IStrategy> strategy) {
        strategies_.push_back(std::move(strategy));
    }

    void set_fx_provider(unique_ptr<IFXRateProvider> fx) { fx_provider_ = std::move(fx); }

    void set_risk_engine(unique_ptr<IRiskEngine> risk) { risk_engine_ = std::move(risk); }

    // Run
    auto run() -> BacktestResult {
        result_ = BacktestResult{};
        result_.run_id = generate_run_id();
        result_.start_date = config_.start_date;
        result_.end_date = config_.end_date;

        // Reset realism state so a re-run of the same engine starts clean.
        last_bar_time_ = Timestamp::min();
        delisted_.clear();
        daily_returns_.clear();

        // Reset metric-accumulation state.
        gross_profit_ = 0.0;
        gross_loss_ = 0.0;
        sum_trade_pnl_ = 0.0;
        holding_periods_days_.clear();
        open_time_.clear();
        dd_sum_ = 0.0;
        dd_count_ = 0;
        peak_time_ = Timestamp{};
        have_peak_time_ = false;
        underwater_ = false;

        if (!data_feed_) {
            return result_;
        }

        // Initialize strategy context
        auto submit_order_fn = [this](Order order) -> OrderId {
            return submit_order(order);
        };
        auto cancel_order_fn = [this](OrderId id) -> bool {
            return execution_engine_.cancel_order(id);
        };
        auto get_open_orders_fn = [this]() -> vector<Order> {
            return execution_engine_.get_open_orders();
        };

        StrategyContext ctx{.portfolio = portfolio_,
                            .fx = *fx_provider_,
                            .current_time = Timestamp{},
                            .current_date = config_.start_date,
                            .submit_order = submit_order_fn,
                            .cancel_order = cancel_order_fn,
                            .get_open_orders = get_open_orders_fn};

        // Call strategy on_start
        for (auto& strategy : strategies_) {
            strategy->on_start(ctx);
        }

        // Initialize high water mark
        double initial_nav = portfolio_.nav(*fx_provider_);
        portfolio_.update_high_water_mark(initial_nav);

        // Daily-return sampling state: prev_nav is the NAV at the last sampled
        // day boundary; day_has_bars marks that at least one bar was processed
        // since, so the day contributes exactly one return sample.
        double prev_nav = initial_nav;
        bool day_has_bars = false;

        // Main loop
        ql::Date prev_date = config_.start_date;

        data_feed_->reset();
        while (data_feed_->has_more()) {
            auto bar_opt = data_feed_->next();
            if (!bar_opt)
                break;

            const BarEvent& bar = *bar_opt;

            // Look-ahead guard: reject bars whose timestamp is before the latest
            // bar time already delivered. Dropped bars never reach the portfolio,
            // execution engine, or strategies.
            if (bar.timestamp < last_bar_time_) {
                result_.out_of_order_bars_dropped++;
                continue;
            }
            last_bar_time_ = bar.timestamp;

            ctx.current_time = bar.timestamp;

            ql::Date current_date = to_ql_date(bar.timestamp);
            ctx.current_date = current_date;

            // Check for new trading day
            if (current_date != prev_date) {
                // Day boundary: the previous day's NAV is final, so record its
                // return. Returns are sampled per CALENDAR DAY (not per bar):
                // sampling every bar of a multi-symbol/intraday feed and then
                // annualizing with sqrt(252) systematically distorted Sharpe.
                // For single-symbol daily feeds this produces exactly the same
                // series as the old per-bar sampling.
                if (day_has_bars) {
                    sample_daily_return(prev_nav);
                    day_has_bars = false;
                }

                // Accrue overnight borrow on short positions before marking the new
                // day, so fees use each short's last-marked (previous-day) value.
                accrue_borrow_costs(prev_date, current_date);
                for (auto& strategy : strategies_) {
                    strategy->on_trading_day_start(current_date, ctx);
                }
                prev_date = current_date;
            }

            // Process delistings whose delist time engine time has now passed.
            process_delistings(bar.timestamp, ctx);

            // Universe filter: drop bars for symbols outside their listing window.
            if (!symbol_active(bar.symbol, bar.timestamp)) {
                result_.inactive_symbol_bars_dropped++;
                continue;
            }

            // Mark portfolio to market
            portfolio_.mark_to_market(bar.symbol, bar.close, bar.timestamp);

            // Process execution engine
            execution_engine_.on_bar(bar);

            // Process fills
            auto fills = execution_engine_.get_pending_fills();
            for (const auto& fill : fills) {
                // Capture pre-fill position state so per-fill realized P&L and the
                // position-lifecycle (open/close) tracking use the entry cost, not the
                // post-fill state (which apply_fill may have reset on a flat close).
                auto pos_before = portfolio_.position(fill.symbol);
                double qty_before = pos_before ? pos_before->quantity : 0.0;
                double avg_before = pos_before ? pos_before->avg_cost : 0.0;
                bool was_flat = std::abs(qty_before) < 1e-10;

                portfolio_.apply_fill(fill, instrument_currency(fill.symbol));
                result_.total_trades++;

                double pnl = realized_pnl(fill, qty_before, avg_before);
                record_trade_pnl(pnl);

                auto pos_after = portfolio_.position(fill.symbol);
                bool is_flat = !pos_after || pos_after->is_flat();
                track_position_lifecycle(fill.symbol, was_flat, is_flat, fill.fill_time);

                for (auto& strategy : strategies_) {
                    strategy->on_fill(fill, ctx);
                }
            }

            // Active risk monitoring
            auto risk_actions = risk_engine_->monitor(portfolio_, *fx_provider_, bar.timestamp);
            // Each action event emitted by active monitoring is a limit breach the
            // engine had to respond to (distinct from `orders_rejected`, which counts
            // pre-trade rejections).
            result_.risk_breaches += static_cast<int>(risk_actions.size());
            for (const auto& action : risk_actions) {
                for (auto& strategy : strategies_) {
                    strategy->on_risk_action(action, ctx);
                }

                if (action.action == finkit::risk::RiskActionEvent::Action::Liquidate) {
                    result_.forced_liquidations++;
                    for (const auto& order : action.generated_orders) {
                        submit_order(order);
                    }
                }
            }

            // Call strategy on_bar
            for (auto& strategy : strategies_) {
                strategy->on_bar(bar, ctx);
            }

            // Update equity curve
            double current_nav = portfolio_.nav(*fx_provider_);

            // Drawdown-duration tracking (peak-to-recovery span, in days). Capture the
            // high-water mark BEFORE it absorbs the current bar so we can detect whether
            // this bar is at/above the prior peak (recovery / new high) or below it
            // (underwater). The longest peak-to-recovery span is the max drawdown
            // duration; an unrecovered drawdown contributes its peak-to-final span.
            double prev_hwm = portfolio_.high_water_mark();
            if (!have_peak_time_) {
                peak_time_ = bar.timestamp;
                have_peak_time_ = true;
            }
            if (current_nav >= prev_hwm) {
                if (underwater_) {
                    result_.max_drawdown_duration_days = std::max(
                        result_.max_drawdown_duration_days, days_between(peak_time_, bar.timestamp));
                    underwater_ = false;
                }
                peak_time_ = bar.timestamp; // at/above prior peak: advance the peak time
            } else {
                underwater_ = true;
                result_.max_drawdown_duration_days = std::max(
                    result_.max_drawdown_duration_days, days_between(peak_time_, bar.timestamp));
            }

            portfolio_.update_high_water_mark(current_nav);

            double drawdown = portfolio_.drawdown(*fx_provider_);
            result_.max_drawdown_pct = std::max(result_.max_drawdown_pct, drawdown);
            // Time-averaged drawdown: mean of the per-bar drawdown series.
            dd_sum_ += drawdown;
            ++dd_count_;

            // This bar belongs to the current (still-open) day; its return is
            // sampled at the next day boundary (or the end-of-run flush).
            day_has_bars = true;
        }

        // Flush the final (never-closed) day's return.
        if (day_has_bars) {
            sample_daily_return(prev_nav);
        }

        // Call strategy on_finish
        for (auto& strategy : strategies_) {
            strategy->on_finish(ctx);
        }

        // Calculate final statistics
        result_.final_nav = portfolio_.nav(*fx_provider_);
        result_.total_return_pct = (result_.final_nav - initial_nav) / initial_nav;

        // ------------------------------------------------------------------
        // Trade statistics
        // ------------------------------------------------------------------
        if (result_.total_trades > 0) {
            result_.win_rate = static_cast<double>(result_.winning_trades) /
                               static_cast<double>(result_.total_trades);
            // Mean per-fill realized P&L. Opening fills contribute 0, matching the
            // realized-P&L convention used for the winner/loser classification.
            result_.avg_trade_pnl =
                sum_trade_pnl_ / static_cast<double>(result_.total_trades);
        }
        if (result_.winning_trades > 0) {
            result_.avg_winner = gross_profit_ / static_cast<double>(result_.winning_trades);
        }
        if (result_.losing_trades > 0) {
            // Stored as a negative number (loss), mirroring largest_loser.
            result_.avg_loser = -gross_loss_ / static_cast<double>(result_.losing_trades);
        }
        // Profit factor = gross profit / gross loss. Left at 0.0 when there are no
        // losing trades (ratio is otherwise undefined / infinite).
        if (gross_loss_ > 1e-10) {
            result_.profit_factor = gross_profit_ / gross_loss_;
        }
        // Average holding period over completed round trips (position flat -> non-flat
        // -> flat). 0.0 when no round trip completed (e.g. a position still open at the
        // end of the run).
        if (!holding_periods_days_.empty()) {
            double sum = 0.0;
            for (double d : holding_periods_days_) {
                sum += d;
            }
            result_.avg_holding_period_days =
                sum / static_cast<double>(holding_periods_days_.size());
        }

        // Average drawdown: time-weighted mean of the per-bar drawdown series.
        if (dd_count_ > 0) {
            result_.avg_drawdown_pct = dd_sum_ / static_cast<double>(dd_count_);
        }

        // ------------------------------------------------------------------
        // Return-based risk metrics (finkit.stats; daily-bar convention, 252/yr).
        // The daily-return series is the same one used for Sharpe, so every ratio
        // shares the engine's leading-zero first-bar return and per-bar sampling.
        // ------------------------------------------------------------------
        const std::span<const double> returns_span{daily_returns_};
        // Sharpe: sample std dev (n-1), annualized sqrt(252) — identical convention to
        // the previous inline computation, so pinned expected values are preserved.
        result_.sharpe_ratio = stats::sharpe_ratio(returns_span);
        // Sortino: downside deviation (population divisor n over all obs), target 0.
        // Returns 0 when there are fewer than 2 sub-target observations.
        result_.sortino_ratio = stats::sortino_ratio(returns_span);
        // Calmar: geometric annualized return / max drawdown, both derived from the
        // return series (max drawdown here is the return-series drawdown, which may
        // differ marginally from the NAV-based max_drawdown_pct above).
        result_.calmar_ratio = stats::calmar_ratio(returns_span);

        if (daily_returns_.size() >= 2) {
            // CAGR: geometric compound annual growth rate (years = n / 252).
            auto metrics = stats::calculate_return_metrics(returns_span);
            result_.cagr = metrics.annualized_return;
            // Annualized return (arithmetic): mean per-period return * periods/year.
            // Deliberately distinct from `cagr` (geometric) so the two fields carry
            // complementary information; stats owns the non-trivial statistics.
            double mean_return = 0.0;
            for (double r : daily_returns_) {
                mean_return += r;
            }
            mean_return /= static_cast<double>(daily_returns_.size());
            result_.annualized_return_pct = mean_return * 252.0;
        }

        return result_;
    }

    [[nodiscard]] auto get_portfolio() const -> const Portfolio& { return portfolio_; }

private:
    auto submit_order(Order order) -> OrderId {
        // Universe gate: reject orders for symbols outside their listing window.
        if (!symbol_active(order.symbol, last_bar_time_)) {
            result_.orders_rejected++;
            order.status = finkit::trading::OrderStatus::Rejected;
            order.rejection_reason = "symbol not tradeable: outside listing window";
            return OrderId{0};
        }

        // Hard-to-borrow gate: reject sells that would create or increase a short.
        if (order.side == finkit::trading::OrderSide::Sell) {
            auto it = config_.borrow.find(order.symbol);
            if (it != config_.borrow.end() && it->second.hard_to_borrow) {
                auto pos = portfolio_.position(order.symbol);
                double current_qty = pos ? pos->quantity : 0.0;
                // Account for outstanding working sells: without this, several
                // sells submitted before any fills would each pass individually
                // and together push the position short.
                double pending_sells = 0.0;
                for (const auto& open : execution_engine_.get_open_orders(order.symbol)) {
                    if (open.side == finkit::trading::OrderSide::Sell) {
                        pending_sells += open.quantity - open.filled_quantity;
                    }
                }
                if (current_qty - pending_sells - order.quantity < -1e-10) {
                    // resulting qty would be short
                    result_.orders_rejected++;
                    order.status = finkit::trading::OrderStatus::Rejected;
                    order.rejection_reason = "hard to borrow";
                    return OrderId{0};
                }
            }
        }

        // Pre-trade risk check
        auto risk_result = risk_engine_->check_pre_trade(order, portfolio_, *fx_provider_);

        if (risk_result.decision == RiskDecision::Reject) {
            result_.orders_rejected++;
            order.status = finkit::trading::OrderStatus::Rejected;
            order.rejection_reason = risk_result.reason;
            return OrderId{0};
        }

        if (risk_result.decision == RiskDecision::Reduce && risk_result.adjusted_quantity) {
            order.quantity = *risk_result.adjusted_quantity;
        }

        return execution_engine_.submit_order(order);
    }

    /// Sample one daily return against the last day-boundary NAV and advance the
    /// boundary. Guards against a zero/negative base (e.g. initial_capital = 0):
    /// a return is only well-defined for a positive base, otherwise 0 is recorded
    /// instead of letting inf/NaN poison the Sharpe/Sortino inputs.
    void sample_daily_return(double& prev_nav) {
        double current_nav = portfolio_.nav(*fx_provider_);
        double ret = (prev_nav > 1e-12 || prev_nav < -1e-12)
                         ? (current_nav - prev_nav) / prev_nav
                         : 0.0;
        daily_returns_.push_back(ret);
        prev_nav = current_nav;
    }

    /// Denomination currency of an instrument: risk reference data when present
    /// (config_.risk.instrument_info), else the backtest base currency. Threaded
    /// into Portfolio::apply_fill so non-base-currency instruments move the right
    /// cash bucket and are FX-converted in NAV (previously hardcoded to USD).
    [[nodiscard]] auto instrument_currency(const string& symbol) const -> Currency {
        if (auto it = config_.risk.instrument_info.find(symbol);
            it != config_.risk.instrument_info.end()) {
            return it->second.currency;
        }
        return config_.base_currency;
    }

    /// Per-fill realized P&L against the pre-fill position, matching the realized-P&L
    /// bookkeeping in Portfolio::apply_fill: a sell realizes on the portion of a prior
    /// long it closes; a buy realizes on the portion of a prior short it covers; fills
    /// that only open/increase an exposure realize nothing.
    [[nodiscard]] static auto realized_pnl(const Fill& fill, double qty_before,
                                           double avg_before) -> double {
        if (fill.side == finkit::trading::OrderSide::Sell) {
            if (qty_before > 1e-10) { // closing (part of) a long
                double closed = std::min(fill.quantity, qty_before);
                return (fill.price - avg_before) * closed;
            }
            return 0.0; // opening/increasing a short
        }
        if (qty_before < -1e-10) { // buy covering (part of) a short
            double covered = std::min(fill.quantity, -qty_before);
            return (avg_before - fill.price) * covered;
        }
        return 0.0; // opening/increasing a long
    }

    /// Fold one fill's realized P&L into the trade-statistics accumulators and the
    /// winner/loser classification. Called for every fill (ordinary and forced).
    void record_trade_pnl(double pnl) {
        sum_trade_pnl_ += pnl;
        if (pnl > 0) {
            result_.winning_trades++;
            result_.largest_winner = std::max(result_.largest_winner, pnl);
            gross_profit_ += pnl;
        } else if (pnl < 0) {
            result_.losing_trades++;
            result_.largest_loser = std::min(result_.largest_loser, pnl);
            gross_loss_ += -pnl;
        }
    }

    /// Track a symbol's position open/close transitions to accumulate holding periods.
    /// A holding period is recorded when a position returns to flat; the entry time is
    /// the fill that first took it away from flat. A sign flip (long<->short without
    /// passing through flat) keeps the original entry time (treated as one continuous
    /// holding), which is an intentional simplification.
    void track_position_lifecycle(const string& symbol, bool was_flat, bool is_flat,
                                  Timestamp fill_time) {
        if (was_flat && !is_flat) {
            open_time_[symbol] = fill_time;
        } else if (!was_flat && is_flat) {
            if (auto it = open_time_.find(symbol); it != open_time_.end()) {
                holding_periods_days_.push_back(days_between(it->second, fill_time));
                open_time_.erase(it);
            }
        }
    }

    /// Whole-and-fractional calendar days between two timestamps (t1 <= t2).
    [[nodiscard]] static auto days_between(Timestamp t1, Timestamp t2) -> double {
        return std::chrono::duration<double, std::ratio<86400>>(t2 - t1).count();
    }

    /// Whether a symbol is tradeable at engine time `ts` given its listing window.
    /// Listing boundary is inclusive (ts >= listed_from active); delisting boundary
    /// is exclusive (ts >= delisted_at inactive). Symbols with no window are always active.
    [[nodiscard]] auto symbol_active(const string& symbol, Timestamp ts) const -> bool {
        auto it = config_.listing_windows.find(symbol);
        if (it == config_.listing_windows.end())
            return true; // no window configured => always active
        const auto& w = it->second;
        if (w.listed_from && ts < *w.listed_from)
            return false;
        if (w.delisted_at && ts >= *w.delisted_at) // delist boundary is EXCLUSIVE:
            return false;                          // a bar exactly at delisted_at is dropped
        return true;
    }

    /// Force-liquidate and record any symbols whose delist time engine time has passed.
    /// Runs on every surviving bar; one event per symbol per run (guarded by delisted_).
    void process_delistings(Timestamp now, StrategyContext& ctx) {
        for (const auto& [symbol, window] : config_.listing_windows) {
            if (!window.delisted_at || now < *window.delisted_at || delisted_.contains(symbol))
                continue;
            delisted_.insert(symbol);

            DelistingEvent ev{.symbol = symbol, .timestamp = now};

            // 1. Cancel any open orders on the symbol.
            for (const auto& order : execution_engine_.get_open_orders(symbol)) {
                if (execution_engine_.cancel_order(order.id))
                    ev.orders_cancelled++;
            }

            // 2. Force-liquidate any open position at its last marked price via a
            //    synthetic fill (order_id 0, zero commission/slippage/spread — no more
            //    bars exist to fill against, so delisting settlements bypass execution).
            auto pos = portfolio_.position(symbol);
            if (pos && !pos->is_flat()) {
                double qty = std::abs(pos->quantity);
                double price = pos->market_price;
                ev.liquidation_price = price;
                ev.quantity_closed = pos->quantity; // signed
                ev.realized_pnl = (pos->quantity > 0)
                                      ? (price - pos->avg_cost) * qty  // long: sell at price
                                      : (pos->avg_cost - price) * qty; // short: buy to cover

                Fill fill{.order_id = OrderId{0},
                          .fill_id = 0,
                          .symbol = symbol,
                          .side = (pos->quantity > 0) ? finkit::trading::OrderSide::Sell
                                                      : finkit::trading::OrderSide::Buy,
                          .quantity = qty,
                          .price = price,
                          .commission = 0.0,
                          .fill_time = now,
                          .is_partial = false,
                          .cumulative_filled = qty};
                portfolio_.apply_fill(fill, instrument_currency(symbol));

                result_.total_trades++;
                result_.forced_liquidations++;
                // The forced liquidation closes the position: route its realized P&L
                // through the same trade-statistics accumulators as ordinary fills, and
                // close out the position's holding period (the buy-in flattens it).
                record_trade_pnl(ev.realized_pnl);
                track_position_lifecycle(symbol, /*was_flat=*/false, /*is_flat=*/true, now);

                for (auto& strategy : strategies_) {
                    strategy->on_fill(fill, ctx);
                }
            }

            result_.delisting_events.push_back(ev); // recorded even when flat
        }
    }

    /// Accrue overnight short-borrow fees over the calendar-day gap between two
    /// trading days. ACT/360 basis; weekends/holidays accrue (3 days over Fri->Mon).
    /// No accrual after the final bar (fees charged only on observed day transitions).
    /// A symbol's accrual window is capped at its delisted_at date: the position is
    /// bought in at the delist boundary, so no borrow is owed past it.
    void accrue_borrow_costs(const ql::Date& prev_date, const ql::Date& current_date) {
        if (current_date <= prev_date)
            return;
        for (const auto& [symbol, pos] : portfolio_.positions()) {
            if (pos.quantity >= -1e-10)
                continue; // longs/flat: no borrow fee
            auto it = config_.borrow.find(symbol);
            if (it == config_.borrow.end() || it->second.borrow_rate_bps <= 0.0)
                continue;
            ql::Date end_date = current_date;
            if (auto w = config_.listing_windows.find(symbol);
                w != config_.listing_windows.end() && w->second.delisted_at) {
                ql::Date delist_date = to_ql_date(*w->second.delisted_at);
                if (delist_date < end_date)
                    end_date = delist_date;
            }
            auto days = static_cast<double>(end_date - prev_date); // ql::Date diff = days
            if (days <= 0.0)
                continue;
            double short_mv = std::abs(pos.quantity) * pos.market_price;
            double cost = short_mv * it->second.borrow_rate_bps / 10000.0 * days / 360.0;
            portfolio_.withdraw(cost, pos.currency);
            result_.borrow_cost_paid += cost;
        }
    }

    /// Convert an engine timestamp to a QuantLib date (via localtime, matching the
    /// main-loop date derivation).
    [[nodiscard]] static auto to_ql_date(Timestamp ts) -> ql::Date {
        auto time_t = std::chrono::system_clock::to_time_t(
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(ts));
        std::tm* tm = std::localtime(&time_t);
        return ql::Date(tm->tm_mday, static_cast<ql::Month>(tm->tm_mon + 1),
                        tm->tm_year + 1900);
    }

    [[nodiscard]] static auto generate_run_id() -> string {
        uuid_t uuid;
        uuid_generate(uuid);
        char uuid_str[37];
        uuid_unparse_lower(uuid, uuid_str);
        return string(uuid_str);
    }

    BacktestConfig config_;
    Portfolio portfolio_;
    unique_ptr<IDataFeed> data_feed_;
    vector<unique_ptr<IStrategy>> strategies_;
    unique_ptr<IFXRateProvider> fx_provider_;
    unique_ptr<IRiskEngine> risk_engine_;
    BacktestExecutionEngine execution_engine_;
    BacktestResult result_;
    vector<double> daily_returns_;

    // Trade-statistics accumulators (reset each run()).
    double gross_profit_{0.0};                    // Sum of positive per-fill realized P&L
    double gross_loss_{0.0};                       // Sum of |negative| per-fill realized P&L
    double sum_trade_pnl_{0.0};                    // Sum of all per-fill realized P&L
    vector<double> holding_periods_days_;          // One entry per completed round trip
    std::map<string, Timestamp> open_time_;        // Symbol -> entry time of the open position

    // Drawdown-series accumulators (reset each run()).
    double dd_sum_{0.0};       // Sum of per-bar drawdowns (for the time-averaged drawdown)
    int dd_count_{0};          // Number of per-bar drawdown samples
    Timestamp peak_time_{};    // Timestamp of the current high-water-mark peak
    bool have_peak_time_{false};
    bool underwater_{false};   // Whether NAV is currently below the prior peak

    Timestamp last_bar_time_{Timestamp::min()}; // Look-ahead guard: max bar time delivered so far
    set<string> delisted_;                      // Symbols whose delisting has been processed
};

} // namespace finkit::backtest
