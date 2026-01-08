# Data Pipeline

This document explains how data flows through fin-kit, from external sources through calculations to output storage.

## Overview

fin-kit separates data into two distinct stores:

1. **InputDataStore** - Read-only market and reference data
2. **OutputDataStore** - Calculation results and run artifacts

This separation enforces a clear boundary: external data feeds write to input, fin-kit calculations write to output.

## Data Flow Diagram

```
External Sources                    fin-kit                         Consumers
================                    =======                         =========

  Market Data ───────┐
  (Bloomberg, etc.)  │
                     │         ┌─────────────────┐
  Fed/Central Banks ─┼────────►│  InputDataStore │ (read-only)
  (SOFR fixings)     │         │    (DuckDB)     │
                     │         └────────┬────────┘
  Reference Data ────┘                  │
  (bonds, futures)                      │
                                        ▼
                          ┌─────────────────────────┐
                          │   Calculation Modules   │
                          │ ─────────────────────── │
                          │  bootstrap (curves)     │
                          │  basis (CTD, CIP)       │
                          │  analysis (bond)        │
                          │  stats (rolling, IC)    │
                          └─────────────┬───────────┘
                                        │
                                        ▼
                          ┌─────────────────────────┐
                          │      Frameworks         │
                          │ ─────────────────────── │
                          │  backtest               │
                          │  risk                   │
                          │  trading                │
                          └─────────────┬───────────┘
                                        │
                         ┌──────────────┴──────────────┐
                         ▼                             ▼
               ┌─────────────────┐           ┌─────────────────┐
               │ OutputDataStore │           │       viz       │
               │    (DuckDB)     │           │  (charts, etc.) │
               └────────┬────────┘           └─────────────────┘
                        │
                        ▼
               Analysis / Reports
```

## InputDataStore

The InputDataStore provides read-only access to market data populated by external feeds. fin-kit never modifies this data.

### Input Tables

| Table | Purpose | Key Fields |
|-------|---------|------------|
| `market_ohlcv` | Price bars | symbol, timestamp, OHLCV |
| `rates_sofr_fixings` | Daily SOFR | fixing_date, rate |
| `rates_sofr_futures` | SR1/SR3 futures | contract_code, price, implied_rate |
| `rates_ois_quotes` | OIS swap rates | currency, tenor, rate |
| `rates_repo` | Repo rates | as_of, gc_rate, term_days |
| `bonds_reference` | Bond static data | cusip, coupon, maturity |
| `bonds_prices` | Bond prices | cusip, as_of, clean_price |
| `futures_treasury` | Treasury futures | contract_code, price, delivery dates |
| `fx_spot` | FX spot rates | base_ccy, quote_ccy, mid/bid/ask |
| `fx_forwards` | FX forwards | tenor, forward_points, outright |
| `reference_fomc_meetings` | FOMC dates | meeting_date, has_press_conference |
| `reference_holidays` | Holiday calendars | calendar, holiday_date |

See [INPUT_REQUIREMENTS.md](../INPUT_REQUIREMENTS.md) for complete schema definitions and field descriptions.

### Data Quality Requirements

Input data should meet these standards:

- **Timing alignment**: All rates for a calculation from the same timestamp
- **Completeness**: Bid/ask spreads for transaction cost analysis
- **Freshness**: Staleness checks per data type (OIS < 5 min, bonds < 15 min)

## OutputDataStore

The OutputDataStore receives all fin-kit calculation results. Each backtest run creates a unique `run_id` that links related outputs.

### Output Tables

| Table | Purpose | Key Fields |
|-------|---------|------------|
| `runs` | Run metadata | run_id, name, status, config |
| `trades` | Executed trades | run_id, timestamp, symbol, side, price |
| `equity` | Portfolio NAV curve | run_id, timestamp, nav, drawdown |
| `orders` | Submitted orders | run_id, order_id, status, fill_price |
| `positions` | Position snapshots | run_id, timestamp, symbol, quantity |
| `risk_events` | Risk breaches | run_id, limit_name, breach_amount |
| `signals` | Strategy signals | run_id, signal_name, strength |
| `calculated_curves` | Bootstrapped curves | as_of, curve_type, zero_rates |
| `calculated_basis` | Basis analysis | instrument_id, net_basis, is_ctd |
| `analytics_summary` | Performance metrics | metric_name, metric_value |

## DuckDB Usage

fin-kit uses [DuckDB](https://duckdb.org/) for both stores:

- **Embedded**: No separate server process
- **OLAP-optimized**: Efficient for analytical queries
- **Memory-mapped**: Large datasets without loading into RAM
- **WAL mode**: Write-ahead logging for durability

### Configuration

Default paths:
```toml
[database]
input_path = "~/.finkit/input.db"
output_path = "~/.finkit/output.db"
wal_mode = true
```

### In-Memory Mode

For testing or ephemeral calculations:
```cpp
auto stores = finkit::data::create_in_memory_stores();
```

## Module Interaction Pattern

Calculation modules follow a consistent pattern:

1. **Load** from InputDataStore (market data)
2. **Calculate** using pure functions (no side effects)
3. **Return** typed results
4. **Write** to OutputDataStore (via frameworks)

```cpp
// Example: bootstrap a curve
auto fixings = load_sofr_fixings(input_store, as_of);
auto futures = load_sofr_futures(input_store, as_of);
auto swaps = load_ois_quotes(input_store, Currency::USD, as_of);

auto result = bootstrap::bootstrap_sofr_curve(fixings, futures, swaps, settle_date);

if (result.success) {
    save_calculated_curve(output_store, run_id, result);
}
```

## Run Lifecycle

Each calculation session follows this pattern:

```
start_run(output_store, "my_analysis")
    │
    ├── Load data from InputDataStore
    ├── Run calculations
    ├── Write results to OutputDataStore
    │
    └── complete_run(output_store, run_id)
```

Run states: `running` -> `completed` or `failed`

## Best Practices

1. **Never write to InputDataStore** from fin-kit code
2. **Always use run_id** to link related outputs
3. **Check data freshness** before calculations
4. **Use transactions** for multi-table writes
5. **Clean up failed runs** with `fail_run()`
