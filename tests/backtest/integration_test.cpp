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

    // --- Newly populated BacktestResult metrics (hand-derived) --------------
    // The daily-return series is r = [0, 0, -0.005, 1500/99500] (n = 4):
    //   NAV path 100000, 100000, 99500, 101000 ⇒ returns above.
    //   mean = 401/159200 = 0.0025188442211055272.
    //
    // Sortino (stats convention): only ONE sub-target (< 0) observation (-0.005),
    // and the standalone sortino_ratio needs >= 2 downside observations ⇒ 0.0.
    EXPECT_DOUBLE_EQ(r.sortino_ratio, 0.0);
    // CAGR (geometric, years = n/252 = 4/252): total_return = 0.01 ⇒
    //   (1.01)^(252/4) − 1 = 1.01^63 − 1 = 0.8717444252272819.
    EXPECT_NEAR(r.cagr, 0.8717444252272819, 1e-9);
    // Annualized return (arithmetic): mean * 252 = 0.0025188442211055272 * 252
    //   = 0.6347487437185929.
    EXPECT_NEAR(r.annualized_return_pct, 0.6347487437185929, 1e-9);
    // Calmar (stats): geometric annualized return / return-series max drawdown
    //   = 0.8717444252272819 / 0.005 = 174.34888504545623.
    EXPECT_NEAR(r.calmar_ratio, 174.34888504545623, 1e-6);
    // Max drawdown duration: peak at 1/3 (NAV 100000), trough 1/4, recovery to a new
    //   high on 1/5 ⇒ 1/5 − 1/3 = 2 calendar days.
    EXPECT_DOUBLE_EQ(r.max_drawdown_duration_days, 2.0);
    // Average (time-weighted) drawdown: per-bar drawdowns [0, 0, 0.005, 0], mean
    //   = 0.005/4 = 0.00125.
    EXPECT_NEAR(r.avg_drawdown_pct, 0.00125, 1e-12);
    // Trade stats: the single fill is an opening buy (realized P&L 0), so there are
    //   no winners, no losers, no completed round trip, and no gross loss.
    EXPECT_DOUBLE_EQ(r.profit_factor, 0.0); // no losing trades ⇒ undefined ⇒ 0
    EXPECT_DOUBLE_EQ(r.avg_trade_pnl, 0.0);
    EXPECT_DOUBLE_EQ(r.avg_winner, 0.0);
    EXPECT_DOUBLE_EQ(r.avg_loser, 0.0);
    EXPECT_DOUBLE_EQ(r.avg_holding_period_days, 0.0); // position never closed
    EXPECT_EQ(r.risk_breaches, 0);                    // monitor emitted no actions

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
// Two-round-trip strategy: buy→sell (winner), buy→sell (loser), scheduled by the
// per-symbol bar index. Exercises the trade-statistics and return-based metrics.
// ============================================================================

class RoundTripStrategy : public IStrategy {
public:
    explicit RoundTripStrategy(std::string sym) : sym_(std::move(sym)) {}
    auto name() const -> std::string override { return "RoundTrip"; }
    auto id() const -> std::string override { return "round_trip_" + sym_; }
    void on_bar(const BarEvent& bar, StrategyContext& ctx) override {
        if (bar.symbol != sym_)
            return;
        const int i = n_++;
        if (i == 0 || i == 5)
            ctx.submit_order(make_market_order(sym_, OrderSide::Buy, 100.0, id()));
        else if (i == 3 || i == 8)
            ctx.submit_order(make_market_order(sym_, OrderSide::Sell, 100.0, id()));
    }

private:
    std::string sym_;
    int n_{0};
};

// ============================================================================
// FullBacktestRoundTripMetrics — pins the trade + return metrics on a scenario
// with two completed round trips (one winner, one loser) and four down days.
// ============================================================================

