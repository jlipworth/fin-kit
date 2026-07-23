#include <algorithm>
#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <optional>
#include <ql/quantlib.hpp>

import finkit.trading;
import finkit.types;

namespace {

using namespace finkit::trading;
using namespace finkit::types;

namespace ql = QuantLib;

// Helper to create a timestamp from year/month/day
auto make_timestamp(int year, int month, int day) -> Timestamp {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    return std::chrono::time_point_cast<std::chrono::nanoseconds>(tp);
}

// Helper to create bar events for testing
auto make_bar(const std::string& symbol, int year, int month, int day, double open, double high,
              double low, double close, double volume = 1000.0) -> BarEvent {
    return BarEvent{.symbol = symbol,
                    .timestamp = make_timestamp(year, month, day),
                    .open = open,
                    .high = high,
                    .low = low,
                    .close = close,
                    .volume = volume};
}

// ============================================================================
// Order / Fill construction & factories
// ============================================================================

TEST(TradingTest, MakeMarketOrder) {
    auto o = make_market_order("AAPL", OrderSide::Sell, 100.0, "strat1");
    EXPECT_EQ(o.symbol, "AAPL");
    EXPECT_EQ(o.side, OrderSide::Sell);
    EXPECT_EQ(o.type, OrderType::Market);
    EXPECT_DOUBLE_EQ(o.quantity, 100.0);
    EXPECT_EQ(o.strategy_id, "strat1");
    EXPECT_EQ(o.status, OrderStatus::Pending);
    EXPECT_FALSE(o.limit_price.has_value());
    EXPECT_DOUBLE_EQ(o.filled_quantity, 0.0);
}

TEST(TradingTest, MakeLimitOrder) {
    auto o = make_limit_order("MSFT", OrderSide::Buy, 50.0, 300.0, "s");
    EXPECT_EQ(o.type, OrderType::Limit);
    ASSERT_TRUE(o.limit_price.has_value());
    EXPECT_DOUBLE_EQ(*o.limit_price, 300.0);
    EXPECT_DOUBLE_EQ(o.quantity, 50.0);
    EXPECT_EQ(o.side, OrderSide::Buy);
}

TEST(TradingTest, OrderIdComparable) {
    OrderId a{1}, b{2}, c{1};
    EXPECT_TRUE(a == c);
    EXPECT_TRUE(a != b);
    EXPECT_TRUE(a < b);
}

// ============================================================================
// Position math
// ============================================================================

TEST(TradingTest, PositionLong) {
    Position p{.symbol = "X", .quantity = 10.0, .avg_cost = 100.0, .market_price = 110.0};
    EXPECT_TRUE(p.is_long());
    EXPECT_FALSE(p.is_short());
    EXPECT_FALSE(p.is_flat());
    EXPECT_DOUBLE_EQ(p.notional(), 1100.0);
    EXPECT_DOUBLE_EQ(p.unrealized_pnl(), 100.0);
}

TEST(TradingTest, PositionShort) {
    Position p{.quantity = -5.0, .avg_cost = 50.0, .market_price = 40.0};
    EXPECT_TRUE(p.is_short());
    EXPECT_DOUBLE_EQ(p.notional(), 200.0);
    EXPECT_DOUBLE_EQ(p.unrealized_pnl(), 50.0);
}

TEST(TradingTest, PositionFlat) {
    Position p{.quantity = 0.0};
    EXPECT_TRUE(p.is_flat());
    EXPECT_FALSE(p.is_long());
    EXPECT_FALSE(p.is_short());
}

// ============================================================================
// Instrument abstraction
// ============================================================================

TEST(TradingTest, EquityInstrument) {
    EquityInstrument eq("AAPL");
    EXPECT_EQ(eq.symbol(), "AAPL");
    EXPECT_EQ(eq.asset_class(), AssetClass::Equity);
    EXPECT_EQ(eq.currency(), Currency::USD);
    EXPECT_DOUBLE_EQ(eq.multiplier(), 1.0);
    EXPECT_DOUBLE_EQ(eq.tick_size(), 0.01);
    EXPECT_DOUBLE_EQ(eq.calculate_pnl(100, 110, 10), 100.0);
    EXPECT_DOUBLE_EQ(eq.notional_value(110, -10), 1100.0);
}

TEST(TradingTest, FuturesInstrument) {
    FuturesInstrument fut("ESZ4", 50.0, 0.25, Currency::USD);
    EXPECT_EQ(fut.asset_class(), AssetClass::Future);
    EXPECT_DOUBLE_EQ(fut.multiplier(), 50.0);
    EXPECT_DOUBLE_EQ(fut.tick_size(), 0.25);
    EXPECT_DOUBLE_EQ(fut.calculate_pnl(4000, 4010, 2), 1000.0);
    EXPECT_DOUBLE_EQ(fut.notional_value(4000, 2), 400000.0);
}

// ============================================================================
// OrderBook
// ============================================================================

TEST(TradingTest, OrderBookLifecycle) {
    OrderBook book;
    auto o = make_market_order("SPY", OrderSide::Buy, 100.0);
    o.id = OrderId{7};
    book.add_order(o);
    EXPECT_FALSE(book.get_order(OrderId{7})->status == OrderStatus::Working); // still Pending
    EXPECT_TRUE(book.get_open_orders().empty()); // Pending is NOT "open"
    book.update_order(OrderId{7}, OrderStatus::Working);
    EXPECT_EQ(book.get_open_orders().size(), 1u);
}

TEST(TradingTest, OrderBookPartialThenFull) {
    OrderBook book;
    auto o = make_market_order("SPY", OrderSide::Buy, 100.0);
    o.id = OrderId{1};
    book.add_order(o);
    book.apply_fill(Fill{.order_id = OrderId{1},
                         .symbol = "SPY",
                         .side = OrderSide::Buy,
                         .quantity = 60.0,
                         .price = 10.0});
    auto after1 = book.get_order(OrderId{1});
    ASSERT_TRUE(after1.has_value());
    EXPECT_DOUBLE_EQ(after1->filled_quantity, 60.0);
    EXPECT_DOUBLE_EQ(after1->avg_fill_price, 10.0);
    EXPECT_EQ(after1->status, OrderStatus::PartialFill);
    book.apply_fill(Fill{.order_id = OrderId{1},
                         .symbol = "SPY",
                         .side = OrderSide::Buy,
                         .quantity = 40.0,
                         .price = 12.0});
    auto after2 = book.get_order(OrderId{1});
    ASSERT_TRUE(after2.has_value());
    EXPECT_DOUBLE_EQ(after2->filled_quantity, 100.0);
    EXPECT_DOUBLE_EQ(after2->avg_fill_price, 10.8);
    EXPECT_EQ(after2->status, OrderStatus::Filled);
    EXPECT_EQ(book.get_all_fills().size(), 2u);
    EXPECT_EQ(book.get_fills_for_order(OrderId{1}).size(), 2u);
    EXPECT_TRUE(book.get_open_orders().empty());
}

TEST(TradingTest, OrderBookGetMissing) {
    OrderBook book;
    EXPECT_FALSE(book.get_order(OrderId{999}).has_value());
    EXPECT_TRUE(book.get_fills_for_order(OrderId{999}).empty());
}

// ============================================================================
// Fill models (direct)
// ============================================================================

TEST(TradingTest, CloseFillBuySlippage) {
    auto bar = make_bar("SPY", 2024, 1, 2, 100.0, 101.0, 99.0, 100.0);
    auto order = make_market_order("SPY", OrderSide::Buy, 10.0);
    order.id = OrderId{1};
    CloseFillModel m;
    auto f = m.simulate_fill(order, bar, 50.0);
    ASSERT_TRUE(f.has_value());
    EXPECT_DOUBLE_EQ(f->price, 100.5);
    EXPECT_DOUBLE_EQ(f->quantity, 10.0);
    EXPECT_EQ(f->side, OrderSide::Buy);
    EXPECT_EQ(f->fill_id, 0u);
    EXPECT_DOUBLE_EQ(f->commission, 0.0);
    EXPECT_EQ(f->order_id, OrderId{1});
}

TEST(TradingTest, CloseFillSellSlippage) {
    auto bar = make_bar("SPY", 2024, 1, 2, 100.0, 101.0, 99.0, 100.0);
    auto order = make_market_order("SPY", OrderSide::Sell, 10.0);
    order.id = OrderId{1};
    CloseFillModel m;
    auto f = m.simulate_fill(order, bar, 50.0);
    ASSERT_TRUE(f.has_value());
    EXPECT_DOUBLE_EQ(f->price, 99.5);
}

TEST(TradingTest, CloseFillBuyLimitReject) {
    auto bar = make_bar("SPY", 2024, 1, 2, 100.0, 101.0, 99.0, 100.0);
    auto order = make_limit_order("SPY", OrderSide::Buy, 10.0, 100.4);
    order.id = OrderId{1};
    CloseFillModel m;
    auto f = m.simulate_fill(order, bar, 50.0);
    EXPECT_FALSE(f.has_value());
}

TEST(TradingTest, CloseFillBuyLimitPass) {
    auto bar = make_bar("SPY", 2024, 1, 2, 100.0, 101.0, 99.0, 100.0);
    auto order = make_limit_order("SPY", OrderSide::Buy, 10.0, 100.6);
    order.id = OrderId{1};
    CloseFillModel m;
    auto f = m.simulate_fill(order, bar, 50.0);
    ASSERT_TRUE(f.has_value());
    EXPECT_DOUBLE_EQ(f->price, 100.5);
}

TEST(TradingTest, CloseFillSymbolMismatch) {
    auto bar = make_bar("SPY", 2024, 1, 2, 100.0, 101.0, 99.0, 100.0);
    auto order = make_market_order("AAPL", OrderSide::Buy, 10.0);
    order.id = OrderId{1};
    CloseFillModel m;
    auto f = m.simulate_fill(order, bar, 50.0);
    EXPECT_FALSE(f.has_value());
}

TEST(TradingTest, NextBarOpenBuyMarket) {
    auto bar = make_bar("SPY", 2024, 1, 2, 100.0, 105.0, 98.0, 103.0);
    auto order = make_market_order("SPY", OrderSide::Buy, 10.0);
    order.id = OrderId{1};
    NextBarOpenFillModel m;
    auto f = m.simulate_fill(order, bar, 50.0);
    ASSERT_TRUE(f.has_value());
    EXPECT_DOUBLE_EQ(f->price, 100.5);
}

TEST(TradingTest, NextBarOpenBuyLimitClampsDown) {
    auto bar = make_bar("SPY", 2024, 1, 2, 100.0, 105.0, 98.0, 103.0);
    auto order = make_limit_order("SPY", OrderSide::Buy, 10.0, 99.5);
    order.id = OrderId{1};
    NextBarOpenFillModel m;
    auto f = m.simulate_fill(order, bar, 50.0);
    ASSERT_TRUE(f.has_value());
    EXPECT_DOUBLE_EQ(f->price, 99.5);
}

TEST(TradingTest, NextBarOpenBuyLimitUnreachable) {
    auto bar = make_bar("SPY", 2024, 1, 2, 100.0, 105.0, 98.0, 103.0);
    auto order = make_limit_order("SPY", OrderSide::Buy, 10.0, 97.0);
    order.id = OrderId{1};
    NextBarOpenFillModel m;
    auto f = m.simulate_fill(order, bar, 50.0);
    EXPECT_FALSE(f.has_value());
}

TEST(TradingTest, NextBarOpenSellLimitClampsUp) {
    auto bar = make_bar("SPY", 2024, 1, 2, 100.0, 105.0, 98.0, 103.0);
    auto order = make_limit_order("SPY", OrderSide::Sell, 10.0, 104.0);
    order.id = OrderId{1};
    NextBarOpenFillModel m;
    auto f = m.simulate_fill(order, bar, 50.0);
    ASSERT_TRUE(f.has_value());
    EXPECT_DOUBLE_EQ(f->price, 104.0);
}

// ============================================================================
// BacktestExecutionEngine
// ============================================================================

TEST(TradingTest, ExecEngineSubmitAssignsIds) {
    BacktestExecutionEngine eng;
    Order o1 = make_market_order("SPY", OrderSide::Buy, 10.0);
    Order o2 = make_market_order("SPY", OrderSide::Buy, 5.0);
    OrderId id1 = eng.submit_order(o1);
    OrderId id2 = eng.submit_order(o2);
    EXPECT_EQ(id1, OrderId{1});
    EXPECT_EQ(id2, OrderId{2});
    auto stored = eng.get_order(id1);
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(stored->status, OrderStatus::Working);
    EXPECT_EQ(eng.get_open_orders().size(), 2u);
    EXPECT_EQ(eng.get_open_orders("SPY").size(), 2u);
    EXPECT_TRUE(eng.get_open_orders("AAPL").empty());
    EXPECT_TRUE(eng.is_connected());
}

TEST(TradingTest, ExecEngineFillOnBar) {
    auto bar = make_bar("SPY", 2024, 1, 2, 200.0, 201.0, 199.0, 200.0);
    BacktestExecutionEngine eng; // default_slippage_bps = 0.5
    OrderId id = eng.submit_order(make_market_order("SPY", OrderSide::Buy, 10.0));
    eng.on_bar(bar); // close=200; slippage=200*0.5/10000=0.01
    auto fills = eng.get_pending_fills();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].price, 200.01);
    EXPECT_DOUBLE_EQ(fills[0].quantity, 10.0);
    EXPECT_EQ(fills[0].fill_id, 1u);
    EXPECT_DOUBLE_EQ(fills[0].commission, 0.0);
    auto o = eng.get_order(id);
    ASSERT_TRUE(o.has_value());
    EXPECT_EQ(o->status, OrderStatus::Filled);
    EXPECT_DOUBLE_EQ(o->filled_quantity, 10.0);
    EXPECT_DOUBLE_EQ(o->avg_fill_price, 200.01);
    EXPECT_TRUE(eng.get_open_orders().empty());
    EXPECT_TRUE(eng.get_pending_fills().empty());
}

