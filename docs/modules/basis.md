# finkit.basis Module

Basis calculations for Treasury bond futures, CIP arbitrage, and index futures.

## Overview

The `finkit.basis` module provides analytical tools for:
- Treasury bond basis: gross/net basis, implied repo, CTD determination
- CIP (Covered Interest Parity) basis: FX forward vs interest rate differentials
- Index futures basis and ETF premium/discount (stubs)

## Treasury Bond Basis

### Conversion Factor

```cpp
auto conversion_factor(double coupon, const ql::Date& maturity,
                       const ql::Date& delivery_date) -> double;
```

CME conversion factor assuming 6% yield, semiannual compounding. Rounds
maturity to nearest quarter per CME specifications.

### Basis Calculations

```cpp
auto gross_basis(const Bond& bond, const FuturesContract& futures, double cf) -> double;
auto to_32nds(double decimal_price) -> double;
auto implied_repo_rate(const Bond& bond, const FuturesContract& futures, double cf,
                       const ql::Date& settle_date, const ql::Date& delivery_date,
                       double accrued_at_delivery = 0.0) -> double;
auto carry_cost(const Bond& bond, double repo_rate, const ql::Date& settle_date,
                const ql::Date& delivery_date, double interim_coupon = 0.0) -> double;
```

### Basket Analysis

```cpp
auto analyze_basket(const FuturesContract& futures, const vector<Bond>& bonds,
                    const ql::Date& settle_date, double repo_rate) -> DeliverableBasket;
```

Analyzes all deliverable bonds for a futures contract:
- Calculates gross basis, net basis (BNOC), implied repo for each bond
- Ranks by net basis to determine CTD (cheapest-to-deliver)
- Provides summary statistics (min/max/avg net basis)

## CIP Basis

### Single Pair

```cpp
auto calculate_cip_basis(const FXSpot& spot, const FXForward& forward,
                         const InterestRate& rate_base,
                         const InterestRate& rate_quote) -> CIPBasisResult;

auto calculate_cip_basis(const FXSwap& swap, const InterestRate& rate_base,
                         const InterestRate& rate_quote) -> CIPBasisResult;
```

Returns `CIPBasisResult` with:
- `theoretical_forward` - CIP-implied forward rate
- `cip_basis_bps` - Deviation from CIP in basis points
- `basis_bid`, `basis_ask` - Transaction-cost adjusted basis
- `is_exploitable`, `arb_direction`, `arb_pnl_per_million`

### G10 Cross-Section

```cpp
auto analyze_g10_basis(const ql::Date& as_of, const string& tenor,
                       const vector<FXSpot>& spots,
                       const vector<FXForward>& forwards,
                       const vector<InterestRate>& rates,
                       const InterestRate& usd_rate) -> G10BasisSnapshot;
```

Analyzes CIP basis across all G10/USD pairs, identifying richest/cheapest
pairs and calculating dispersion.

### Cross-Currency Funding

```cpp
auto implied_funding_rate(const XCCYBasisSwap& xccy,
                          double domestic_ois_rate) -> double;
```

## Stub Functions

The following are not yet implemented:
- `calculate_index_basis` - SPX/ES, NDX/NQ basis
- `calculate_etf_basis` - ETF premium/discount vs NAV
- `value_delivery_options` - Quality, timing, end-of-month options

## Dependencies

- `finkit.types` - Bond, FX, and basis result types
- `QuantLib` - Date calculations

## Usage

```cpp
import finkit.basis;
import finkit.types;

using namespace finkit::basis;
using namespace finkit::types;

// Treasury basis analysis
FuturesContract ty_future{.code = "TYH5", .product = TreasuryFuturesProduct::TY,
                          .price = 110.5, /* ... */};
vector<Bond> deliverables = { /* ... */ };
auto basket = analyze_basket(ty_future, deliverables, settle_date, 0.05);

// CTD is first bond (lowest net basis)
auto& ctd = basket.bonds[0];
double net_basis_32nds = ctd.net_basis_32nds;
double implied_repo = ctd.implied_repo;

// CIP basis
FXSpot eurusd{.base = Currency::EUR, .quote = Currency::USD, .mid = 1.0850, /* ... */};
FXForward fwd{.tenor = "3M", .outright_mid = 1.0875, /* ... */};
InterestRate eur_rate{.currency = Currency::EUR, .rate_mid = 0.035, /* ... */};
InterestRate usd_rate{.currency = Currency::USD, .rate_mid = 0.05, /* ... */};

auto cip = calculate_cip_basis(eurusd, fwd, eur_rate, usd_rate);
// cip.cip_basis_bps shows deviation from parity
```

## Related

- [types.md](types.md) - Bond, FX, and result type definitions
- [bootstrap.md](bootstrap.md) - Curve construction for rate inputs
- [INPUT_REQUIREMENTS.md](../INPUT_REQUIREMENTS.md) - Data requirements
