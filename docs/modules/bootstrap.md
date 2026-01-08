# finkit.bootstrap Module

Interest rate curve bootstrapping for SOFR and G10 OIS curves.

## Overview

The `finkit.bootstrap` module provides yield curve construction from market
instruments using QuantLib. It supports SOFR curve building from futures and
swaps, generic OIS curve bootstrapping for G10 currencies, and Fed probability
calculations from futures prices.

## Curve Bootstrapping

### SOFR Curve

```cpp
auto bootstrap_sofr_curve(
    const vector<SOFRFixing>& fixings,
    const vector<SOFRFuture>& futures,
    const vector<OISQuote>& swaps,
    const ql::Date& settle_date,
    const CurveConfig& config = CurveConfig{}
) -> BootstrapResult;
```

Builds USD SOFR curve using:
1. Historical fixings (for index fixing history)
2. SR1/SR3 futures (short end, with convexity adjustment)
3. OIS swaps (long end)

### Generic OIS Curve

```cpp
auto bootstrap_ois_curve(
    const vector<OISQuote>& quotes,
    Currency currency,
    ql::DayCounter day_count,
    ql::Calendar calendar,
    const ql::Date& settle_date,
    const CurveConfig& config = CurveConfig{}
) -> BootstrapResult;
```

Supports USD (SOFR), EUR (ESTR), and GBP (SONIA). Other G10 currencies return
an error (not yet implemented).

## Curve Accessors

```cpp
auto forward_rate(const ql::YieldTermStructure& curve,
                  const ql::Date& start, const ql::Date& end,
                  ql::DayCounter day_count = ql::Actual360()) -> double;

auto zero_rate(const ql::YieldTermStructure& curve,
               const ql::Date& date,
               ql::DayCounter day_count = ql::Actual360()) -> double;

auto discount_factor(const ql::YieldTermStructure& curve,
                     const ql::Date& date) -> double;
```

## Fed Probability Calculations

### Single Meeting

```cpp
auto calculate_fed_probability(
    double ff_futures_price,
    const ql::Date& meeting_date,
    double current_target_rate,
    const ql::Date& valuation_date
) -> FedProbabilityResult;
```

Returns `FedProbabilityResult` with:
- `implied_rate_pre`, `implied_rate_post`
- `prob_hike_25bp`, `prob_cut_25bp`, `prob_no_change`
- `expected_move_bps`

### Cumulative Probabilities

```cpp
auto calculate_cumulative_fed_probabilities(
    const vector<double>& ff_futures_prices,
    const vector<FOMCMeeting>& meetings,
    double current_target_rate,
    const ql::Date& valuation_date
) -> CumulativeFedProbabilities;
```

## Utilities

```cpp
auto parse_tenor(const string& tenor) -> ql::Period;
```

Parses tenor strings like `"1M"`, `"3M"`, `"1Y"`, `"5Y"` to QuantLib periods.

## Dependencies

- `finkit.types` - Curve and rate types
- `QuantLib` - Curve construction and date handling

## Usage

```cpp
import finkit.bootstrap;
import finkit.types;

using namespace finkit::bootstrap;
using namespace finkit::types;

// Build SOFR curve
vector<SOFRFuture> futures = { /* ... */ };
vector<OISQuote> swaps = { /* ... */ };
auto result = bootstrap_sofr_curve({}, futures, swaps, ql::Date(15, ql::Jan, 2025));

if (result.success) {
    double df_1y = discount_factor(*result.curve, ql::Date(15, ql::Jan, 2026));
    double zr_1y = zero_rate(*result.curve, ql::Date(15, ql::Jan, 2026));
}
```

## Related

- [types.md](types.md) - Curve and rate type definitions
- [basis.md](basis.md) - Uses curves for basis calculations
