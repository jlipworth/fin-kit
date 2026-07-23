#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <memory>
#include <ql/quantlib.hpp>
#include <string>
#include <utility>

import finkit.backtest;
import finkit.trading;
import finkit.types;

namespace {

using namespace finkit::backtest;
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
// Deterministic strategies (fixed share counts, no NAV-derived rounding)
// ============================================================================

class FixedBuyStrategy : public IStrategy {
public:
    FixedBuyStrategy(std::string sym, double qty) : sym_(std::move(sym)), qty_(qty) {}
    auto name() const -> std::string override { return "FixedBuy"; }
    auto id() const -> std::string override { return "fixed_buy_" + sym_; }
    void on_bar(const BarEvent& bar, StrategyContext& ctx) override {
        if (bar.symbol != sym_)
            return;
        if (!done_) {
            done_ = true;
            ctx.submit_order(make_market_order(sym_, OrderSide::Buy, qty_, id()));
        }
    }

private:
    std::string sym_;
    double qty_;
    bool done_{false};
};

class TwoBuyStrategy : public IStrategy {
public:
    explicit TwoBuyStrategy(std::string sym) : sym_(std::move(sym)) {}
    auto name() const -> std::string override { return "TwoBuy"; }
    auto id() const -> std::string override { return "two_buy_" + sym_; }
    void on_bar(const BarEvent& bar, StrategyContext& ctx) override {
        if (bar.symbol != sym_)
            return;
        ++n_;
        if (n_ == 1 || n_ == 3)
            ctx.submit_order(make_market_order(sym_, OrderSide::Buy, 50.0, id()));
    }

private:
    std::string sym_;
    int n_{0};
};

// ============================================================================
// FullBacktestKnownPnL — main happy path
// ============================================================================

TEST(IntegrationTest, FullBacktestKnownPnL) {
    BacktestConfig config{.start_date = ql::Date(2, ql::January, 2024),
                          .end_date = ql::Date(5, ql::January, 2024),
                          .initial_capital = 100000.0,
                          .base_currency = Currency::USD,
                          .slippage_bps = 0.0,
                          .commission_per_share = 0.0,
                          .commission_pct = 0.0};
    BacktestEngine engine(config);
    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar(make_bar("XYZ", 2024, 1, 2, 100, 100, 100, 100));
    feed->add_bar(make_bar("XYZ", 2024, 1, 3, 110, 110, 110, 110));
    feed->add_bar(make_bar("XYZ", 2024, 1, 4, 105, 105, 105, 105));
    feed->add_bar(make_bar("XYZ", 2024, 1, 5, 120, 120, 120, 120));
    engine.set_data_feed(std::move(feed));
    engine.add_strategy(std::make_unique<FixedBuyStrategy>("XYZ", 100.0));
    auto r = engine.run();

    EXPECT_EQ(r.total_trades, 1);
    EXPECT_EQ(r.winning_trades, 0);
    EXPECT_EQ(r.losing_trades, 0);
    EXPECT_DOUBLE_EQ(r.win_rate, 0.0);
    EXPECT_EQ(r.orders_rejected, 0);
    EXPECT_EQ(r.forced_liquidations, 0);
    EXPECT_DOUBLE_EQ(r.final_nav, 101000.0);
    EXPECT_DOUBLE_EQ(r.total_return_pct, 0.01);
    EXPECT_NEAR(r.max_drawdown_pct, 0.005, 1e-12);
    EXPECT_NEAR(r.sharpe_ratio, 4.597860488980133, 1e-9);

    const auto& pf = engine.get_portfolio();
    auto p = pf.position("XYZ");
    ASSERT_TRUE(p.has_value());
    EXPECT_DOUBLE_EQ(p->quantity, 100.0);
    EXPECT_DOUBLE_EQ(p->avg_cost, 110.0);
}

// ============================================================================
// FullBacktestRejectedOrder — rejected-order path
// ============================================================================

TEST(IntegrationTest, FullBacktestRejectedOrder) {
    BacktestConfig config{.start_date = ql::Date(2, ql::January, 2024),
                          .end_date = ql::Date(5, ql::January, 2024),
                          .initial_capital = 100000.0,
                          .base_currency = Currency::USD,
                          .slippage_bps = 0.0,
                          .commission_per_share = 0.0};
    config.risk.limits.default_position_limits.max_quantity = 50.0;

    BacktestEngine engine(config);
    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar(make_bar("XYZ", 2024, 1, 2, 100, 100, 100, 100));
    feed->add_bar(make_bar("XYZ", 2024, 1, 3, 110, 110, 110, 110));
    feed->add_bar(make_bar("XYZ", 2024, 1, 4, 105, 105, 105, 105));
    feed->add_bar(make_bar("XYZ", 2024, 1, 5, 120, 120, 120, 120));
    engine.set_data_feed(std::move(feed));
    engine.add_strategy(std::make_unique<TwoBuyStrategy>("XYZ"));
    auto r = engine.run();

    EXPECT_EQ(r.total_trades, 1);
    EXPECT_EQ(r.orders_rejected, 1);
    EXPECT_DOUBLE_EQ(r.final_nav, 100500.0);
    EXPECT_DOUBLE_EQ(r.total_return_pct, 0.005);
    auto p = engine.get_portfolio().position("XYZ");
    ASSERT_TRUE(p.has_value());
    EXPECT_DOUBLE_EQ(p->quantity, 50.0);
}

// ============================================================================
// FullBacktestNoStrategyNoTrades — sanity / regression
// ============================================================================

TEST(IntegrationTest, FullBacktestNoStrategyNoTrades) {
    BacktestConfig config{.start_date = ql::Date(2, ql::January, 2024),
                          .end_date = ql::Date(5, ql::January, 2024),
                          .initial_capital = 100000.0,
                          .base_currency = Currency::USD};
    BacktestEngine engine(config);
    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar(make_bar("XYZ", 2024, 1, 2, 100, 100, 100, 100));
    feed->add_bar(make_bar("XYZ", 2024, 1, 3, 110, 110, 110, 110));
    engine.set_data_feed(std::move(feed));
    auto r = engine.run();
    EXPECT_EQ(r.total_trades, 0);
    EXPECT_EQ(r.orders_rejected, 0);
    EXPECT_DOUBLE_EQ(r.final_nav, 100000.0);
    EXPECT_DOUBLE_EQ(r.total_return_pct, 0.0);
    EXPECT_DOUBLE_EQ(r.max_drawdown_pct, 0.0);
}

} // namespace
