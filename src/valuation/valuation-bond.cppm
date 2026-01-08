/// @file valuation-bond.cppm
/// @brief Bond valuation using QuantLib
///
/// Provides bond pricing, yield calculations, and relative value analysis.

module;

#include <algorithm>
#include <cmath>
#include <optional>
#include <ql/quantlib.hpp>
#include <span>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

export module finkit.valuation:bond;

import finkit.types;

export namespace finkit::valuation {

using std::optional;
using std::span;
using std::string;
using std::vector;

namespace ql = QuantLib;

using finkit::types::Bond;
using finkit::types::Currency;

/// Bond valuation result
struct BondValuation {
    string cusip;
    double clean_price{0.0};
    double dirty_price{0.0};
    double accrued_interest{0.0};
    double yield_to_maturity{0.0}; // As decimal
    double modified_duration{0.0};
    double macaulay_duration{0.0};
    double convexity{0.0};
    double dv01{0.0};                // Dollar value of 1bp
    double spread_to_benchmark{0.0}; // Spread vs curve
    ql::Date settlement_date;
};

/// Relative value metrics for bond ranking
struct BondRelativeValue {
    string cusip;
    double z_spread{0.0};          // Zero-volatility spread
    double oas{0.0};               // Option-adjusted spread (for callables)
    double carry{0.0};             // Expected carry at horizon
    double roll_down{0.0};         // Roll-down return
    double rich_cheap_zscore{0.0}; // Relative to sector
    int rank{0};                   // 1 = most attractive
};

/// Build QuantLib FixedRateBond from our Bond type
auto make_ql_bond(const Bond& bond) -> ql::FixedRateBond {
    // Treasury bond conventions
    ql::Calendar calendar = ql::UnitedStates(ql::UnitedStates::GovernmentBond);
    ql::DayCounter day_counter = ql::ActualActual(ql::ActualActual::Bond);
    ql::BusinessDayConvention bdc = ql::Unadjusted;

    // Build schedule from issue to maturity
    // Bond struct has ql::Date directly for issue_date and maturity
    ql::Schedule schedule(bond.issue_date, bond.maturity, ql::Period(ql::Semiannual), calendar, bdc,
                          bdc, ql::DateGeneration::Backward,
                          false // End of month
    );

    // Create bond
    return ql::FixedRateBond(0,                       // Settlement days (we'll set explicitly)
                             100.0,                   // Face value
                             schedule, {bond.coupon}, // Coupon rate
                             day_counter, bdc,
                             100.0 // Redemption
    );
}

/// Value a single bond given a discount curve
/// @param bond Bond to value
/// @param discount_curve QuantLib yield term structure for discounting
/// @param settlement_date Settlement date for pricing
/// @return BondValuation with all metrics
auto value_bond(const Bond& bond, const ql::Handle<ql::YieldTermStructure>& discount_curve,
                const ql::Date& settlement_date) -> BondValuation {
    BondValuation result;
    result.cusip = bond.cusip;
    result.settlement_date = settlement_date;

    try {
        // Set evaluation date
        // NOTE: QuantLib Settings is a global singleton. Multi-threaded usage
        // requires external synchronization or per-thread evaluation dates.
        ql::Settings::instance().evaluationDate() = settlement_date;

        // Build QuantLib bond
        auto ql_bond = make_ql_bond(bond);

        // Set pricing engine
        auto engine = ql::ext::make_shared<ql::DiscountingBondEngine>(discount_curve);
        ql_bond.setPricingEngine(engine);

        // Get prices
        result.dirty_price = ql_bond.dirtyPrice();
        result.clean_price = ql_bond.cleanPrice();
        result.accrued_interest = ql_bond.accruedAmount();

        // Calculate yield at clean price using Bond::Price wrapper
        result.yield_to_maturity =
            ql_bond.yield(ql::Bond::Price(result.clean_price, ql::Bond::Price::Clean),
                          ql::ActualActual(ql::ActualActual::Bond), ql::Compounded, ql::Semiannual);

        // Duration and convexity
        ql::InterestRate yield_rate(result.yield_to_maturity,
                                    ql::ActualActual(ql::ActualActual::Bond), ql::Compounded,
                                    ql::Semiannual);

        result.macaulay_duration = ql::BondFunctions::duration(
            ql_bond, yield_rate, ql::Duration::Macaulay, settlement_date);
        result.modified_duration = ql::BondFunctions::duration(
            ql_bond, yield_rate, ql::Duration::Modified, settlement_date);
        result.convexity = ql::BondFunctions::convexity(ql_bond, yield_rate, settlement_date);

        // DV01: dollar value of 1bp yield change
        result.dv01 = result.modified_duration * result.dirty_price / 10000.0;

    } catch (const std::exception& e) {
        spdlog::warn("Bond valuation failed for {}: {}", bond.cusip, e.what());
        // Return partial result on error
        result.clean_price = bond.clean_price;
    }

    return result;
}

/// Value a bond using market price (for yield calculation)
auto value_bond_from_price(const Bond& bond, double market_price,
                           const ql::Date& settlement_date) -> BondValuation {
    BondValuation result;
    result.cusip = bond.cusip;
    result.settlement_date = settlement_date;
    result.clean_price = market_price;

    try {
        ql::Settings::instance().evaluationDate() = settlement_date;
        auto ql_bond = make_ql_bond(bond);

        result.accrued_interest = ql_bond.accruedAmount();
        result.dirty_price = market_price + result.accrued_interest;

        // Calculate yield at market price using Bond::Price wrapper
        result.yield_to_maturity =
            ql_bond.yield(ql::Bond::Price(market_price, ql::Bond::Price::Clean),
                          ql::ActualActual(ql::ActualActual::Bond), ql::Compounded, ql::Semiannual);

        ql::InterestRate yield_rate(result.yield_to_maturity,
                                    ql::ActualActual(ql::ActualActual::Bond), ql::Compounded,
                                    ql::Semiannual);

        result.macaulay_duration = ql::BondFunctions::duration(
            ql_bond, yield_rate, ql::Duration::Macaulay, settlement_date);
        result.modified_duration = ql::BondFunctions::duration(
            ql_bond, yield_rate, ql::Duration::Modified, settlement_date);
        result.convexity = ql::BondFunctions::convexity(ql_bond, yield_rate, settlement_date);
        result.dv01 = result.modified_duration * result.dirty_price / 10000.0;

    } catch (const std::exception& e) {
        spdlog::warn("Bond valuation from price failed for {}: {}", bond.cusip, e.what());
        result.clean_price = market_price;
    }

    return result;
}

/// Calculate Z-spread for a bond
/// @param bond Bond with market price
/// @param discount_curve Risk-free curve
/// @param settlement_date Settlement date
/// @return Z-spread in basis points
auto calculate_z_spread(const Bond& bond, const ql::Handle<ql::YieldTermStructure>& discount_curve,
                        const ql::Date& settlement_date) -> double {
    try {
        ql::Settings::instance().evaluationDate() = settlement_date;
        auto ql_bond = make_ql_bond(bond);

        double z_spread = ql::BondFunctions::zSpread(
            ql_bond, ql::Bond::Price(bond.clean_price, ql::Bond::Price::Clean),
            discount_curve.currentLink(), ql::ActualActual(ql::ActualActual::Bond), ql::Compounded,
            ql::Semiannual, settlement_date);

        return z_spread * 10000.0; // Convert to bps
    } catch (const std::exception& e) {
        spdlog::warn("Z-spread calculation failed for {}: {}", bond.cusip, e.what());
        return 0.0;
    }
}

/// Calculate expected carry for a bond
/// @param bond Bond
/// @param repo_rate Financing rate (decimal)
/// @param horizon_days Horizon in days
/// @return Expected carry in price points
auto calculate_carry(const Bond& bond, double repo_rate, int horizon_days) -> double {
    // Carry = coupon income - financing cost
    double daily_coupon = bond.coupon * 100.0 / 365.0;
    double daily_financing = bond.clean_price * repo_rate / 365.0;
    return static_cast<double>(horizon_days) * (daily_coupon - daily_financing);
}

/// Calculate roll-down return
/// @param valuation Current bond valuation
/// @param curve Yield curve
/// @param horizon_days Roll horizon
/// @return Expected roll-down in price points
auto calculate_roll_down(const BondValuation& valuation,
                         const ql::Handle<ql::YieldTermStructure>& curve,
                         int horizon_days) -> double {
    // Simplified: roll-down = duration * expected yield change
    // More accurate would be to re-price at shorter maturity
    try {
        // Current yield at bond maturity
        double current_yield =
            curve
                ->zeroRate(valuation.settlement_date + horizon_days,
                           ql::ActualActual(ql::ActualActual::Bond), ql::Compounded, ql::Semiannual)
                .rate();

        // Yield after roll (shorter maturity)
        double rolled_yield =
            curve
                ->zeroRate(valuation.settlement_date, ql::ActualActual(ql::ActualActual::Bond),
                           ql::Compounded, ql::Semiannual)
                .rate();

        // Price change from yield change
        double yield_change = rolled_yield - current_yield;
        return -valuation.modified_duration * valuation.dirty_price * yield_change;
    } catch (const std::exception& e) {
        spdlog::warn("Roll-down calculation failed for {}: {}", valuation.cusip, e.what());
        return 0.0;
    }
}

/// Rank bonds by relative value
/// @param bonds Vector of bonds with prices
/// @param discount_curve Discount curve for valuation
/// @param settlement_date Settlement date
/// @param repo_rate Repo rate for carry calculation
/// @return Vector of BondRelativeValue sorted by attractiveness
auto rank_bonds(span<const Bond> bonds, const ql::Handle<ql::YieldTermStructure>& discount_curve,
                const ql::Date& settlement_date, double repo_rate) -> vector<BondRelativeValue> {
    vector<BondRelativeValue> results;
    results.reserve(bonds.size());

    for (const auto& bond : bonds) {
        BondRelativeValue rv;
        rv.cusip = bond.cusip;

        // Calculate Z-spread
        rv.z_spread = calculate_z_spread(bond, discount_curve, settlement_date);

        // Calculate carry (30-day horizon)
        rv.carry = calculate_carry(bond, repo_rate, 30);

        // Value the bond for duration info
        auto valuation = value_bond(bond, discount_curve, settlement_date);
        rv.roll_down = calculate_roll_down(valuation, discount_curve, 30);

        results.push_back(rv);
    }

    // Calculate z-scores relative to group
    if (!results.empty()) {
        // Mean and std of z-spreads
        double sum = 0.0, sum_sq = 0.0;
        for (const auto& rv : results) {
            sum += rv.z_spread;
            sum_sq += rv.z_spread * rv.z_spread;
        }
        double mean = sum / static_cast<double>(results.size());
        double variance = sum_sq / static_cast<double>(results.size()) - mean * mean;
        double std_dev = std::sqrt(std::max(variance, 1e-10));

        for (auto& rv : results) {
            rv.rich_cheap_zscore = (rv.z_spread - mean) / std_dev;
        }
    }

    // Rank by total value (higher z-spread + carry + roll is better)
    std::sort(results.begin(), results.end(),
              [](const BondRelativeValue& a, const BondRelativeValue& b) {
                  double score_a = a.z_spread + a.carry + a.roll_down;
                  double score_b = b.z_spread + b.carry + b.roll_down;
                  return score_a > score_b;
              });

    // Assign ranks
    for (size_t i = 0; i < results.size(); ++i) {
        results[i].rank = static_cast<int>(i + 1);
    }

    return results;
}

} // namespace finkit::valuation