TEST(TradingTest, ExecEngineSlippageBuySell) {
    auto bar = make_bar("SPY", 2024, 1, 2, 200.0, 201.0, 199.0, 200.0);
    BacktestExecutionConfig cfg{.default_slippage_bps = 10.0};

    BacktestExecutionEngine eng_buy(cfg);
    (void)eng_buy.submit_order(make_market_order("SPY", OrderSide::Buy, 100.0));
    eng_buy.on_bar(bar);
    auto buy_fills = eng_buy.get_pending_fills();
    ASSERT_EQ(buy_fills.size(), 1u);
    EXPECT_DOUBLE_EQ(buy_fills[0].price, 200.2);

    BacktestExecutionEngine eng_sell(cfg);
    (void)eng_sell.submit_order(make_market_order("SPY", OrderSide::Sell, 100.0));
    eng_sell.on_bar(bar);
    auto sell_fills = eng_sell.get_pending_fills();
    ASSERT_EQ(sell_fills.size(), 1u);
    EXPECT_DOUBLE_EQ(sell_fills[0].price, 199.8);
}

TEST(TradingTest, CommissionPerShare) {
    auto bar50 = make_bar("SPY", 2024, 1, 2, 50, 50, 50, 50);
    BacktestExecutionConfig cfg{.default_slippage_bps = 0.0, .commission_per_share = 0.01};
    BacktestExecutionEngine eng(cfg);
    (void)eng.submit_order(make_market_order("SPY", OrderSide::Buy, 100.0));
    eng.on_bar(bar50);
    auto fills = eng.get_pending_fills();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].commission, 1.0);
}

