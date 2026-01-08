# finkit.data Module

Configuration loading and database management for market data and calculation results.

## Overview

The `finkit.data` module provides:
- TOML configuration loading with sensible defaults
- DuckDB database connections with thread-safe query execution
- Separate input (read-only market data) and output (calculation results) data stores
- Run management for tracking backtest/calculation executions

## Configuration

### Structs

| Struct | Purpose |
|--------|---------|
| `DatabaseConfig` | Input/output database paths, WAL mode |
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

## Data Stores

### InputDataStore

Read-only access to external market data. Schema includes:
- `market_ohlcv` - OHLCV price data
- `rates_sofr_fixings`, `rates_sofr_futures`, `rates_ois_quotes` - Interest rates
- `rates_repo` - Repo rates
- `bonds_reference`, `bonds_prices` - Bond data
- `futures_treasury` - Treasury futures
- `fx_spot`, `fx_forwards` - FX data
- `reference_fomc_meetings`, `reference_holidays` - Reference data

### OutputDataStore

Write access for calculation results:
- `runs` - Run metadata
- `trades`, `orders`, `positions` - Trade records
- `equity` - Portfolio equity curve
- `signals`, `risk_events` - Strategy outputs
- `calculated_curves`, `calculated_basis` - Computed analytics

### Factory Functions

```cpp
auto create_data_stores(const Config& config) -> DataStores;
auto create_in_memory_stores() -> DataStores;
```

## Run Management

```cpp
auto start_run(OutputDataStore& db, string_view name, ...) -> string;
void complete_run(OutputDataStore& db, string_view run_id);
void fail_run(OutputDataStore& db, string_view run_id, string_view error_msg);
```

## Dependencies

- `finkit.core` - Path expansion
- `duckdb` - Embedded database
- `toml++` - Configuration parsing
- `spdlog` - Logging

## Usage

```cpp
import finkit.data;

auto config = finkit::data::load_config();
auto stores = finkit::data::create_data_stores(config);

// Query input data
auto result = stores.input->query("SELECT * FROM rates_sofr_fixings");

// Start a calculation run
auto run_id = finkit::data::start_run(*stores.output, "backtest_v1");
// ... perform calculations ...
finkit::data::complete_run(*stores.output, run_id);
```

## Related

- [core.md](core.md) - Path expansion utilities
- [INPUT_REQUIREMENTS.md](../INPUT_REQUIREMENTS.md) - Data schema details
