module;

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <ql/quantlib.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

export module finkit.analysis;

import finkit.data;

export namespace finkit::analysis {

using std::optional;
using std::string;
using std::vector;

namespace ql = QuantLib;

// ============================================================================
// Bond Data Types
// See: docs/INPUT_REQUIREMENTS.md Section 1.1
// ============================================================================

struct Bond {
    // Identifiers
    string cusip; // 9-character CUSIP
    string isin;  // 12-character ISIN (optional)

    // Static data (from reference)
    double coupon;              // Annual coupon rate (e.g., 0.04375)
    ql::Date maturity;          // Maturity date
    ql::Date issue_date;        // Original issue date
    ql::Date first_coupon_date; // First coupon payment date

    // Market data (point-in-time)
    double clean_price;           // Quoted price (excludes accrued)
    double accrued_interest{0.0}; // Accrued interest per 100 face

    // Calculated (optional, for caching)
    optional<double> dirty_price; // clean + accrued
    optional<double> yield_to_maturity;
    optional<double> modified_duration;
    optional<double> convexity;

    // Computed dirty price if not cached
    auto full_price() const -> double {
        return dirty_price.value_or(clean_price + accrued_interest);
    }
};

// ============================================================================
// Futures Contract Types
// See: docs/INPUT_REQUIREMENTS.md Section 1.2
// ============================================================================

enum class TreasuryFuturesProduct {
    TU, // 2-Year Note ($200k notional)
    FV, // 5-Year Note ($100k notional)
    TY, // 10-Year Note ($100k notional)
    TN, // Ultra 10-Year ($100k notional)
    US, // 30-Year Bond ($100k notional)
    WN  // Ultra Bond ($100k notional)
};

struct FuturesContract {
    // Identifiers
    string code; // e.g., "TYH5" for 10Y Mar 2025
    TreasuryFuturesProduct product;

    // Market data
    double price; // Futures price (in 32nds decimal)

    // Contract specifications
    ql::Date first_delivery;      // First day of delivery month
    ql::Date last_delivery;       // Last delivery day
    ql::Date last_trade;          // Last trading day
    double notional;              // $100,000 or $200,000
    double tick_size{1.0 / 32.0}; // Minimum price increment

    // Deliverable grade specifications (maturity range in years)
    double min_maturity_years; // e.g., 6.5 for TY
    double max_maturity_years; // e.g., 10.0 for TY
};

// ============================================================================
// Repo/Financing Data
// See: docs/INPUT_REQUIREMENTS.md Section 1.4
// ============================================================================

struct RepoRate {
    ql::Date as_of;
    double gc_rate;                // General collateral rate
    optional<double> special_rate; // Issue-specific (if on special)
    double term_days;              // Term of repo (days to delivery)
    double haircut{0.02};          // Typical 2% haircut
};

// ============================================================================
// Basis Analysis Results
// ============================================================================

struct BasisResult {
    // Identifiers
    string cusip;
    string futures_code;

    // Conversion factor (CME 6% yield assumption)
    double conversion_factor;

    // Basis measures (in 32nds for standard quoting)
    double gross_basis;       // Cash - (Futures × CF)
    double net_basis;         // Gross basis - Carry (BNOC)
    double gross_basis_32nds; // Gross basis in 32nds
    double net_basis_32nds;   // Net basis in 32nds

    // Repo and carry
    double implied_repo;  // Implied repo rate (annualized)
    double carry;         // Cost of carry to delivery
    double coupon_income; // Interim coupon if any

    // Invoice calculation
    double invoice_price; // Futures × CF + Accrued at delivery

    // CTD determination
    bool is_ctd{false}; // Cheapest to deliver flag
    int ctd_rank{0};    // Rank by net basis (1 = CTD)

    // Risk metrics
    optional<double> basis_dv01;  // DV01 of basis position
    optional<double> hedge_ratio; // Futures per $1M cash
};

struct DeliverableBasket {
    FuturesContract futures;
    ql::Date settle_date;
    ql::Date analysis_date;
    double repo_rate;

    vector<BasisResult> bonds;
    optional<string> ctd_cusip;