TEST(TradingTest, CommissionMinFloor) {
    auto bar50 = make_bar("SPY", 2024, 1, 2, 50, 50, 50, 50);
    BacktestExecutionConfig cfg{
        .default_slippage_bps = 0.0, .commission_per_share = 0.001, .min_commission = 5.0};
    BacktestExecutionEngine eng(cfg);
    (void)eng.submit_order(make_market_order("SPY", OrderSide::Buy, 100.0));
    eng.on_bar(bar50);
    auto fills = eng.get_pending_fills();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].commission, 5.0);
}

TEST(TradingTest, CommissionPct) {
    auto bar50 = make_bar("SPY", 2024, 1, 2, 50, 50, 50, 50);
    BacktestExecutionConfig cfg{.default_slippage_bps = 0.0, .commission_pct = 0.001};
    BacktestExecutionEngine eng(cfg);
    (void)eng.submit_order(make_market_order("SPY", OrderSide::Buy, 100.0));
    eng.on_bar(bar50);
    auto fills = eng.get_pending_fills();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].commission, 5.0);
}

TEST(TradingTest, CommissionPerContract) {
    auto bar50 = make_bar("SPY", 2024, 1, 2, 50, 50, 50, 50);
    BacktestExecutionConfig cfg{.default_slippage_bps = 0.0, .commission_per_contract = 2.5};
    BacktestExecutionEngine eng(cfg);
    (void)eng.submit_order(make_market_order("SPY", OrderSide::Buy, 10.0));
    eng.on_bar(bar50);
    auto fills = eng.get_pending_fills();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].commission, 25.0);
}

