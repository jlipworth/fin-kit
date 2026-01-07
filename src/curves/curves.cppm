module;

#include <chrono>
#include <optional>
#include <ql/quantlib.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <unordered_map>
#include <vector>

export module finkit.curves;

import finkit.data;

export namespace finkit::curves {

using std::optional;
using std::string;
using std::vector;

namespace ql = QuantLib;

// ============================================================================
// Currency and Index Types
// See: docs/INPUT_REQUIREMENTS.md Section 2.5
// ============================================================================

enum class Currency { USD, EUR, GBP, JPY, CHF, AUD, CAD, NZD, NOK, SEK };

enum class OvernightIndex {
    SOFR,   // USD - Secured Overnight Financing Rate
    ESTR,   // EUR - Euro Short-Term Rate (was EONIA)
    SONIA,  // GBP - Sterling Overnight Index Average
    TONAR,  // JPY - Tokyo Overnight Average Rate
    SARON,  // CHF - Swiss Average Rate Overnight
    AONIA,  // AUD - RBA Interbank Overnight Cash Rate
    CORRA,  // CAD - Canadian Overnight Repo Rate Average
    NZONIA, // NZD - Official Cash Rate
    NOWA,   // NOK - Norwegian Overnight Weighted Average
    SWESTR  // SEK - Swedish krona Short Term Rate
};

// Currency conventions table
struct CurrencyConvention {
    Currency currency;
    OvernightIndex ois_index;
    ql::DayCounter day_count;
    int spot_lag;    // T+1 for CAD, T+2 for others
    double pip_size; // 0.0001 for most, 0.01 for JPY
};

auto get_convention(Currency ccy) -> CurrencyConvention {
    switch (ccy) {
    case Currency::USD:
        return {ccy, OvernightIndex::SOFR, ql::Actual360(), 2, 0.0001};
    case Currency::EUR:
        return {ccy, OvernightIndex::ESTR, ql::Actual360(), 2, 0.0001};
    case Currency::GBP:
        return {ccy, OvernightIndex::SONIA, ql::Actual365Fixed(), 2, 0.0001};
    case Currency::JPY:
        return {ccy, OvernightIndex::TONAR, ql::Actual365Fixed(), 2, 0.01};
    case Currency::CHF:
        return {ccy, OvernightIndex::SARON, ql::Actual360(), 2, 0.0001};
    case Currency::AUD:
        return {ccy, OvernightIndex::AONIA, ql::Actual365Fixed(), 2, 0.0001};
    case Currency::CAD:
        return {ccy, OvernightIndex::CORRA, ql::Actual365Fixed(), 1, 0.0001};
    case Currency::NZD:
        return {ccy, OvernightIndex::NZONIA, ql::Actual365Fixed(), 2, 0.0001};
    case Currency::NOK:
        return {ccy, OvernightIndex::NOWA, ql::Actual360(), 2, 0.0001};
    case Currency::SEK:
        return {ccy, OvernightIndex::SWESTR, ql::Actual360(), 2, 0.0001};
    }
    return {Currency::USD, OvernightIndex::SOFR, ql::Actual360(), 2, 0.0001};
}

// ============================================================================
// SOFR Curve Bootstrapping Types
// See: docs/INPUT_REQUIREMENTS.md Section 3
// ============================================================================

// SOFR overnight fixing (historical data needed for swap pricing)
struct SOFRFixing {
    ql::Date fixing_date; // Publication date
    double rate;          // Overnight rate (e.g., 0.053 for 5.3%)
};

// SOFR Futures (SR1 = 1-month, SR3 = 3-month)
enum class SOFRFuturesType { SR1, SR3 };

struct SOFRFuture {
    string contract_code; // e.g., "SFRZ4"
    SOFRFuturesType type;
    double price;             // Quote (100 - rate)
    double implied_rate;      // 100 - price
    ql::Date reference_start; // Start of averaging period
    ql::Date reference_end;   // End of averaging period
    ql::Date last_trade_date;
    optional<double> convexity_adj; // Futures vs forward adjustment
};

// OIS Swap quote
struct OISQuote {
    string tenor; // e.g., "1Y", "5Y", "10Y"
    double rate;  // Quoted fixed rate
    ql::Date as_of;
    ql::Date effective_date; // Swap start
    ql::Date maturity_date;  // Swap end
};

// FOMC meeting dates (for turn adjustments)
struct FOMCMeeting {
    ql::Date meeting_date;
    bool has_press_conference;
    optional<double> implied_move; // Market-implied rate change
};

// Curve construction parameters
struct CurveConfig {
    enum class Interpolation { Linear, LogLinear, Cubic, MonotonicCubic };
    enum class InterpolationVariable { ZeroRates, DiscountFactors, ForwardRates };

