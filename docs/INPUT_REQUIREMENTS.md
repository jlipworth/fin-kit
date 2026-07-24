# Input Requirements for Financial Calculations

This document specifies all data inputs required for each calculation module.
These define what must be available from the database or market data feeds.

## References

- **Bond Basis**: Burghardt et al. "The Treasury Bond Basis"; [CME Conversion Factors](https://www.cmegroup.com/education/courses/introduction-to-treasuries/the-basics-of-treasuries-basis.html)
- **CIP Basis**: [Du, Tepper, Verdelhan (2018) "Deviations from Covered Interest Rate Parity"](https://onlinelibrary.wiley.com/doi/abs/10.1111/jofi.12620); [IMF Working Paper](https://www.imf.org/-/media/Files/Publications/WP/2019/wp1914.ashx)
- **SOFR Curves**: [ARRC User's Guide to SOFR](https://www.newyorkfed.org/medialibrary/Microsites/arrc/files/2021/users-guide-to-sofr2021-update.pdf); [QuantLib Guide](https://www.quantlibguide.com/Curve%20bootstrapping.html)

---

## 1. Bond Basis Analysis (`finkit::analysis`)

### Purpose
Calculate gross/net basis, implied repo, and identify CTD for Treasury futures.

### Key Insight: Repo Rates, Not Yield Curves
Bond basis analysis compares **implied repo** against **actual repo rates** - it does NOT
require a bootstrapped yield curve. The core calculation is a financing arbitrage:

```
Buy bond + Sell futures + Repo finance = Basis trade

Implied_Repo = [(Futures_Invoice / Cash_Full_Price) - 1] × (360 / days_to_delivery)

Trade PnL = Implied_Repo - Actual_Repo_Rate
```

If implied repo > actual repo → buy basis (long bond, short futures)
If implied repo < actual repo → sell basis (short bond, long futures)

### Formulas
```
Gross_Basis = Cash_Price - (Futures_Price × Conversion_Factor)
Net_Basis = Gross_Basis - Carry
Carry = (Coupon_Income - Financing_Cost) to delivery
Implied_Repo = [(Invoice_Price / Purchase_Price) - 1] × (360 / days)

Invoice_Price = Futures_Price × CF + Accrued_at_Delivery
Purchase_Price = Clean_Price + Accrued_at_Settlement + Financing
```

### Required Inputs

#### 1.1 Bond (Cash) Data
| Field | Type | Description | Source |
|-------|------|-------------|--------|
| `cusip` | string | 9-character CUSIP identifier | Reference data |
| `isin` | string | 12-character ISIN (optional) | Reference data |
| `coupon` | double | Annual coupon rate (e.g., 0.04375 for 4.375%) | Reference data |
| `maturity_date` | date | Bond maturity date | Reference data |
| `issue_date` | date | Original issue date | Reference data |
| `first_coupon_date` | date | First coupon payment date | Reference data |
| `clean_price` | double | Quoted price (excludes accrued) | Market data |
| `dirty_price` | double | Full price (includes accrued) | Calculated |
| `accrued_interest` | double | Accrued interest per 100 face | Calculated |
| `yield_to_maturity` | double | YTM at current price | Calculated |
| `modified_duration` | double | Interest rate sensitivity | Calculated |
| `convexity` | double | Second-order rate sensitivity | Calculated |
| `settlement_date` | date | Trade settlement date (T+1 for Treasuries) | Convention |

#### 1.2 Futures Contract Data
| Field | Type | Description | Source |
|-------|------|-------------|--------|
| `contract_code` | string | e.g., "TYH5" (10Y Mar 2025) | Reference data |
| `product_code` | string | TU/FV/TY/TN/US/WN (2/5/10/10U/30/Ultra) | Reference data |
| `futures_price` | double | Quoted futures price | Market data |
| `first_delivery_date` | date | First day of delivery month | Contract spec |
| `last_delivery_date` | date | Last delivery day | Contract spec |
| `last_trade_date` | date | Last trading day | Contract spec |
| `notional` | double | Contract size ($100,000 or $200,000) | Contract spec |
| `tick_size` | double | Minimum price increment | Contract spec |
| `deliverable_grades` | list | Maturity range for deliverables | Contract spec |

#### 1.3 Conversion Factor Inputs
| Field | Type | Description | Source |
|-------|------|-------------|--------|
| `coupon` | double | Bond coupon rate | From bond |
| `maturity_date` | date | Bond maturity | From bond |
| `first_delivery_date` | date | Delivery month start | From futures |
| `assumed_yield` | double | Always 6% for CME | Convention |

**CME CF Formula** (rounded to nearest quarter):
```
CF = (1/1.03^z) × [c/2 × (1 + (1 - 1.03^(-2n))/0.03) + 1.03^(-2n)] - c/2 × (6 - v)/6

where:
  z = months to maturity / 12 (rounded down to quarter)
  n = whole years in z
  v = months in excess of whole years (after rounding)
  c = annual coupon rate
```

#### 1.4 Repo/Financing Data (Critical for Basis)
This is the core data for basis trade valuation - compare implied repo to these rates.

| Field | Type | Description | Source |
|-------|------|-------------|--------|
| `term_repo_rate` | double | Term repo rate to delivery date | Market data / broker |
| `gc_rate` | double | Treasury General Collateral rate | DTCC GCF, NY Fed |
| `sofr_rate` | double | SOFR (proxy for overnight GC) | NY Fed |
| `special_rate` | double | Issue-specific repo if "on special" | Market data |
| `repo_tenor_days` | int | Days for term repo (match to delivery) | Calculated |
| `haircut` | double | Repo margin/haircut (typically 2%) | Convention |
| `is_special` | bool | Is issue trading special? | Market data |

**Repo Rate Hierarchy**:
1. **Term repo to delivery** (ideal) - lock in financing to futures delivery
2. **GC repo rate** - general collateral, roll overnight/term
3. **SOFR** - overnight proxy, need to estimate term spread
4. **Fed funds** - less relevant post-2008

**Special Issues**: When a bond is "on special" (high demand for borrowing),
the special repo rate can be significantly below GC. This affects basis
calculations - a bond on special has higher carry, lower net basis.

#### 1.5 Delivery Option Inputs (Advanced - Requires Yield Curve)
**Note**: These are only needed for advanced delivery option valuation,
NOT for basic implied repo / CTD analysis. Most basis traders focus on
implied repo vs actual repo without modeling delivery options explicitly.

| Field | Type | Description | Source |
|-------|------|-------------|--------|
| `yield_curve` | curve | Full Treasury yield curve | Bootstrapped |
| `yield_volatility` | double | Yield vol for option valuation | Market data |
| `switch_option_value` | double | Quality/switch option value | Model |
| `timing_option_value` | double | Delivery timing option value | Model |
| `eom_option_value` | double | End-of-month option value | Model |

**When you need this**: If you want to decompose net basis into:
- Carry
- Quality option (right to deliver any eligible bond)
- Timing option (right to choose delivery date)
- End-of-month option (wild card)

### Outputs
- Conversion factor per bond
- Gross basis (32nds)
- Net basis (32nds)
- Implied repo rate
- Carry to delivery
- CTD flag (lowest net basis = highest implied repo)
- Basis net of carry (BNOC)

---

## 2. CIP / CCY Basis Analysis (`finkit::curves`)

### Purpose
Calculate covered interest parity deviations across G10 currency pairs.
Identify relative value and potential arbitrage opportunities.

### Formula
```
CIP Condition: F/S = (1 + r_quote × t) / (1 + r_base × t)

Theoretical Forward: F_theo = S × (1 + r_quote × t) / (1 + r_base × t)

CIP Basis (bps): basis = [(F/S - 1)/t - (r_quote - r_base)] × 10000

Cross-Currency Basis: The spread added to one leg of an xccy swap
```

### Required Inputs

#### 2.1 FX Spot Data
| Field | Type | Description | Source |
|-------|------|-------------|--------|
| `base_ccy` | enum | Base currency (e.g., EUR) | - |
| `quote_ccy` | enum | Quote currency (e.g., USD) | - |
| `spot_mid` | double | Mid-market spot rate | Market data |
| `spot_bid` | double | Bid price | Market data |
| `spot_ask` | double | Ask price | Market data |
| `spot_date` | date | Spot settlement date (typically T+2) | Convention |
| `as_of_timestamp` | timestamp | Quote timestamp | Market data |

#### 2.2 FX Forward/Swap Data
| Field | Type | Description | Source |
|-------|------|-------------|--------|
| `base_ccy` | enum | Base currency | - |
| `quote_ccy` | enum | Quote currency | - |
| `tenor` | string | Forward tenor (ON, TN, SN, 1W, 1M, 3M, 6M, 1Y...) | - |
| `forward_points_mid` | double | Forward points (pips) | Market data |
| `forward_points_bid` | double | Bid forward points | Market data |
| `forward_points_ask` | double | Ask forward points | Market data |
| `outright_mid` | double | Outright forward rate | Calculated |
| `value_date` | date | Forward settlement date | Calculated |
| `spot_reference` | double | Spot rate used for points | Market data |

#### 2.3 Interest Rate Data (Both Currencies)
| Field | Type | Description | Source |
|-------|------|-------------|--------|
| `currency` | enum | Currency for rate | - |
| `rate_type` | enum | OIS, LIBOR, Deposit, Repo | - |
| `tenor` | string | Rate tenor (must match FX tenor) | - |
| `rate_mid` | double | Mid-market rate | Market data |
| `rate_bid` | double | Bid rate | Market data |
| `rate_ask` | double | Ask rate | Market data |
| `day_count` | enum | ACT/360, ACT/365, 30/360 | Convention |

**Rate Hierarchy** (per Du, Tepper, Verdelhan):
1. OIS rates (preferred - closest to risk-free)
2. Repo rates (secured funding)
3. LIBOR/Term rates (includes credit premium)
4. Government bill yields

#### 2.4 Cross-Currency Basis Swap Data (Longer Tenors)
| Field | Type | Description | Source |
|-------|------|-------------|--------|
| `base_ccy` | enum | Base currency (typically non-USD) | - |
| `quote_ccy` | enum | Quote currency (typically USD) | - |
| `tenor` | string | Swap tenor (1Y, 2Y, 5Y, 10Y, 30Y) | - |
| `basis_spread` | double | Quoted basis spread (bps) | Market data |
| `spread_on_leg` | enum | Which leg has the spread | Convention |
| `float_index_base` | string | Floating index for base leg | Convention |
| `float_index_quote` | string | Floating index for quote leg | Convention |

#### 2.5 Calendar/Convention Data
| Field | Type | Description | Source |
|-------|------|-------------|--------|
| `spot_lag` | int | Days from trade to spot (usually 2) | Convention |
| `holiday_calendars` | list | Relevant holiday calendars | Reference |
| `day_count_fx` | enum | Day count for FX (ACT/360 typical) | Convention |
| `quote_convention` | enum | Direct vs indirect quote | Convention |
| `pip_size` | double | Size of one pip (0.0001 or 0.01 for JPY) | Convention |

### Currency-Specific Conventions

| Currency | OIS Index | Day Count | Spot Lag | Notes |
|----------|-----------|-----------|----------|-------|
| USD | SOFR | ACT/360 | T+2 | - |
| EUR | ESTR | ACT/360 | T+2 | Was EONIA |
| GBP | SONIA | ACT/365 | T+2 | - |
| JPY | TONAR | ACT/365 | T+2 | Pip = 0.01 |
| CHF | SARON | ACT/360 | T+2 | - |
| AUD | AONIA | ACT/365 | T+2 | - |
| CAD | CORRA | ACT/365 | T+1 | Note: T+1 |
| NZD | OCR | ACT/365 | T+2 | - |
| NOK | NOWA | ACT/360 | T+2 | - |
| SEK | SWESTR | ACT/360 | T+2 | - |

### Outputs
- Theoretical forward rate
- CIP basis (basis points)
- Annualized basis
- Bid/ask basis (transaction cost adjusted)
- Arbitrage P&L per notional (if exploitable)
- Richest/cheapest ranking across pairs

---

## 3. SOFR Curve Bootstrapping (`finkit::curves`)

### Purpose
Build a SOFR discount curve from market instruments for:
- Discounting cash flows
- Projecting forward rates
- Pricing OIS swaps and other derivatives

### Methodology
Iterative bootstrap: Each instrument adds a node, solving for the discount factor that reprices it exactly. Short-to-long maturity order.

### Required Inputs

#### 3.1 SOFR Overnight Fixing
| Field | Type | Description | Source |
|-------|------|-------------|--------|
| `fixing_date` | date | Publication date | NY Fed |
| `rate` | double | Overnight SOFR rate | NY Fed |
| `effective_date` | date | Rate effective date | Calculated |

**Note**: Need historical fixings for pricing swaps in their accrual period.

#### 3.2 SOFR Futures (Short End: 0-2Y)
| Field | Type | Description | Source |
|-------|------|-------------|--------|
| `contract_code` | string | e.g., "SFRZ4" (SR3 Dec 2024) | - |
| `contract_type` | enum | SR1 (1-month) or SR3 (3-month) | - |
| `price` | double | Futures price (100 - rate) | Market data |
| `implied_rate` | double | 100 - price | Calculated |
| `reference_start` | date | Start of reference period | Contract spec |
| `reference_end` | date | End of reference period | Contract spec |
| `last_trade_date` | date | Expiration | Contract spec |
| `convexity_adjustment` | double | Futures vs forward adjustment | Model |

**SR1 (1-Month SOFR Futures)**:
- Monthly contracts
- Settle to arithmetic average of daily SOFR
- Reference period: calendar month

**SR3 (3-Month SOFR Futures)**:
- Quarterly contracts (Mar, Jun, Sep, Dec cycle)
- Settle to compounded average of daily SOFR
- Reference period: IMM dates (3rd Wed to 3rd Wed)

#### 3.3 SOFR OIS Swaps (Medium/Long End: 1Y+)
| Field | Type | Description | Source |
|-------|------|-------------|--------|
| `tenor` | string | Swap tenor (1Y, 2Y, 3Y, 5Y, 7Y, 10Y, 15Y, 20Y, 30Y) | - |
| `fixed_rate` | double | Quoted fixed rate | Market data |
| `effective_date` | date | Swap start date | Convention |
| `maturity_date` | date | Swap end date | Calculated |
| `fixed_leg_freq` | enum | Annual (standard for USD) | Convention |
| `float_leg_freq` | enum | Annual (standard for USD) | Convention |
| `fixed_day_count` | enum | ACT/360 | Convention |
| `float_day_count` | enum | ACT/360 | Convention |
| `payment_lag` | int | Days after period end (typically 2) | Convention |
| `lookback` | int | Lookback days (typically 2) | Convention |
| `lockout` | int | Lockout days (typically 0 or 2) | Convention |

#### 3.4 FOMC Meeting Dates (Turn Adjustments)
| Field | Type | Description | Source |
|-------|------|-------------|--------|
| `meeting_date` | date | FOMC announcement date | Fed calendar |
| `is_press_conference` | bool | Has press conference | Fed calendar |
| `market_implied_move` | double | Implied rate change (from fed funds futures) | Calculated |

**Purpose**: FOMC meetings create discontinuities in the short-end curve. Between meetings, overnight rate is relatively stable.

#### 3.5 Curve Construction Parameters
| Field | Type | Description | Default |
|-------|------|-------------|---------|
| `interpolation` | enum | Linear, LogLinear, Cubic, MonotonicCubic | LogLinear |
| `interpolation_variable` | enum | ZeroRates, DiscountFactors, ForwardRates | DiscountFactors |
| `day_count` | enum | Day count for zero rates | ACT/360 |
| `compounding` | enum | Continuous, Simple, Compounded | Continuous |
| `compounding_freq` | enum | Annual, Semiannual, etc. | Annual |

### Instrument Priority by Tenor

| Tenor Range | Primary Instrument | Secondary | Notes |
|-------------|-------------------|-----------|-------|
| O/N | SOFR fixing | - | Published rate |
| 1W - 1M | SR1 futures | OIS | SR1 more liquid |
| 1M - 3M | SR3 futures | SR1, OIS | - |
| 3M - 2Y | SR3 futures | OIS | Overlap region |
| 2Y - 30Y | OIS swaps | - | Most liquid |

### Outputs
- `YieldTermStructure` handle
- Pillar dates and discount factors
- Zero rates at standard tenors
- Forward rates at standard tenors
- Curve sensitivities (DV01 per instrument)

---

## 4. Database Schema

The authoritative schema is created in code — see `ensure_input_schema()` and
`ensure_output_schema()` in `src/data/data.cppm`. Actual table names:

**Input tables:** `bonds_reference`, `bonds_prices`, `futures_treasury`,
`fx_spot`, `fx_forwards`, `rates_repo`, `rates_sofr_fixings`,
`rates_sofr_futures`, `rates_ois_quotes`, `reference_fomc_meetings`,
`reference_holidays`. Price bars come from the Python repo's
`timeseries_ohlcv` table (external — fin-kit does not create it).

**Output tables:** see `ensure_output_schema()` (calculated results plus the
`calc_runs` run-tracking table).

An earlier draft of this section sketched a different, aspirational schema
(`bonds`, `bond_prices`, `futures_contracts`, `repo_specials`, …); it was
removed because none of those tables exist. Consult `data.cppm` for DDL.

---

## 5. Data Quality Requirements

### Timing Alignment
- All rates for a calculation must be from the same timestamp
- FX spots and forwards must use consistent spot reference
- Historical fixings must be available for swap valuations

### Quote Completeness
- Need bid/ask for transaction cost analysis
- Missing quotes should be flagged, not silently interpolated

### Staleness Checks
- Define maximum age for each data type
- OIS swaps: < 5 minutes during trading hours
- Bond prices: < 15 minutes
- FX spots: < 1 minute

### Cross-Validation
- Implied repo should be within bounds of GC repo
- CIP basis should be consistent across tenors (no arbitrage)
- Futures prices should be consistent with cash (basis within range)
