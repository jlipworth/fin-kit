/// @file trading-engine.cppm
/// @brief Execution engine interface and backtest implementation
///
/// Provides the swappable execution engine abstraction for backtesting
/// and future live trading integration.

module;

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

export module finkit.trading:engine;

import :types;

export namespace finkit::trading {

using std::map;
using std::optional;
using std::string;
using std::unique_ptr;
using std::vector;

// ============================================================================
// Fill Model Interface
// ============================================================================

/// Interface for simulating order fills
class IFillModel {
public:
    virtual ~IFillModel() = default;

    /// Simulate a fill given order and market data
    [[nodiscard]] virtual auto simulate_fill(const Order& order, const BarEvent& bar,
                                             double slippage_bps) -> optional<Fill> = 0;
};

/// Whether a stop-family order's trigger has been touched by this bar.
/// Buy stops arm when the market trades up to the stop (bar.high >= stop);
/// sell stops arm when it trades down to it (bar.low <= stop). Orders without
/// a stop price degrade to their base (market/limit) behavior.
[[nodiscard]] auto stop_triggered(const Order& order, const BarEvent& bar) -> bool {
    if (order.type != OrderType::Stop && order.type != OrderType::StopLimit) {
        return true; // not a stop-family order
    }
    if (!order.stop_price) {
        return true; // no trigger configured: degrade to the base type
    }
    return order.side == OrderSide::Buy ? bar.high >= *order.stop_price
                                        : bar.low <= *order.stop_price;
}

/// Whether the order type carries a binding limit price (Limit, StopLimit
/// after its trigger, and Limit-On-Close).
[[nodiscard]] auto has_limit_semantics(OrderType type) -> bool {
    return type == OrderType::Limit || type == OrderType::StopLimit || type == OrderType::LOC;
}

/// Fill at bar close price
class CloseFillModel : public IFillModel {
public:
    [[nodiscard]] auto simulate_fill(const Order& order, const BarEvent& bar,
                                     double slippage_bps) -> optional<Fill> override {
        // Skip if symbol doesn't match
        if (order.symbol != bar.symbol) {
            return std::nullopt;
        }

        // Stop / StopLimit orders rest until the bar touches the trigger.
        if (!stop_triggered(order, bar)) {
            return std::nullopt;
        }

        double fill_price = bar.close;

        // Apply slippage (adverse to trader)
        double slippage = fill_price * slippage_bps / 10000.0;
        if (order.side == OrderSide::Buy) {
            fill_price += slippage;
        } else {
            fill_price -= slippage;
        }

        // Check limit-constrained orders (Limit, triggered StopLimit, LOC)
        if (has_limit_semantics(order.type) && order.limit_price) {
            if (order.side == OrderSide::Buy && fill_price > *order.limit_price) {
                return std::nullopt; // Price too high for buy limit
            }
            if (order.side == OrderSide::Sell && fill_price < *order.limit_price) {
                return std::nullopt; // Price too low for sell limit
            }
        }

        return Fill{.order_id = order.id,
                    .fill_id = 0, // Will be assigned by engine
                    .symbol = order.symbol,
                    .side = order.side,
                    .quantity = order.quantity - order.filled_quantity,
                    .price = fill_price,
                    .commission = 0.0, // Will be calculated by engine
                    .fill_time = bar.timestamp,
                    .is_partial = false,
                    .cumulative_filled = order.quantity};
    }
};

/// Fill at next bar open (more realistic for market orders)
class NextBarOpenFillModel : public IFillModel {
public:
    [[nodiscard]] auto simulate_fill(const Order& order, const BarEvent& bar,
                                     double slippage_bps) -> optional<Fill> override {
        if (order.symbol != bar.symbol) {
            return std::nullopt;
        }

        // Stop / StopLimit orders rest until the bar touches the trigger.
        if (!stop_triggered(order, bar)) {
            return std::nullopt;
        }

        // Use open price for next-bar fills
        double fill_price = bar.open;

        double slippage = fill_price * slippage_bps / 10000.0;
        if (order.side == OrderSide::Buy) {
            fill_price += slippage;
        } else {
            fill_price -= slippage;
        }

        // For limit-constrained orders, check if the limit was reached in this bar
        if (has_limit_semantics(order.type) && order.limit_price) {
            if (order.side == OrderSide::Buy) {
                if (bar.low > *order.limit_price) {
                    return std::nullopt;
                }
                fill_price = std::min(fill_price, *order.limit_price);
            } else {
                if (bar.high < *order.limit_price) {
                    return std::nullopt;
                }
                fill_price = std::max(fill_price, *order.limit_price);
            }
        }

        return Fill{.order_id = order.id,
                    .symbol = order.symbol,
                    .side = order.side,
                    .quantity = order.quantity - order.filled_quantity,
                    .price = fill_price,
                    .fill_time = bar.timestamp};
    }
};

// ============================================================================
// Execution Engine Interface
// ============================================================================

/// Abstract execution engine - swappable for backtest/paper/live
class IExecutionEngine {
public:
    virtual ~IExecutionEngine() = default;