    Interpolation interpolation{Interpolation::LogLinear};
    InterpolationVariable variable{InterpolationVariable::DiscountFactors};
    ql::DayCounter day_count{ql::Actual360()};
    ql::Compounding compounding{ql::Continuous};
};

// Bootstrap result
struct BootstrapResult {
    ql::ext::shared_ptr<ql::YieldTermStructure> curve;
    vector<ql::Date> pillar_dates;
    vector<double> zero_rates;
    vector<double> discount_factors;
    vector<double> forward_rates; // Instantaneous forward at pillars
    bool success{false};
    string error_message;
};

// ============================================================================
// SOFR Curve Bootstrapping
// ============================================================================

// Build SOFR curve from market instruments
// Instrument priority: SOFR fixing -> SR1/SR3 futures -> OIS swaps
auto bootstrap_sofr_curve(const vector<SOFRFixing>& fixings, const vector<SOFRFuture>& futures,
                          const vector<OISQuote>& swaps, const ql::Date& settle_date,
                          const CurveConfig& config = CurveConfig{}) -> BootstrapResult {
    // TODO: Implement SOFR curve bootstrapping using QuantLib
    //
    // QuantLib classes to use:
    // - ql::Sofr (overnight index)
    // - ql::OISRateHelper (for swap quotes)
    // - ql::OvernightIndexFutureRateHelper (for futures)
    // - ql::PiecewiseYieldCurve<Discount, LogLinear>
    //
    // Steps:
    // 1. Create Sofr index with historical fixings
    // 2. Build rate helpers:
    //    - Short end: futures (SR1, SR3) with convexity adjustment
    //    - Long end: OIS swaps
    // 3. Handle instrument overlaps (prefer futures in overlap region)
    // 4. Bootstrap curve
    // 5. Extract pillar dates, zero rates, discount factors

    (void)fixings;
    (void)futures;
    (void)swaps;
    (void)settle_date;
    (void)config;

    spdlog::warn("bootstrap_sofr_curve: Not yet implemented");

    BootstrapResult result;
    result.success = false;
    result.error_message = "Not implemented";
    return result;
}

// Forward SOFR rate between two dates
auto forward_rate(const ql::YieldTermStructure& curve, const ql::Date& start, const ql::Date& end,
                  ql::DayCounter day_count = ql::Actual360()) -> double {
    return curve.forwardRate(start, end, day_count, ql::Simple).rate();
}

// Zero rate at a given date
auto zero_rate(const ql::YieldTermStructure& curve, const ql::Date& date,
               ql::DayCounter day_count = ql::Actual360()) -> double {
    return curve.zeroRate(date, day_count, ql::Continuous).rate();
}

// Discount factor at a given date
auto discount_factor(const ql::YieldTermStructure& curve, const ql::Date& date) -> double {
    return curve.discount(date);
}

// ============================================================================
// FX and CIP Basis Types
// See: docs/INPUT_REQUIREMENTS.md Section 2
// Reference: Du, Tepper, Verdelhan (2018)
// ============================================================================

struct FXSpot {
    Currency base;  // e.g., EUR
    Currency quote; // e.g., USD (EURUSD)
    double mid;
    double bid;
    double ask;
    ql::Date spot_date; // Settlement date (typically T+2)
    ql::Date as_of;     // Quote timestamp
};

struct FXForward {
    Currency base;
    Currency quote;
    string tenor; // ON, TN, SN, 1W, 1M, 3M, 6M, 1Y, etc.

    double forward_points_mid; // In pips
    double forward_points_bid;
    double forward_points_ask;

    double outright_mid; // Spot + points
    double outright_bid;
    double outright_ask;

