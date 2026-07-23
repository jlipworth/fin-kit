#include <gtest/gtest.h>
#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <ql/quantlib.hpp>
#include <string>
#include <utility>
#include <vector>

import finkit.backtest;
import finkit.trading;
import finkit.types;

namespace {

using namespace finkit::backtest;
using namespace finkit::trading;
using finkit::types::Currency;
namespace ql = QuantLib;

// Same construction as backtest_test.cpp so day-boundary tests are timezone-stable.
auto make_timestamp(int year, int month, int day) -> Timestamp {
    std::tm tm{};
    tm.tm_year = year - 1900;
    tm.tm_mon = month - 1;
    tm.tm_mday = day;
    auto tp = std::chrono::system_clock::from_time_t(std::mktime(&tm));
    return std::chrono::time_point_cast<std::chrono::nanoseconds>(tp);
}

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

/// Strategy that records every bar it sees and runs a scripted action keyed by
/// the 0-based sequence number of bars received.
class ScriptedStrategy : public IStrategy {
public:
    using Action = std::function<void(const BarEvent&, StrategyContext&)>;
    explicit ScriptedStrategy(std::map<int, Action> actions) : actions_(std::move(actions)) {}
    [[nodiscard]] auto name() const -> std::string override { return "Scripted"; }
    [[nodiscard]] auto id() const -> std::string override { return "scripted"; }
    void on_bar(const BarEvent& bar, StrategyContext& ctx) override {
        seen_bars.push_back(bar);
        if (auto it = actions_.find(bar_index_); it != actions_.end())
            it->second(bar, ctx);
        ++bar_index_;
    }
    std::vector<BarEvent> seen_bars;

private:
    std::map<int, Action> actions_;
    int bar_index_{0};
};

auto base_config() -> BacktestConfig {
    return BacktestConfig{.start_date = ql::Date(1, ql::January, 2024),
                          .end_date = ql::Date(31, ql::December, 2024),
                          .initial_capital = 100000.0,
                          .slippage_bps = 0.0,
                          .commission_per_share = 0.0,
                          .commission_pct = 0.0};
}

// ============================================================================
// T1 - Look-ahead guard
// ============================================================================

TEST(RealismTest, OutOfOrderBarIsDroppedAndCounted) {
    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar(make_bar("AAA", 2024, 1, 5, 100, 100, 100, 100));
    feed->add_bar(make_bar("AAA", 2024, 1, 3, 999, 999, 999, 999));
    feed->add_bar(make_bar("AAA", 2024, 1, 6, 101, 101, 101, 101));

    BacktestEngine engine(base_config());
    engine.set_data_feed(std::move(feed));
    auto s = std::make_unique<ScriptedStrategy>(std::map<int, ScriptedStrategy::Action>{});
    auto* strat = s.get();
    engine.add_strategy(std::move(s));

    auto result = engine.run();

    ASSERT_EQ(strat->seen_bars.size(), 2u);
    EXPECT_DOUBLE_EQ(strat->seen_bars[0].close, 100.0);
    EXPECT_DOUBLE_EQ(strat->seen_bars[1].close, 101.0);
    EXPECT_EQ(result.out_of_order_bars_dropped, 1);
    EXPECT_EQ(result.inactive_symbol_bars_dropped, 0);
    EXPECT_FALSE(engine.get_portfolio().position("AAA").has_value());
}

// ============================================================================
// T2 - Equal timestamps both delivered
// ============================================================================

TEST(RealismTest, EqualTimestampBarsBothDelivered) {
    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar(make_bar("AAA", 2024, 1, 5, 100, 100, 100, 100));
    feed->add_bar(make_bar("BBB", 2024, 1, 5, 50, 50, 50, 50));
    feed->add_bar(make_bar("AAA", 2024, 1, 6, 101, 101, 101, 101));

    BacktestEngine engine(base_config());
    engine.set_data_feed(std::move(feed));
    auto s = std::make_unique<ScriptedStrategy>(std::map<int, ScriptedStrategy::Action>{});
    auto* strat = s.get();
    engine.add_strategy(std::move(s));

    auto result = engine.run();

    EXPECT_EQ(strat->seen_bars.size(), 3u);
    EXPECT_EQ(result.out_of_order_bars_dropped, 0);
}

// ============================================================================
// T3 - Monotonic time invariant visible to strategy
// ============================================================================

TEST(RealismTest, MonotonicTimeInvariantVisibleToStrategy) {
    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar(make_bar("AAA", 2024, 1, 5, 100, 100, 100, 100));
    feed->add_bar(make_bar("AAA", 2024, 1, 3, 999, 999, 999, 999));
    feed->add_bar(make_bar("AAA", 2024, 1, 6, 101, 101, 101, 101));

    std::vector<Timestamp> recorded_times;
    std::vector<Timestamp> recorded_bar_times;
    std::map<int, ScriptedStrategy::Action> actions;
    auto record = [&](const BarEvent& bar, StrategyContext& ctx) {
        recorded_times.push_back(ctx.current_time);
        recorded_bar_times.push_back(bar.timestamp);
    };
    actions[0] = record;
    actions[1] = record;

    BacktestEngine engine(base_config());
    engine.set_data_feed(std::move(feed));
    engine.add_strategy(std::make_unique<ScriptedStrategy>(std::move(actions)));

    engine.run();

    ASSERT_EQ(recorded_times.size(), 2u);
    for (size_t i = 0; i < recorded_times.size(); ++i) {
        EXPECT_EQ(recorded_times[i], recorded_bar_times[i]);
        if (i > 0)
            EXPECT_TRUE(recorded_times[i - 1] <= recorded_times[i]);
    }
}

// ============================================================================
// T4 - Pre-listing bars dropped
// ============================================================================

TEST(RealismTest, PreListingBarsDropped) {
    auto config = base_config();
    config.listing_windows["AAA"] = ListingWindow{.listed_from = make_timestamp(2024, 1, 5)};

    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar(make_bar("AAA", 2024, 1, 3, 10, 10, 10, 10));
    feed->add_bar(make_bar("AAA", 2024, 1, 5, 11, 11, 11, 11));
    feed->add_bar(make_bar("AAA", 2024, 1, 6, 12, 12, 12, 12));

    BacktestEngine engine(config);
    engine.set_data_feed(std::move(feed));
    auto s = std::make_unique<ScriptedStrategy>(std::map<int, ScriptedStrategy::Action>{});
    auto* strat = s.get();
    engine.add_strategy(std::move(s));

    auto result = engine.run();

    ASSERT_EQ(strat->seen_bars.size(), 2u);
    EXPECT_DOUBLE_EQ(strat->seen_bars[0].close, 11.0);
    EXPECT_DOUBLE_EQ(strat->seen_bars[1].close, 12.0);
    EXPECT_EQ(result.inactive_symbol_bars_dropped, 1);
}

// ============================================================================
// T5 - Delisting force-liquidates long at last price
// ============================================================================

TEST(RealismTest, DelistingForceLiquidatesLongAtLastPrice) {
    auto config = base_config();
    config.listing_windows["AAA"] = ListingWindow{.delisted_at = make_timestamp(2024, 1, 10)};

    std::map<int, ScriptedStrategy::Action> actions;
    actions[0] = [](const BarEvent&, StrategyContext& ctx) {
        ctx.submit_order(make_market_order("AAA", OrderSide::Buy, 100.0));
    };

    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar(make_bar("AAA", 2024, 1, 6, 50, 50, 50, 50));
    feed->add_bar(make_bar("AAA", 2024, 1, 7, 55, 55, 55, 55));
    feed->add_bar(make_bar("AAA", 2024, 1, 8, 60, 60, 60, 60));
    feed->add_bar(make_bar("AAA", 2024, 1, 12, 70, 70, 70, 70));
    feed->add_bar(make_bar("BBB", 2024, 1, 12, 10, 10, 10, 10));

    BacktestEngine engine(config);
    engine.set_data_feed(std::move(feed));
    engine.add_strategy(std::make_unique<ScriptedStrategy>(std::move(actions)));

    auto result = engine.run();

    ASSERT_EQ(result.delisting_events.size(), 1u);
    const auto& ev = result.delisting_events[0];
    EXPECT_EQ(ev.symbol, "AAA");
    EXPECT_DOUBLE_EQ(ev.liquidation_price, 60.0);
    EXPECT_DOUBLE_EQ(ev.quantity_closed, 100.0);
    EXPECT_DOUBLE_EQ(ev.realized_pnl, 500.0);
    EXPECT_EQ(ev.orders_cancelled, 0);

    EXPECT_EQ(result.forced_liquidations, 1);
    EXPECT_EQ(result.inactive_symbol_bars_dropped, 1);
    EXPECT_EQ(result.total_trades, 2);
    EXPECT_EQ(result.winning_trades, 1);

    auto pos = engine.get_portfolio().position("AAA");
    ASSERT_TRUE(pos.has_value());
    EXPECT_TRUE(pos->is_flat());
    EXPECT_DOUBLE_EQ(result.final_nav, 100500.0);
    EXPECT_DOUBLE_EQ(engine.get_portfolio().cash(Currency::USD), 100500.0);
}

// ============================================================================
// T6 - Delisting cancels open orders and rejects new ones
// ============================================================================

TEST(RealismTest, DelistingCancelsOpenOrdersAndRejectsNewOnes) {
    auto config = base_config();
    config.listing_windows["AAA"] = ListingWindow{.delisted_at = make_timestamp(2024, 1, 10)};

    OrderId late_id{123}; // sentinel to detect it was overwritten
    std::map<int, ScriptedStrategy::Action> actions;
    actions[0] = [](const BarEvent&, StrategyContext& ctx) {
        ctx.submit_order(make_limit_order("AAA", OrderSide::Buy, 100.0, 1.0));
    };
    actions[1] = [&late_id](const BarEvent&, StrategyContext& ctx) {
        late_id = ctx.submit_order(make_market_order("AAA", OrderSide::Buy, 10.0));
    };

    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar(make_bar("AAA", 2024, 1, 8, 50, 50, 50, 50));
    feed->add_bar(make_bar("BBB", 2024, 1, 12, 10, 10, 10, 10));

    BacktestEngine engine(config);
    engine.set_data_feed(std::move(feed));
    engine.add_strategy(std::make_unique<ScriptedStrategy>(std::move(actions)));

    auto result = engine.run();

    ASSERT_EQ(result.delisting_events.size(), 1u);
    const auto& ev = result.delisting_events[0];
    EXPECT_EQ(ev.orders_cancelled, 1);
    EXPECT_DOUBLE_EQ(ev.quantity_closed, 0.0);
    EXPECT_DOUBLE_EQ(ev.realized_pnl, 0.0);

    EXPECT_EQ(late_id, OrderId{0});
    EXPECT_EQ(result.orders_rejected, 1);
    EXPECT_EQ(result.forced_liquidations, 0);
    EXPECT_EQ(result.total_trades, 0);
}

// ============================================================================
// T7 - Hard-to-borrow blocks short but allows long reduction
// ============================================================================

TEST(RealismTest, HardToBorrowBlocksShortButAllowsLongReduction) {
    auto config = base_config();
    config.borrow["AAA"] = BorrowConfig{.borrow_rate_bps = 0.0, .hard_to_borrow = true};

    OrderId reject_id{123};
    std::map<int, ScriptedStrategy::Action> actions;
    actions[0] = [](const BarEvent&, StrategyContext& ctx) {
        ctx.submit_order(make_market_order("AAA", OrderSide::Buy, 100.0));
    };
    actions[2] = [](const BarEvent&, StrategyContext& ctx) {
        ctx.submit_order(make_market_order("AAA", OrderSide::Sell, 40.0));
    };
    actions[4] = [&reject_id](const BarEvent&, StrategyContext& ctx) {
        reject_id = ctx.submit_order(make_market_order("AAA", OrderSide::Sell, 100.0));
    };

    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar(make_bar("AAA", 2024, 1, 5, 50, 50, 50, 50));
    feed->add_bar(make_bar("AAA", 2024, 1, 6, 50, 50, 50, 50));
    feed->add_bar(make_bar("AAA", 2024, 1, 7, 50, 50, 50, 50));
    feed->add_bar(make_bar("AAA", 2024, 1, 8, 50, 50, 50, 50));
    feed->add_bar(make_bar("AAA", 2024, 1, 9, 50, 50, 50, 50));

    BacktestEngine engine(config);
    engine.set_data_feed(std::move(feed));
    engine.add_strategy(std::make_unique<ScriptedStrategy>(std::move(actions)));

    auto result = engine.run();

    EXPECT_EQ(reject_id, OrderId{0});
    EXPECT_EQ(result.orders_rejected, 1);
    auto pos = engine.get_portfolio().position("AAA");
    ASSERT_TRUE(pos.has_value());
    EXPECT_DOUBLE_EQ(pos->quantity, 60.0);
    EXPECT_EQ(result.total_trades, 2);
}

// ============================================================================
// T8 - Borrow cost accrues daily ACT/360
// ============================================================================

TEST(RealismTest, BorrowCostAccruesDailyAct360) {
    auto config = base_config();
    config.borrow["AAA"] = BorrowConfig{.borrow_rate_bps = 100.0, .hard_to_borrow = false};

    std::map<int, ScriptedStrategy::Action> actions;
    actions[0] = [](const BarEvent&, StrategyContext& ctx) {
        ctx.submit_order(make_market_order("AAA", OrderSide::Sell, 100.0));
    };

    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar(make_bar("AAA", 2024, 1, 5, 50, 50, 50, 50));
    feed->add_bar(make_bar("AAA", 2024, 1, 6, 50, 50, 50, 50));
    feed->add_bar(make_bar("AAA", 2024, 1, 7, 50, 50, 50, 50));
    feed->add_bar(make_bar("AAA", 2024, 1, 10, 50, 50, 50, 50));

    BacktestEngine engine(config);
    engine.set_data_feed(std::move(feed));
    engine.add_strategy(std::make_unique<ScriptedStrategy>(std::move(actions)));

    auto result = engine.run();

    EXPECT_NEAR(result.borrow_cost_paid, 200.0 / 360.0, 1e-9);
    EXPECT_NEAR(engine.get_portfolio().cash(Currency::USD), 105000.0 - 200.0 / 360.0, 1e-9);
    EXPECT_NEAR(result.final_nav, 100000.0 - 200.0 / 360.0, 1e-9);
    EXPECT_EQ(result.orders_rejected, 0);
    EXPECT_TRUE(result.delisting_events.empty());
}

// ============================================================================
// Delisting with an open short — realized P&L vs the short entry price
// ============================================================================

TEST(RealismTest, DelistingForceLiquidatesShortAtLastPrice) {
    auto config = base_config();
    config.listing_windows["AAA"] = ListingWindow{.delisted_at = make_timestamp(2024, 1, 10)};

    std::map<int, ScriptedStrategy::Action> actions;
    actions[0] = [](const BarEvent&, StrategyContext& ctx) {
        ctx.submit_order(make_market_order("AAA", OrderSide::Sell, 100.0));
    };

    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar(make_bar("AAA", 2024, 1, 6, 50, 50, 50, 50));
    feed->add_bar(make_bar("AAA", 2024, 1, 7, 50, 50, 50, 50)); // short fills here @50
    feed->add_bar(make_bar("AAA", 2024, 1, 8, 45, 45, 45, 45)); // marked 45
    feed->add_bar(make_bar("BBB", 2024, 1, 12, 10, 10, 10, 10)); // triggers delist

    BacktestEngine engine(config);
    engine.set_data_feed(std::move(feed));
    engine.add_strategy(std::make_unique<ScriptedStrategy>(std::move(actions)));

    auto result = engine.run();

    // Short 100 @ 50, bought in at the last mark 45: realized = (50 - 45) * 100.
    ASSERT_EQ(result.delisting_events.size(), 1u);
    const auto& ev = result.delisting_events[0];
    EXPECT_EQ(ev.symbol, "AAA");
    EXPECT_DOUBLE_EQ(ev.liquidation_price, 45.0);
    EXPECT_DOUBLE_EQ(ev.quantity_closed, -100.0);
    EXPECT_DOUBLE_EQ(ev.realized_pnl, 500.0);

    EXPECT_EQ(result.forced_liquidations, 1);
    EXPECT_EQ(result.total_trades, 2);
    EXPECT_EQ(result.winning_trades, 1);

    auto pos = engine.get_portfolio().position("AAA");
    ASSERT_TRUE(pos.has_value());
    EXPECT_TRUE(pos->is_flat());
    EXPECT_DOUBLE_EQ(pos->realized_pnl, 500.0);
    // Cash: 100000 + 5000 (short sale) - 4500 (buy-in at 45) = 100500.
    EXPECT_DOUBLE_EQ(engine.get_portfolio().cash(Currency::USD), 100500.0);
    EXPECT_DOUBLE_EQ(result.final_nav, 100500.0);
}

// ============================================================================
// Borrow accrual stops at the delisting date
// ============================================================================

TEST(RealismTest, BorrowCostStopsAtDelisting) {
    auto config = base_config();
    config.borrow["AAA"] = BorrowConfig{.borrow_rate_bps = 100.0, .hard_to_borrow = false};
    config.listing_windows["AAA"] = ListingWindow{.delisted_at = make_timestamp(2024, 1, 10)};

    std::map<int, ScriptedStrategy::Action> actions;
    actions[0] = [](const BarEvent&, StrategyContext& ctx) {
        ctx.submit_order(make_market_order("AAA", OrderSide::Sell, 100.0));
    };

    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar(make_bar("AAA", 2024, 1, 5, 50, 50, 50, 50));
    feed->add_bar(make_bar("AAA", 2024, 1, 6, 50, 50, 50, 50)); // short fills here @50
    feed->add_bar(make_bar("AAA", 2024, 1, 7, 50, 50, 50, 50));
    feed->add_bar(make_bar("BBB", 2024, 1, 12, 10, 10, 10, 10)); // delist processed here

    BacktestEngine engine(config);
    engine.set_data_feed(std::move(feed));
    engine.add_strategy(std::make_unique<ScriptedStrategy>(std::move(actions)));

    auto result = engine.run();

    // Jan-6 -> Jan-7: 1 day on 5000 short MV = 50/360.
    // Jan-7 -> Jan-12: capped at the Jan-10 delist date = 3 days = 150/360
    // (5 days = 250/360 would be an overcharge past the buy-in).
    EXPECT_NEAR(result.borrow_cost_paid, 200.0 / 360.0, 1e-9);
    ASSERT_EQ(result.delisting_events.size(), 1u);
    EXPECT_DOUBLE_EQ(result.delisting_events[0].realized_pnl, 0.0);
    EXPECT_NEAR(engine.get_portfolio().cash(Currency::USD), 100000.0 - 200.0 / 360.0, 1e-9);
    EXPECT_NEAR(result.final_nav, 100000.0 - 200.0 / 360.0, 1e-9);
}

// ============================================================================
// Hard-to-borrow gate accounts for outstanding working sells
// ============================================================================

TEST(RealismTest, HardToBorrowCountsPendingSells) {
    auto config = base_config();
    config.borrow["AAA"] = BorrowConfig{.borrow_rate_bps = 0.0, .hard_to_borrow = true};

    OrderId second_id{123};
    std::map<int, ScriptedStrategy::Action> actions;
    actions[0] = [](const BarEvent&, StrategyContext& ctx) {
        ctx.submit_order(make_market_order("AAA", OrderSide::Buy, 100.0));
    };
    actions[2] = [&second_id](const BarEvent&, StrategyContext& ctx) {
        ctx.submit_order(make_market_order("AAA", OrderSide::Sell, 60.0)); // allowed
        // Individually fine vs the +100 position, but together with the working
        // sell above it would leave the position short 20.
        second_id = ctx.submit_order(make_market_order("AAA", OrderSide::Sell, 60.0));
    };

    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar(make_bar("AAA", 2024, 1, 5, 50, 50, 50, 50));
    feed->add_bar(make_bar("AAA", 2024, 1, 6, 50, 50, 50, 50));
    feed->add_bar(make_bar("AAA", 2024, 1, 7, 50, 50, 50, 50));
    feed->add_bar(make_bar("AAA", 2024, 1, 8, 50, 50, 50, 50));
    feed->add_bar(make_bar("AAA", 2024, 1, 9, 50, 50, 50, 50));

    BacktestEngine engine(config);
    engine.set_data_feed(std::move(feed));
    engine.add_strategy(std::make_unique<ScriptedStrategy>(std::move(actions)));

    auto result = engine.run();

    EXPECT_EQ(second_id, OrderId{0});
    EXPECT_EQ(result.orders_rejected, 1);
    auto pos = engine.get_portfolio().position("AAA");
    ASSERT_TRUE(pos.has_value());
    EXPECT_DOUBLE_EQ(pos->quantity, 40.0);
    EXPECT_EQ(result.total_trades, 2);
}

// ============================================================================
// T11 - Half-spread flows through BacktestConfig (end-to-end)
// ============================================================================

TEST(RealismTest, HalfSpreadFlowsThroughBacktestConfig) {
    auto config = base_config();
    config.half_spread_bps = 10.0;

    std::map<int, ScriptedStrategy::Action> actions;
    actions[0] = [](const BarEvent&, StrategyContext& ctx) {
        ctx.submit_order(make_market_order("AAA", OrderSide::Buy, 100.0));
    };

    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar(make_bar("AAA", 2024, 1, 5, 50, 50, 50, 50));
    feed->add_bar(make_bar("AAA", 2024, 1, 6, 50, 50, 50, 50));
    feed->add_bar(make_bar("AAA", 2024, 1, 7, 50, 50, 50, 50));

    BacktestEngine engine(config);
    engine.set_data_feed(std::move(feed));
    engine.add_strategy(std::make_unique<ScriptedStrategy>(std::move(actions)));

    auto result = engine.run();

    EXPECT_DOUBLE_EQ(engine.get_portfolio().cash(Currency::USD), 94995.0);
    EXPECT_DOUBLE_EQ(result.final_nav, 99995.0);
}

// ============================================================================
// T12 - Defaults preserve legacy behavior
// ============================================================================

TEST(RealismTest, DefaultsPreserveLegacyBehavior) {
    auto config = base_config(); // slippage 0, no windows/borrow, half_spread 0

    std::map<int, ScriptedStrategy::Action> actions;
    actions[0] = [](const BarEvent&, StrategyContext& ctx) {
        ctx.submit_order(make_market_order("AAA", OrderSide::Buy, 100.0));
    };

    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar(make_bar("AAA", 2024, 1, 5, 50, 50, 50, 50));
    feed->add_bar(make_bar("AAA", 2024, 1, 6, 50, 50, 50, 50));

    BacktestEngine engine(config);
    engine.set_data_feed(std::move(feed));
    engine.add_strategy(std::make_unique<ScriptedStrategy>(std::move(actions)));

    auto result = engine.run();

    EXPECT_EQ(result.out_of_order_bars_dropped, 0);
    EXPECT_EQ(result.inactive_symbol_bars_dropped, 0);
    EXPECT_TRUE(result.delisting_events.empty());
    EXPECT_DOUBLE_EQ(result.borrow_cost_paid, 0.0);
    EXPECT_DOUBLE_EQ(engine.get_portfolio().cash(Currency::USD), 95000.0);
    EXPECT_DOUBLE_EQ(result.final_nav, 100000.0);
}

} // namespace
