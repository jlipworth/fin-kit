#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <ql/quantlib.hpp>

import finkit.backtest;
import finkit.trading;
import finkit.risk;
import finkit.types;

namespace {

using namespace finkit::backtest;
using namespace finkit::trading;
using namespace finkit::risk;
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
// Portfolio Tests
// ============================================================================

TEST(BacktestTest, PortfolioInitialCash) {
    Portfolio portfolio(Currency::USD);
    portfolio.deposit(1000000.0, Currency::USD);

    StaticFXProvider fx;
    EXPECT_DOUBLE_EQ(portfolio.cash(Currency::USD), 1000000.0);
    EXPECT_DOUBLE_EQ(portfolio.nav(fx), 1000000.0);
}

TEST(BacktestTest, PortfolioMultiCurrency) {
    Portfolio portfolio(Currency::USD);
    portfolio.deposit(1000000.0, Currency::USD);
    portfolio.deposit(100000.0, Currency::EUR);

    StaticFXProvider fx;
    fx.set_rate(Currency::EUR, Currency::USD, 1.10); // EUR/USD = 1.10
    double expected_nav = 1000000.0 + 100000.0 * 1.10;
    EXPECT_NEAR(portfolio.nav(fx), expected_nav, 0.01);
}

TEST(BacktestTest, PortfolioApplyBuyFill) {
    Portfolio portfolio(Currency::USD);
    portfolio.deposit(100000.0, Currency::USD);

    Fill fill{.order_id = OrderId{1},
              .fill_id = 1,
              .symbol = "AAPL",
              .side = OrderSide::Buy,
              .quantity = 100.0,
              .price = 150.0,
              .commission = 1.0,
              .fill_time = make_timestamp(2024, 1, 15),
              .is_partial = false,
              .cumulative_filled = 100.0};

    portfolio.apply_fill(fill);

    auto pos = portfolio.position("AAPL");
    ASSERT_TRUE(pos.has_value());
    EXPECT_DOUBLE_EQ(pos->quantity, 100.0);
    EXPECT_DOUBLE_EQ(pos->avg_cost, 150.0);
    EXPECT_DOUBLE_EQ(pos->market_price, 150.0);

    // Cash reduced by fill value + commission
    EXPECT_DOUBLE_EQ(portfolio.cash(Currency::USD), 100000.0 - 100.0 * 150.0 - 1.0);
}

TEST(BacktestTest, PortfolioApplySellFill) {
    Portfolio portfolio(Currency::USD);
    portfolio.deposit(100000.0, Currency::USD);

    // Buy first
    Fill buy_fill{.order_id = OrderId{1},
                  .fill_id = 1,
                  .symbol = "AAPL",
                  .side = OrderSide::Buy,
                  .quantity = 100.0,
                  .price = 150.0,
                  .commission = 1.0,
                  .fill_time = make_timestamp(2024, 1, 15)};
    portfolio.apply_fill(buy_fill);

    // Sell at higher price
    Fill sell_fill{.order_id = OrderId{2},
                   .fill_id = 2,
                   .symbol = "AAPL",
                   .side = OrderSide::Sell,
                   .quantity = 50.0,
                   .price = 160.0,
                   .commission = 1.0,
                   .fill_time = make_timestamp(2024, 1, 16)};
    portfolio.apply_fill(sell_fill);

    auto pos = portfolio.position("AAPL");
    ASSERT_TRUE(pos.has_value());
    EXPECT_DOUBLE_EQ(pos->quantity, 50.0);

    // Realized P&L: (160 - 150) * 50 = 500
    EXPECT_DOUBLE_EQ(pos->realized_pnl, 500.0);
}

TEST(BacktestTest, PortfolioDrawdown) {
    Portfolio portfolio(Currency::USD);
    portfolio.deposit(100000.0, Currency::USD);

    StaticFXProvider fx;

    // Set high water mark
    portfolio.update_high_water_mark(100000.0);
    EXPECT_DOUBLE_EQ(portfolio.high_water_mark(), 100000.0);
    EXPECT_DOUBLE_EQ(portfolio.drawdown(fx), 0.0);

    // Simulate loss by withdrawing
    portfolio.withdraw(10000.0, Currency::USD);

    // Drawdown = (100000 - 90000) / 100000 = 0.1 = 10%
    EXPECT_NEAR(portfolio.drawdown(fx), 0.1, 0.001);
}

// ============================================================================
// Data Feed Tests
// ============================================================================

TEST(BacktestTest, InMemoryDataFeedBasic) {
    InMemoryDataFeed feed;

    feed.add_bar(make_bar("SPY", 2024, 1, 2, 470.0, 475.0, 469.0, 474.0));
    feed.add_bar(make_bar("SPY", 2024, 1, 3, 474.0, 478.0, 473.0, 477.0));
    feed.add_bar(make_bar("SPY", 2024, 1, 4, 477.0, 480.0, 476.0, 479.0));

    EXPECT_TRUE(feed.has_more());

    auto bar1 = feed.next();
    ASSERT_TRUE(bar1.has_value());
    EXPECT_EQ(bar1->symbol, "SPY");
    EXPECT_DOUBLE_EQ(bar1->close, 474.0);

    auto bar2 = feed.next();
    ASSERT_TRUE(bar2.has_value());
    EXPECT_DOUBLE_EQ(bar2->close, 477.0);

    auto bar3 = feed.next();
    ASSERT_TRUE(bar3.has_value());
    EXPECT_DOUBLE_EQ(bar3->close, 479.0);

    EXPECT_FALSE(feed.has_more());
    EXPECT_FALSE(feed.next().has_value());
}

TEST(BacktestTest, InMemoryDataFeedReset) {
    InMemoryDataFeed feed;
    feed.add_bar(make_bar("SPY", 2024, 1, 2, 470.0, 475.0, 469.0, 474.0));
    feed.add_bar(make_bar("SPY", 2024, 1, 3, 474.0, 478.0, 473.0, 477.0));

    feed.next();
    feed.next();
    EXPECT_FALSE(feed.has_more());

    feed.reset();
    EXPECT_TRUE(feed.has_more());

    auto bar = feed.next();
    ASSERT_TRUE(bar.has_value());
    EXPECT_DOUBLE_EQ(bar->close, 474.0);
}

// ============================================================================
// Backtest Engine Tests
// ============================================================================

TEST(BacktestTest, BacktestEngineEmptyRun) {
    BacktestConfig config{.start_date = ql::Date(1, ql::January, 2024),
                          .end_date = ql::Date(31, ql::December, 2024),
                          .initial_capital = 100000.0,
                          .base_currency = Currency::USD};

    BacktestEngine engine(config);
    auto result = engine.run();

    // No data feed, should return empty result
    EXPECT_EQ(result.total_trades, 0);
}

TEST(BacktestTest, BacktestEngineBuyAndHold) {
    BacktestConfig config{.start_date = ql::Date(2, ql::January, 2024),
                          .end_date = ql::Date(5, ql::January, 2024),
                          .initial_capital = 100000.0,
                          .base_currency = Currency::USD,
                          .slippage_bps = 0.0,
                          .commission_per_share = 0.0};

    BacktestEngine engine(config);

    // Add data feed with 3 bars
    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar(make_bar("SPY", 2024, 1, 2, 470.0, 475.0, 469.0, 474.0)); // Day 1
    feed->add_bar(make_bar("SPY", 2024, 1, 3, 474.0, 478.0, 473.0, 477.0)); // Day 2
    feed->add_bar(make_bar("SPY", 2024, 1, 4, 477.0, 480.0, 476.0, 479.0)); // Day 3
    engine.set_data_feed(std::move(feed));

    // Add buy-and-hold strategy
    auto strategy = std::make_unique<BuyAndHoldStrategy>("SPY", 1.0);
    engine.add_strategy(std::move(strategy));

    auto result = engine.run();

    // Should have 1 trade (the initial buy)
    EXPECT_EQ(result.total_trades, 1);

    // Check final NAV - bought ~211 shares at 474, final price 479
    // Initial: 100000, Shares: floor(100000/474) = 210
    // Final NAV: 210 * 479 + (100000 - 210*474) = 100590
    // Actually this is approximate due to how position sizing works
    EXPECT_GT(result.final_nav, 100000.0); // Should have positive return
    EXPECT_GT(result.total_return_pct, 0.0);
}

// ============================================================================
// Strategy Tests
// ============================================================================

TEST(BacktestTest, BuyAndHoldStrategyIdentity) {
    BuyAndHoldStrategy strategy("AAPL", 0.5);

    EXPECT_EQ(strategy.name(), "BuyAndHold");
    EXPECT_EQ(strategy.id(), "buy_and_hold_AAPL");

    auto params = strategy.parameters();
    EXPECT_EQ(params["symbol"], "AAPL");
    EXPECT_EQ(params["target_weight"], "0.500000");
}

TEST(BacktestTest, MACrossoverStrategyIdentity) {
    MACrossoverStrategy strategy("SPY", 10, 50);

    EXPECT_EQ(strategy.name(), "MACrossover");
    EXPECT_EQ(strategy.id(), "ma_crossover_SPY");
}

// ============================================================================
// Expected Results with Known Data
// ============================================================================

// Test with precisely calculated expected values
TEST(BacktestTest, PrecisePortfolioPnL) {
    Portfolio portfolio(Currency::USD);
    portfolio.deposit(10000.0, Currency::USD);

    StaticFXProvider fx;

    // Buy 10 shares at $100
    Fill buy{.order_id = OrderId{1},
             .fill_id = 1,
             .symbol = "TEST",
             .side = OrderSide::Buy,
             .quantity = 10.0,
             .price = 100.0,
             .commission = 0.0,
             .fill_time = make_timestamp(2024, 1, 1)};
    portfolio.apply_fill(buy);

    // Cash: 10000 - 10*100 = 9000
    EXPECT_DOUBLE_EQ(portfolio.cash(Currency::USD), 9000.0);

    // Mark to market at $110
    portfolio.mark_to_market("TEST", 110.0, make_timestamp(2024, 1, 2));

    auto pos = portfolio.position("TEST");
    ASSERT_TRUE(pos.has_value());
    EXPECT_DOUBLE_EQ(pos->market_price, 110.0);

    // NAV: 9000 + 10*110 = 10100
    EXPECT_DOUBLE_EQ(portfolio.nav(fx), 10100.0);

    // Unrealized P&L: 10 * (110 - 100) = 100
    EXPECT_DOUBLE_EQ(pos->unrealized_pnl(), 100.0);

    // Sell 5 shares at $110
    Fill sell{.order_id = OrderId{2},
              .fill_id = 2,
              .symbol = "TEST",
              .side = OrderSide::Sell,
              .quantity = 5.0,
              .price = 110.0,
              .commission = 0.0,
              .fill_time = make_timestamp(2024, 1, 2)};
    portfolio.apply_fill(sell);

    // Cash: 9000 + 5*110 = 9550
    EXPECT_DOUBLE_EQ(portfolio.cash(Currency::USD), 9550.0);

    // Remaining position: 5 shares
    pos = portfolio.position("TEST");
    ASSERT_TRUE(pos.has_value());
    EXPECT_DOUBLE_EQ(pos->quantity, 5.0);

    // Realized P&L from sale: 5 * (110 - 100) = 50
    EXPECT_DOUBLE_EQ(pos->realized_pnl, 50.0);

    // NAV: 9550 + 5*110 = 10100 (unchanged, just moved from position to cash)
    EXPECT_DOUBLE_EQ(portfolio.nav(fx), 10100.0);
}

TEST(BacktestTest, PreciseDrawdownCalculation) {
    Portfolio portfolio(Currency::USD);
    portfolio.deposit(100.0, Currency::USD);

    StaticFXProvider fx;

    // Start at 100
    portfolio.update_high_water_mark(100.0);
    EXPECT_DOUBLE_EQ(portfolio.drawdown(fx), 0.0);

    // Grow to 120 (high water mark updates)
    portfolio.deposit(20.0, Currency::USD);
    portfolio.update_high_water_mark(portfolio.nav(fx));
    EXPECT_DOUBLE_EQ(portfolio.high_water_mark(), 120.0);
    EXPECT_DOUBLE_EQ(portfolio.drawdown(fx), 0.0);

    // Drop to 90 (25% drawdown from 120)
    portfolio.withdraw(30.0, Currency::USD);
    // Drawdown = (120 - 90) / 120 = 0.25
    EXPECT_DOUBLE_EQ(portfolio.drawdown(fx), 0.25);

    // Recover to 100 (still 16.67% from high water mark of 120)
    portfolio.deposit(10.0, Currency::USD);
    // Drawdown = (120 - 100) / 120 = 0.1667
    EXPECT_NEAR(portfolio.drawdown(fx), 1.0 / 6.0, 0.0001);
}

} // namespace
