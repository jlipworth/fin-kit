module;

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <ql/quantlib.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

export module finkit.basis;

import finkit.types;

export namespace finkit::basis {

using std::optional;
using std::string;
using std::vector;

namespace ql = QuantLib;

// Re-export types for convenience
using finkit::types::BasisResult;
using finkit::types::Bond;
using finkit::types::CIPBasisResult;
using finkit::types::Currency;
using finkit::types::DeliverableBasket;
using finkit::types::DeliveryOptionValues;
using finkit::types::FuturesContract;
using finkit::types::FXForward;
using finkit::types::FXSpot;
using finkit::types::FXSwap;
using finkit::types::G10BasisSnapshot;
using finkit::types::InterestRate;
using finkit::types::RepoRate;
using finkit::types::TreasuryFuturesProduct;
using finkit::types::XCCYBasisSwap;

// Use types functions
using finkit::types::currency_to_string;
using finkit::types::g10_pairs;
using finkit::types::get_convention;
using finkit::types::string_to_currency;

// ============================================================================
// Treasury Bond Basis Calculations
// Reference: CME "Calculating U.S. Treasury Futures Conversion Factors"
// ============================================================================

/// CME conversion factor for Treasury futures
/// Assumes 6% yield, semiannual compounding
/// Rounds maturity down to nearest quarter (3 months)
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
    // CF = a^(v/6) x [c/2 x (1 + a^2 + a^4 + ... + a^(2n)) + a^(2n)]
    //      - c/2 x (6 - v)/6
    //
    // The summation simplifies to: (1 - a^(2n+2)) / (1 - a^2)
    // But we use: c/2 x (1 + (1 - a^(2n)) / (1 - a^2)) + a^(2n)

    double a_2n = std::pow(a, 2.0 * n);
    double geometric_sum = (1.0 - a_2n) / (1.0 - a * a); // = (1-a^2n)/(1-a^2)

    double cf = std::pow(a, static_cast<double>(v_adj) / 6.0) *
                    (coupon / 2.0 * (1.0 + geometric_sum) + a_2n) -
                coupon / 2.0 * (6.0 - v_adj) / 6.0;

    return cf;
}

/// Calculate gross basis
/// Gross Basis = Cash Price - (Futures Price x Conversion Factor)
auto gross_basis(const Bond& bond, const FuturesContract& futures, double cf) -> double {
    return bond.full_price() - futures.price * cf;
}

/// Convert basis to 32nds (standard quoting convention)
auto to_32nds(double decimal_price) -> double {
    return decimal_price * 32.0;
}

/// Implied repo rate calculation
/// Formula: repo = ((Invoice / Purchase) - 1) x (360 / days)
/// Uses ACT/360 day count convention
auto implied_repo_rate(const Bond& bond, const FuturesContract& futures, double cf,
                       const ql::Date& settle_date, const ql::Date& delivery_date,
                       double accrued_at_delivery = 0.0) -> double {
    // Invoice price = Futures x CF + Accrued at delivery
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

/// Carry calculation (financing cost minus coupon income)
/// Carry = Financing cost - Coupon income
/// For bonds with no interim coupon, carry = financing only
auto carry_cost(const Bond& bond, double repo_rate, const ql::Date& settle_date,
                const ql::Date& delivery_date, double interim_coupon = 0.0) -> double {
    int days = delivery_date - settle_date;
    double financing = bond.full_price() * repo_rate * days / 360.0;
    return financing - interim_coupon;
}

/// Calculate interim coupon income (if coupon date falls in holding period)
auto interim_coupon_income(const Bond& bond, const ql::Date& settle_date,
                           const ql::Date& delivery_date) -> double {
    // TODO: Check if a coupon payment date falls between settle and delivery
    // If so, return coupon/2 x reinvestment factor
    // For now, return 0 (conservative assumption)
    (void)bond;
    (void)settle_date;
    (void)delivery_date;
    return 0.0;
}

/// Analyze a deliverable basket for a futures contract
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

        // Hedge ratio: CF x (Futures DV01 / Cash DV01)
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

    // Mark CTD and populate CTD summary fields
    basket.ctd_cusip = ctd;
    if (!basket.bonds.empty()) {
        basket.bonds[0].is_ctd = true;
        basket.ctd_gross_basis_32nds = basket.bonds[0].gross_basis_32nds;
        basket.ctd_net_basis_32nds = basket.bonds[0].net_basis_32nds;
        basket.ctd_implied_repo = basket.bonds[0].implied_repo;
    }

    // Summary statistics
    basket.min_net_basis = min_basis;
    basket.max_net_basis = max_basis;
    basket.avg_net_basis = bonds.empty() ? 0.0 : sum_basis / static_cast<double>(bonds.size());

    return basket;
}

