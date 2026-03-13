# finkit.data Module

Configuration loading and database management for market data and calculation results.

## Overview

The `finkit.data` module provides:
- TOML configuration loading with sensible defaults
- TimescaleDB connections via libpqxx with thread-safe query execution
- Unified DataStore for reading market data and writing calculation results
- Run management for tracking backtest/calculation executions
- Shared schema with the Python repo for seamless data exchange

## Configuration

### Structs

| Struct | Purpose |
|--------|---------|
| `DatabaseConfig` | TimescaleDB connection parameters (host, port, database, user, password) |
| `LoggingConfig` | Log level setting |
| `BacktestConfig` | Default timezone |
| `Config` | Combined configuration |

### Functions

```cpp
auto default_config_paths() -> vector<fs::path>;
auto load_config(const fs::path& path) -> optional<Config>;
auto load_config() -> Config;  // Auto-discovers config file
```

Config files searched in order:
1. `~/.config/finkit/config.toml`
2. `~/.finkit/config.toml`
3. `./finkit.toml`

### Environment Variables

Connection parameters can also be set via environment variables (checked before TOML):

| Variable | Fallback | Purpose |
|----------|----------|---------|
| `TSDB_HOST` / `POSTGRES_HOST` | `localhost` | Database host |
| `TSDB_PORT` / `POSTGRES_PORT` | `5432` | Database port |
| `TSDB_DATABASE` / `POSTGRES_DB` | `finkit` | Database name |
| `TSDB_USER` / `POSTGRES_USER` | `finkit` | Username |
| `TSDB_PASSWORD` / `POSTGRES_PASSWORD` | (none) | Password |

## Data Store

### DataStore

Unified read/write access to TimescaleDB. The schema is shared with the Python repo, so market data written by the Python pipeline is directly available.

**Read tables** (populated by Python repo):
- `market_ohlcv` - OHLCV price data
- `rates_sofr_fixings`, `rates_sofr_futures`, `rates_ois_quotes` - Interest rates
- `rates_repo` - Repo rates
- `bonds_reference`, `bonds_prices` - Bond data
- `futures_treasury` - Treasury futures
- `fx_spot`, `fx_forwards` - FX data
- `reference_fomc_meetings`, `reference_holidays` - Reference data

**Write tables** (populated by fin-kit calculations):
- `runs` - Run metadata
- `trades`, `orders`, `positions` - Trade records
- `equity` - Portfolio equity curve
- `signals`, `risk_events` - Strategy outputs
- `calculated_curves`, `calculated_basis` - Computed analytics

### Factory Functions

```cpp
auto create_data_store(const Config& config) -> DataStore;
auto create_data_store_from_env() -> DataStore;
```

## Run Management

```cpp
auto start_run(DataStore& db, string_view name, ...) -> string;
void complete_run(DataStore& db, string_view run_id);
void fail_run(DataStore& db, string_view run_id, string_view error_msg);
```

## Dependencies

- `finkit.core` - Path expansion
- `libpqxx` - PostgreSQL/TimescaleDB client
- `toml++` - Configuration parsing
- `spdlog` - Logging

## Usage

```cpp
import finkit.data;

// Option 1: From config file
auto config = finkit::data::load_config();
auto store = finkit::data::create_data_store(config);

// Option 2: From environment variables
auto store = finkit::data::create_data_store_from_env();

// Query market data (populated by Python repo)
auto result = store.query("SELECT * FROM rates_sofr_fixings WHERE fixing_date >= $1", as_of);

// Start a calculation run
auto run_id = finkit::data::start_run(store, "backtest_v1");
// ... perform calculations ...
finkit::data::complete_run(store, run_id);
```

## Related

- [core.md](core.md) - Path expansion utilities
- [INPUT_REQUIREMENTS.md](../INPUT_REQUIREMENTS.md) - Data schema details
