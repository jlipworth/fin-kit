module;

#include <chrono>
#include <cstdint>
#include <map>
#include <optional>
#include <ql/quantlib.hpp>
#include <string>
#include <unordered_map>
#include <vector>

export module finkit.types;

export namespace finkit::types {

using std::map;
using std::optional;
using std::string;
using std::vector;

namespace ql = QuantLib;

// ============================================================================
// Timestamp Types
// ============================================================================

using Timestamp = std::chrono::time_point<std::chrono::system_clock, std::chrono::nanoseconds>;

// ============================================================================
// Currency and Index Types
// See: docs/reference/conventions.md
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

/// Get market convention for a currency
[[nodiscard]] auto get_convention(Currency ccy) -> CurrencyConvention {
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
// Asset Classes
// ============================================================================

enum class AssetClass { Equity, Bond, Future, FX, Option, Swap, ETF, Index, Commodity, Crypto };

// ============================================================================
// Currency Utilities
// ============================================================================

[[nodiscard]] auto g10_pairs() -> vector<std::pair<Currency, Currency>> {
    return {
        {Currency::EUR, Currency::USD}, {Currency::GBP, Currency::USD},
        {Currency::JPY, Currency::USD}, {Currency::CHF, Currency::USD},
        {Currency::AUD, Currency::USD}, {Currency::CAD, Currency::USD},
        {Currency::NZD, Currency::USD}, {Currency::NOK, Currency::USD},
        {Currency::SEK, Currency::USD},
    };
}

[[nodiscard]] auto currency_to_string(Currency ccy) -> string {
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

[[nodiscard]] auto string_to_currency(const string& s) -> optional<Currency> {
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
// FX Types
// See: docs/modules/types.md
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

// ============================================================================
// Interest Rate Types
// ============================================================================

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
// SOFR Curve Types
// See: docs/modules/bootstrap.md
// ============================================================================

struct SOFRFixing {
    ql::Date fixing_date; // Publication date
    double rate;          // Overnight rate (e.g., 0.053 for 5.3%)
};

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

struct OISQuote {
    string tenor; // e.g., "1Y", "5Y", "10Y"
    double rate;  // Quoted fixed rate
    ql::Date as_of;
    ql::Date effective_date; // Swap start
    ql::Date maturity_date;  // Swap end
};

struct FOMCMeeting {
    ql::Date meeting_date;
    bool has_press_conference;
    optional<double> implied_move; // Market-implied rate change
};

// ============================================================================
// Curve Construction Types
// ============================================================================

struct CurveConfig {
    enum class Interpolation { Linear, LogLinear, Cubic, MonotonicCubic };
    enum class InterpolationVariable { ZeroRates, DiscountFactors, ForwardRates };

    Interpolation interpolation{Interpolation::LogLinear};
    InterpolationVariable variable{InterpolationVariable::DiscountFactors};
    ql::DayCounter day_count{ql::Actual360()};
    ql::Compounding compounding{ql::Continuous};
};

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
// Bond Types
// See: docs/modules/types.md
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
// Futures Types
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
// Repo Types
// ============================================================================

struct RepoRate {
    ql::Date as_of;
    double gc_rate;                // General collateral rate
    optional<double> special_rate; // Issue-specific (if on special)
    double term_days;              // Term of repo (days to delivery)
    double haircut{0.02};          // Typical 2% haircut
};

// ============================================================================
// Basis Analysis Types
// See: docs/modules/basis.md
// ============================================================================

struct BasisResult {
    // Identifiers
    string cusip;
    string futures_code;

    // Conversion factor (CME 6% yield assumption)
    double conversion_factor;

    // Basis measures (in 32nds for standard quoting)
    double gross_basis;       // Cash - (Futures x CF)
    double net_basis;         // Gross basis - Carry (BNOC)
    double gross_basis_32nds; // Gross basis in 32nds
    double net_basis_32nds;   // Net basis in 32nds

    // Repo and carry
    double implied_repo;  // Implied repo rate (annualized)
    double carry;         // Cost of carry to delivery
    double coupon_income; // Interim coupon if any

    // Invoice calculation
    double invoice_price; // Futures x CF + Accrued at delivery

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

    // CTD summary in 32nds (for convenient access)
    double ctd_gross_basis_32nds{0.0};
    double ctd_net_basis_32nds{0.0};
    double ctd_implied_repo{0.0};

    // Summary statistics
    double avg_net_basis;
    double min_net_basis;
    double max_net_basis;
};

// ============================================================================
// CIP Basis Types
// See: docs/modules/basis.md
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
    double theoretical_forward; // F = S x (1 + r_q x t) / (1 + r_b x t)
    double cip_basis_bps;       // (Implied_r - Actual_r) x 10000
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

// ============================================================================
// Option Types (Stub for future)
// ============================================================================

struct OptionQuote {
    string underlying;
    double strike;
    ql::Date expiry;
    enum class Type { Call, Put } option_type;
    double bid;
    double ask;
    double mid;
    optional<double> implied_vol;
    optional<double> delta;
    ql::Date as_of;
};

struct VolSurface {
    string underlying;
    ql::Date as_of;
    vector<ql::Date> expiries;
    vector<double> strikes;
    vector<vector<double>> vols; // vols[expiry_idx][strike_idx]
};

// ============================================================================
// Delivery Option Types
// ============================================================================

struct DeliveryOptionValues {
    double quality_option; // Switch/quality option value
    double timing_option;  // Delivery timing option
    double eom_option;     // End-of-month option
    double total;          // Sum of all options
};

} // namespace finkit::types
