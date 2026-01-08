# Bond Basis Trading

This document explains the concepts behind Treasury bond basis trading as implemented in fin-kit.

## What is Bond Basis?

The "basis" is the relationship between a cash Treasury bond and its corresponding futures contract. Basis trading exploits mispricings between these two markets.

A **basis trade** involves:
- Buying (or selling) a Treasury bond in the cash market
- Taking the opposite position in Treasury futures

The goal is to profit when the cash-futures relationship normalizes, regardless of where rates move.

## The Core Insight: Repo Rates

Bond basis analysis is fundamentally about **financing**. The key question:

> Is the implied financing rate from the cash-futures spread better or worse than actual repo rates?

If the implied repo rate exceeds actual repo rates, there is an arbitrage opportunity.

```
Implied_Repo = [(Invoice_Price / Purchase_Price) - 1] x (360 / days_to_delivery)
```

This is the return you'd earn by buying the bond, financing it via repo, and delivering into futures.

## Gross Basis vs Net Basis

### Gross Basis

The raw price difference, not accounting for carry:

```
Gross_Basis = Cash_Price - (Futures_Price x Conversion_Factor)
```

Quoted in **32nds** (Treasury convention). A gross basis of 0.50 means the cash bond trades 16/32 above the futures-implied price.

### Net Basis (BNOC)

Gross basis minus the cost of carry to delivery:

```
Net_Basis = Gross_Basis - Carry
Carry = Financing_Cost - Coupon_Income
```

Net basis isolates the "pure" pricing difference after accounting for time value.

**The CTD always has the lowest net basis** (or equivalently, the highest implied repo rate).

## Conversion Factors

Treasury futures allow delivery of any bond in a specified maturity range. The **conversion factor** adjusts for coupon differences so that all bonds should theoretically be equivalent.

CME calculates conversion factors assuming a **6% yield** with semiannual compounding:

```
CF = a^(v/6) x [c/2 x (1 + geometric_sum) + a^(2n)] - c/2 x (6-v)/6

where:
  a = 1/1.03 (semiannual discount factor at 6%)
  c = annual coupon rate
  n = whole years to maturity
  v = months in excess of whole years
```

In fin-kit, this is implemented in `finkit::analysis::conversion_factor()`.

## CTD Determination

The **Cheapest to Deliver** is the bond the futures seller will choose to deliver. Finding the CTD involves:

1. Calculate conversion factor for each deliverable bond
2. Calculate gross basis for each bond
3. Calculate carry to first delivery date
4. Net basis = gross basis - carry
5. **Lowest net basis = CTD**

Equivalently, the CTD has the **highest implied repo rate**.

### Why CTD Changes

The CTD can switch based on:
- **Yield levels**: Low yields favor low-coupon bonds; high yields favor high-coupon bonds
- **Curve shape**: Steep curves favor longer maturities
- **Repo specials**: Bonds trading "on special" have lower carry, affecting net basis

## Implied Repo Rate

The implied repo rate is the financing return embedded in the basis:

```
Invoice_Price = Futures_Price x CF + Accrued_at_Delivery
Purchase_Price = Clean_Price + Accrued_at_Settlement

Implied_Repo = [(Invoice / Purchase) - 1] x (360 / days)
```

**Trading signal**:
- `Implied_Repo > Actual_Repo` -> Buy basis (long bond, short futures)
- `Implied_Repo < Actual_Repo` -> Sell basis (short bond, long futures)

## Implementation in fin-kit

The `finkit::analysis` and `finkit::basis` modules provide:

### Key Functions

```cpp
// Calculate conversion factor
auto cf = conversion_factor(coupon, maturity, delivery_date);

// Calculate gross basis
auto gross = gross_basis(bond, futures, cf);

// Calculate implied repo
auto repo = implied_repo_rate(bond, futures, cf, settle, delivery, accrued_at_delivery);

// Analyze entire deliverable basket
auto basket = analyze_basket(futures, bonds, settle_date, repo_rate);
```

### Basket Analysis Output

The `analyze_basket()` function returns a `DeliverableBasket` containing:

| Field | Description |
|-------|-------------|
| `bonds` | Vector of `BasisResult` for each deliverable |
| `ctd_cusip` | CUSIP of the cheapest to deliver |
| `ctd_net_basis_32nds` | CTD net basis in 32nds |
| `ctd_implied_repo` | CTD implied repo rate |
| `avg_net_basis` | Average net basis across basket |

Each `BasisResult` includes:
- `gross_basis_32nds` / `net_basis_32nds`
- `implied_repo`
- `conversion_factor`
- `ctd_rank` (1 = CTD)
- `is_ctd` flag

## Repo Rate Importance

Bond basis calculations require **repo rates**, not yield curves:

| Rate Type | Use |
|-----------|-----|
| Term repo to delivery | Best: locks in financing |
| GC (General Collateral) | Standard Treasury rate |
| SOFR | Overnight proxy |
| Special rate | If bond is "on special" |

See [INPUT_REQUIREMENTS.md](../INPUT_REQUIREMENTS.md) Section 1.4 for the repo data schema.

## Example: Basis Trade P&L

```
Scenario: TY (10-year) futures, bond A is CTD

Bond A: Clean price 98.50, CF = 0.9234
Futures: 106.75
GC repo: 5.25%

Gross basis = 98.50 - (106.75 x 0.9234) = 0.0355
Implied repo = 5.45%

Trade: Buy bond, sell futures
P&L = (Implied_Repo - Actual_Repo) x Notional x Days/360
    = (5.45% - 5.25%) x $1M x 30/360
    = $166.67 per $1M for 30 days
```

## Further Reading

- Burghardt et al., "The Treasury Bond Basis"
- CME Group education: [The Basics of Treasury Basis](https://www.cmegroup.com/education/courses/introduction-to-treasuries/the-basics-of-treasuries-basis.html)
- `src/basis/basis.cppm` and `src/analysis/analysis.cppm` for implementation