    // Order management
    [[nodiscard]] virtual auto submit_order(const Order& order) -> OrderId = 0;
    [[nodiscard]] virtual auto cancel_order(OrderId id) -> bool = 0;
    [[nodiscard]] virtual auto modify_order(OrderId id, optional<double> new_qty,
                                            optional<double> new_price) -> bool = 0;

    // Order state
    [[nodiscard]] virtual auto get_order(OrderId id) -> optional<Order> = 0;
    [[nodiscard]] virtual auto get_open_orders() -> vector<Order> = 0;
    [[nodiscard]] virtual auto get_open_orders(const string& symbol) -> vector<Order> = 0;
    [[nodiscard]] virtual auto get_fills(OrderId id) -> vector<Fill> = 0;

    // Engine state
    [[nodiscard]] virtual auto is_connected() -> bool = 0;

    // Backtest-specific (called by backtest engine)
    virtual void on_bar(const BarEvent& bar) = 0;
    virtual void set_time(Timestamp ts) = 0;
    [[nodiscard]] virtual auto get_pending_fills() -> vector<Fill> = 0;
};

// ============================================================================
// Backtest Execution Configuration
// ============================================================================

struct BacktestExecutionConfig {
    double default_slippage_bps{0.5};
    double commission_per_share{0.0};
    double commission_per_contract{0.0};
    double commission_pct{0.0};
    double min_commission{0.0};
    bool allow_partial_fills{false};
    double half_spread_bps{0.0}; // Bid/ask half-spread in bps of fill price; buys pay
                                 // mid + half-spread, sells receive mid - half-spread.
                                 // 0 = disabled (legacy behavior).
};

// ============================================================================
// Backtest Execution Engine
// ============================================================================

class BacktestExecutionEngine : public IExecutionEngine {
public:
    explicit BacktestExecutionEngine(BacktestExecutionConfig config = {})
        : config_(std::move(config)), fill_model_(std::make_unique<CloseFillModel>()) {}

    void set_fill_model(unique_ptr<IFillModel> model) { fill_model_ = std::move(model); }

    [[nodiscard]] auto submit_order(const Order& order) -> OrderId override {
        Order new_order = order;
        new_order.id = OrderId{next_order_id_++};
        new_order.created_at = current_time_;
        // A submitted order starts with a clean fill history even when the
        // caller reused a previously (partially) filled Order as a template;
        // stale filled_quantity would otherwise shrink or zero the next fill.
        new_order.filled_quantity = 0.0;
        new_order.avg_fill_price = 0.0;
        new_order.rejection_reason = std::nullopt;

        // Validate quantity: a non-positive (or NaN) quantity would produce a
        // zero-share "fill" with avg_fill_price = 0/0 downstream.
        if (!(new_order.quantity > 0.0)) {
            new_order.status = OrderStatus::Rejected;
            new_order.rejection_reason = "order quantity must be positive";
            orders_[new_order.id] = new_order;
            return new_order.id;
        }

        new_order.status = OrderStatus::Working;
        orders_[new_order.id] = new_order;
        working_orders_.push_back(new_order.id);

        return new_order.id;
    }

    [[nodiscard]] auto cancel_order(OrderId id) -> bool override {
        auto it = orders_.find(id);
        if (it == orders_.end()) {
            return false;
        }

        if (it->second.status == OrderStatus::Working ||
            it->second.status == OrderStatus::PartialFill) {
            it->second.status = OrderStatus::Cancelled;
            remove_from_working(id);
            return true;
        }

        return false;
    }