TEST(TradingTest, CommissionPriority) {
    auto bar50 = make_bar("SPY", 2024, 1, 2, 50, 50, 50, 50);
    BacktestExecutionConfig cfg{
        .default_slippage_bps = 0.0, .commission_per_share = 0.01, .commission_pct = 0.001};
    BacktestExecutionEngine eng(cfg);
    (void)eng.submit_order(make_market_order("SPY", OrderSide::Buy, 100.0));
    eng.on_bar(bar50);
    auto fills = eng.get_pending_fills();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].commission, 1.0); // per-share wins
}

// Test-local fill model that fills in fixed chunks (exercises PartialFill branch).
class ChunkFillModel : public IFillModel {
public:
    explicit ChunkFillModel(double chunk) : chunk_(chunk) {}
    auto simulate_fill(const Order& order, const BarEvent& bar,
                       double /*slip*/) -> std::optional<Fill> override {
        double remaining = order.quantity - order.filled_quantity;
        if (remaining < 1e-9)
            return std::nullopt;
        double q = std::min(remaining, chunk_);
        return Fill{.order_id = order.id,
                    .symbol = order.symbol,
                    .side = order.side,
                    .quantity = q,
                    .price = bar.close,
                    .fill_time = bar.timestamp};
    }

private:
    double chunk_;
};

