# Loading Market Data

This guide explains how to load market data into fin-kit's DuckDB stores.

## Data Store Architecture

fin-kit separates data into two stores:

| Store | Purpose | Access |
|-------|---------|--------|
| **InputDataStore** | Market data, reference data, external feeds | Read-only |
| **OutputDataStore** | Calculation results, trades, equity curves | Read-write |

This separation ensures calculations never modify source data and results are traceable to specific runs.

## Creating Data Stores

```cpp
import finkit.data;

using namespace finkit::data;

// Option 1: From config file (recommended for production)
auto config = load_config();  // Loads from ~/.config/finkit/config.toml
auto stores = create_data_stores(config);

// Option 2: In-memory (for testing)
auto stores = create_in_memory_stores();

// Option 3: Explicit paths
auto input = std::make_unique<InputDataStore>("/path/to/input.db");
auto output = std::make_unique<OutputDataStore>("/path/to/output.db");
```

## Configuration File

Create `~/.config/finkit/config.toml`:

```toml
[database]
input_path = "~/.finkit/input.db"
output_path = "~/.finkit/output.db"
wal_mode = true

[logging]
level = "info"

[backtest]
default_timezone = "America/New_York"
```

## Input Data Schema

InputDataStore creates tables automatically on first connection. For the complete schema with field descriptions, see [INPUT_REQUIREMENTS.md](../INPUT_REQUIREMENTS.md).

Key tables:

- `market_ohlcv` - OHLCV price bars
- `bonds_reference` - Bond static data
- `bonds_prices` - Bond price history
- `rates_sofr_fixings` - SOFR daily fixings
- `rates_ois_quotes` - OIS swap quotes
- `rates_repo` - Repo rates (critical for bond basis)
- `fx_spot` - FX spot rates
- `fx_forwards` - FX forward points

## Inserting Data

### Direct SQL

```cpp
InputDataStore input("~/.finkit/input.db");

// Insert OHLCV data
input.query(R"(
    INSERT INTO market_ohlcv (symbol, timestamp, open, high, low, close, volume)
    VALUES
        ('SPY', '2024-01-02 09:30:00-05', 470.0, 475.0, 469.0, 474.0, 50000000),
        ('SPY', '2024-01-03 09:30:00-05', 474.0, 478.0, 473.0, 477.0, 45000000)
)");

// Insert bond reference data
input.query(R"(
    INSERT INTO bonds_reference (cusip, coupon, maturity, issue_date)
    VALUES
        ('912810TM0', 0.04375, '2053-08-15', '2023-08-15'),
        ('912810TS7', 0.04125, '2053-11-15', '2023-11-15')
)");

// Insert repo rates
input.query(R"(
    INSERT INTO rates_repo (as_of, gc_rate, term_days, collateral_type)
    VALUES
        ('2024-01-02', 0.0530, 1, 'Treasury'),
        ('2024-01-02', 0.0528, 30, 'Treasury')
)");
```

### Prepared Statements

```cpp
// For bulk inserts, use prepared statements
auto result = input.execute(
    R"(INSERT INTO bonds_prices (cusip, as_of, clean_price, yield_to_maturity)
       VALUES ($1, $2, $3, $4))",
    std::string{"912810TM0"},
    std::string{"2024-01-02 16:00:00-05"},
    98.5,
    0.0445
);
```

### Loading from CSV

DuckDB can read CSV files directly:

```cpp
input.query(R"(
    INSERT INTO market_ohlcv
    SELECT * FROM read_csv_auto('/path/to/prices.csv')
)");
```

Or with explicit schema:

```cpp
input.query(R"(
    INSERT INTO bonds_reference
    SELECT
        cusip,
        coupon::DOUBLE,
        maturity::DATE,
        issue_date::DATE
    FROM read_csv('/path/to/bonds.csv',
        columns = {'cusip': 'VARCHAR', 'coupon': 'VARCHAR',
                   'maturity': 'VARCHAR', 'issue_date': 'VARCHAR'})
)");
```

## Querying Data

```cpp
auto result = input.query(R"(
    SELECT cusip, clean_price, yield_to_maturity
    FROM bonds_prices
    WHERE as_of >= '2024-01-01'
    ORDER BY cusip, as_of
)");

if (!result->HasError()) {
    for (auto& row : *result) {
        std::string cusip = row.GetValue<std::string>(0);
        double price = row.GetValue<double>(1);
        // Process row...
    }
}
```

## Output Data Store

The OutputDataStore holds calculation results, organized by `run_id`:

```cpp
OutputDataStore output("~/.finkit/output.db");

// Start a new calculation run
auto run_id = start_run(output, "Bond Basis Analysis", "abc123def");

// ... run calculations ...

// Mark run complete
complete_run(output, run_id);
```

Output tables include:

- `runs` - Run metadata and status
- `trades` - Trade records from backtests
- `equity` - Portfolio equity curve
- `positions` - Position snapshots
- `calculated_basis` - Bond basis results
- `calculated_curves` - Bootstrapped curves

## Example: Loading Data for Bond Basis

```cpp
#include <string>

import finkit.data;

void load_bond_basis_data() {
    auto stores = finkit::data::create_in_memory_stores();
    auto& input = *stores.input;

    // Load bond reference
    input.query(R"(
        INSERT INTO bonds_reference (cusip, coupon, maturity, issue_date)
        VALUES ('912810TM0', 0.04375, '2053-08-15', '2023-08-15')
    )");

    // Load bond prices
    input.query(R"(
        INSERT INTO bonds_prices (cusip, as_of, clean_price)
        VALUES ('912810TM0', '2024-01-02 16:00:00', 98.5)
    )");

    // Load futures
    input.query(R"(
        INSERT INTO futures_treasury
            (contract_code, product, price, first_delivery, last_delivery)
        VALUES ('USH4', 'US', 118.25, '2024-03-01', '2024-03-28')
    )");

    // Load repo rates (essential for basis calculations)
    input.query(R"(
        INSERT INTO rates_repo (as_of, gc_rate, term_days)
        VALUES ('2024-01-02', 0.0530, 1)
    )");
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
