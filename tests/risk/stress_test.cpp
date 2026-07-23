#include <cmath>
#include <gtest/gtest.h>
#include <map>
#include <optional>
#include <string>
#include <vector>

import finkit.risk;
import finkit.trading;
import finkit.types;

namespace {

using namespace finkit::risk;
using namespace finkit::trading;
using namespace finkit::types;

auto make_position(std::string symbol, double qty, double price,
                   Currency ccy = Currency::USD) -> Position {
    return Position{.symbol = std::move(symbol),
                    .quantity = qty,
                    .avg_cost = price,
                    .market_price = price,
                    .currency = ccy};
}

// Book fixture:
//   AAPL: 100 @ 150 USD (Equity, Tech)
//   BUND: 50 @ 100 EUR (Bond, Govt, dur 8.0)
// fx: EUR/USD = 1.10, nav = 30000, base USD.
// Pre-shock: AAPL exposure = 15000 USD; BUND = 5000 EUR = 5500 USD.
auto make_book() -> std::map<std::string, Position> {
    return {{"AAPL", make_position("AAPL", 100, 150.0)},
            {"BUND", make_position("BUND", 50, 100.0, Currency::EUR)}};
}

auto make_info() -> std::map<std::string, InstrumentRiskInfo> {
    return {{"AAPL",
             InstrumentRiskInfo{.asset_class = AssetClass::Equity,
                                .currency = Currency::USD,
                                .sector = "Tech",
                                .adv = 0.0,
                                .duration = 0.0}},
            {"BUND",
             InstrumentRiskInfo{.asset_class = AssetClass::Bond,
                                .currency = Currency::EUR,
                                .sector = "Govt",
                                .adv = 0.0,
                                .duration = 8.0}}};
}

// ============================================================================
// Stress Tests
// ============================================================================

TEST(RiskTest, StressSingleSymbolPriceShock) {
    auto positions = make_book();
    auto info = make_info();
    StaticFXProvider fx;
    fx.set_rate(Currency::EUR, Currency::USD, 1.10);

    Scenario s;
    s.name = "aapl_down_20";
    s.shocks.push_back(Shock{.type = ShockType::PricePct, .value = -0.20, .symbol = "AAPL"});

    auto r = apply_scenario(s, positions, info, fx, 30000.0);
    EXPECT_DOUBLE_EQ(r.total_pnl, -3000.0);
    // impacts in map order: AAPL, BUND
    ASSERT_EQ(r.impacts.size(), 2u);
    EXPECT_EQ(r.impacts[0].symbol, "AAPL");
    EXPECT_DOUBLE_EQ(r.impacts[0].pnl, -3000.0);
    EXPECT_EQ(r.impacts[1].symbol, "BUND");
    EXPECT_DOUBLE_EQ(r.impacts[1].pnl, 0.0);
}

TEST(RiskTest, Stress2008Crisis) {
    auto positions = make_book();
    auto info = make_info();
    StaticFXProvider fx;
    fx.set_rate(Currency::EUR, Currency::USD, 1.10);

    auto r = apply_scenario(scenario_2008_crisis(), positions, info, fx, 30000.0);
    // AAPL: 15000 * -0.40 = -6000
    // BUND: rate = -8 * (100/10000) * 5000 EUR = -400 EUR -> -440 USD
    //       FX = 5500 * -0.10 = -550 -> BUND pnl = -990
    EXPECT_NEAR(r.total_pnl, -6990.0, 1e-9);
    EXPECT_NEAR(r.pnl_pct_of_nav, -0.233, 1e-9);
    ASSERT_EQ(r.impacts.size(), 2u);
    EXPECT_NEAR(r.impacts[0].pnl, -6000.0, 1e-9);
    EXPECT_NEAR(r.impacts[1].pnl, -990.0, 1e-9);
    EXPECT_NEAR(r.impacts[1].base_value, 5500.0, 1e-9);
    EXPECT_NEAR(r.impacts[1].shocked_value, 4510.0, 1e-9);
}

TEST(RiskTest, StressRatesOnly) {
    auto positions = make_book();
    auto info = make_info();
    StaticFXProvider fx;
    fx.set_rate(Currency::EUR, Currency::USD, 1.10);

    auto r = apply_scenario(scenario_rates_up_100bp(), positions, info, fx, 30000.0);
    // AAPL: duration 0 -> 0; BUND: -8 * 0.01 * 5000 EUR = -400 EUR -> -440 USD
    EXPECT_NEAR(r.total_pnl, -440.0, 1e-9);
}

TEST(RiskTest, StressUsdRally) {
    auto positions = make_book();
    auto info = make_info();
    StaticFXProvider fx;
    fx.set_rate(Currency::EUR, Currency::USD, 1.10);

    auto r = apply_scenario(scenario_usd_rally_10pct(), positions, info, fx, 30000.0);
    // AAPL: USD not shocked -> 0; BUND: 5500 * -0.10 = -550
    EXPECT_NEAR(r.total_pnl, -550.0, 1e-9);
}

TEST(RiskTest, StressShortPositionGains) {
    std::map<std::string, Position> positions = {{"SPY", make_position("SPY", -100, 150.0)}};
    std::map<std::string, InstrumentRiskInfo> info; // no entry -> defaults Equity/USD
    StaticFXProvider fx;

    Scenario s;
    s.name = "equity_down_40";
    s.shocks.push_back(Shock{.type = ShockType::PricePct,
                             .value = -0.40,
                             .symbol = std::nullopt,
                             .asset_class = AssetClass::Equity});

    auto r = apply_scenario(s, positions, info, fx, 100000.0);
    // (-100 * 150) * -0.40 = +6000
    EXPECT_DOUBLE_EQ(r.total_pnl, 6000.0);
}

} // namespace
