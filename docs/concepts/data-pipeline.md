# Data Pipeline

This document explains how data flows through fin-kit, from external sources through calculations to output storage.

> **Environment note:** this concept doc assumes your build environment is
> already set up. Use [SETUP.md](../SETUP.md) for the LLVM/QuantLib/Conan build
> toolchain and [LOCAL_SERVICES.md](../LOCAL_SERVICES.md) for TimescaleDB,
> Redis, Docker Desktop, and WSL-specific local service setup.

## Overview

fin-kit uses a unified DataStore backed by TimescaleDB, shared with the Python repo:

1. **Market data** - Populated by the Python repo's data pipeline
2. **Calculation results** - Written by fin-kit calculations and frameworks

This shared database eliminates the need for file-based data transfer and ensures both repos operate on the same data.

## Data Flow Diagram

```
External Sources                    fin-kit                         Consumers
================                    =======                         =========

  Market Data ───────┐
  (Bloomberg, etc.)  │
                     │         ┌─────────────────┐
  Fed/Central Banks ─┼────────►│    DataStore    │
  (SOFR fixings)     │         │  (TimescaleDB)  │ ◄── shared with Python repo
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
               │    DataStore    │           │       viz       │
               │  (TimescaleDB)  │           │  (charts, etc.) │
               └────────┬────────┘           └─────────────────┘
                        │
                        ▼
               Analysis / Reports
```

## DataStore

The DataStore provides unified access to TimescaleDB. Market data is populated by the Python repo; fin-kit reads it for calculations and writes results back.

### Input Tables

| Table | Purpose | Key Fields |
|-------|---------|------------|
| `timeseries_ohlcv` | Price bars (owned by the Python repo — not created by fin-kit) | symbol, timestamp, OHLCV |
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

## TimescaleDB Usage

fin-kit uses [TimescaleDB](https://www.timescale.com/) (PostgreSQL extension) via libpqxx:

- **Shared database**: Same instance used by Python repo for data ingestion
- **Time-series optimized**: Hypertables with automatic partitioning
- **Standard SQL**: Full PostgreSQL compatibility via libpqxx
- **Connection pooling**: Efficient connection management for concurrent access

### Configuration

Environment variables (recommended):
```bash
export TSDB_HOST=localhost
export TSDB_PORT=5432
export TSDB_DATABASE=finkit
export TSDB_USER=finkit
export TSDB_PASSWORD=secret
```

Or TOML config:
```toml
[database]
host = "localhost"
port = 5432
database = "finkit"
user = "finkit"
password = "secret"
```

## Module Interaction Pattern

Calculation modules follow a consistent pattern:

1. **Load** from DataStore (market data)
2. **Calculate** using pure functions (no side effects)
3. **Return** typed results
4. **Write** to DataStore (via frameworks)

```cpp
// Illustrative pseudocode — the load_*/save_* helpers shown here are not
// implemented; callers currently issue SQL via DataStore::query()/execute()
// directly (see src/data/data.cppm).
auto fixings = load_sofr_fixings(store, as_of);
auto futures = load_sofr_futures(store, as_of);
auto swaps = load_ois_quotes(store, Currency::USD, as_of);

auto result = bootstrap::bootstrap_sofr_curve(fixings, futures, swaps, settle_date);

if (result.success) {
    save_calculated_curve(store, run_id, result);
}
```

## Run Lifecycle

Each calculation session follows this pattern:

```
start_run(store, "my_analysis")
    |
    +-- Load data from DataStore
    +-- Run calculations
    +-- Write results to DataStore
    |
    +-- complete_run(store, run_id)
```

Run states: `running` -> `completed` or `failed`

## Best Practices

1. **Use the Python repo for data ingestion** - fin-kit reads, Python writes market data
2. **Always use run_id** to link related outputs
3. **Check data freshness** before calculations
4. **Use transactions** for multi-table writes
5. **Clean up failed runs** with `fail_run()`
