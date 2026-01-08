# Bond Basis Analyzer

A command-line application for analyzing Treasury bond basis against futures contracts.

## Overview

The bond basis analyzer calculates:

- **Gross Basis**: Cash price minus (futures price x conversion factor)
- **Net Basis**: Gross basis minus carry
- **Implied Repo**: The financing rate implied by basis trade arbitrage
- **CTD Identification**: Identifies the cheapest-to-deliver bond

For detailed formulas and input requirements, see [INPUT_REQUIREMENTS.md](../../docs/INPUT_REQUIREMENTS.md#1-bond-basis-analysis-finkitanalysis).

## Building

```bash
# From repository root
cmake --build build/build/Release

# The executable is at:
# build/build/Release/apps/bond_basis/bond_basis
```

## Usage

```bash
./build/build/Release/apps/bond_basis/bond_basis
```

## Dependencies

The application uses:

- `finkit.analysis` - Bond basis calculation functions
- `finkit.data` - DuckDB data access for market data

## Data Requirements

Before running, populate the InputDataStore with:

| Table | Required Fields |
|-------|-----------------|
| `bonds_reference` | cusip, coupon, maturity |
| `bonds_prices` | cusip, as_of, clean_price |
| `futures_treasury` | contract_code, product, price, first_delivery |
| `rates_repo` | as_of, gc_rate |

See [Loading Market Data](../../docs/guides/loading-market-data.md) for details.

## Example Output

```
fin-kit bond basis analyzer

Bond: 912810TM0 (4.375% Aug-53)
  Clean Price: 98.500
  Futures: USH4 @ 118.250
  Conversion Factor: 0.8234
  Gross Basis: 1.125 (36/32nds)
  Net Basis: 0.875 (28/32nds)
  Implied Repo: 5.42%
  CTD: Yes

Bond: 912810TS7 (4.125% Nov-53)
  Clean Price: 96.750
  Futures: USH4 @ 118.250
  Conversion Factor: 0.8089
  Gross Basis: 1.375 (44/32nds)
  Net Basis: 1.125 (36/32nds)
  Implied Repo: 5.18%
  CTD: No
```

## Key Insight

Bond basis analysis compares **implied repo** against **actual repo rates**, not yield curves. See the [architecture docs](../../docs/architecture.md#basis) for more on the calculation approach.

## Related Documentation

- [INPUT_REQUIREMENTS.md](../../docs/INPUT_REQUIREMENTS.md) - Data schema and formulas
- [Architecture](../../docs/architecture.md) - Module design
- [Getting Started](../../docs/getting-started.md) - Quick start guide
