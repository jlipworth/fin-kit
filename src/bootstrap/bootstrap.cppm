module;

#include <algorithm>
#include <cmath>
#include <optional>
#include <ql/quantlib.hpp>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

export module finkit.bootstrap;

import finkit.types;

export namespace finkit::bootstrap {

using std::optional;
using std::string;
using std::vector;

namespace ql = QuantLib;

// Re-export types for convenience
using finkit::types::BootstrapResult;
using finkit::types::Currency;
using finkit::types::CurveConfig;
using finkit::types::FOMCMeeting;
using finkit::types::OISQuote;
using finkit::types::OvernightIndex;
using finkit::types::SOFRFixing;
using finkit::types::SOFRFuture;
using finkit::types::SOFRFuturesType;

using finkit::types::get_convention;

// ============================================================================
// Utility: Parse tenor string to QuantLib Period
// ============================================================================

/// Parse tenor string like "1M", "3M", "1Y", "5Y" to QuantLib Period
auto parse_tenor(const string& tenor) -> ql::Period {
    if (tenor.empty()) {
        throw std::invalid_argument("Empty tenor string");
    }

    // Parse number and unit
    size_t pos = 0;
    int n = std::stoi(tenor, &pos);
    if (pos >= tenor.size()) {
        throw std::invalid_argument("Invalid tenor: missing unit");
    }

    char unit = tenor[pos];
    switch (std::toupper(unit)) {
    case 'D':
        return ql::Period(n, ql::Days);
    case 'W':
        return ql::Period(n, ql::Weeks);
    case 'M':
        return ql::Period(n, ql::Months);
    case 'Y':
        return ql::Period(n, ql::Years);
    default:
        throw std::invalid_argument(fmt::format("Unknown tenor unit: {}", unit));
    }
}

// ============================================================================
// Curve Accessors
// ============================================================================

/// Forward rate between two dates
auto forward_rate(const ql::YieldTermStructure& curve, const ql::Date& start, const ql::Date& end,
                  ql::DayCounter day_count = ql::Actual360()) -> double {
    return curve.forwardRate(start, end, day_count, ql::Simple).rate();
}

/// Zero rate at a given date
auto zero_rate(const ql::YieldTermStructure& curve, const ql::Date& date,
               ql::DayCounter day_count = ql::Actual360()) -> double {
    return curve.zeroRate(date, day_count, ql::Continuous).rate();
}

/// Discount factor at a given date
auto discount_factor(const ql::YieldTermStructure& curve, const ql::Date& date) -> double {
    return curve.discount(date);
}

// ============================================================================
// SOFR Curve Bootstrapping
// Reference: QuantLib OvernightIndexedSwap documentation
// ============================================================================

/// Build SOFR curve from market instruments
/// Instrument priority: SOFR fixing -> SR1/SR3 futures -> OIS swaps
auto bootstrap_sofr_curve(const vector<SOFRFixing>& fixings, const vector<SOFRFuture>& futures,
                          const vector<OISQuote>& swaps, const ql::Date& settle_date,
                          const CurveConfig& config = CurveConfig{}) -> BootstrapResult {
    BootstrapResult result;
    result.success = false;

    try {
        ql::Settings::instance().evaluationDate() = settle_date;

        // Create SOFR index
        auto sofr = ql::ext::make_shared<ql::Sofr>();

        // Add historical fixings if provided
        for (const auto& fixing : fixings) {
            if (fixing.fixing_date < settle_date) {
                sofr->addFixing(fixing.fixing_date, fixing.rate);
            }
        }

        // Build rate helpers
        vector<ql::ext::shared_ptr<ql::RateHelper>> helpers;

        // Add futures rate helpers (short end)
        for (const auto& fut : futures) {
            // Convexity adjustment (use provided or estimate)
            double convexity = fut.convexity_adj.value_or(0.0);
            double adj_rate = fut.implied_rate - convexity;

            auto helper = ql::ext::make_shared<ql::OvernightIndexFutureRateHelper>(
                ql::Handle<ql::Quote>(ql::ext::make_shared<ql::SimpleQuote>(adj_rate)),
                fut.reference_start, fut.reference_end, sofr);
            helpers.push_back(helper);
        }

        // Add OIS swap helpers (long end)
        for (const auto& swap : swaps) {
            auto period = parse_tenor(swap.tenor);
            auto helper = ql::ext::make_shared<ql::OISRateHelper>(
                2, // Settlement days
                period, ql::Handle<ql::Quote>(ql::ext::make_shared<ql::SimpleQuote>(swap.rate)),
                sofr);
            helpers.push_back(helper);
        }

        if (helpers.empty()) {
            result.error_message = "No rate helpers provided";
            return result;
        }

        // Bootstrap the curve
        // Note: MonotonicCubic not supported by PiecewiseYieldCurve in this version
        ql::ext::shared_ptr<ql::YieldTermStructure> curve;

        switch (config.interpolation) {
        case CurveConfig::Interpolation::Linear:
            curve = ql::ext::make_shared<ql::PiecewiseYieldCurve<ql::Discount, ql::Linear>>(
                settle_date, helpers, config.day_count);
            break;
        case CurveConfig::Interpolation::Cubic:
        case CurveConfig::Interpolation::MonotonicCubic:
            // Use Cubic for both - MonotonicCubicNaturalSpline has compatibility issues
            curve = ql::ext::make_shared<ql::PiecewiseYieldCurve<ql::Discount, ql::Cubic>>(
                settle_date, helpers, config.day_count);
            break;
        case CurveConfig::Interpolation::LogLinear:
        default:
            curve = ql::ext::make_shared<ql::PiecewiseYieldCurve<ql::Discount, ql::LogLinear>>(
                settle_date, helpers, config.day_count);
            break;
        }

        // Extract pillar dates from helpers
        result.pillar_dates.push_back(settle_date);
        for (const auto& helper : helpers) {
            result.pillar_dates.push_back(helper->latestDate());
        }
        // Sort and remove duplicates
        std::sort(result.pillar_dates.begin(), result.pillar_dates.end());
        result.pillar_dates.erase(
            std::unique(result.pillar_dates.begin(), result.pillar_dates.end()),
            result.pillar_dates.end());

        // Extract curve values at pillar dates
        for (const auto& date : result.pillar_dates) {
            result.discount_factors.push_back(curve->discount(date));
            result.zero_rates.push_back(
                curve->zeroRate(date, config.day_count, ql::Continuous).rate());
            // Instantaneous forward at each pillar
            if (date > settle_date) {
                result.forward_rates.push_back(
                    curve->forwardRate(date, date, config.day_count, ql::Continuous).rate());
            } else {
                result.forward_rates.push_back(result.zero_rates.back());
            }
        }

        result.curve = curve;
        result.success = true;
        spdlog::info("SOFR curve bootstrapped with {} pillars", result.pillar_dates.size());

    } catch (const std::exception& e) {
        result.error_message = fmt::format("Bootstrap failed: {}", e.what());
        spdlog::error(result.error_message);
    }

    return result;
}

// ============================================================================
// Generic OIS Curve Bootstrapping
// Supports any currency with overnight index
// ============================================================================

/// Bootstrap OIS curve for any G10 currency
/// Convention-agnostic: caller provides day count and calendar
auto bootstrap_ois_curve(const vector<OISQuote>& quotes, Currency currency,
                         ql::DayCounter day_count, ql::Calendar /* calendar */,
                         const ql::Date& settle_date,
                         const CurveConfig& /* config */ = CurveConfig{}) -> BootstrapResult {
    BootstrapResult result;
    result.success = false;

    try {
        ql::Settings::instance().evaluationDate() = settle_date;

        // Get the appropriate overnight index for this currency
        ql::ext::shared_ptr<ql::OvernightIndex> index;

        switch (currency) {
        case Currency::USD:
            index = ql::ext::make_shared<ql::Sofr>();
            break;
        case Currency::EUR:
            index = ql::ext::make_shared<ql::Estr>();
            break;
        case Currency::GBP:
            index = ql::ext::make_shared<ql::Sonia>();
            break;
        default:
            // For other currencies, create a generic OvernightIndex
            // This is a simplified approach - production code would have
            // specific index implementations for each currency
            result.error_message = fmt::format("Currency {} not yet supported",
                                               finkit::types::currency_to_string(currency));
            spdlog::warn(result.error_message);
            return result;
        }

        // Build rate helpers
        vector<ql::ext::shared_ptr<ql::RateHelper>> helpers;

        for (const auto& quote : quotes) {
            auto period = parse_tenor(quote.tenor);
            auto helper = ql::ext::make_shared<ql::OISRateHelper>(
                2, // Settlement days
                period, ql::Handle<ql::Quote>(ql::ext::make_shared<ql::SimpleQuote>(quote.rate)),
                index);
            helpers.push_back(helper);
        }

        if (helpers.empty()) {
            result.error_message = "No rate helpers provided";
            return result;
        }

        // Bootstrap the curve
        auto curve = ql::ext::make_shared<ql::PiecewiseYieldCurve<ql::Discount, ql::LogLinear>>(
            settle_date, helpers, day_count);

        // Extract pillar dates from helpers
        result.pillar_dates.push_back(settle_date);
        for (const auto& helper : helpers) {
            result.pillar_dates.push_back(helper->latestDate());
        }
        // Sort and remove duplicates
        std::sort(result.pillar_dates.begin(), result.pillar_dates.end());
        result.pillar_dates.erase(
            std::unique(result.pillar_dates.begin(), result.pillar_dates.end()),
            result.pillar_dates.end());

        // Extract curve values at pillar dates
        for (const auto& date : result.pillar_dates) {
            result.discount_factors.push_back(curve->discount(date));
            result.zero_rates.push_back(curve->zeroRate(date, day_count, ql::Continuous).rate());
            if (date > settle_date) {
                result.forward_rates.push_back(
                    curve->forwardRate(date, date, day_count, ql::Continuous).rate());
            } else {
                result.forward_rates.push_back(result.zero_rates.back());
            }
        }

        result.curve = curve;
        result.success = true;
        spdlog::info("OIS curve for {} bootstrapped with {} pillars",
                     finkit::types::currency_to_string(currency), result.pillar_dates.size());

    } catch (const std::exception& e) {
        result.error_message = fmt::format("Bootstrap failed: {}", e.what());
        spdlog::error(result.error_message);
    }

    return result;
}

// ============================================================================
// Central Bank Probability Calculations
// Reference: ZQ futures and Fed Funds futures pricing
// ============================================================================

/// Result of Fed probability calculation from futures
struct FedProbabilityResult {
    ql::Date meeting_date;
    double current_target_rate; // Current Fed target rate
    double implied_rate_pre;    // Implied rate before meeting
    double implied_rate_post;   // Implied rate after meeting
    double expected_move_bps;   // Expected move in bps
    int days_to_meeting;
    int lower_move_bps; // Lower 25bp bracket (floor)
    double prob_lower;  // Probability of lower bracket outcome
    int upper_move_bps; // Upper 25bp bracket (lower + 25)
    double prob_upper;  // Probability of upper bracket outcome
};

/// Calculate implied Fed rate from Fed Funds futures price
/// Price = 100 - average_rate_for_month
auto implied_rate_from_ff_futures(double futures_price) -> double {
    return (100.0 - futures_price) / 100.0;
}

/// Calculate Fed probabilities using the meeting month vs prior month approach
/// Requires:
/// - Fed Funds future for the meeting month
/// - Number of days until FOMC meeting in that month
/// - Current target rate
auto calculate_fed_probability(double ff_futures_price, const ql::Date& meeting_date,
                               double current_target_rate,
                               const ql::Date& valuation_date) -> FedProbabilityResult {
    FedProbabilityResult result;
    result.meeting_date = meeting_date;
    result.current_target_rate = current_target_rate;
    result.days_to_meeting = meeting_date - valuation_date;

    // Implied average rate for the month
    double implied_avg = implied_rate_from_ff_futures(ff_futures_price);

    // Days in the month
    ql::Date month_end = ql::Date::endOfMonth(meeting_date);
    int days_in_month = month_end.dayOfMonth();

    // Days before meeting (at current rate) vs after meeting (at new rate)
    int day_of_meeting = meeting_date.dayOfMonth();
    int days_before = day_of_meeting;
    int days_after = days_in_month - day_of_meeting;

    // Solve for post-meeting rate:
    // implied_avg = (days_before * current_rate + days_after * post_rate) / days_in_month
    // post_rate = (implied_avg * days_in_month - days_before * current_rate) / days_after
    if (days_after > 0) {
        result.implied_rate_post =
            (implied_avg * days_in_month - days_before * current_target_rate) / days_after;
    } else {
        result.implied_rate_post = implied_avg;
    }
    result.implied_rate_pre = current_target_rate;

    // Calculate expected move
    result.expected_move_bps = (result.implied_rate_post - current_target_rate) * 10000.0;

    // Bracket the expected move between two adjacent 25bp outcomes
    int lower = static_cast<int>(std::floor(result.expected_move_bps / 25.0)) * 25;
    int upper = lower + 25;
    double p_upper = (result.expected_move_bps - lower) / 25.0;

    result.lower_move_bps = lower;
    result.upper_move_bps = upper;
    result.prob_upper = p_upper;
    result.prob_lower = 1.0 - p_upper;

    return result;
}

/// Calculate cumulative Fed probabilities across multiple meetings
/// Uses the step-wise approach: each meeting probability depends on prior
struct CumulativeFedProbabilities {
    vector<FedProbabilityResult> by_meeting;
    double total_expected_cuts_by_year_end; // In 25bp increments
    double total_expected_hikes_by_year_end;
    double implied_terminal_rate;
};

auto calculate_cumulative_fed_probabilities(
    const vector<double>& ff_futures_prices, const vector<FOMCMeeting>& meetings,
    double current_target_rate, const ql::Date& valuation_date) -> CumulativeFedProbabilities {
    CumulativeFedProbabilities result;
    result.total_expected_cuts_by_year_end = 0.0;
    result.total_expected_hikes_by_year_end = 0.0;

    if (ff_futures_prices.size() != meetings.size()) {
        spdlog::warn("Mismatch between futures prices and meetings count");
        return result;
    }

    double running_rate = current_target_rate;

    for (size_t i = 0; i < meetings.size(); ++i) {
        auto prob = calculate_fed_probability(ff_futures_prices[i], meetings[i].meeting_date,
                                              running_rate, valuation_date);
        result.by_meeting.push_back(prob);

        // Update running rate for next meeting calculation
        running_rate = prob.implied_rate_post;

        // Accumulate expected moves
        if (prob.expected_move_bps < 0) {
            result.total_expected_cuts_by_year_end += std::abs(prob.expected_move_bps) / 25.0;
        } else {
            result.total_expected_hikes_by_year_end += prob.expected_move_bps / 25.0;
        }
    }

    result.implied_terminal_rate = running_rate;
    return result;
}

// ============================================================================
// Inflation Curve Bootstrapping (Stub)
// ============================================================================

struct InflationCurveResult {
    bool success{false};
    string error_message;
    vector<ql::Date> pillar_dates;
    vector<double> zero_inflation_rates;
    vector<double> real_zero_rates;
    vector<double> breakeven_rates;
};

/// Bootstrap inflation curve from TIPS and nominals
/// TODO: Implement using QuantLib ZeroCouponInflationSwapHelper
auto bootstrap_inflation_curve(const ql::Date& /* settle_date */) -> InflationCurveResult {
    spdlog::warn("bootstrap_inflation_curve: Not yet implemented");
    InflationCurveResult result;
    result.success = false;
    result.error_message = "Not implemented";
    return result;
}

} // namespace finkit::bootstrap
