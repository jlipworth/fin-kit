/// @file valuation-swap.cppm
/// @brief Interest rate swap valuation using QuantLib
///
/// Provides swap pricing, par rate calculation, and risk metrics.

module;

#include <cmath>
#include <optional>
#include <ql/quantlib.hpp>
#include <string>
#include <vector>

export module finkit.valuation:swap;

import finkit.types;

export namespace finkit::valuation {

using std::optional;
using std::string;
using std::vector;

namespace ql = QuantLib;

using finkit::types::Currency;
using finkit::types::OvernightIndex;

/// Swap valuation result
struct SwapValuation {
    double npv{0.0};              // Net present value
    double par_rate{0.0};         // Fair fixed rate (decimal)
    double fixed_leg_bpv{0.0};    // Basis point value of fixed leg
    double floating_leg_bpv{0.0}; // BPV of floating leg
    double dv01{0.0};             // Dollar value of 1bp parallel shift
    double fixed_leg_npv{0.0};
    double floating_leg_npv{0.0};
    ql::Date effective_date;
    ql::Date maturity_date;
};

/// OIS swap specification
struct OISSwapSpec {
    Currency currency{Currency::USD};
    OvernightIndex index{OvernightIndex::SOFR};
    double notional{1000000.0};
    double fixed_rate{0.0}; // As decimal (0.05 = 5%)
    int tenor_years{0};     // Tenor in years
    bool pay_fixed{true};   // True = pay fixed, receive float
};

/// Get QuantLib overnight index for currency
auto get_ql_overnight_index(OvernightIndex idx, const ql::Handle<ql::YieldTermStructure>& curve)
    -> ql::ext::shared_ptr<ql::OvernightIndex> {
    switch (idx) {
    case OvernightIndex::SOFR:
        return ql::ext::make_shared<ql::Sofr>(curve);
    case OvernightIndex::ESTR:
        return ql::ext::make_shared<ql::Estr>(curve);
    case OvernightIndex::SONIA:
        return ql::ext::make_shared<ql::Sonia>(curve);
    case OvernightIndex::TONAR:
        return ql::ext::make_shared<ql::Tonar>(curve);
    case OvernightIndex::AONIA:
        return ql::ext::make_shared<ql::Aonia>(curve);
    case OvernightIndex::CORRA:
        return ql::ext::make_shared<ql::Corra>(curve);
    case OvernightIndex::NZONIA:
        return ql::ext::make_shared<ql::Nzocr>(curve);
    default:
        return ql::ext::make_shared<ql::Sofr>(curve);
    }
}

/// Get calendar for currency
auto get_calendar(Currency ccy) -> ql::Calendar {
    switch (ccy) {
    case Currency::USD:
        return ql::UnitedStates(ql::UnitedStates::GovernmentBond);
    case Currency::EUR:
        return ql::TARGET();
    case Currency::GBP:
        return ql::UnitedKingdom();
    case Currency::JPY:
        return ql::Japan();
    case Currency::CHF:
        return ql::Switzerland();
    case Currency::AUD:
        return ql::Australia();
    case Currency::CAD:
        return ql::Canada();
    case Currency::NZD:
        return ql::NewZealand();
    case Currency::SEK:
        return ql::Sweden();
    case Currency::NOK:
        return ql::Norway();
    default:
        return ql::NullCalendar();
    }
}

/// Get day counter for currency OIS
auto get_ois_day_counter(Currency ccy) -> ql::DayCounter {
    switch (ccy) {
    case Currency::GBP:
    case Currency::JPY:
    case Currency::AUD:
    case Currency::CAD:
    case Currency::NZD:
        return ql::Actual365Fixed();
    default:
        return ql::Actual360();
    }
}

/// Value an OIS swap
/// @param spec Swap specification
/// @param forecast_curve Curve for projecting floating rates
/// @param discount_curve Curve for discounting (usually same as forecast for OIS)
/// @param valuation_date Valuation date
/// @return SwapValuation with NPV and risk metrics
auto value_ois_swap(const OISSwapSpec& spec,
                    const ql::Handle<ql::YieldTermStructure>& forecast_curve,
                    const ql::Handle<ql::YieldTermStructure>& discount_curve,
                    const ql::Date& valuation_date) -> SwapValuation {
    SwapValuation result;

    try {
        ql::Settings::instance().evaluationDate() = valuation_date;

        ql::Calendar calendar = get_calendar(spec.currency);
        ql::DayCounter day_counter = get_ois_day_counter(spec.currency);

        // Settlement: T+2 for most markets
        int settlement_days = 2;
        ql::Date effective = calendar.advance(valuation_date, settlement_days, ql::Days);
        ql::Date maturity = calendar.advance(effective, spec.tenor_years, ql::Years);

        result.effective_date = effective;
        result.maturity_date = maturity;

        // Get overnight index
        auto ois_index = get_ql_overnight_index(spec.index, forecast_curve);

        // Build OIS swap
        ql::OvernightIndexedSwap::Type swap_type =
            spec.pay_fixed ? ql::OvernightIndexedSwap::Payer : ql::OvernightIndexedSwap::Receiver;

        ql::OvernightIndexedSwap swap(
            swap_type, spec.notional,
            ql::Schedule(effective, maturity, ql::Period(1, ql::Years), // Annual fixed payments
                         calendar, ql::ModifiedFollowing, ql::ModifiedFollowing,
                         ql::DateGeneration::Backward, false),
            spec.fixed_rate, day_counter, ois_index);

        // Set pricing engine
        auto engine = ql::ext::make_shared<ql::DiscountingSwapEngine>(discount_curve);
        swap.setPricingEngine(engine);

        // Get values
        result.npv = swap.NPV();
        result.fixed_leg_npv = swap.fixedLegNPV();
        result.floating_leg_npv = swap.overnightLegNPV();

        // Calculate par rate
        result.par_rate = swap.fairRate();

        // Calculate BPV by bumping rate
        double bump = 0.0001; // 1 bp

        ql::OvernightIndexedSwap swap_bumped(
            swap_type, spec.notional,
            ql::Schedule(effective, maturity, ql::Period(1, ql::Years), calendar,
                         ql::ModifiedFollowing, ql::ModifiedFollowing, ql::DateGeneration::Backward,
                         false),
            spec.fixed_rate + bump, day_counter, ois_index);
        swap_bumped.setPricingEngine(engine);

        result.fixed_leg_bpv = (swap_bumped.fixedLegNPV() - swap.fixedLegNPV()) / bump * 0.0001;
        result.dv01 = std::abs(swap_bumped.NPV() - swap.NPV());

    } catch (const std::exception& e) {
        // Return default values on error
    }

    return result;
}

/// Calculate par swap rate for given tenor
/// @param tenor_years Swap tenor in years
/// @param forecast_curve Projection curve
/// @param discount_curve Discount curve
/// @param currency Currency for conventions
/// @param valuation_date Valuation date
/// @return Par swap rate as decimal
auto par_swap_rate(int tenor_years, const ql::Handle<ql::YieldTermStructure>& forecast_curve,
                   const ql::Handle<ql::YieldTermStructure>& discount_curve, Currency currency,
                   OvernightIndex index, const ql::Date& valuation_date) -> double {
    OISSwapSpec spec{.currency = currency,
                     .index = index,
                     .notional = 1000000.0,
                     .fixed_rate = 0.0, // Will calculate par rate
                     .tenor_years = tenor_years,
                     .pay_fixed = true};

    auto valuation = value_ois_swap(spec, forecast_curve, discount_curve, valuation_date);
    return valuation.par_rate;
}

/// Generate par swap curve
/// @param tenors Vector of tenors in years
/// @param curve Yield curve
/// @param currency Currency
/// @param index Overnight index
/// @param valuation_date Valuation date
/// @return Vector of par rates corresponding to tenors
auto par_swap_curve(const vector<int>& tenors, const ql::Handle<ql::YieldTermStructure>& curve,
                    Currency currency, OvernightIndex index,
                    const ql::Date& valuation_date) -> vector<double> {
    vector<double> rates;
    rates.reserve(tenors.size());

    for (int tenor : tenors) {
        rates.push_back(par_swap_rate(tenor, curve, curve, currency, index, valuation_date));
    }

    return rates;
}

/// Calculate DV01 for a swap portfolio
/// @param swaps Vector of swap specs (with current fixed rates)
/// @param curve Yield curve
/// @param valuation_date Valuation date
/// @return Total portfolio DV01
auto portfolio_dv01(const vector<OISSwapSpec>& swaps,
                    const ql::Handle<ql::YieldTermStructure>& curve,
                    const ql::Date& valuation_date) -> double {
    double total_dv01 = 0.0;

    for (const auto& spec : swaps) {
        auto val = value_ois_swap(spec, curve, curve, valuation_date);
        total_dv01 += spec.pay_fixed ? val.dv01 : -val.dv01;
    }

    return total_dv01;
}

} // namespace finkit::valuation
