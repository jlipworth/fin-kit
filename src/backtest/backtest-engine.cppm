/// @file backtest-engine.cppm
/// @brief Backtest engine implementation
///
/// The main backtest engine that orchestrates data feed, strategy,
/// execution, and risk management.

module;

#include <chrono>
#include <cmath>
#include <memory>
#include <ql/quantlib.hpp>
#include <string>
#include <uuid/uuid.h>
#include <vector>

export module finkit.backtest:engine;

import :types;
import :strategy;
import finkit.types;
import finkit.trading;
import finkit.risk;

export namespace finkit::backtest {

using std::make_unique;
using std::string;
using std::unique_ptr;
using std::vector;

namespace ql = QuantLib;

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
                                      .commission_pct = config_.commission_pct}) {
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
        double prev_nav = initial_nav;

        // Main loop
        ql::Date prev_date = config_.start_date;
        int bar_count = 0;

        data_feed_->reset();
        while (data_feed_->has_more()) {
            auto bar_opt = data_feed_->next();
            if (!bar_opt)
                break;

            const BarEvent& bar = *bar_opt;
            ctx.current_time = bar.timestamp;

            // Convert timestamp to QuantLib date (simplified)
            auto time_t = std::chrono::system_clock::to_time_t(
                std::chrono::time_point_cast<std::chrono::system_clock::duration>(bar.timestamp));
            std::tm* tm = std::localtime(&time_t);
            ql::Date current_date(tm->tm_mday, static_cast<ql::Month>(tm->tm_mon + 1),
                                  tm->tm_year + 1900);
            ctx.current_date = current_date;

            // Check for new trading day
            if (current_date != prev_date) {
                for (auto& strategy : strategies_) {
                    strategy->on_trading_day_start(current_date, ctx);
                }
                prev_date = current_date;
            }

            // Mark portfolio to market
            portfolio_.mark_to_market(bar.symbol, bar.close, bar.timestamp);

            // Process execution engine
            execution_engine_.on_bar(bar);

            // Process fills
            auto fills = execution_engine_.get_pending_fills();
            for (const auto& fill : fills) {
                portfolio_.apply_fill(fill);
                result_.total_trades++;

                double pnl = (fill.side == finkit::trading::OrderSide::Sell)
                                 ? (fill.price - get_avg_cost(fill.symbol)) * fill.quantity
                                 : 0.0;

                if (pnl > 0) {
                    result_.winning_trades++;
                    result_.largest_winner = std::max(result_.largest_winner, pnl);
                } else if (pnl < 0) {
                    result_.losing_trades++;
                    result_.largest_loser = std::min(result_.largest_loser, pnl);
                }

                for (auto& strategy : strategies_) {
                    strategy->on_fill(fill, ctx);
                }
            }

            // Active risk monitoring
            auto risk_actions = risk_engine_->monitor(portfolio_, *fx_provider_, bar.timestamp);
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
            portfolio_.update_high_water_mark(current_nav);

            double drawdown = portfolio_.drawdown(*fx_provider_);
            result_.max_drawdown_pct = std::max(result_.max_drawdown_pct, drawdown);

            // Track daily return for Sharpe calculation
            if (bar_count % config_.equity_snapshot_frequency_bars == 0) {
                double daily_return = (current_nav - prev_nav) / prev_nav;
                daily_returns_.push_back(daily_return);
                prev_nav = current_nav;
            }

            bar_count++;
        }

        // Call strategy on_finish
        for (auto& strategy : strategies_) {
            strategy->on_finish(ctx);
        }

        // Calculate final statistics
        result_.final_nav = portfolio_.nav(*fx_provider_);
        result_.total_return_pct = (result_.final_nav - initial_nav) / initial_nav;

        if (result_.total_trades > 0) {
            result_.win_rate = static_cast<double>(result_.winning_trades) /
                               static_cast<double>(result_.total_trades);
        }

        // Calculate Sharpe ratio
        if (!daily_returns_.empty()) {
            double mean_return = 0.0;
            for (double r : daily_returns_)
                mean_return += r;
            mean_return /= static_cast<double>(daily_returns_.size());

            double variance = 0.0;
            for (double r : daily_returns_) {
                variance += (r - mean_return) * (r - mean_return);
            }
            variance /= static_cast<double>(daily_returns_.size());
            double std_dev = std::sqrt(variance);

            if (std_dev > 1e-10) {
                result_.sharpe_ratio = mean_return / std_dev * std::sqrt(252.0);
            }
        }

        return result_;
    }

    [[nodiscard]] auto get_portfolio() const -> const Portfolio& { return portfolio_; }

private:
    auto submit_order(Order order) -> OrderId {
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

    [[nodiscard]] auto get_avg_cost(const string& symbol) const -> double {
        auto pos = portfolio_.position(symbol);
        return pos ? pos->avg_cost : 0.0;
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
};

} // namespace finkit::backtest
