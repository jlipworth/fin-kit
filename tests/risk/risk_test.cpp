#include <chrono>
#include <cmath>
#include <gtest/gtest.h>
#include <map>
#include <optional>
#include <ql/quantlib.hpp>
#include <string>

import finkit.risk;
import finkit.trading;
import finkit.types;

namespace {

using namespace finkit::risk;
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

// ============================================================================
// Test-local portfolio implementing finkit::risk::IPortfolio
// ============================================================================

class MockPortfolio : public IPortfolio {
public:
    void set_position(const Position& p) { positions_[p.symbol] = p; }
    void set_nav(double n) { nav_ = n; }
    void set_hwm(double h) { hwm_ = h; }
    void set_cash(Currency c, double v) { cash_[c] = v; }

    auto positions() const -> const std::map<std::string, Position>& override { return positions_; }
    auto position(const std::string& s) const -> std::optional<Position> override {
        auto it = positions_.find(s);
        return it != positions_.end() ? std::optional{it->second} : std::nullopt;
    }
    auto nav(const IFXRateProvider&) const -> double override { return nav_; }
    auto cash(Currency c) const -> double override {
        auto it = cash_.find(c);
        return it != cash_.end() ? it->second : 0.0;
    }
    auto high_water_mark() const -> double override { return hwm_; }

private:
    std::map<std::string, Position> positions_;
    std::map<Currency, double> cash_;
    double nav_{0.0};
    double hwm_{0.0};
};

auto pos(const std::string& s, double qty, double price,
         Currency c = Currency::USD) -> Position {
    return Position{.symbol = s, .quantity = qty, .avg_cost = price, .market_price = price,
                    .currency = c};
}

// ============================================================================
// StaticFXProvider
// ============================================================================

TEST(RiskTest, FXProviderRatesAndConvert) {
    StaticFXProvider fx;
    fx.set_rate(Currency::EUR, Currency::USD, 1.25);
    EXPECT_DOUBLE_EQ(fx.rate(Currency::EUR, Currency::USD), 1.25);
    EXPECT_DOUBLE_EQ(fx.rate(Currency::USD, Currency::EUR), 0.8);
    EXPECT_DOUBLE_EQ(fx.rate(Currency::USD, Currency::USD), 1.0);
    EXPECT_DOUBLE_EQ(fx.rate(Currency::GBP, Currency::JPY), 1.0);
    EXPECT_DOUBLE_EQ(fx.convert(1000.0, Currency::EUR, Currency::USD), 1250.0);
}

// ============================================================================
// Limit / config defaults
// ============================================================================

TEST(RiskTest, LimitDefaults) {
    PositionLimits pl;
    EXPECT_DOUBLE_EQ(pl.max_quantity, 0.0);
    EXPECT_DOUBLE_EQ(pl.max_concentration_pct, 1.0);
    PortfolioLimits pf;
    EXPECT_DOUBLE_EQ(pf.max_gross_exposure, 0.0);
    EXPECT_DOUBLE_EQ(pf.max_leverage, 0.0);
    PnLLimits pn;
    EXPECT_DOUBLE_EQ(pn.max_drawdown_pct, 1.0);
    RiskConfig rc;
    EXPECT_TRUE(rc.check_pre_trade);
}

// ============================================================================
// get_metrics — exposure aggregation & FX conversion
// ============================================================================

TEST(RiskTest, MetricsExposureAndFX) {
    StandardRiskEngine eng;
    StaticFXProvider fx;
    fx.set_rate(Currency::EUR, Currency::USD, 1.2);
    MockPortfolio pf;
    pf.set_position(pos("FOO", 100.0, 10.0, Currency::EUR)); // notional 1000 EUR
    pf.set_position(pos("BAR", -50.0, 20.0, Currency::USD)); // notional 1000 USD (short)
    pf.set_nav(2200.0);
    pf.set_hwm(2750.0);
    auto m = eng.get_metrics(pf, fx);
    EXPECT_DOUBLE_EQ(m.gross_exposure, 2200.0);
    EXPECT_DOUBLE_EQ(m.long_exposure, 1200.0);
    EXPECT_DOUBLE_EQ(m.short_exposure, 1000.0);
    EXPECT_DOUBLE_EQ(m.net_exposure, 200.0);
    EXPECT_DOUBLE_EQ(m.leverage, 1.0);
    EXPECT_DOUBLE_EQ(m.drawdown_pct, 0.2);
}

// ============================================================================
// Pre-trade: per-symbol position limits (max_quantity)
// ============================================================================

TEST(RiskTest, PreTradeMaxQtyAllowAtLimit) {
    RiskConfig cfg;
    cfg.limits.default_position_limits.max_quantity = 100.0;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    MockPortfolio pf;
    auto r = eng.check_pre_trade(make_market_order("AAPL", OrderSide::Buy, 100.0), pf, fx);
    EXPECT_EQ(r.decision, RiskDecision::Allow);
}

TEST(RiskTest, PreTradeMaxQtyReduceFresh) {
    RiskConfig cfg;
    cfg.limits.default_position_limits.max_quantity = 100.0;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    MockPortfolio pf;
    auto r = eng.check_pre_trade(make_market_order("AAPL", OrderSide::Buy, 150.0), pf, fx);
    EXPECT_EQ(r.decision, RiskDecision::Reduce);
    ASSERT_TRUE(r.adjusted_quantity.has_value());
    EXPECT_DOUBLE_EQ(*r.adjusted_quantity, 100.0);
    ASSERT_FALSE(r.violated_limits.empty());
    EXPECT_EQ(r.violated_limits[0], "position.max_quantity");
}

TEST(RiskTest, PreTradeMaxQtyRejectAtCap) {
    RiskConfig cfg;
    cfg.limits.default_position_limits.max_quantity = 100.0;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    MockPortfolio pf;
    pf.set_position(pos("AAPL", 100.0, 10.0));
    auto r = eng.check_pre_trade(make_market_order("AAPL", OrderSide::Buy, 50.0), pf, fx);
    EXPECT_EQ(r.decision, RiskDecision::Reject);
    EXPECT_EQ(r.reason, "Exceeds max quantity limit");
}

TEST(RiskTest, PreTradePerSymbolOverride) {
    RiskConfig cfg;
    cfg.limits.default_position_limits.max_quantity = 1000.0;
    cfg.limits.position_limits["AAPL"].max_quantity = 10.0;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    MockPortfolio pf;
    EXPECT_EQ(
        eng.check_pre_trade(make_market_order("AAPL", OrderSide::Buy, 20.0), pf, fx).decision,
        RiskDecision::Reduce);
    EXPECT_EQ(
        eng.check_pre_trade(make_market_order("MSFT", OrderSide::Buy, 20.0), pf, fx).decision,
        RiskDecision::Allow);
}

// ============================================================================
// Pre-trade: concentration limit (max_concentration_pct)
// ============================================================================

TEST(RiskTest, PreTradeConcentrationAllow) {
    RiskConfig cfg;
    cfg.limits.default_position_limits.max_quantity = 0.0;
    cfg.limits.default_position_limits.max_concentration_pct = 0.7;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    MockPortfolio pf;
    pf.set_position(pos("AAPL", 50.0, 100.0));
    pf.set_nav(10000.0);
    auto r = eng.check_pre_trade(make_market_order("AAPL", OrderSide::Buy, 10.0), pf, fx);
    EXPECT_EQ(r.decision, RiskDecision::Allow); // 60*100/10000 = 0.6 <= 0.7
}

TEST(RiskTest, PreTradeConcentrationReject) {
    RiskConfig cfg;
    cfg.limits.default_position_limits.max_quantity = 0.0;
    cfg.limits.default_position_limits.max_concentration_pct = 0.7;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    MockPortfolio pf;
    pf.set_position(pos("AAPL", 50.0, 100.0));
    pf.set_nav(10000.0);
    auto r = eng.check_pre_trade(make_market_order("AAPL", OrderSide::Buy, 30.0), pf, fx);
    EXPECT_EQ(r.decision, RiskDecision::Reject); // 80*100/10000 = 0.8 > 0.7
    EXPECT_EQ(r.reason, "Exceeds concentration limit");
    ASSERT_FALSE(r.violated_limits.empty());
    EXPECT_EQ(r.violated_limits[0], "position.max_concentration");
}

// ============================================================================
// Pre-trade: portfolio gross exposure (reject-only)
// ============================================================================

TEST(RiskTest, PreTradeGrossReject) {
    RiskConfig cfg;
    cfg.limits.portfolio.max_gross_exposure = 6000.0;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    MockPortfolio pf;
    pf.set_position(pos("AAPL", 100.0, 50.0)); // gross 5000
    pf.set_nav(10000.0);
    auto r = eng.check_pre_trade(make_market_order("AAPL", OrderSide::Buy, 30.0), pf, fx);
    EXPECT_EQ(r.decision, RiskDecision::Reject); // new gross 6500 > 6000
    ASSERT_FALSE(r.violated_limits.empty());
    EXPECT_EQ(r.violated_limits[0], "portfolio.max_gross_exposure");
}

TEST(RiskTest, PreTradeGrossAllow) {
    RiskConfig cfg;
    cfg.limits.portfolio.max_gross_exposure = 7000.0;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    MockPortfolio pf;
    pf.set_position(pos("AAPL", 100.0, 50.0));
    pf.set_nav(10000.0);
    auto r = eng.check_pre_trade(make_market_order("AAPL", OrderSide::Buy, 30.0), pf, fx);
    EXPECT_EQ(r.decision, RiskDecision::Allow); // 6500 < 7000
}

// ============================================================================
// Pre-trade: leverage (reject-only)
// ============================================================================

TEST(RiskTest, PreTradeLeverageReject) {
    RiskConfig cfg;
    cfg.limits.portfolio.max_gross_exposure = 0.0;
    cfg.limits.portfolio.max_leverage = 0.9;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    MockPortfolio pf;
    pf.set_position(pos("AAPL", 100.0, 50.0)); // gross 5000
    pf.set_nav(10000.0);
    auto r = eng.check_pre_trade(make_market_order("AAPL", OrderSide::Buy, 100.0), pf, fx);
    EXPECT_EQ(r.decision, RiskDecision::Reject); // new_leverage 1.0 > 0.9
    ASSERT_FALSE(r.violated_limits.empty());
    EXPECT_EQ(r.violated_limits[0], "portfolio.max_leverage");
}

TEST(RiskTest, PreTradeLeverageAllow) {
    RiskConfig cfg;
    cfg.limits.portfolio.max_gross_exposure = 0.0;
    cfg.limits.portfolio.max_leverage = 1.5;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    MockPortfolio pf;
    pf.set_position(pos("AAPL", 100.0, 50.0));
    pf.set_nav(10000.0);
    auto r = eng.check_pre_trade(make_market_order("AAPL", OrderSide::Buy, 100.0), pf, fx);
    EXPECT_EQ(r.decision, RiskDecision::Allow); // 1.0 < 1.5
}

// ============================================================================
// Unenforced limits (lock current behavior)
// ============================================================================

TEST(RiskTest, PreTradeMaxNotionalNotEnforced) {
    RiskConfig cfg;
    cfg.limits.default_position_limits.max_notional = 1.0;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    MockPortfolio pf;
    EXPECT_EQ(
        eng.check_pre_trade(make_market_order("AAPL", OrderSide::Buy, 1000.0), pf, fx).decision,
        RiskDecision::Allow);
}

// ============================================================================
// check_post_trade is a no-op stub
// ============================================================================

TEST(RiskTest, PostTradeAlwaysAllow) {
    StandardRiskEngine eng;
    StaticFXProvider fx;
    MockPortfolio pf;
    Fill f{.order_id = OrderId{1}, .symbol = "AAPL", .side = OrderSide::Buy, .quantity = 1e9,
           .price = 1e9};
    EXPECT_EQ(eng.check_post_trade(f, pf, fx).decision, RiskDecision::Allow);
}

// ============================================================================
// monitor — drawdown auto-liquidation
// ============================================================================

TEST(RiskTest, MonitorTriggersLiquidation) {
    RiskConfig cfg;
    cfg.limits.pnl.max_drawdown_pct = 0.10;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    MockPortfolio pf;
    pf.set_hwm(100000.0);
    pf.set_nav(85000.0); // drawdown 0.15 > 0.10
    pf.set_position(pos("AAPL", 100.0, 50.0));
    pf.set_position(pos("MSFT", -40.0, 25.0));
    auto actions = eng.monitor(pf, fx, make_timestamp(2024, 6, 1));
    ASSERT_EQ(actions.size(), 1u);
    const auto& a = actions[0];
    EXPECT_EQ(a.action, RiskActionEvent::Action::Liquidate);
    EXPECT_EQ(a.limit_name, "max_drawdown");
    EXPECT_EQ(a.affected_symbols.size(), 2u);
    ASSERT_EQ(a.generated_orders.size(), 2u);
    EXPECT_EQ(a.generated_orders[0].symbol, "AAPL");
    EXPECT_EQ(a.generated_orders[0].side, OrderSide::Sell);
    EXPECT_DOUBLE_EQ(a.generated_orders[0].quantity, 100.0);
    EXPECT_EQ(a.generated_orders[0].metadata.at("source"), "risk_liquidation");
    EXPECT_EQ(a.generated_orders[1].symbol, "MSFT");
    EXPECT_EQ(a.generated_orders[1].side, OrderSide::Buy);
    EXPECT_DOUBLE_EQ(a.generated_orders[1].quantity, 40.0);
    EXPECT_EQ(eng.get_state(), PortfolioState::Liquidating);
    EXPECT_TRUE(eng.is_symbol_locked("AAPL"));
    EXPECT_TRUE(eng.is_symbol_locked("MSFT"));
}

TEST(RiskTest, MonitorNoTriggerBelowLimit) {
    RiskConfig cfg;
    cfg.limits.pnl.max_drawdown_pct = 0.20;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    MockPortfolio pf;
    pf.set_hwm(100000.0);
    pf.set_nav(85000.0); // drawdown 0.15 < 0.20
    EXPECT_TRUE(eng.monitor(pf, fx, make_timestamp(2024, 6, 1)).empty());
    EXPECT_EQ(eng.get_state(), PortfolioState::Normal);
}

TEST(RiskTest, MonitorNoTriggerZeroNav) {
    StandardRiskEngine eng(RiskConfig{});
    StaticFXProvider fx;
    MockPortfolio pf;
    pf.set_hwm(100000.0);
    pf.set_nav(0.0);
    EXPECT_TRUE(eng.monitor(pf, fx, make_timestamp(2024, 6, 1)).empty());
}

// ============================================================================
// Liquidation-mode gating in check_pre_trade + reset_liquidation
// ============================================================================

TEST(RiskTest, PreTradeLiquidationGating) {
    RiskConfig cfg;
    cfg.limits.pnl.max_drawdown_pct = 0.10;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    MockPortfolio pf;
    pf.set_hwm(100000.0);
    pf.set_nav(85000.0);
    pf.set_position(pos("AAPL", 100.0, 50.0));
    pf.set_position(pos("MSFT", -40.0, 25.0));
    (void)eng.monitor(pf, fx, make_timestamp(2024, 6, 1)); // trigger liquidation

    // (a) Increasing order on a position in liquidation -> Reject (conflicts_with_liquidation).
    // AAPL is locked, but the liquidation-mode gate is checked first for a non-reducing order.
    auto inc = eng.check_pre_trade(make_market_order("AAPL", OrderSide::Buy, 10.0), pf, fx);
    EXPECT_EQ(inc.decision, RiskDecision::Reject);
    EXPECT_TRUE(inc.conflicts_with_liquidation);
    EXPECT_EQ(inc.reason, "Portfolio in liquidation mode - only reducing orders allowed");

    // (b) Reducing order on a LOCKED symbol -> passes liquidation gate but rejected by symbol lock.
    auto red = eng.check_pre_trade(make_market_order("AAPL", OrderSide::Sell, 10.0), pf, fx);
    EXPECT_EQ(red.decision, RiskDecision::Reject);
    EXPECT_TRUE(red.conflicts_with_liquidation);
    EXPECT_EQ(red.reason, "Symbol AAPL is locked for liquidation");
}

// ============================================================================
// Pre-trade: liquidity limits (ADV-based sizing)
// ============================================================================

// adv 1,000,000; order cap = 1% ADV = 10000; position cap = 5% ADV = 50000.
auto liquidity_config() -> RiskConfig {
    RiskConfig cfg;
    cfg.limits.liquidity.max_adv_pct = 0.05;
    cfg.limits.liquidity.max_order_adv_pct = 0.01;
    cfg.instrument_info["AAPL"] = InstrumentRiskInfo{.asset_class = AssetClass::Equity,
                                                     .currency = Currency::USD,
                                                     .sector = "Tech",
                                                     .adv = 1'000'000.0};
    return cfg;
}

TEST(RiskTest, LiquidityOrderSizeCap) {
    StandardRiskEngine eng(liquidity_config());
    StaticFXProvider fx;
    MockPortfolio pf;
    pf.set_nav(1e9);
    auto r = eng.check_pre_trade(make_market_order("AAPL", OrderSide::Buy, 20000.0), pf, fx);
    EXPECT_EQ(r.decision, RiskDecision::Reduce);
    ASSERT_TRUE(r.adjusted_quantity.has_value());
    EXPECT_DOUBLE_EQ(*r.adjusted_quantity, 10000.0);
    ASSERT_EQ(r.violated_limits.size(), 1u);
    EXPECT_EQ(r.violated_limits[0], "liquidity.max_order_adv_pct");
}

TEST(RiskTest, LiquidityPositionCapBinds) {
    StandardRiskEngine eng(liquidity_config());
    StaticFXProvider fx;
    MockPortfolio pf;
    pf.set_nav(1e9);
    pf.set_position(pos("AAPL", 45000.0, 100.0));
    auto r = eng.check_pre_trade(make_market_order("AAPL", OrderSide::Buy, 20000.0), pf, fx);
    // Both caps violated; allowed = min(order cap 10000, headroom 50000-45000) = 5000.
    EXPECT_EQ(r.decision, RiskDecision::Reduce);
    ASSERT_TRUE(r.adjusted_quantity.has_value());
    EXPECT_DOUBLE_EQ(*r.adjusted_quantity, 5000.0);
    ASSERT_EQ(r.violated_limits.size(), 2u);
    EXPECT_EQ(r.violated_limits[0], "liquidity.max_order_adv_pct");
    EXPECT_EQ(r.violated_limits[1], "liquidity.max_adv_pct");
}

TEST(RiskTest, LiquidityPositionCapExhausted) {
    auto cfg = liquidity_config();
    cfg.limits.liquidity.max_order_adv_pct = 0.0; // order cap disabled
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    MockPortfolio pf;
    pf.set_nav(1e9);
    pf.set_position(pos("AAPL", 50000.0, 100.0)); // exactly at the position cap
    auto r = eng.check_pre_trade(make_market_order("AAPL", OrderSide::Buy, 1000.0), pf, fx);
    EXPECT_EQ(r.decision, RiskDecision::Reject); // headroom 0
    EXPECT_EQ(r.reason, "Exceeds liquidity (ADV) limits");
    ASSERT_EQ(r.violated_limits.size(), 1u);
    EXPECT_EQ(r.violated_limits[0], "liquidity.max_adv_pct");
}

TEST(RiskTest, LiquidityUnknownAdvAllows) {
    StandardRiskEngine eng(liquidity_config());
    StaticFXProvider fx;
    MockPortfolio pf;
    pf.set_nav(1e9);
    // No instrument_info entry for MSFT -> adv unknown -> not enforced.
    auto r = eng.check_pre_trade(make_market_order("MSFT", OrderSide::Buy, 500000.0), pf, fx);
    EXPECT_EQ(r.decision, RiskDecision::Allow);
}

TEST(RiskTest, LiquidityReducingOrderSkipsPositionCap) {
    StandardRiskEngine eng(liquidity_config());
    StaticFXProvider fx;
    MockPortfolio pf;
    pf.set_nav(1e9);
    pf.set_position(pos("AAPL", 60000.0, 100.0)); // already over the position cap
    // Reducing order (|new| < |current|); 5000 <= order cap 10000 -> Allow.
    auto r = eng.check_pre_trade(make_market_order("AAPL", OrderSide::Sell, 5000.0), pf, fx);
    EXPECT_EQ(r.decision, RiskDecision::Allow);
}

// ============================================================================
// Pre-trade: concentration limits (sector / currency gross exposure)
// ============================================================================

// nav 100000; AAPL 30000 (Tech), MSFT 20000 (Tech), XOM 10000 (Energy), all USD.
auto concentration_fixture(RiskConfig& cfg, MockPortfolio& pf) -> void {
    pf.set_nav(100000.0);
    pf.set_position(pos("AAPL", 200.0, 150.0));
    pf.set_position(pos("MSFT", 200.0, 100.0));
    pf.set_position(pos("XOM", 100.0, 100.0));
    cfg.instrument_info["AAPL"] = InstrumentRiskInfo{.sector = "Tech"};
    cfg.instrument_info["MSFT"] = InstrumentRiskInfo{.sector = "Tech"};
    cfg.instrument_info["XOM"] = InstrumentRiskInfo{.sector = "Energy"};
}

TEST(RiskTest, ConcentrationSectorReject) {
    RiskConfig cfg;
    MockPortfolio pf;
    concentration_fixture(cfg, pf);
    cfg.limits.concentration.max_sector_pct["Tech"] = 0.55;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    // New MSFT = 300 * 100 = 30000; Tech gross = 30000 + 30000 = 60000 -> 0.60 > 0.55.
    auto r = eng.check_pre_trade(make_market_order("MSFT", OrderSide::Buy, 100.0), pf, fx);
    EXPECT_EQ(r.decision, RiskDecision::Reject);
    EXPECT_EQ(r.reason, "Exceeds sector concentration limit");
    ASSERT_EQ(r.violated_limits.size(), 1u);
    EXPECT_EQ(r.violated_limits[0], "concentration.sector.Tech");
}

TEST(RiskTest, ConcentrationSectorAllow) {
    RiskConfig cfg;
    MockPortfolio pf;
    concentration_fixture(cfg, pf);
    cfg.limits.concentration.max_sector_pct["Tech"] = 0.55;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    // New MSFT = 240 * 100 = 24000; Tech gross = 54000 -> 0.54 <= 0.55.
    auto r = eng.check_pre_trade(make_market_order("MSFT", OrderSide::Buy, 40.0), pf, fx);
    EXPECT_EQ(r.decision, RiskDecision::Allow);
}

TEST(RiskTest, ConcentrationCurrencyReject) {
    RiskConfig cfg;
    MockPortfolio pf;
    concentration_fixture(cfg, pf);
    pf.set_position(pos("BUND", 50.0, 100.0, Currency::EUR));
    cfg.instrument_info["BUND"] = InstrumentRiskInfo{.asset_class = AssetClass::Bond,
                                                     .currency = Currency::EUR,
                                                     .sector = "Govt"};
    cfg.limits.concentration.max_currency_pct[Currency::EUR] = 0.05;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    fx.set_rate(Currency::EUR, Currency::USD, 1.10);
    // New BUND = 60 * 100 = 6000 EUR = 6600 USD -> 0.066 > 0.05.
    auto r = eng.check_pre_trade(make_market_order("BUND", OrderSide::Buy, 10.0), pf, fx);
    EXPECT_EQ(r.decision, RiskDecision::Reject);
    EXPECT_EQ(r.reason, "Exceeds currency concentration limit");
    ASSERT_EQ(r.violated_limits.size(), 1u);
    EXPECT_EQ(r.violated_limits[0], "concentration.currency.EUR");
}

TEST(RiskTest, ConcentrationReducingOrderAllowed) {
    RiskConfig cfg;
    MockPortfolio pf;
    concentration_fixture(cfg, pf);
    pf.set_position(pos("MSFT", 300.0, 100.0)); // Tech gross now 60000 = 0.60 of nav
    cfg.limits.concentration.max_sector_pct["Tech"] = 0.55;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    // Already over the limit, but a reducing order must never be blocked.
    auto r = eng.check_pre_trade(make_market_order("MSFT", OrderSide::Sell, 50.0), pf, fx);
    EXPECT_EQ(r.decision, RiskDecision::Allow);
}

// ============================================================================
// Check ordering regression: position limits run before liquidity
// ============================================================================

TEST(RiskTest, PreTradeStillChecksPositionLimitsFirst) {
    RiskConfig cfg;
    cfg.limits.default_position_limits.max_quantity = 100.0;
    cfg.limits.liquidity.max_order_adv_pct = 0.05; // order cap = 50 with adv 1000
    cfg.instrument_info["AAPL"] = InstrumentRiskInfo{.adv = 1000.0};
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    MockPortfolio pf;
    pf.set_nav(1e9);
    auto r = eng.check_pre_trade(make_market_order("AAPL", OrderSide::Buy, 500.0), pf, fx);
    // The position-limit check must fire first: Reduce to 100 (not the ADV cap 50).
    EXPECT_EQ(r.decision, RiskDecision::Reduce);
    ASSERT_TRUE(r.adjusted_quantity.has_value());
    EXPECT_DOUBLE_EQ(*r.adjusted_quantity, 100.0);
    ASSERT_FALSE(r.violated_limits.empty());
    EXPECT_EQ(r.violated_limits[0], "position.max_quantity");
}

TEST(RiskTest, ResetLiquidationClearsState) {
    RiskConfig cfg;
    cfg.limits.pnl.max_drawdown_pct = 0.10;
    StandardRiskEngine eng(cfg);
    StaticFXProvider fx;
    MockPortfolio pf;
    pf.set_hwm(100000.0);
    pf.set_nav(85000.0);
    pf.set_position(pos("AAPL", 100.0, 50.0));
    (void)eng.monitor(pf, fx, make_timestamp(2024, 6, 1)); // trigger liquidation

    eng.reset_liquidation();
    EXPECT_EQ(eng.get_state(), PortfolioState::Normal);
    EXPECT_FALSE(eng.is_symbol_locked("AAPL"));
    EXPECT_EQ(
        eng.check_pre_trade(make_market_order("AAPL", OrderSide::Buy, 10.0), pf, fx).decision,
        RiskDecision::Allow);
}

} // namespace