    ql::Date value_date;   // Forward settlement
    double spot_reference; // Spot rate used for points
};

struct FXSwap {
    Currency base;
    Currency quote;
    double spot_rate;
    double forward_points;
    ql::Date near_date; // Near leg (typically spot)
    ql::Date far_date;  // Far leg
};

// Interest rate for CIP calculation
struct InterestRate {
    Currency currency;
    string tenor;
    double rate_mid;
    double rate_bid;
    double rate_ask;
    ql::DayCounter day_count;
    enum class Type { OIS, Repo, Deposit, LIBOR } rate_type;
    ql::Date as_of;
};

// Cross-currency basis swap
struct XCCYBasisSwap {
    Currency base;            // Non-USD leg
    Currency quote;           // USD leg
    string tenor;             // 1Y, 2Y, 5Y, 10Y, 30Y
    double basis_spread;      // Spread in bps
    bool spread_on_base_leg;  // True if spread on non-USD leg
    string base_float_index;  // e.g., "ESTR"
    string quote_float_index; // e.g., "SOFR"
};

// ============================================================================
// CIP Basis Calculation Results
// ============================================================================

struct CIPBasisResult {
    Currency base;
    Currency quote;
    string tenor;
    ql::Date start_date;
    ql::Date end_date;
    double year_fraction;

    // Input rates used
    double fx_spot;
    double fx_forward;
    double rate_base;  // OIS rate in base currency
    double rate_quote; // OIS rate in quote currency

    // CIP calculation
    double theoretical_forward; // F = S × (1 + r_q × t) / (1 + r_b × t)
    double cip_basis_bps;       // (Implied_r - Actual_r) × 10000
    double annualized_basis;

    // Transaction cost analysis
    double basis_bid;           // Using bid rates
    double basis_ask;           // Using ask rates
    double round_trip_cost_bps; // Bid-ask spread in bps

    // Arbitrage analysis
    double arb_pnl_per_million; // P&L per $1M notional
    bool is_exploitable;        // After transaction costs
    string arb_direction;       // "borrow_base_lend_quote" or reverse
};

// ============================================================================
// CIP Basis Calculations
// ============================================================================

// Calculate CIP basis from FX forward and interest rates
// CIP: F/S = (1 + r_quote × t) / (1 + r_base × t)
// Basis = implied_r_quote - actual_r_quote
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
    // F/S = (1 + r_q_implied × t) / (1 + r_b × t)
    // r_q_implied = ((F/S) × (1 + r_b × t) - 1) / t
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
    // If basis_bid > 0: borrow base, lend quote, sell forward → profit
    // If basis_ask < 0: borrow quote, lend base, buy forward → profit
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

// Calculate CIP basis from FX swap
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

// Implied funding rate via cross-currency basis swap
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
// G10 Currency Utilities
// ============================================================================

auto g10_pairs() -> vector<std::pair<Currency, Currency>> {
    return {
        {Currency::EUR, Currency::USD}, {Currency::GBP, Currency::USD},
        {Currency::JPY, Currency::USD}, {Currency::CHF, Currency::USD},
        {Currency::AUD, Currency::USD}, {Currency::CAD, Currency::USD},
        {Currency::NZD, Currency::USD}, {Currency::NOK, Currency::USD},
        {Currency::SEK, Currency::USD},
    };
}

auto currency_to_string(Currency ccy) -> string {
    switch (ccy) {
    case Currency::USD:
        return "USD";
    case Currency::EUR:
        return "EUR";
    case Currency::GBP:
        return "GBP";
    case Currency::JPY:
        return "JPY";
    case Currency::CHF:
        return "CHF";
    case Currency::AUD:
        return "AUD";
    case Currency::CAD:
        return "CAD";
    case Currency::NZD:
        return "NZD";
    case Currency::NOK:
        return "NOK";
    case Currency::SEK:
        return "SEK";
    }
    return "???";
}

auto string_to_currency(const string& s) -> optional<Currency> {
    static const std::unordered_map<string, Currency> map = {
        {"USD", Currency::USD}, {"EUR", Currency::EUR}, {"GBP", Currency::GBP},
        {"JPY", Currency::JPY}, {"CHF", Currency::CHF}, {"AUD", Currency::AUD},
        {"CAD", Currency::CAD}, {"NZD", Currency::NZD}, {"NOK", Currency::NOK},
        {"SEK", Currency::SEK},
    };
    auto it = map.find(s);
    return it != map.end() ? optional{it->second} : std::nullopt;
}

// ============================================================================
// Batch G10 Analysis
// ============================================================================

struct G10BasisSnapshot {
    ql::Date as_of;
    string tenor;
    vector<CIPBasisResult> basis_by_pair;

    // Summary
    optional<string> richest_pair;  // Most positive basis
    optional<string> cheapest_pair; // Most negative basis
    double avg_basis_bps;
    double basis_dispersion; // Std dev of basis
};

// Analyze CIP basis across all G10 USD pairs for a given tenor
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
