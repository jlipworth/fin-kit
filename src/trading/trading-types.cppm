/// @file trading-types.cppm
/// @brief Trading types - orders, fills, positions
///
/// Core types for order management and execution simulation.

module;

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

export module finkit.trading:types;

import finkit.types;

export namespace finkit::trading {

using std::map;
using std::optional;
using std::string;
using std::vector;

using finkit::types::Currency;

using Timestamp = std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>;

// ============================================================================
// Order Types
// ============================================================================

/// Unique order identifier
struct OrderId {
    uint64_t id{0};
    bool operator==(const OrderId&) const = default;
    auto operator<=>(const OrderId&) const = default;
};

enum class OrderSide { Buy, Sell };
enum class OrderType { Market, Limit, Stop, StopLimit, MOC, LOC };
enum class OrderStatus { Pending, Submitted, Working, PartialFill, Filled, Cancelled, Rejected };
enum class TimeInForce { Day, GTC, IOC, FOK, GTD, OPG };

/// Order specification
struct Order {
    OrderId id;
    string symbol;
    OrderSide side{OrderSide::Buy};
    OrderType type{OrderType::Market};
    double quantity{0.0};
    optional<double> limit_price;
    optional<double> stop_price;
    TimeInForce tif{TimeInForce::Day};
    string strategy_id;
    map<string, string> metadata;
    Timestamp created_at;
    OrderStatus status{OrderStatus::Pending};

    // For partial fills
    double filled_quantity{0.0};
    double avg_fill_price{0.0};
    optional<string> rejection_reason;
};

/// Order creation helper
auto make_market_order(string symbol, OrderSide side, double qty,
                       string strategy_id = "") -> Order {
    return Order{.symbol = std::move(symbol),
                 .side = side,
                 .type = OrderType::Market,
                 .quantity = qty,
                 .strategy_id = std::move(strategy_id),
                 .created_at = std::chrono::system_clock::now()};
}

auto make_limit_order(string symbol, OrderSide side, double qty, double price,
                      string strategy_id = "") -> Order {
    return Order{.symbol = std::move(symbol),
                 .side = side,
                 .type = OrderType::Limit,
                 .quantity = qty,
                 .limit_price = price,
                 .strategy_id = std::move(strategy_id),
                 .created_at = std::chrono::system_clock::now()};
}

// ============================================================================
// Fill Types
// ============================================================================

/// Execution fill
struct Fill {
    OrderId order_id;
    uint64_t fill_id{0};
    string symbol;
    OrderSide side{OrderSide::Buy};
    double quantity{0.0};
    double price{0.0};
    double commission{0.0};
    Timestamp fill_time;
    bool is_partial{false};
    double cumulative_filled{0.0};
};

// ============================================================================
// Position Types
// ============================================================================

/// Position in a single instrument
struct Position {
    string symbol;
    double quantity{0.0}; // Signed: positive = long, negative = short
    double avg_cost{0.0}; // In instrument's currency
    double market_price{0.0};
    Currency currency{Currency::USD};
    double realized_pnl{0.0};
    Timestamp last_updated;

    [[nodiscard]] auto notional() const -> double { return std::abs(quantity * market_price); }

    [[nodiscard]] auto unrealized_pnl() const -> double {
        return quantity * (market_price - avg_cost);
    }

    [[nodiscard]] auto is_long() const -> bool { return quantity > 0; }
    [[nodiscard]] auto is_short() const -> bool { return quantity < 0; }
    [[nodiscard]] auto is_flat() const -> bool { return std::abs(quantity) < 1e-10; }
};

// ============================================================================
// Market Data Events
// ============================================================================

/// OHLCV bar data
struct BarEvent {
    string symbol;
    Timestamp timestamp;
    double open{0.0};
    double high{0.0};
    double low{0.0};
    double close{0.0};
    double volume{0.0};
    optional<double> vwap;
    optional<int64_t> trade_count;
};

/// Tick data
struct TickEvent {
    string symbol;
    Timestamp timestamp;
    double bid{0.0};
    double ask{0.0};
    double last{0.0};
    double bid_size{0.0};
    double ask_size{0.0};
    double last_size{0.0};
    enum class TickType { Trade, Quote } tick_type{TickType::Quote};
};

// ============================================================================
// Order Book
// ============================================================================

/// Order book tracks orders and fills
class OrderBook {
public:
    void add_order(const Order& order) { orders_[order.id] = order; }

    void update_order(OrderId id, OrderStatus status) {
        if (auto it = orders_.find(id); it != orders_.end()) {
            it->second.status = status;
        }
    }