    [[nodiscard]] auto modify_order(OrderId id, optional<double> new_qty,
                                    optional<double> new_price) -> bool override {
        auto it = orders_.find(id);
        // Working AND partially filled orders are live and modifiable (cancel
        // already accepts both; refusing PartialFill here made a partially
        // filled order impossible to reprice).
        if (it == orders_.end() || (it->second.status != OrderStatus::Working &&
                                    it->second.status != OrderStatus::PartialFill)) {
            return false;
        }

        if (new_qty) {
            if (!(*new_qty > 0.0)) {
                return false; // same validation as submit_order
            }
            it->second.quantity = *new_qty;
        }
        if (new_price) {
            // Route the price to the field the order type actually consults:
            // a plain Stop has no limit price, so new_price moves its trigger.
            if (it->second.type == OrderType::Stop) {
                it->second.stop_price = *new_price;
            } else {
                it->second.limit_price = *new_price;
            }
        }

        return true;
    }

    [[nodiscard]] auto get_order(OrderId id) -> optional<Order> override {
        auto it = orders_.find(id);
        return it != orders_.end() ? optional{it->second} : std::nullopt;
    }

    [[nodiscard]] auto get_open_orders() -> vector<Order> override {
        vector<Order> result;
        for (const auto& id : working_orders_) {
            if (auto it = orders_.find(id); it != orders_.end()) {
                result.push_back(it->second);
            }
        }
        return result;
    }

    [[nodiscard]] auto get_open_orders(const string& symbol) -> vector<Order> override {
        vector<Order> result;
        for (const auto& id : working_orders_) {
            if (auto it = orders_.find(id); it != orders_.end()) {
                if (it->second.symbol == symbol) {
                    result.push_back(it->second);
                }
            }
        }
        return result;
    }

    [[nodiscard]] auto get_fills(OrderId id) -> vector<Fill> override {
        auto it = fills_by_order_.find(id);
        return it != fills_by_order_.end() ? it->second : vector<Fill>{};
    }

    [[nodiscard]] auto is_connected() -> bool override {
        return true; // Backtest engine is always "connected"
    }

    void on_bar(const BarEvent& bar) override {
        current_time_ = bar.timestamp;

        // Try to fill working orders
        vector<OrderId> to_remove;

        for (const auto& id : working_orders_) {
            auto it = orders_.find(id);
            if (it == orders_.end())
                continue;

            Order& order = it->second;

            // Time-in-force is evaluated against bars the order can actually
            // trade on (same-symbol bars only).
            if (order.symbol != bar.symbol)
                continue;

            // Day orders live for the bars of ONE calendar date — the first
            // date on which they are eligible to trade (for daily-bar feeds,
            // an order submitted intraday works the next session). A bar on a
            // later date expires them unfilled.
            if (order.tif == TimeInForce::Day) {
                int bar_date = civil_date(bar.timestamp);
                auto de = day_order_date_.find(id);
                if (de != day_order_date_.end() && bar_date > de->second) {
                    order.status = OrderStatus::Cancelled;
                    order.rejection_reason = "Day order expired";
                    to_remove.push_back(id);
                    continue;
                }
                if (de == day_order_date_.end()) {
                    day_order_date_[id] = bar_date;
                }
            }

            auto fill_opt = fill_model_->simulate_fill(order, bar, config_.default_slippage_bps);

            // Apply bid/ask half-spread as an additional adverse price adjustment
            // (buys pay mid + half-spread, sells receive mid - half-spread).
            if (fill_opt && config_.half_spread_bps > 0.0) {
                Fill& fill = *fill_opt;
                double raw_price = fill.price; // model price, pre-spread
                double spread_adj = fill.price * config_.half_spread_bps / 10000.0;
                fill.price += (order.side == OrderSide::Buy) ? spread_adj : -spread_adj;

                // A spread-adjusted price may no longer satisfy the order's limit.
                if (has_limit_semantics(order.type) && order.limit_price) {
                    if ((order.side == OrderSide::Buy && fill.price > *order.limit_price) ||
                        (order.side == OrderSide::Sell && fill.price < *order.limit_price)) {
                        if (raw_price == *order.limit_price) {
                            // The fill model clamped a marketable limit order to its
                            // limit (market traded through it intra-bar): a resting
                            // limit would fill at the limit, so cap rather than
                            // discard — discarding would starve the order forever.
                            fill.price = *order.limit_price;
                        } else {
                            // Single-price fill (e.g. close model): the spread-adjusted
                            // quote is through the limit, so no fill this bar; the
                            // order stays Working (may fill on a later bar).
                            fill_opt = std::nullopt;
                        }
                    }
                }
            }

            // Fill-or-Kill is all-or-none: discard a fill that would not
            // complete the order (then the kill below cancels the order).
            if (fill_opt && order.tif == TimeInForce::FOK &&
                fill_opt->quantity < order.quantity - order.filled_quantity - 1e-10) {
                fill_opt = std::nullopt;
            }

            if (fill_opt && fill_opt->quantity > 1e-10) {
                Fill& fill = *fill_opt;
                fill.fill_id = next_fill_id_++;
                fill.commission = calculate_commission(fill);

                // Update order
                order.filled_quantity += fill.quantity;
                double old_value = order.avg_fill_price * (order.filled_quantity - fill.quantity);
                double new_value = fill.price * fill.quantity;
                order.avg_fill_price = (old_value + new_value) / order.filled_quantity;

                if (order.filled_quantity >= order.quantity - 1e-10) {
                    order.status = OrderStatus::Filled;
                    to_remove.push_back(id);
                } else {
                    order.status = OrderStatus::PartialFill;
                    fill.is_partial = true;
                }

                fill.cumulative_filled = order.filled_quantity;

                fills_by_order_[id].push_back(fill);
                pending_fills_.push_back(fill);
            }

            // Immediate-or-Cancel / Fill-or-Kill: whatever is left after the
            // first eligible bar's attempt is cancelled, never rested.
            if ((order.tif == TimeInForce::IOC || order.tif == TimeInForce::FOK) &&
                order.status != OrderStatus::Filled) {
                order.status = OrderStatus::Cancelled;
                to_remove.push_back(id);
            }
        }

        // Remove filled orders from working list
        for (const auto& id : to_remove) {
            remove_from_working(id);
        }
    }

