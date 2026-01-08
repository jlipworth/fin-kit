# Curve Bootstrapping

This document explains rate curve construction as implemented in fin-kit's bootstrap module.

## What is Bootstrapping?

**Bootstrapping** is the process of constructing a yield curve from market instruments. Each instrument provides one "pillar point" on the curve. We solve iteratively, short-to-long maturity, for the discount factors that exactly reprice each instrument.

The result is a complete term structure that can:
- Discount future cash flows
- Project forward rates for any period
- Price OIS swaps and other derivatives

## The SOFR Curve

Since the LIBOR transition, SOFR (Secured Overnight Financing Rate) is the primary USD benchmark. The fin-kit bootstrap module constructs SOFR curves from:

1. **SOFR fixings** (overnight, published daily)
2. **SOFR futures** (short end: 0-2 years)
3. **OIS swaps** (medium/long end: 1-30 years)

## Instruments by Tenor

| Tenor Range | Primary Instrument | Notes |
|-------------|-------------------|-------|
| O/N | SOFR fixing | Published rate |
| 1W - 1M | SR1 futures | 1-month SOFR futures |
| 1M - 3M | SR3 futures | 3-month SOFR futures |
| 3M - 2Y | SR3 futures | Overlap with swaps |
| 2Y - 30Y | OIS swaps | Most liquid |

### SR1 Futures (1-Month)

- Monthly contracts
- Settle to arithmetic average of daily SOFR
- Reference period: calendar month

### SR3 Futures (3-Month)

- Quarterly contracts (Mar, Jun, Sep, Dec)
- Settle to compounded SOFR
- Reference period: IMM dates (3rd Wednesday to 3rd Wednesday)

## Bootstrap Algorithm

The algorithm works iteratively:

```
For each instrument (sorted by maturity):
    1. Use existing curve to value the instrument
    2. Solve for the discount factor at instrument maturity
       that makes the instrument price match market
    3. Add this pillar point to the curve
    4. Interpolate between pillars
```

### Discount Factor Relationship

For a zero-coupon rate `r` at time `t`:
```
Discount_Factor = exp(-r x t)    [continuous compounding]
```

### Forward Rate Calculation

Forward rate between dates `t1` and `t2`:
```
Forward = [DF(t1) / DF(t2) - 1] / (t2 - t1)    [simple compounding]
```

## Interpolation Methods

fin-kit supports several interpolation methods:

| Method | Behavior |
|--------|----------|
| LogLinear | Log-linear in discount factors (default) |
| Linear | Linear in discount factors |
| Cubic | Cubic spline |
| MonotonicCubic | Shape-preserving cubic |

LogLinear on discount factors is equivalent to linear interpolation on continuously compounded zero rates.

## Implementation in fin-kit

### Building a SOFR Curve

```cpp
#include <finkit.bootstrap>

// Gather market data
vector<SOFRFixing> fixings = load_sofr_fixings(db, as_of);
vector<SOFRFuture> futures = load_sofr_futures(db, as_of);
vector<OISQuote> swaps = load_ois_quotes(db, Currency::USD, as_of);

// Configure and bootstrap
CurveConfig config;
config.interpolation = CurveConfig::Interpolation::LogLinear;
config.day_count = ql::Actual360();

auto result = bootstrap_sofr_curve(fixings, futures, swaps, settle_date, config);

if (result.success) {
    // Use the curve
    double df_1y = discount_factor(*result.curve, settle_date + 365);
    double fwd_3m = forward_rate(*result.curve, settle_date, settle_date + 90);
}
```

### Curve Accessor Functions

```cpp
double z = zero_rate(curve, date, day_count);       // Zero rate at a date
double f = forward_rate(curve, start, end);         // Forward rate between dates
double df = discount_factor(curve, date);           // Discount factor at a date
```

## Convexity Adjustments

Futures prices differ from forward rates due to daily margining. This "convexity bias" must be corrected. The `SOFRFuture` type includes an optional `convexity_adj` field.

## Multi-Currency Support

The bootstrap module supports G10 currencies via `bootstrap_ois_curve()`:

| Currency | Index | Day Count |
|----------|-------|-----------|
| USD | SOFR | ACT/360 |
| EUR | ESTR | ACT/360 |
| GBP | SONIA | ACT/365 |
| JPY | TONAR | ACT/365 |
| CHF | SARON | ACT/360 |

## Further Reading

- [INPUT_REQUIREMENTS.md](../INPUT_REQUIREMENTS.md) Section 3 for input schema
- NY Fed: [SOFR User's Guide](https://www.newyorkfed.org/medialibrary/Microsites/arrc/files/2021/users-guide-to-sofr2021-update.pdf)
- Implementation: `src/bootstrap/bootstrap.cppm`