TEST(IntegrationTest, FullBacktestRoundTripMetrics) {
    BacktestConfig config{.start_date = ql::Date(2, ql::January, 2024),
                          .end_date = ql::Date(11, ql::January, 2024),
                          .initial_capital = 100000.0,
                          .base_currency = Currency::USD,
                          .slippage_bps = 0.0,
                          .commission_per_share = 0.0,
                          .commission_pct = 0.0};
    BacktestEngine engine(config);
    auto feed = std::make_unique<InMemoryDataFeed>();
    // Flat bars (o=h=l=c) so the market-order fill price equals the bar price
    // regardless of fill model. Market orders fill on the NEXT bar.
    feed->add_bar(make_bar("RT", 2024, 1, 2, 100, 100, 100, 100));  // i0: submit BUY
    feed->add_bar(make_bar("RT", 2024, 1, 3, 100, 100, 100, 100));  // i1: BUY fills @100
    feed->add_bar(make_bar("RT", 2024, 1, 4, 98, 98, 98, 98));      // i2: mark 98 (down)
    feed->add_bar(make_bar("RT", 2024, 1, 5, 96, 96, 96, 96));      // i3: mark 96 (down), submit SELL
    feed->add_bar(make_bar("RT", 2024, 1, 6, 110, 110, 110, 110));  // i4: SELL fills @110, close RT1
    feed->add_bar(make_bar("RT", 2024, 1, 7, 110, 110, 110, 110));  // i5: submit BUY
    feed->add_bar(make_bar("RT", 2024, 1, 8, 110, 110, 110, 110));  // i6: BUY fills @110
    feed->add_bar(make_bar("RT", 2024, 1, 9, 112, 112, 112, 112));  // i7: mark 112 (up)
    feed->add_bar(make_bar("RT", 2024, 1, 10, 109, 109, 109, 109)); // i8: mark 109 (down), submit SELL
    feed->add_bar(make_bar("RT", 2024, 1, 11, 105, 105, 105, 105)); // i9: SELL fills @105, close RT2
    engine.set_data_feed(std::move(feed));
    engine.add_strategy(std::make_unique<RoundTripStrategy>("RT"));
    auto r = engine.run();

    // Four fills: BUY@100, SELL@110 (RT1), BUY@110, SELL@105 (RT2).
    //   RT1 realized = (110 − 100) * 100 = +1000 (winner)
    //   RT2 realized = (105 − 110) * 100 =  −500 (loser)
    // Opening buys realize 0.
    EXPECT_EQ(r.total_trades, 4);
    EXPECT_EQ(r.winning_trades, 1);
    EXPECT_EQ(r.losing_trades, 1);
    EXPECT_DOUBLE_EQ(r.win_rate, 0.25);
    EXPECT_DOUBLE_EQ(r.largest_winner, 1000.0);
    EXPECT_DOUBLE_EQ(r.largest_loser, -500.0);
    EXPECT_DOUBLE_EQ(r.avg_winner, 1000.0);  // 1000 / 1
    EXPECT_DOUBLE_EQ(r.avg_loser, -500.0);   // −500 / 1
    EXPECT_DOUBLE_EQ(r.profit_factor, 2.0);  // gross 1000 / gross 500
    EXPECT_DOUBLE_EQ(r.avg_trade_pnl, 125.0);      // (0 + 1000 + 0 − 500) / 4
    EXPECT_DOUBLE_EQ(r.avg_holding_period_days, 3.0); // RT1 1/3→1/6, RT2 1/8→1/11

    EXPECT_DOUBLE_EQ(r.final_nav, 100500.0);
    EXPECT_DOUBLE_EQ(r.total_return_pct, 0.005);

    // NAV path: 100000, 100000, 99800, 99600, 101000, 101000, 101000, 101200,
    //           100900, 100500  ⇒ daily returns (n=10):
    //   [0, 0, −0.002, −1000/499000, 1400/99600, 0, 0, 200/101000,
    //    −300/101200, −400/100900]  (four sub-zero observations).
    // Values below are the stats-module results for that return series.
    EXPECT_NEAR(r.sharpe_ratio, 1.5977520821878204, 1e-9);
    EXPECT_NEAR(r.sortino_ratio, 4.492718590047375, 1e-9);
    EXPECT_NEAR(r.calmar_ratio, 19.36188931222231, 1e-6);
    EXPECT_NEAR(r.cagr, 0.13392611184343295, 1e-9);
    EXPECT_NEAR(r.annualized_return_pct, 0.12861240628037865, 1e-9);
    // NAV-based drawdown: peak 101200 (1/9), trough 100500 (1/11) ⇒ 700/101200.
    EXPECT_NEAR(r.max_drawdown_pct, 0.00691699604743083, 1e-12);
    EXPECT_NEAR(r.avg_drawdown_pct, 0.0015881422924901186, 1e-12);
    // Longest peak-to-recovery span: the peak on 1/3 (NAV 100000) stays underwater
    //   through 1/4, 1/5 and recovers to a new high on 1/6 ⇒ 1/6 − 1/3 = 3 days. The
    //   final underwater run (peak 1/9 → unrecovered at end 1/11) is only 2 days.
    EXPECT_DOUBLE_EQ(r.max_drawdown_duration_days, 3.0);

    EXPECT_EQ(r.orders_rejected, 0);
    EXPECT_EQ(r.forced_liquidations, 0);
    EXPECT_EQ(r.risk_breaches, 0);

    auto p = engine.get_portfolio().position("RT");
    ASSERT_TRUE(p.has_value());
    EXPECT_TRUE(p->is_flat());
}