    void set_time(Timestamp ts) override { current_time_ = ts; }

    [[nodiscard]] auto get_pending_fills() -> vector<Fill> override {
        vector<Fill> result = std::move(pending_fills_);
        pending_fills_.clear();
        return result;
    }

private:
    void remove_from_working(OrderId id) {
        working_orders_.erase(std::remove(working_orders_.begin(), working_orders_.end(), id),
                              working_orders_.end());
        day_order_date_.erase(id);
    }

    /// Calendar date (yyyymmdd) of a timestamp, derived via localtime to match
    /// the date convention used elsewhere in the codebase (and the mktime-built
    /// timestamps in tests).
    [[nodiscard]] static auto civil_date(Timestamp ts) -> int {
        auto tt = std::chrono::system_clock::to_time_t(
            std::chrono::time_point_cast<std::chrono::system_clock::duration>(ts));
        std::tm tm_buf{};
        localtime_r(&tt, &tm_buf);
        return (tm_buf.tm_year + 1900) * 10000 + (tm_buf.tm_mon + 1) * 100 + tm_buf.tm_mday;
    }

    [[nodiscard]] auto calculate_commission(const Fill& fill) const -> double {
        double comm = 0.0;

        if (config_.commission_per_share > 0) {
            comm = config_.commission_per_share * std::abs(fill.quantity);
        } else if (config_.commission_pct > 0) {
            comm = config_.commission_pct * std::abs(fill.quantity * fill.price);
        } else if (config_.commission_per_contract > 0) {
            comm = config_.commission_per_contract * std::abs(fill.quantity);
        }

        return std::max(comm, config_.min_commission);
    }

    BacktestExecutionConfig config_;
    unique_ptr<IFillModel> fill_model_;

    uint64_t next_order_id_{1};
    uint64_t next_fill_id_{1};
    Timestamp current_time_;

    map<OrderId, Order> orders_;
    vector<OrderId> working_orders_;
    map<OrderId, vector<Fill>> fills_by_order_;
    vector<Fill> pending_fills_;
    map<OrderId, int> day_order_date_; // Day-TIF: first eligible calendar date (yyyymmdd)
};

} // namespace finkit::trading
