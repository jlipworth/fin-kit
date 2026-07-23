/// @file risk-stress.cppm
/// @brief Scenario stress testing: shock definitions, application, predefined scenarios
///
/// First-order (linear) framework: each shock's P&L is computed against the
/// unshocked book and summed; cross terms (e.g. FX x price) are ignored.

module;

#include <cmath>
#include <map>
#include <optional>
#include <string>
#include <vector>

export module finkit.risk:stress;

import :types;
import :engine; // IFXRateProvider
import finkit.types;
import finkit.trading;

export namespace finkit::risk {

using std::map;
using std::optional;
using std::string;
using std::vector;

using finkit::trading::Position;
using finkit::types::AssetClass;
using finkit::types::Currency;

// ============================================================================
// Types
// ============================================================================

enum class ShockType {
    PricePct, // value = fractional price move, e.g. -0.40 for -40%
    RateBp,   // value = parallel rate shift in basis points, e.g. +100.0
    FXPct     // value = fractional move of `currency` vs base, e.g. -0.10 = ccy weakens 10%
};

/// A single shock. Filter semantics (checked in this order):
///  - symbol set        -> applies only to that symbol
///  - else asset_class  -> applies to positions of that asset class (PricePct/RateBp)
///  - else               -> PricePct/RateBp apply to ALL positions
///  - FXPct requires currency; applies to positions denominated in that currency
struct Shock {
    ShockType type{ShockType::PricePct};
    double value{0.0};
    optional<string> symbol;
    optional<AssetClass> asset_class;
    optional<Currency> currency; // FXPct only; required for FXPct
};

/// A named collection of shocks applied jointly (additively)
struct Scenario {
    string name;
    string description;
    vector<Shock> shocks;
};

/// Per-position stress impact (base currency)
struct PositionImpact {
    string symbol;
    double base_value{0.0};    // signed pre-shock exposure, base ccy
    double shocked_value{0.0}; // base_value + pnl
    double pnl{0.0};
};

/// Result of applying one scenario
struct StressResult {
    string scenario_name;
    double total_pnl{0.0};          // base ccy; negative = loss
    double pnl_pct_of_nav{0.0};     // total_pnl / nav (0 if nav <= 0)
    vector<PositionImpact> impacts; // one per non-flat position, portfolio map order
};

} // namespace finkit::risk

// File-local helpers (not exported) — placed after the exported types they use.
// Same non-exported `detail` namespace idiom as risk-var.cppm.
namespace finkit::risk::detail {

[[nodiscard]] auto shock_matches(const Shock& shock, const string& symbol,
                                 const InstrumentRiskInfo& ri, Currency base) -> bool {
    if (shock.symbol) {
        return *shock.symbol == symbol;
    }
    if (shock.type == ShockType::FXPct) {
        if (!shock.currency) {
            return false; // malformed FX shock -> no-op
        }
        if (*shock.currency == base) {
            return false; // shocking base vs itself -> no-op
        }
        return ri.currency == *shock.currency;
    }
    if (shock.asset_class) {
        return ri.asset_class == *shock.asset_class;
    }
    return true; // unfiltered PricePct/RateBp: all positions
}

[[nodiscard]] auto price_shock(double value, AssetClass ac) -> Shock {
    Shock s;
    s.type = ShockType::PricePct;
    s.value = value;
    s.asset_class = ac;
    return s;
}

[[nodiscard]] auto rate_shock(double bp) -> Shock {
    Shock s;
    s.type = ShockType::RateBp;
    s.value = bp;
    return s;
}

[[nodiscard]] auto fx_shock(double value, Currency c) -> Shock {
    Shock s;
    s.type = ShockType::FXPct;
    s.value = value;
    s.currency = c;
    return s;
}

// Every Currency enum value except USD, in enum order.
[[nodiscard]] auto non_usd_currencies() -> vector<Currency> {
    return {Currency::EUR, Currency::GBP, Currency::JPY, Currency::CHF, Currency::AUD,
            Currency::CAD, Currency::NZD, Currency::NOK, Currency::SEK};
}

} // namespace finkit::risk::detail