// ============================================================================
// DrawdownBreachForcedLiquidationExecutes — end-to-end regression for the
// risk-engine liquidation flow: the engine's own liquidation order must pass
// the symbol lock (it previously deadlocked on it and never flattened the
// book), and monitor() must latch (previously it re-fired a fresh Liquidate
// event with duplicate orders on every bar while the breach persisted).
// ============================================================================

TEST(IntegrationTest, DrawdownBreachForcedLiquidationExecutes) {
    BacktestConfig config{.start_date = ql::Date(2, ql::January, 2024),
                          .end_date = ql::Date(5, ql::January, 2024),
                          .initial_capital = 100000.0,
                          .base_currency = Currency::USD,
                          .slippage_bps = 0.0,
                          .commission_per_share = 0.0,
                          .commission_pct = 0.0};
    config.risk.limits.pnl.max_drawdown_pct = 0.10;

    BacktestEngine engine(config);
    auto feed = std::make_unique<InMemoryDataFeed>();
    // NAV path: buy 900 @ 100 fills on 1/3 (cash 10000, position 90000).
    // 1/4 marks 88: nav = 10000 + 900*88 = 89200, drawdown 10.8% > 10% ->
    // monitor emits ONE Liquidate event and a sell-900 order; the sell fills
    // on 1/5 @ 88, realizing (88 - 100) * 900 = -10800 and flattening the book.
    feed->add_bar(make_bar("XYZ", 2024, 1, 2, 100, 100, 100, 100));
    feed->add_bar(make_bar("XYZ", 2024, 1, 3, 100, 100, 100, 100));
    feed->add_bar(make_bar("XYZ", 2024, 1, 4, 88, 88, 88, 88));
    feed->add_bar(make_bar("XYZ", 2024, 1, 5, 88, 88, 88, 88));
    engine.set_data_feed(std::move(feed));
    engine.add_strategy(std::make_unique<FixedBuyStrategy>("XYZ", 900.0));
    auto r = engine.run();

    // The liquidation order must NOT be rejected by its own symbol lock.
    EXPECT_EQ(r.orders_rejected, 0);
    // One breach, one Liquidate event (latched), one forced liquidation.
    EXPECT_EQ(r.risk_breaches, 1);
    EXPECT_EQ(r.forced_liquidations, 1);
    // Both fills happened: opening buy + liquidation sell.
    EXPECT_EQ(r.total_trades, 2);
    EXPECT_EQ(r.losing_trades, 1);
    EXPECT_DOUBLE_EQ(r.largest_loser, -10800.0);
    // Book flattened at the liquidation price: nav = 10000 + 900*88 = 89200.
    EXPECT_DOUBLE_EQ(r.final_nav, 89200.0);
    auto p = engine.get_portfolio().position("XYZ");
    ASSERT_TRUE(p.has_value());
    EXPECT_TRUE(p->is_flat());
}

// ============================================================================
// InstrumentCurrencyFlowsIntoPortfolio — regression: the engine hardcoded USD
// into Portfolio::apply_fill, so a non-USD instrument's cash leg landed in the
// USD bucket and its position value was never FX-converted in NAV.
// ============================================================================

