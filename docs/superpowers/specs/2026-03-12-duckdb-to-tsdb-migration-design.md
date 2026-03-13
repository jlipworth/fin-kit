# DuckDB to TimescaleDB Migration

## Summary

Replace DuckDB with libpqxx (PostgreSQL C++ client) in the `finkit.data` module so fin-kit reads/writes the same TimescaleDB instance used by the Python repo (Refinitiv Projects).

## Problem

`src/data/data.cppm` uses DuckDB for all data storage via file-based databases (`~/.finkit/input.db`, `~/.finkit/output.db`). The Python repo already loads market data (including FF_CONTINUOUS) into TimescaleDB. Having two separate databases means data duplication and no shared state.

## Design

### Connection model

Replace `duckdb::DuckDB` + `duckdb::Connection` with `pqxx::connection`. Configuration changes from file paths to a PostgreSQL connection string, loaded from environment variables (same `POSTGRES_*` vars the Python repo uses via Infisical).

```cpp
struct DatabaseConfig {
    string host;
    int port{5432};
    string database;
    string user;
    string password;
    string connection_string; // optional override
};
```

`DatabaseConfig` loads from env vars first (`POSTGRES_HOST`, `POSTGRES_PORT`, `POSTGRES_DB`, `POSTGRES_USER`, `POSTGRES_PASSWORD`), falling back to `finkit.toml` if present.

### Merge InputDataStore and OutputDataStore

Both now point at the same database. Replace with a single `DataStore` class:

```cpp
class DataStore {
public:
    explicit DataStore(const DatabaseConfig& config);

    auto query(string_view sql) -> pqxx::result;

    template <typename... Args>
    auto execute(string_view sql, Args&&... args) -> pqxx::result;

    auto connection() -> pqxx::connection&;

private:
    pqxx::connection conn_;
    std::mutex mutex_;
};
```

The `init_input_schema()` and `init_output_schema()` methods become a single `ensure_schema()` that runs `CREATE TABLE IF NOT EXISTS` — safe to call on an already-populated database.

### Factory function

```cpp
auto create_data_store(const Config& config) -> unique_ptr<DataStore>;
auto create_data_store_from_env() -> unique_ptr<DataStore>;
```

`create_data_store_from_env()` reads `POSTGRES_*` env vars directly — the common case when running under `infisical run`.

### SQL compatibility

Almost all SQL is standard and works unchanged. Three minor adjustments:
- `DOUBLE` → `DOUBLE PRECISION`
- `JSON` → `JSONB`
- `gen_random_uuid()` — works in PostgreSQL 13+ natively (no extension needed)

### Schema ownership

Input tables (`market_ohlcv`, `rates_*`, `bonds_*`, `fx_*`, `reference_*`) are owned by the Python repo and already exist in TSDB. `ensure_schema()` uses `CREATE TABLE IF NOT EXISTS` so it won't conflict.

Output tables (`runs`, `trades`, `equity`, `orders`, `positions`, `risk_events`, `signals`, `calculated_*`, `analytics_summary`) are created by fin-kit if they don't exist.

### Run management

`start_run`, `complete_run`, `fail_run` — same logic, just use `pqxx::work` transactions instead of DuckDB's connection.

### Thread safety

Replace single-mutex `duckdb::Connection` with `pqxx::connection` (which is not thread-safe). For now, keep the mutex approach — same pattern, different library. Connection pooling can come later if needed.

### Remove in-memory mode

No more `duckdb::DuckDB(nullptr)` for in-memory databases. Tests hit a real TSDB (localhost or remote via Infisical). For pure unit tests that don't need a database, use JSON fixtures in memory.

## Schema changes

### Input tables — name mapping

The Python repo uses `timeseries_ohlcv` while fin-kit currently defines `market_ohlcv`. Decision: **use the Python repo's existing table names where they overlap**. For tables that only exist in fin-kit (rates, bonds, etc.), keep the current names and create them via `ensure_schema()`.

| fin-kit table | Python repo equivalent | Action |
|---|---|---|
| `market_ohlcv` | `timeseries_ohlcv` | Use `timeseries_ohlcv` (Python repo is source of truth) |
| `reference_fomc_meetings` | (exists in TSDB) | Keep as-is |
| All other input tables | (no equivalent) | Create via `ensure_schema()` |

### Type adjustments

All `DOUBLE` → `DOUBLE PRECISION`, all `JSON` → `JSONB`. Everything else is compatible.

## Files to modify

| File | Changes |
|------|---------|
| `conanfile.py` | Remove `duckdb/1.4.3`, add `libpqxx` |
| `CMakeLists.txt` | Replace `find_package(duckdb)` with `find_package(libpqxx)` |
| `src/data/CMakeLists.txt` | Replace `duckdb::duckdb` with `libpqxx::pqxx` |
| `src/data/data.cppm` | Full rewrite: Database/DataStore class, config, schema |
| `src/analysis/analysis.cppm` | Update import if DataStore API changes |
| `src/curves/curves.cppm` | Update import if DataStore API changes |
| `apps/bond_basis/main.cpp` | Update DataStore usage |
| `tests/data/data_test.cpp` | Rewrite for PostgreSQL |
| `CLAUDE.md` | Remove DuckDB references |
| `docs/architecture.md` | Update data layer description |
| `docs/SETUP.md` | Update dependencies and config |
| `docs/DEPENDENCIES.md` | Swap DuckDB for libpqxx |
| ~6 other doc files | Update DuckDB references |

## Verification

- `DataStore` connects to TSDB and runs a simple query
- `ensure_schema()` creates output tables without affecting existing input tables
- `start_run` / `complete_run` / `fail_run` work with pqxx transactions
- Can read from `timeseries_ohlcv` (FF_CONTINUOUS data already there)
- All existing tests pass (adapted for PostgreSQL)
- Build: `cmake --build build/build/Release`
