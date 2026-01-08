/// @file trading-engine.cppm
/// @brief Execution engine interface and backtest implementation
///
/// Provides the swappable execution engine abstraction for backtesting
/// and future live trading integration.

module;

#include <chrono>
#include <cmath>
#include <cstdint>
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

/// Fill at bar close price
class CloseFillModel : public IFillModel {
public:
    [[nodiscard]] auto simulate_fill(const Order& order, const BarEvent& bar,
                                     double slippage_bps) -> optional<Fill> override {
        // Skip if symbol doesn't match
        if (order.symbol != bar.symbol) {
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

        // Check limit orders
        if (order.type == OrderType::Limit && order.limit_price) {
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

        // Use open price for next-bar fills
        double fill_price = bar.open;

        double slippage = fill_price * slippage_bps / 10000.0;
        if (order.side == OrderSide::Buy) {
            fill_price += slippage;
        } else {
            fill_price -= slippage;
        }

        // For limit orders, check if limit was reached in this bar
        if (order.type == OrderType::Limit && order.limit_price) {
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
        new_order.status = OrderStatus::Working;
        new_order.created_at = current_time_;

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
        if (it == orders_.end() || it->second.status != OrderStatus::Working) {
            return false;
        }

        if (new_qty) {
            it->second.quantity = *new_qty;
        }
        if (new_price) {
            it->second.limit_price = *new_price;
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

            auto fill_opt = fill_model_->simulate_fill(order, bar, config_.default_slippage_bps);

            if (fill_opt) {
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
};

} // namespace finkit::trading