TEST(IntegrationTest, InstrumentCurrencyFlowsIntoPortfolio) {
    BacktestConfig config{.start_date = ql::Date(2, ql::January, 2024),
                          .end_date = ql::Date(5, ql::January, 2024),
                          .initial_capital = 100000.0,
                          .base_currency = Currency::USD,
                          .slippage_bps = 0.0,
                          .commission_per_share = 0.0,
                          .commission_pct = 0.0};
    // EUR-denominated instrument, declared via risk reference data.
    config.risk.instrument_info["EFOO"].currency = Currency::EUR;

    BacktestEngine engine(config);
    auto fx = std::make_unique<StaticFXProvider>();
    fx->set_rate(Currency::EUR, Currency::USD, 1.25);
    engine.set_fx_provider(std::move(fx));

    auto feed = std::make_unique<InMemoryDataFeed>();
    feed->add_bar(make_bar("EFOO", 2024, 1, 2, 50, 50, 50, 50)); // submit buy 100
    feed->add_bar(make_bar("EFOO", 2024, 1, 3, 50, 50, 50, 50)); // fills @ EUR 50
    feed->add_bar(make_bar("EFOO", 2024, 1, 4, 60, 60, 60, 60)); // marks EUR 60
    engine.set_data_feed(std::move(feed));
    engine.add_strategy(std::make_unique<FixedBuyStrategy>("EFOO", 100.0));
    auto r = engine.run();

    const auto& pf = engine.get_portfolio();
    // The cash leg is EUR -5000 (100 @ EUR 50), NOT USD -5000.
    EXPECT_DOUBLE_EQ(pf.cash(Currency::EUR), -5000.0);
    EXPECT_DOUBLE_EQ(pf.cash(Currency::USD), 100000.0);
    auto p = pf.position("EFOO");
    ASSERT_TRUE(p.has_value());
    EXPECT_EQ(p->currency, Currency::EUR);
    // NAV (USD): 100000 + 1.25 * (-5000 + 100*60) = 100000 + 1250 = 101250.
    EXPECT_DOUBLE_EQ(r.final_nav, 101250.0);
    EXPECT_DOUBLE_EQ(r.total_return_pct, 0.0125);
}

// ============================================================================
// MultiBarDaysSampleOneReturnPerDay — regression: "daily" returns were sampled
// per BAR, so a multi-symbol/intraday feed produced several observations per
// day that were still annualized with sqrt(252), understating Sharpe by
// ~sqrt(bars-per-day).
// ============================================================================

TEST(IntegrationTest, MultiBarDaysSampleOneReturnPerDay) {
    BacktestConfig config{.start_date = ql::Date(2, ql::January, 2024),
                          .end_date = ql::Date(3, ql::January, 2024),
                          .initial_capital = 100000.0,
                          .base_currency = Currency::USD,
                          .slippage_bps = 0.0,
                          .commission_per_share = 0.0,
                          .commission_pct = 0.0};
    BacktestEngine engine(config);
    auto feed = std::make_unique<InMemoryDataFeed>();
    // TWO bars per calendar day (equal timestamps are legal and delivered).
    feed->add_bar(make_bar("XYZ", 2024, 1, 2, 100, 100, 100, 100)); // submit buy
    feed->add_bar(make_bar("XYZ", 2024, 1, 2, 100, 100, 100, 100)); // fills @100
    feed->add_bar(make_bar("XYZ", 2024, 1, 3, 121, 121, 121, 121));
    feed->add_bar(make_bar("XYZ", 2024, 1, 3, 121, 121, 121, 121));
    engine.set_data_feed(std::move(feed));
    engine.add_strategy(std::make_unique<FixedBuyStrategy>("XYZ", 100.0));
    auto r = engine.run();

    // Daily returns are [0, 0.021] (day1 flat, day2 +2100/100000): n = 2, one
    // per DAY, not one per bar.
    //   mean = 0.0105; sample std = 0.0105*sqrt(2)
    //   sharpe = mean/std * sqrt(252) = sqrt(252/2) = sqrt(126).
    // Per-bar sampling ([0, 0, 0.021, 0], n = 4) gave 0.5*sqrt(252) ~ 7.94.
    EXPECT_NEAR(r.sharpe_ratio, std::sqrt(126.0), 1e-12);
    // Arithmetic annualization over the DAILY series: 0.0105 * 252 = 2.646.
    EXPECT_NEAR(r.annualized_return_pct, 2.646, 1e-12);
    EXPECT_DOUBLE_EQ(r.final_nav, 102100.0);
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