    // Summary statistics
    double avg_net_basis;
    double min_net_basis;
    double max_net_basis;
};

// ============================================================================
// Conversion Factor Calculation (CME Standard)
// Reference: CME "Calculating U.S. Treasury Futures Conversion Factors"
// ============================================================================

// CME conversion factor for Treasury futures
// Assumes 6% yield, semiannual compounding
// Rounds maturity down to nearest quarter (3 months)
auto conversion_factor(double coupon, const ql::Date& maturity,
                       const ql::Date& delivery_date) -> double {
    // Calculate months to maturity
    int months_to_maturity = static_cast<int>((maturity - delivery_date) / 30.0);

    // Round down to nearest quarter
    int quarters = months_to_maturity / 3;
    int rounded_months = quarters * 3;

    // v = months in excess of whole years (0, 3, or 6 after rounding)
    int v = rounded_months % 12;

    // Adjust for odd first coupon (CME rule)
    // If v > 6, subtract 6; if v <= 6, use v directly
    int v_adj = (v > 6) ? v - 6 : v;

    // z = rounded months / 12 (can be fractional)
    double z = rounded_months / 12.0;

    // n = whole years after z calculation
    int n = static_cast<int>(z);

    // a = 1/1.03 (semiannual discount at 6% annual)
    constexpr double a = 1.0 / 1.03;

    // CME formula:
    // CF = a^(v/6) × [c/2 × (1 + a^2 + a^4 + ... + a^(2n)) + a^(2n)]
    //      - c/2 × (6 - v)/6
    //
    // The summation simplifies to: (1 - a^(2n+2)) / (1 - a^2)
    // But we use: c/2 × (1 + (1 - a^(2n)) / (1 - a^2)) + a^(2n)

    double a_2n = std::pow(a, 2.0 * n);
    double geometric_sum = (1.0 - a_2n) / (1.0 - a * a); // = (1-a^2n)/(1-a^2)

    double cf = std::pow(a, static_cast<double>(v_adj) / 6.0) *
                    (coupon / 2.0 * (1.0 + geometric_sum) + a_2n) -
                coupon / 2.0 * (6.0 - v_adj) / 6.0;

    return cf;
}

// ============================================================================
// Basis Calculations
// ============================================================================

auto gross_basis(const Bond& bond, const FuturesContract& futures, double cf) -> double {
    return bond.full_price() - futures.price * cf;
}

// Convert basis to 32nds
auto to_32nds(double decimal_price) -> double {
    return decimal_price * 32.0;
}

// Implied repo rate calculation
// Formula: repo = ((Invoice / Purchase) - 1) × (360 / days)
// Uses ACT/360 day count convention
auto implied_repo_rate(const Bond& bond, const FuturesContract& futures, double cf,
                       const ql::Date& settle_date, const ql::Date& delivery_date,
                       double accrued_at_delivery = 0.0) -> double {
    // Invoice price = Futures × CF + Accrued at delivery
    double invoice_price = futures.price * cf + accrued_at_delivery;

    // Purchase price = Clean + Accrued at settlement
    double purchase_price = bond.full_price();

    // Days from settlement to delivery
    int days = delivery_date - settle_date;
    if (days <= 0)
        return 0.0;

    // Simple interest formula
    double repo = ((invoice_price / purchase_price) - 1.0) * (360.0 / days);
    return repo;
}

// Carry calculation (financing cost minus coupon income)
// Carry = Financing cost - Coupon income
// For bonds with no interim coupon, carry = financing only
auto carry_cost(const Bond& bond, double repo_rate, const ql::Date& settle_date,
                const ql::Date& delivery_date, double interim_coupon = 0.0) -> double {
    int days = delivery_date - settle_date;
    double financing = bond.full_price() * repo_rate * days / 360.0;
    return financing - interim_coupon;
}

// Calculate interim coupon income (if coupon date falls in holding period)
auto interim_coupon_income(const Bond& bond, const ql::Date& settle_date,
                           const ql::Date& delivery_date) -> double {
    // TODO: Check if a coupon payment date falls between settle and delivery
    // If so, return coupon/2 × reinvestment factor
    // For now, return 0 (conservative assumption)
    (void)bond;
    (void)settle_date;
    (void)delivery_date;
    return 0.0;
}

// ============================================================================
// Deliverable Basket Analysis
// ============================================================================

auto analyze_basket(const FuturesContract& futures, const vector<Bond>& bonds,
                    const ql::Date& settle_date, double repo_rate) -> DeliverableBasket {
    DeliverableBasket basket;
    basket.futures = futures;
    basket.settle_date = settle_date;
    basket.analysis_date = settle_date;
    basket.repo_rate = repo_rate;

    double min_basis = std::numeric_limits<double>::max();
    double max_basis = std::numeric_limits<double>::lowest();
    double sum_basis = 0.0;
    optional<string> ctd;

    for (const auto& bond : bonds) {
        BasisResult result;
        result.cusip = bond.cusip;
        result.futures_code = futures.code;

        // Conversion factor
        result.conversion_factor =
            conversion_factor(bond.coupon, bond.maturity, futures.first_delivery);

        // Invoice price at delivery
        // TODO: Calculate accrued at delivery date properly
        double accrued_at_delivery = bond.accrued_interest; // Simplified
        result.invoice_price = futures.price * result.conversion_factor + accrued_at_delivery;

        // Gross basis
        result.gross_basis = gross_basis(bond, futures, result.conversion_factor);
        result.gross_basis_32nds = to_32nds(result.gross_basis);

        // Interim coupon income
        result.coupon_income = interim_coupon_income(bond, settle_date, futures.first_delivery);

        // Carry
        result.carry =
            carry_cost(bond, repo_rate, settle_date, futures.first_delivery, result.coupon_income);

        // Net basis (BNOC)
        result.net_basis = result.gross_basis - result.carry;
        result.net_basis_32nds = to_32nds(result.net_basis);

        // Implied repo
        result.implied_repo =
            implied_repo_rate(bond, futures, result.conversion_factor, settle_date,
                              futures.first_delivery, accrued_at_delivery);

        // Hedge ratio: CF × (Futures DV01 / Cash DV01)
        // Simplified: just CF for now
        result.hedge_ratio = result.conversion_factor;

        result.is_ctd = false;

        // Track statistics
        sum_basis += result.net_basis;
        if (result.net_basis < min_basis) {
            min_basis = result.net_basis;
            ctd = bond.cusip;
        }
        if (result.net_basis > max_basis) {
            max_basis = result.net_basis;
        }

        basket.bonds.push_back(result);
    }

    // Sort by net basis and assign ranks
    std::sort(basket.bonds.begin(), basket.bonds.end(),
              [](const BasisResult& a, const BasisResult& b) { return a.net_basis < b.net_basis; });

    for (size_t i = 0; i < basket.bonds.size(); ++i) {
        basket.bonds[i].ctd_rank = static_cast<int>(i + 1);
    }

    // Mark CTD
    basket.ctd_cusip = ctd;
    if (!basket.bonds.empty()) {
        basket.bonds[0].is_ctd = true;
    }

    // Summary statistics
    basket.min_net_basis = min_basis;
    basket.max_net_basis = max_basis;
    basket.avg_net_basis = bonds.empty() ? 0.0 : sum_basis / static_cast<double>(bonds.size());

    return basket;
}

// ============================================================================
// Delivery Option Valuation (Stubs)
// See: docs/INPUT_REQUIREMENTS.md Section 1.5
// ============================================================================

struct DeliveryOptionValues {
    double quality_option; // Switch/quality option value
    double timing_option;  // Delivery timing option
    double eom_option;     // End-of-month option
    double total;          // Sum of all options
};

// TODO: Implement delivery option models
// These require yield curve vol and potentially Monte Carlo
auto value_delivery_options(const DeliverableBasket& /* basket */,
                            const ql::Handle<ql::YieldTermStructure>& /* curve */,
                            double /* yield_vol */) -> DeliveryOptionValues {
    spdlog::warn("value_delivery_options: Not yet implemented");
    return DeliveryOptionValues{0.0, 0.0, 0.0, 0.0};
}

// ============================================================================
// QuantLib-based Bond Analytics
// ============================================================================

auto price_bond(double coupon, const ql::Date& maturity, const ql::Date& settle_date,
                const ql::Handle<ql::YieldTermStructure>& curve) -> double {
    ql::Settings::instance().evaluationDate() = settle_date;

    ql::Schedule schedule(settle_date, maturity, ql::Period(ql::Semiannual),
                          ql::UnitedStates(ql::UnitedStates::GovernmentBond), ql::Unadjusted,
                          ql::Unadjusted, ql::DateGeneration::Backward, false);

    ql::FixedRateBond bond(0,     // settlement days (already on settle_date)
                           100.0, // face amount
                           schedule, vector<double>{coupon},
                           ql::ActualActual(ql::ActualActual::Bond));

    auto engine = ql::ext::make_shared<ql::DiscountingBondEngine>(curve);
    bond.setPricingEngine(engine);

    return bond.cleanPrice();
}

// Calculate yield to maturity from price
auto yield_from_price(double coupon, const ql::Date& maturity, const ql::Date& settle_date,
                      double clean_price) -> double {
    ql::Settings::instance().evaluationDate() = settle_date;

    ql::Schedule schedule(settle_date, maturity, ql::Period(ql::Semiannual),
                          ql::UnitedStates(ql::UnitedStates::GovernmentBond), ql::Unadjusted,
                          ql::Unadjusted, ql::DateGeneration::Backward, false);

    ql::FixedRateBond bond(0, 100.0, schedule, vector<double>{coupon},
                           ql::ActualActual(ql::ActualActual::Bond));

    // QuantLib 1.40+ uses Bond::Price struct
    ql::Bond::Price price(clean_price, ql::Bond::Price::Clean);
    return bond.yield(price, ql::ActualActual(ql::ActualActual::Bond), ql::Compounded,
                      ql::Semiannual);
}

// Calculate modified duration
auto modified_duration(double coupon, const ql::Date& maturity, const ql::Date& settle_date,
                       double yield) -> double {
    ql::Settings::instance().evaluationDate() = settle_date;

    ql::Schedule schedule(settle_date, maturity, ql::Period(ql::Semiannual),
                          ql::UnitedStates(ql::UnitedStates::GovernmentBond), ql::Unadjusted,
                          ql::Unadjusted, ql::DateGeneration::Backward, false);

    ql::FixedRateBond bond(0, 100.0, schedule, vector<double>{coupon},
                           ql::ActualActual(ql::ActualActual::Bond));

    return ql::BondFunctions::duration(bond, yield, ql::ActualActual(ql::ActualActual::Bond),
                                       ql::Compounded, ql::Semiannual, ql::Duration::Modified);
}

} // namespace finkit::analysis