TEST(TradingTest, ExecEnginePartialFills) {
    BacktestExecutionConfig cfg{.default_slippage_bps = 0.0};
    BacktestExecutionEngine eng(cfg);
    eng.set_fill_model(std::make_unique<ChunkFillModel>(60.0));
    OrderId id = eng.submit_order(make_market_order("SPY", OrderSide::Buy, 100.0));

    eng.on_bar(make_bar("SPY", 2024, 1, 2, 10, 10, 10, 10));
    auto f1 = eng.get_pending_fills();
    ASSERT_EQ(f1.size(), 1u);
    EXPECT_DOUBLE_EQ(f1[0].quantity, 60.0);
    EXPECT_TRUE(f1[0].is_partial);
    EXPECT_DOUBLE_EQ(f1[0].cumulative_filled, 60.0);
    EXPECT_EQ(eng.get_order(id)->status, OrderStatus::PartialFill);
    EXPECT_DOUBLE_EQ(eng.get_order(id)->avg_fill_price, 10.0);

    eng.on_bar(make_bar("SPY", 2024, 1, 3, 12, 12, 12, 12));
    auto f2 = eng.get_pending_fills();
    ASSERT_EQ(f2.size(), 1u);
    EXPECT_DOUBLE_EQ(f2[0].quantity, 40.0);
    EXPECT_EQ(eng.get_order(id)->status, OrderStatus::Filled);
    EXPECT_DOUBLE_EQ(eng.get_order(id)->avg_fill_price, 10.8);
    EXPECT_EQ(eng.get_fills(id).size(), 2u);
}

// ============================================================================
// Half-spread (bid/ask) adjustment
// ============================================================================

