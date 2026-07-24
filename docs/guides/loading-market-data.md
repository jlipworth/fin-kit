# Loading Market Data

This guide explains how to query and work with market data from fin-kit's TimescaleDB store.

> **Prerequisite:** this is a TimescaleDB-backed workflow, not a standalone
> build-only example. Complete [SETUP.md](../SETUP.md) first, then use
> [LOCAL_SERVICES.md](../LOCAL_SERVICES.md) for TimescaleDB credential and local
> service setup.

## Data Store Architecture

fin-kit uses a unified DataStore backed by TimescaleDB, shared with the Python repo:

| Concern | Description |
|---------|-------------|
| **Market data** | Populated by the Python repo's data pipeline |
| **Calculation results** | Written by fin-kit calculations and backtests |

The Python repo handles data ingestion (Bloomberg, central banks, etc.) and writes to TimescaleDB. fin-kit reads this data for calculations and writes results back to the same database.

## Connecting to TimescaleDB

```cpp
import finkit.data;

using namespace finkit::data;

// Option 1: From config file (recommended for production)
auto config = load_config();  // Loads from ~/.config/finkit/config.toml
auto store = create_data_store(config);

// Option 2: From environment variables
auto store = create_data_store_from_env();
```

## Configuration

### Environment Variables (recommended)

```bash
# Via Infisical (recommended)
infisical run --env=dev --path="/kubernetes/infrastructure/timescaledb" -- \
  ./build/build/Release/apps/bond_basis/bond_basis

# Or set manually
export TSDB_HOST=localhost
export TSDB_PORT=5432
export TSDB_DATABASE=finkit
export TSDB_USER=finkit
export TSDB_PASSWORD=secret
```

### TOML Config

Create `~/.config/finkit/config.toml`:

```toml
[database]
host = "localhost"
port = 5432
database = "finkit"
user = "finkit"
password = "secret"

[logging]
level = "info"

[backtest]
default_timezone = "America/New_York"
```

## Input Data Schema

Market data tables are populated by the Python repo's data pipeline. For the complete schema with field descriptions, see [INPUT_REQUIREMENTS.md](../INPUT_REQUIREMENTS.md).

Key tables:

- `timeseries_ohlcv` - OHLCV price bars (external — owned by the Python repo)
- `bonds_reference` - Bond static data
- `bonds_prices` - Bond price history
- `rates_sofr_fixings` - SOFR daily fixings
- `rates_ois_quotes` - OIS swap quotes
- `rates_repo` - Repo rates (critical for bond basis)
- `fx_spot` - FX spot rates
- `fx_forwards` - FX forward points

## Querying Data

```cpp
auto result = store->execute(R"(
    SELECT cusip, clean_price, yield_to_maturity
    FROM bonds_prices
    WHERE as_of >= $1
    ORDER BY cusip, as_of
)", as_of);

for (const auto& row : result) {
    auto cusip = row["cusip"].as<std::string>();
    auto price = row["clean_price"].as<double>();
    // Process row...
}
```

## Writing Results

The DataStore also writes calculation results, organized by `run_id`:

```cpp
// Start a new calculation run
auto run_id = start_run(*store, "Bond Basis Analysis", "abc123def");

// ... run calculations, write results ...

// Mark run complete
complete_run(*store, run_id);
```

Output tables include:

- `runs` - Run metadata and status
- `trades` - Trade records from backtests
- `equity` - Portfolio equity curve
- `positions` - Position snapshots
- `calculated_basis` - Bond basis results
- `calculated_curves` - Bootstrapped curves

## Example: Querying Data for Bond Basis

```cpp
#include <string>

import finkit.data;

void query_bond_basis_data() {
    auto store = finkit::data::create_data_store_from_env();

    // Query bond reference data
    auto bonds = store->execute(R"(
        SELECT cusip, coupon, maturity, issue_date
        FROM bonds_reference
        WHERE maturity > $1
    )", "2050-01-01");

    // Query bond prices
    auto prices = store->execute(R"(
        SELECT cusip, as_of, clean_price
        FROM bonds_prices
        WHERE as_of = $1
    )", "2024-01-02");

    // Query futures
    auto futures = store->query(R"(
        SELECT contract_code, product, price, first_delivery, last_delivery
        FROM futures_treasury
        WHERE product = 'US'
    )");

    // Query repo rates (essential for basis calculations)
    auto repo = store->execute(R"(
        SELECT as_of, gc_rate, term_days
        FROM rates_repo
        WHERE as_of = $1
    )", "2024-01-02");
}
```

## Data Quality

See [INPUT_REQUIREMENTS.md](../INPUT_REQUIREMENTS.md#5-data-quality-requirements) for:

- Timing alignment requirements
- Staleness thresholds by data type
- Cross-validation checks

## Next Steps

- [Running a Backtest](running-a-backtest.md) - Use loaded data in backtests
- [INPUT_REQUIREMENTS.md](../INPUT_REQUIREMENTS.md) - Complete schema reference