    void apply_fill(const Fill& fill) {
        fills_by_order_[fill.order_id].push_back(fill);
        all_fills_.push_back(fill);

        // Update order
        if (auto it = orders_.find(fill.order_id); it != orders_.end()) {
            it->second.filled_quantity += fill.quantity;
            double old_value =
                it->second.avg_fill_price * (it->second.filled_quantity - fill.quantity);
            double new_value = fill.price * fill.quantity;
            it->second.avg_fill_price = (old_value + new_value) / it->second.filled_quantity;

            if (it->second.filled_quantity >= it->second.quantity - 1e-10) {
                it->second.status = OrderStatus::Filled;
            } else {
                it->second.status = OrderStatus::PartialFill;
            }
        }
    }

    [[nodiscard]] auto get_order(OrderId id) -> optional<Order> {
        if (auto it = orders_.find(id); it != orders_.end()) {
            return it->second;
        }
        return std::nullopt;
    }

    [[nodiscard]] auto get_open_orders() -> vector<Order> {
        vector<Order> result;
        for (const auto& [id, order] : orders_) {
            if (order.status == OrderStatus::Working || order.status == OrderStatus::PartialFill ||
                order.status == OrderStatus::Submitted) {
                result.push_back(order);
            }
        }
        return result;
    }

    [[nodiscard]] auto get_fills_for_order(OrderId id) -> vector<Fill> {
        if (auto it = fills_by_order_.find(id); it != fills_by_order_.end()) {
            return it->second;
        }
        return {};
    }

    [[nodiscard]] auto get_all_fills() const -> const vector<Fill>& { return all_fills_; }

private:
    map<OrderId, Order> orders_;
    map<OrderId, vector<Fill>> fills_by_order_;
    vector<Fill> all_fills_;
};

// ============================================================================
// Instrument Abstraction
// ============================================================================

enum class AssetClass { Equity, Bond, Future, FX, Option, Swap, ETF, Index, Commodity, Crypto };

/// Abstract instrument interface
class IInstrument {
public:
    virtual ~IInstrument() = default;

    [[nodiscard]] virtual auto symbol() const -> string = 0;
    [[nodiscard]] virtual auto asset_class() const -> AssetClass = 0;
    [[nodiscard]] virtual auto currency() const -> Currency = 0;
    [[nodiscard]] virtual auto multiplier() const -> double = 0;
    [[nodiscard]] virtual auto tick_size() const -> double = 0;

    [[nodiscard]] virtual auto calculate_pnl(double entry_price, double exit_price,
                                             double quantity) const -> double = 0;
    [[nodiscard]] virtual auto notional_value(double price, double quantity) const -> double = 0;
};

/// Simple equity instrument
class EquityInstrument : public IInstrument {
public:
    explicit EquityInstrument(string sym, Currency ccy = Currency::USD)
        : symbol_(std::move(sym)), currency_(ccy) {}

    [[nodiscard]] auto symbol() const -> string override { return symbol_; }
    [[nodiscard]] auto asset_class() const -> AssetClass override { return AssetClass::Equity; }
    [[nodiscard]] auto currency() const -> Currency override { return currency_; }
    [[nodiscard]] auto multiplier() const -> double override { return 1.0; }
    [[nodiscard]] auto tick_size() const -> double override { return 0.01; }

    [[nodiscard]] auto calculate_pnl(double entry, double exit,
                                     double qty) const -> double override {
        return (exit - entry) * qty;
    }
    [[nodiscard]] auto notional_value(double price, double qty) const -> double override {
        return price * std::abs(qty);
    }

private:
    string symbol_;
    Currency currency_;
};

/// Futures instrument
class FuturesInstrument : public IInstrument {
public:
    FuturesInstrument(string sym, double mult, double tick, Currency ccy = Currency::USD)
        : symbol_(std::move(sym)), multiplier_(mult), tick_size_(tick), currency_(ccy) {}

    [[nodiscard]] auto symbol() const -> string override { return symbol_; }
    [[nodiscard]] auto asset_class() const -> AssetClass override { return AssetClass::Future; }
    [[nodiscard]] auto currency() const -> Currency override { return currency_; }
    [[nodiscard]] auto multiplier() const -> double override { return multiplier_; }
    [[nodiscard]] auto tick_size() const -> double override { return tick_size_; }

    [[nodiscard]] auto calculate_pnl(double entry, double exit,
                                     double qty) const -> double override {
        return (exit - entry) * qty * multiplier_;
    }
    [[nodiscard]] auto notional_value(double price, double qty) const -> double override {
        return price * std::abs(qty) * multiplier_;
    }

private:
    string symbol_;
    double multiplier_;
    double tick_size_;
    Currency currency_;
};

} // namespace finkit::trading