export namespace finkit::risk {

// ============================================================================
// Scenario application
// ============================================================================

/// Apply a scenario to a book of positions.
/// @param positions Book, e.g. IPortfolio::positions()
/// @param info      Per-symbol metadata (asset class / duration / currency); a symbol with
///                  no entry defaults to InstrumentRiskInfo{ .currency = position.currency }
///                  (i.e. AssetClass::Equity, duration 0, empty sector).
/// @param nav       Portfolio NAV in base ccy, used only for pnl_pct_of_nav
[[nodiscard]] auto apply_scenario(const Scenario& scenario, const map<string, Position>& positions,
                                  const map<string, InstrumentRiskInfo>& info,
                                  const IFXRateProvider& fx, double nav,
                                  Currency base = Currency::USD) -> StressResult {
    StressResult result;
    result.scenario_name = scenario.name;

    for (const auto& [symbol, pos] : positions) {
        if (pos.is_flat()) {
            continue;
        }
        InstrumentRiskInfo ri;
        if (auto it = info.find(symbol); it != info.end()) {
            ri = it->second;
        } else {
            ri.currency = pos.currency;
        }

        double e_local = pos.quantity * pos.market_price; // signed local-ccy exposure
        double base_value = fx.convert(e_local, ri.currency, base);
        double pnl = 0.0;

        for (const auto& shock : scenario.shocks) {
            if (!detail::shock_matches(shock, symbol, ri, base)) {
                continue;
            }
            switch (shock.type) {
            case ShockType::PricePct:
                pnl += fx.convert(e_local * shock.value, ri.currency, base);
                break;
            case ShockType::RateBp:
                pnl += fx.convert(-ri.duration * (shock.value / 10000.0) * e_local, ri.currency,
                                  base);
                break;
            case ShockType::FXPct:
                pnl += base_value * shock.value;
                break;
            }
        }

        PositionImpact impact;
        impact.symbol = symbol;
        impact.base_value = base_value;
        impact.shocked_value = base_value + pnl;
        impact.pnl = pnl;
        result.impacts.push_back(std::move(impact));
        result.total_pnl += pnl;
    }

    result.pnl_pct_of_nav = nav > 0.0 ? result.total_pnl / nav : 0.0;
    return result;
}

/// Apply each scenario in turn; results in input order.
[[nodiscard]] auto run_scenarios(const vector<Scenario>& scenarios,
                                 const map<string, Position>& positions,
                                 const map<string, InstrumentRiskInfo>& info,
                                 const IFXRateProvider& fx, double nav,
                                 Currency base = Currency::USD) -> vector<StressResult> {
    vector<StressResult> results;
    results.reserve(scenarios.size());
    for (const auto& scenario : scenarios) {
        results.push_back(apply_scenario(scenario, positions, info, fx, nav, base));
    }
    return results;
}

// ============================================================================
// Predefined scenarios
// ============================================================================

[[nodiscard]] auto scenario_2008_crisis() -> Scenario {
    Scenario s;
    s.name = "2008_crisis";
    s.description = "Equity -40%, rates +100bp, USD +10% vs G10";
    s.shocks.push_back(detail::price_shock(-0.40, AssetClass::Equity));
    s.shocks.push_back(detail::price_shock(-0.40, AssetClass::ETF));
    s.shocks.push_back(detail::rate_shock(100.0));
    for (auto c : detail::non_usd_currencies()) {
        s.shocks.push_back(detail::fx_shock(-0.10, c));
    }
    return s;
}

[[nodiscard]] auto scenario_black_monday_1987() -> Scenario {
    Scenario s;
    s.name = "black_monday_1987";
    s.description = "Equity -22.6% (Black Monday, Oct 19 1987)";
    s.shocks.push_back(detail::price_shock(-0.226, AssetClass::Equity));
    s.shocks.push_back(detail::price_shock(-0.226, AssetClass::ETF));
    return s;
}

[[nodiscard]] auto scenario_rates_up_100bp() -> Scenario {
    Scenario s;
    s.name = "rates_up_100bp";
    s.description = "Parallel rate shift +100bp";
    s.shocks.push_back(detail::rate_shock(100.0));
    return s;
}

[[nodiscard]] auto scenario_usd_rally_10pct() -> Scenario {
    Scenario s;
    s.name = "usd_rally_10pct";
    s.description = "USD +10% vs G10";
    for (auto c : detail::non_usd_currencies()) {
        s.shocks.push_back(detail::fx_shock(-0.10, c));
    }
    return s;
}

[[nodiscard]] auto predefined_scenarios() -> vector<Scenario> {
    return {scenario_2008_crisis(), scenario_black_monday_1987(), scenario_rates_up_100bp(),
            scenario_usd_rally_10pct()};
}

} // namespace finkit::risk