// ============================================================================
// CIP Basis Calculations
// Reference: Du, Tepper, Verdelhan (2018)
// ============================================================================

/// Calculate CIP basis from FX forward and interest rates
/// CIP: F/S = (1 + r_quote x t) / (1 + r_base x t)
/// Basis = implied_r_quote - actual_r_quote
auto calculate_cip_basis(const FXSpot& spot, const FXForward& forward,
                         const InterestRate& rate_base,
                         const InterestRate& rate_quote) -> CIPBasisResult {

    CIPBasisResult result;
    result.base = spot.base;
    result.quote = spot.quote;
    result.tenor = forward.tenor;
    result.start_date = spot.spot_date;
    result.end_date = forward.value_date;

    // Year fraction using quote currency convention
    auto convention = get_convention(spot.quote);
    result.year_fraction = convention.day_count.yearFraction(spot.spot_date, forward.value_date);

    result.fx_spot = spot.mid;
    result.fx_forward = forward.outright_mid;
    result.rate_base = rate_base.rate_mid;
    result.rate_quote = rate_quote.rate_mid;

    double t = result.year_fraction;
    double S = spot.mid;
    double F = forward.outright_mid;
    double r_b = rate_base.rate_mid;
    double r_q = rate_quote.rate_mid;

    // Theoretical forward (CIP condition)
    result.theoretical_forward = S * (1.0 + r_q * t) / (1.0 + r_b * t);

    // Implied quote currency rate from forward
    // F/S = (1 + r_q_implied x t) / (1 + r_b x t)
    // r_q_implied = ((F/S) x (1 + r_b x t) - 1) / t
    double implied_r_q = ((F / S) * (1.0 + r_b * t) - 1.0) / t;

    // CIP basis = implied - actual
    result.cip_basis_bps = (implied_r_q - r_q) * 10000.0;
    result.annualized_basis = implied_r_q - r_q;

    // Transaction cost adjusted basis (using worst-case rates)
    // For positive basis arbitrage: borrow base (pay ask), lend quote (receive bid)
    double implied_bid =
        ((forward.outright_bid / spot.ask) * (1.0 + rate_base.rate_ask * t) - 1.0) / t;
    double implied_ask =
        ((forward.outright_ask / spot.bid) * (1.0 + rate_base.rate_bid * t) - 1.0) / t;

    result.basis_bid = (implied_bid - rate_quote.rate_ask) * 10000.0;
    result.basis_ask = (implied_ask - rate_quote.rate_bid) * 10000.0;
    result.round_trip_cost_bps = result.basis_ask - result.basis_bid;

    // Arbitrage analysis
    // If basis_bid > 0: borrow base, lend quote, sell forward -> profit
    // If basis_ask < 0: borrow quote, lend base, buy forward -> profit
    if (result.basis_bid > 0) {
        result.is_exploitable = true;
        result.arb_direction = "borrow_base_lend_quote";
        result.arb_pnl_per_million = result.basis_bid * 100.0; // $100 per bp per $1M
    } else if (result.basis_ask < 0) {
        result.is_exploitable = true;
        result.arb_direction = "borrow_quote_lend_base";
        result.arb_pnl_per_million = -result.basis_ask * 100.0;
    } else {
        result.is_exploitable = false;
        result.arb_direction = "none";
        result.arb_pnl_per_million = 0.0;
    }

    return result;
}

/// Calculate CIP basis from FX swap
auto calculate_cip_basis(const FXSwap& swap, const InterestRate& rate_base,
                         const InterestRate& rate_quote) -> CIPBasisResult {

    // Convert FX swap to spot + forward
    FXSpot spot;
    spot.base = swap.base;
    spot.quote = swap.quote;
    spot.mid = swap.spot_rate;
    spot.bid = swap.spot_rate; // Simplified
    spot.ask = swap.spot_rate;
    spot.spot_date = swap.near_date;

    FXForward forward;
    forward.base = swap.base;
    forward.quote = swap.quote;
    forward.outright_mid = swap.spot_rate + swap.forward_points;
    forward.outright_bid = forward.outright_mid;
    forward.outright_ask = forward.outright_mid;
    forward.value_date = swap.far_date;

    return calculate_cip_basis(spot, forward, rate_base, rate_quote);
}

/// Implied funding rate via cross-currency basis swap
auto implied_funding_rate(const XCCYBasisSwap& xccy, double domestic_ois_rate) -> double {
    // If you fund in USD and swap to EUR:
    // Effective EUR rate = USD OIS + xccy basis
    // (Sign depends on which leg has the spread)
    if (xccy.spread_on_base_leg) {
        return domestic_ois_rate + xccy.basis_spread / 10000.0;
    } else {
        return domestic_ois_rate - xccy.basis_spread / 10000.0;
    }
}