TEST(TradingTest, HalfSpreadAppliedToMarketFills) {
    BacktestExecutionEngine eng{
        BacktestExecutionConfig{.default_slippage_bps = 0.0, .half_spread_bps = 20.0}};

    auto buy_id = eng.submit_order(make_market_order("AAA", OrderSide::Buy, 100.0));
    auto sell_id = eng.submit_order(make_market_order("AAA", OrderSide::Sell, 100.0));
    (void)buy_id;
    (void)sell_id;

    eng.on_bar(make_bar("AAA", 2024, 1, 5, 50, 50, 50, 50));
    auto fills = eng.get_pending_fills();

    ASSERT_EQ(fills.size(), 2u);
    for (const auto& f : fills) {
        if (f.side == OrderSide::Buy) {
            EXPECT_DOUBLE_EQ(f.price, 50.1);
        } else {
            EXPECT_DOUBLE_EQ(f.price, 49.9);
        }
    }
}

TEST(TradingTest, HalfSpreadRespectsLimitPrice) {
    BacktestExecutionEngine eng{
        BacktestExecutionConfig{.default_slippage_bps = 0.0, .half_spread_bps = 20.0}};

    auto id = eng.submit_order(make_limit_order("AAA", OrderSide::Buy, 100.0, 50.05));
    (void)id;

    eng.on_bar(make_bar("AAA", 2024, 1, 5, 50, 50, 50, 50));
    EXPECT_TRUE(eng.get_pending_fills().empty());
    EXPECT_EQ(eng.get_open_orders().size(), 1u);

    eng.on_bar(make_bar("AAA", 2024, 1, 6, 49, 49, 49, 49));
    auto fills = eng.get_pending_fills();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].price, 49.098);
    EXPECT_TRUE(eng.get_open_orders().empty());
}

// A marketable limit order whose fill the model clamped to the limit price must
// fill AT the limit, not be discarded: with NextBarOpenFillModel the market
// traded through the limit intra-bar, and discarding would starve the order on
// every subsequent bar with open above the limit.
TEST(TradingTest, HalfSpreadCapsClampedMarketableLimitAtLimit) {
    BacktestExecutionConfig cfg{.default_slippage_bps = 0.0, .half_spread_bps = 20.0};
    BacktestExecutionEngine eng(cfg);
    eng.set_fill_model(std::make_unique<NextBarOpenFillModel>());

    OrderId id = eng.submit_order(make_limit_order("SPY", OrderSide::Buy, 10.0, 100.0));

    // Open 105 above the limit, low 95 trades through it: model clamps to 100,
    // spread-adjusted 100.2 violates the limit -> capped back to 100.
    eng.on_bar(make_bar("SPY", 2024, 1, 2, 105.0, 106.0, 95.0, 96.0));
    auto fills = eng.get_pending_fills();
    ASSERT_EQ(fills.size(), 1u);
    EXPECT_DOUBLE_EQ(fills[0].price, 100.0);
    EXPECT_EQ(eng.get_order(id)->status, OrderStatus::Filled);
    EXPECT_TRUE(eng.get_open_orders().empty());
}

TEST(TradingTest, ExecEngineCancel) {
    BacktestExecutionEngine eng;
    OrderId id = eng.submit_order(make_market_order("SPY", OrderSide::Buy, 10.0));
    EXPECT_TRUE(eng.cancel_order(id));
    EXPECT_EQ(eng.get_order(id)->status, OrderStatus::Cancelled);
    EXPECT_TRUE(eng.get_open_orders().empty());
    EXPECT_FALSE(eng.cancel_order(id));
    EXPECT_FALSE(eng.cancel_order(OrderId{999}));
}

TEST(TradingTest, ExecEngineModify) {
    BacktestExecutionEngine eng;
    OrderId id = eng.submit_order(make_limit_order("SPY", OrderSide::Buy, 10.0, 100.0));
    EXPECT_TRUE(eng.modify_order(id, 20.0, 105.0));
    auto o = eng.get_order(id);
    ASSERT_TRUE(o.has_value());
    EXPECT_DOUBLE_EQ(o->quantity, 20.0);
    ASSERT_TRUE(o->limit_price.has_value());
    EXPECT_DOUBLE_EQ(*o->limit_price, 105.0);
    EXPECT_FALSE(eng.modify_order(OrderId{999}, 1.0, std::nullopt));
}

} // namespace
