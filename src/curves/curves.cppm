module;

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <ql/quantlib.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

export module finkit.curves;

import finkit.types;
import finkit.data;
import finkit.bootstrap;

export namespace finkit::curves {

using std::optional;
using std::string;
using std::vector;

namespace ql = QuantLib;

// Re-export types for convenience
using finkit::types::CIPBasisResult;
using finkit::types::Currency;
using finkit::types::FXForward;
using finkit::types::FXSpot;
using finkit::types::FXSwap;
using finkit::types::G10BasisSnapshot;
using finkit::types::InterestRate;
using finkit::types::XCCYBasisSwap;

// Use types functions
using finkit::types::currency_to_string;
using finkit::types::g10_pairs;
using finkit::types::get_convention;
using finkit::types::string_to_currency;

// Re-export bootstrap functions for backward compatibility
using finkit::bootstrap::bootstrap_ois_curve;
using finkit::bootstrap::bootstrap_sofr_curve;
using finkit::bootstrap::discount_factor;
using finkit::bootstrap::forward_rate;
using finkit::bootstrap::zero_rate;

// ============================================================================
// CIP Basis Calculations
// Reference: Du, Tepper, Verdelhan (2018)
// TODO: Move to finkit.basis module in future refactoring
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

// ============================================================================
// Batch G10 Analysis
// ============================================================================

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

} // namespace finkit::curves
