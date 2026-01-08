# finkit.types Module

Financial type definitions used across all analysis modules.

## Overview

The `finkit.types` module defines data structures for currencies, bonds, futures,
FX instruments, interest rates, and analysis results. These types provide a
consistent interface between data ingestion and analytical calculations.

## Currency and Convention Types

### Enums

```cpp
enum class Currency { USD, EUR, GBP, JPY, CHF, AUD, CAD, NZD, NOK, SEK };
enum class OvernightIndex { SOFR, ESTR, SONIA, TONAR, SARON, AONIA, CORRA, NZONIA, NOWA, SWESTR };
enum class AssetClass { Equity, Bond, Future, FX, Option, Swap, ETF, Index, Commodity, Crypto };
```

### CurrencyConvention

Market conventions for each currency:
- `ois_index` - Overnight rate index
- `day_count` - Day count convention (Act/360, Act/365F)
- `spot_lag` - Settlement days (T+1 for CAD, T+2 for others)
- `pip_size` - FX pip size (0.01 for JPY, 0.0001 for others)

```cpp
auto get_convention(Currency ccy) -> CurrencyConvention;
auto g10_pairs() -> vector<pair<Currency, Currency>>;
```

## FX Types

| Struct | Purpose |
|--------|---------|
| `FXSpot` | Spot quote with bid/ask/mid, spot date |
| `FXForward` | Forward points and outright rates by tenor |
| `FXSwap` | Near/far leg dates with forward points |

## Interest Rate Types

| Struct | Purpose |
|--------|---------|
| `InterestRate` | Generic rate with tenor, bid/ask/mid |
| `XCCYBasisSwap` | Cross-currency basis swap spread |
| `SOFRFixing` | Historical SOFR fixing |
| `SOFRFuture` | SR1/SR3 futures with convexity adjustment |
| `OISQuote` | OIS swap quote by tenor |
| `FOMCMeeting` | Fed meeting date and implied move |

## Curve Types

| Struct | Purpose |
|--------|---------|
| `CurveConfig` | Interpolation method, day count, compounding |
| `BootstrapResult` | Bootstrapped curve with pillar dates and rates |

## Bond Types

### Bond

Treasury bond representation:
- Identifiers: `cusip`, `isin`
- Static data: `coupon`, `maturity`, `issue_date`, `first_coupon_date`
- Market data: `clean_price`, `accrued_interest`
- Calculated: `dirty_price`, `yield_to_maturity`, `modified_duration`, `convexity`

### FuturesContract

Treasury futures with product type (TU, FV, TY, TN, US, WN), delivery dates,
notional, and deliverable grade specifications.

## Basis Analysis Types

| Struct | Purpose |
|--------|---------|
| `BasisResult` | Bond basis analysis: gross/net basis, implied repo, CTD rank |
| `DeliverableBasket` | Full deliverable basket with CTD determination |
| `CIPBasisResult` | CIP violation analysis with arbitrage P&L |
| `G10BasisSnapshot` | Cross-sectional CIP basis across G10 pairs |
| `RepoRate` | GC and special repo rates |

## Dependencies

- `QuantLib` - Date and day count types

## Usage

```cpp
import finkit.types;

using namespace finkit::types;

// Get market conventions
auto usd_conv = get_convention(Currency::USD);
// usd_conv.ois_index == OvernightIndex::SOFR

// Create a bond
Bond bond{
    .cusip = "91282CJL6",
    .coupon = 0.04375,
    .maturity = ql::Date(15, ql::November, 2034),
    .clean_price = 99.5,
    .accrued_interest = 0.25
};
double full = bond.full_price();  // 99.75
```

## Related

- [bootstrap.md](bootstrap.md) - Uses curve types
- [basis.md](basis.md) - Uses bond and CIP types
- [conventions.md](../reference/conventions.md) - Currency convention details