/// Analyze CIP basis across all G10 USD pairs for a given tenor
auto analyze_g10_basis(const ql::Date& as_of, const string& tenor, const vector<FXSpot>& spots,
                       const vector<FXForward>& forwards, const vector<InterestRate>& rates,
                       const InterestRate& usd_rate) -> G10BasisSnapshot {

    G10BasisSnapshot snapshot;
    snapshot.as_of = as_of;
    snapshot.tenor = tenor;

    double max_basis = std::numeric_limits<double>::lowest();
    double min_basis = std::numeric_limits<double>::max();
    double sum_basis = 0.0;
    double sum_sq_basis = 0.0;
    int count = 0;

    for (const auto& [base_ccy, quote_ccy] : g10_pairs()) {
        // Find matching spot
        auto spot_it = std::find_if(spots.begin(), spots.end(), [&](const FXSpot& s) {
            return s.base == base_ccy && s.quote == quote_ccy;
        });
        if (spot_it == spots.end())
            continue;

        // Find matching forward for tenor
        auto fwd_it = std::find_if(forwards.begin(), forwards.end(), [&](const FXForward& f) {
            return f.base == base_ccy && f.quote == quote_ccy && f.tenor == tenor;
        });
        if (fwd_it == forwards.end())
            continue;

        // Find matching base currency rate
        auto rate_it = std::find_if(rates.begin(), rates.end(), [&](const InterestRate& r) {
            return r.currency == base_ccy && r.tenor == tenor;
        });
        if (rate_it == rates.end())
            continue;

        auto result = calculate_cip_basis(*spot_it, *fwd_it, *rate_it, usd_rate);
        snapshot.basis_by_pair.push_back(result);

        double basis = result.cip_basis_bps;
        sum_basis += basis;
        sum_sq_basis += basis * basis;
        count++;

        string pair_str = currency_to_string(base_ccy) + currency_to_string(quote_ccy);
        if (basis > max_basis) {
            max_basis = basis;
            snapshot.richest_pair = pair_str;
        }
        if (basis < min_basis) {
            min_basis = basis;
            snapshot.cheapest_pair = pair_str;
        }
    }

    if (count > 0) {
        snapshot.avg_basis_bps = sum_basis / count;
        double variance =
            (sum_sq_basis / count) - (snapshot.avg_basis_bps * snapshot.avg_basis_bps);
        snapshot.basis_dispersion = std::sqrt(std::max(0.0, variance));
    }

    return snapshot;
}

// ============================================================================
// Index Futures Basis (Stub)
// ============================================================================

struct IndexBasisResult {
    string index_symbol; // e.g., "SPX"
    string futures_code; // e.g., "ESH5"
    double index_level;
    double futures_price;
    double theoretical_basis; // Dividend-adjusted fair value
    double actual_basis;      // Market basis
    double mispricing_bps;
    double implied_financing_rate;
    double dividend_yield;
    int days_to_expiry;
};

/// Calculate index futures basis (SPX vs ES, NDX vs NQ, etc.)
/// TODO: Implement with proper dividend forecasting
auto calculate_index_basis(const string& /* index_symbol */, double /* index_level */,
                           const string& /* futures_code */, double /* futures_price */,
                           double /* risk_free_rate */, double /* dividend_yield */,
                           const ql::Date& /* settle_date */,
                           const ql::Date& /* expiry_date */) -> IndexBasisResult {
    spdlog::warn("calculate_index_basis: Not yet implemented");
    return {};
}

// ============================================================================
// ETF Basis (Stub)
// ============================================================================

struct ETFBasisResult {
    string etf_symbol; // e.g., "SPY"
    double etf_price;
    double nav; // Net Asset Value
    double premium_discount_pct;
    double implied_tracking_error;
    bool is_significant; // Outside normal bid-ask
};

/// Calculate ETF premium/discount vs NAV
/// TODO: Implement with proper NAV calculation
auto calculate_etf_basis(const string& /* etf_symbol */, double /* etf_price */,
                         double /* nav */) -> ETFBasisResult {
    spdlog::warn("calculate_etf_basis: Not yet implemented");
    return {};
}

// ============================================================================
// Delivery Option Valuation (Stub)
// See: docs/INPUT_REQUIREMENTS.md Section 1.5
// ============================================================================

/// TODO: Implement delivery option models
/// These require yield curve vol and potentially Monte Carlo
auto value_delivery_options(const DeliverableBasket& /* basket */,
                            const ql::Handle<ql::YieldTermStructure>& /* curve */,
                            double /* yield_vol */) -> DeliveryOptionValues {
    spdlog::warn("value_delivery_options: Not yet implemented");
    return DeliveryOptionValues{0.0, 0.0, 0.0, 0.0};
}

} // namespace finkit::basis
