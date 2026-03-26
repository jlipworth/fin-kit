# DuckDB to TimescaleDB Migration — Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> **Historical plan note:** This document captures the migration plan and may
> contain point-in-time command examples. Use [../../SETUP.md](../../SETUP.md)
> for the current build/toolchain flow and
> [../../LOCAL_SERVICES.md](../../LOCAL_SERVICES.md) for TimescaleDB, Redis,
> Docker Desktop, and WSL-backed local service setup.

**Goal:** Replace DuckDB with libpqxx (PostgreSQL C++ client) so fin-kit reads/writes the same TimescaleDB instance used by the Python repo.

**Architecture:** Merge `InputDataStore`/`OutputDataStore` into a single `DataStore` class backed by `pqxx::connection`. Configuration shifts from file paths to PostgreSQL env vars (`TSDB_*` primary, `POSTGRES_*` fallback — matching the Python repo). Output tables are created by `ensure_schema()` with `CREATE TABLE IF NOT EXISTS`; input tables that exist in the Python repo (e.g., `timeseries_ohlcv`) are queried directly.

**Tech Stack:** C++20 modules, libpqxx 7.x, PostgreSQL/TimescaleDB, GoogleTest, CMake, Conan

**Spec:** `docs/superpowers/specs/2026-03-12-duckdb-to-tsdb-migration-design.md`

**Deviation from spec:** The spec says `POSTGRES_*` env vars. This plan uses `TSDB_*` primary with `POSTGRES_*` fallback, matching the Python repo's actual behavior (`lseg_toolkit/timeseries/config.py`) and Infisical secret path (`/kubernetes/infrastructure/timescaledb`).

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `conanfile.py` | Modify | Remove `duckdb/1.4.3`, add `libpqxx/7.9.2` |
| `CMakeLists.txt` | Modify | `find_package(duckdb)` → `find_package(libpqxx)` |
| `src/data/CMakeLists.txt` | Modify | `duckdb::duckdb` → `libpqxx::pqxx` |
| `src/data/data.cppm` | Rewrite | Full module rewrite: DataStore, config, schema |
| `tests/data/data_test.cpp` | Rewrite | Unit + integration tests for PostgreSQL |
| `CLAUDE.md` | Modify | Update data storage reference |
| `docs/architecture.md` | Modify | Update data layer description |
| `docs/SETUP.md` | Modify | Update dependencies, config, and Conan instructions |
| `docs/DEPENDENCIES.md` | Modify | Swap DuckDB for libpqxx |
| `docs/STATUS.md` | Modify | Update module description |
| `docs/modules/data.md` | Modify | Update API documentation |
| `docs/getting-started.md` | Modify | Update data store references |
| `docs/guides/loading-market-data.md` | Modify | Rewrite for PostgreSQL API |
| `docs/concepts/data-pipeline.md` | Modify | Update architecture diagrams |
| `docs/diagrams/module-overview.md` | Modify | Update dependency graph |
| `docs/roadmap.md` | Modify | Update DuckDB references |
| `apps/bond_basis/README.md` | Modify | Update dependency mention |

---

## Chunk 1: Build System + Configuration

### Task 1: Build system dependency swap

**Files:**
- Modify: `conanfile.py:15`
- Modify: `CMakeLists.txt:50`
- Modify: `src/data/CMakeLists.txt:5-6`

- [ ] **Step 1: Update `conanfile.py` — swap duckdb for libpqxx**

Replace line 15:
```python
        self.requires("duckdb/1.4.3")
```
With:
```python
        self.requires("libpqxx/7.9.2")
```

- [ ] **Step 2: Update root `CMakeLists.txt` — swap find_package**

Replace line 50:
```cmake
find_package(duckdb REQUIRED)
```
With:
```cmake
find_package(libpqxx REQUIRED)
```

- [ ] **Step 3: Update `src/data/CMakeLists.txt` — swap link target**

Replace lines 5-6:
```cmake
target_link_libraries(finkit_data PUBLIC finkit::core duckdb::duckdb
                                         tomlplusplus::tomlplusplus)
```
With:
```cmake
target_link_libraries(finkit_data PUBLIC finkit::core libpqxx::pqxx
                                         tomlplusplus::tomlplusplus)
```

- [ ] **Step 4: Install dependencies with Conan**

Run:
```bash
# macOS
CC=/opt/homebrew/opt/llvm/bin/clang \
CXX=/opt/homebrew/opt/llvm/bin/clang++ \
LDFLAGS="-L/opt/homebrew/opt/llvm/lib/c++ -Wl,-rpath,/opt/homebrew/opt/llvm/lib/c++" \
uv run conan install . --build=missing -of=build \
  -s compiler=clang -s compiler.version=21 -s compiler.cppstd=20 -s compiler.libcxx=libc++
```

Expected: libpqxx and libpq build successfully. If libpq build fails, install via Homebrew (`brew install libpq`) and re-run.

- [ ] **Step 5: Verify the CMake target name**

Run:
```bash
grep -r "libpqxx" build/ --include="*.cmake" | head -5
```

Expected: Shows target name (likely `libpqxx::pqxx`). If it's `libpqxx::libpqxx`, update `src/data/CMakeLists.txt` accordingly.

**Note:** Do NOT commit yet — the build is intentionally broken (data.cppm still includes `<duckdb.hpp>`). Task 2 completes the module rewrite, then both tasks commit together.

---

### Task 2: Rewrite data.cppm

**Files:**
- Rewrite: `src/data/data.cppm`

This is the core of the migration. The full module is rewritten from DuckDB to libpqxx.

- [ ] **Step 1: Write the complete `src/data/data.cppm`**

```cpp
module;

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
#include <pqxx/pqxx>
#include <spdlog/spdlog.h>
#include <string>
#include <string_view>
#include <toml++/toml.hpp>
#include <vector>

export module finkit.data;

import finkit.core;

export namespace finkit::data {

using std::optional;
using std::string;
using std::string_view;
using std::unique_ptr;
using std::vector;
namespace fs = std::filesystem;

// ============================================================================
// Configuration
// ============================================================================

struct DatabaseConfig {
    string host;
    int port{5432};
    string database;
    string user;
    string password;
    string connection_string; // optional override — takes precedence if set
};

struct LoggingConfig {
    string level{"info"};
};

struct BacktestConfig {
    string default_timezone{"America/New_York"};
};

struct Config {
    DatabaseConfig database;
    LoggingConfig logging;
    BacktestConfig backtest;
};

// Helper: read env var with primary/fallback prefix
auto env_or(const char* primary, const char* fallback) -> optional<string> {
    if (auto v = std::getenv(primary)) return string{v};
    if (auto v = std::getenv(fallback)) return string{v};
    return std::nullopt;
}

auto load_database_config_from_env() -> optional<DatabaseConfig> {
    auto host = env_or("TSDB_HOST", "POSTGRES_HOST");
    if (!host) return std::nullopt;

    DatabaseConfig cfg;
    cfg.host = *host;
    if (auto p = env_or("TSDB_PORT", "POSTGRES_PORT")) {
        try { cfg.port = std::stoi(*p); }
        catch (...) { spdlog::warn("Invalid port '{}', using default 5432", *p); }
    }
    cfg.database = env_or("TSDB_DATABASE", "POSTGRES_DB").value_or("finkit");
    cfg.user = env_or("TSDB_USER", "POSTGRES_USER").value_or("");
    cfg.password = env_or("TSDB_PASSWORD", "POSTGRES_PASSWORD").value_or("");
    return cfg;
}

// Quote a value for a libpq connection string (single-quote + escape)
auto pq_quote(const string& val) -> string {
    if (val.find_first_of(" '\\") == string::npos) return val;
    string out = "'";
    for (char c : val) {
        if (c == '\'' || c == '\\') out += '\\';
        out += c;
    }
    out += '\'';
    return out;
}

auto build_connection_string(const DatabaseConfig& cfg) -> string {
    if (!cfg.connection_string.empty()) return cfg.connection_string;
    return "host=" + pq_quote(cfg.host) +
           " port=" + std::to_string(cfg.port) +
           " dbname=" + pq_quote(cfg.database) +
           " user=" + pq_quote(cfg.user) +
           " password=" + pq_quote(cfg.password);
}

auto default_config_paths() -> vector<fs::path> {
    return {
        finkit::core::expand_path("~/.config/finkit/config.toml"),
        finkit::core::expand_path("~/.finkit/config.toml"),
        fs::path{"finkit.toml"},
    };
}

auto load_config(const fs::path& path) -> optional<Config> {
    if (!fs::exists(path)) return std::nullopt;

    try {
        auto tbl = toml::parse_file(path.string());
        Config cfg;

        if (auto db = tbl["database"].as_table()) {
            if (auto p = db->get("host"))
                cfg.database.host = p->value_or(string{});
            if (auto p = db->get("port"))
                cfg.database.port = p->value_or(5432);
            if (auto p = db->get("database"))
                cfg.database.database = p->value_or(string{"finkit"});
            if (auto p = db->get("user"))
                cfg.database.user = p->value_or(string{});
            if (auto p = db->get("password"))
                cfg.database.password = p->value_or(string{});
            if (auto p = db->get("connection_string"))
                cfg.database.connection_string = p->value_or(string{});
        }

        if (auto log = tbl["logging"].as_table()) {
            if (auto l = log->get("level"))
                cfg.logging.level = l->value_or(string{"info"});
        }

        if (auto bt = tbl["backtest"].as_table()) {
            if (auto tz = bt->get("default_timezone"))
                cfg.backtest.default_timezone = tz->value_or(string{"America/New_York"});
        }

        return cfg;
    } catch (const toml::parse_error& err) {
        spdlog::error("Failed to parse config {}: {}", path.string(), err.what());
        return std::nullopt;
    }
}

auto load_config() -> Config {
    Config cfg;
    auto env_db = load_database_config_from_env();

    if (env_db) {
        cfg.database = *env_db;
        spdlog::info("Loaded database config from environment variables");
    }

    // Load remaining config (logging, backtest) from TOML
    for (const auto& path : default_config_paths()) {
        if (auto file_cfg = load_config(path)) {
            spdlog::info("Loaded config from {}", path.string());
            if (!env_db) {
                cfg.database = file_cfg->database;
            }
            cfg.logging = file_cfg->logging;
            cfg.backtest = file_cfg->backtest;
            return cfg;
        }
    }

    spdlog::info("No config file found, using defaults");
    return cfg;
}

// ============================================================================
// DataStore
// ============================================================================

class DataStore {
public:
    explicit DataStore(const DatabaseConfig& config)
        : conn_{build_connection_string(config)} {
        spdlog::info("Connected to PostgreSQL: {}:{}/{}",
                     config.host, config.port, config.database);
    }

    // Simple query — no parameters, no explicit transaction.
    // Use for DDL, simple SELECTs, and schema setup.
    auto query(string_view sql) -> pqxx::result {
        std::lock_guard lock{mutex_};
        pqxx::nontransaction ntx{conn_};
        return ntx.exec(string{sql});
    }

    // Parameterized query with transaction. Use for all writes
    // and parameterized reads. Commits on success, aborts on exception.
    template <typename... Args>
    auto execute(string_view sql, Args&&... args) -> pqxx::result {
        std::lock_guard lock{mutex_};
        pqxx::work txn{conn_};
        auto result = txn.exec_params(string{sql}, std::forward<Args>(args)...);
        txn.commit();
        return result;
    }

    auto connection() -> pqxx::connection& { return conn_; }

    // Creates all tables via CREATE TABLE IF NOT EXISTS.
    // Each DDL runs in its own implicit transaction. If the connection drops
    // mid-setup, re-calling ensure_schema() is safe (all statements are idempotent).
    void ensure_schema() {
        ensure_input_schema();
        ensure_output_schema();
        spdlog::debug("Schema initialized");
    }

private:
    pqxx::connection conn_;
    std::mutex mutex_; // pqxx::connection is not thread-safe; serialize all access

    void ensure_input_schema() {
        // NOTE: market_ohlcv is NOT created here — use Python repo's
        // timeseries_ohlcv table directly (it is source of truth).

        query(R"(
            CREATE TABLE IF NOT EXISTS rates_sofr_fixings (
                fixing_date DATE PRIMARY KEY,
                rate DOUBLE PRECISION NOT NULL,
                source VARCHAR
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS rates_sofr_futures (
                contract_code VARCHAR PRIMARY KEY,
                futures_type VARCHAR NOT NULL,
                price DOUBLE PRECISION,
                implied_rate DOUBLE PRECISION,
                reference_start DATE,
                reference_end DATE,
                last_trade_date DATE,
                convexity_adj DOUBLE PRECISION,
                as_of TIMESTAMPTZ
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS rates_ois_quotes (
                currency VARCHAR NOT NULL,
                tenor VARCHAR NOT NULL,
                rate DOUBLE PRECISION NOT NULL,
                effective_date DATE,
                maturity_date DATE,
                as_of TIMESTAMPTZ NOT NULL,
                PRIMARY KEY (currency, tenor, as_of)
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS rates_repo (
                as_of DATE NOT NULL,
                gc_rate DOUBLE PRECISION NOT NULL,
                term_days INTEGER DEFAULT 1,
                collateral_type VARCHAR DEFAULT 'Treasury',
                source VARCHAR,
                PRIMARY KEY (as_of, term_days, collateral_type)
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS bonds_reference (
                cusip VARCHAR(9) PRIMARY KEY,
                isin VARCHAR(12),
                coupon DOUBLE PRECISION NOT NULL,
                maturity DATE NOT NULL,
                issue_date DATE,
                first_coupon_date DATE,
                security_type VARCHAR DEFAULT 'Note'
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS bonds_prices (
                cusip VARCHAR(9) NOT NULL,
                as_of TIMESTAMPTZ NOT NULL,
                clean_price DOUBLE PRECISION NOT NULL,
                accrued_interest DOUBLE PRECISION,
                yield_to_maturity DOUBLE PRECISION,
                modified_duration DOUBLE PRECISION,
                source VARCHAR,
                PRIMARY KEY (cusip, as_of)
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS futures_treasury (
                contract_code VARCHAR PRIMARY KEY,
                product VARCHAR NOT NULL,
                price DOUBLE PRECISION,
                first_delivery DATE,
                last_delivery DATE,
                last_trade DATE,
                notional DOUBLE PRECISION,
                as_of TIMESTAMPTZ
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS fx_spot (
                base_ccy VARCHAR(3) NOT NULL,
                quote_ccy VARCHAR(3) NOT NULL,
                as_of TIMESTAMPTZ NOT NULL,
                mid DOUBLE PRECISION NOT NULL,
                bid DOUBLE PRECISION,
                ask DOUBLE PRECISION,
                spot_date DATE,
                PRIMARY KEY (base_ccy, quote_ccy, as_of)
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS fx_forwards (
                base_ccy VARCHAR(3) NOT NULL,
                quote_ccy VARCHAR(3) NOT NULL,
                tenor VARCHAR NOT NULL,
                as_of TIMESTAMPTZ NOT NULL,
                forward_points_mid DOUBLE PRECISION,
                forward_points_bid DOUBLE PRECISION,
                forward_points_ask DOUBLE PRECISION,
                outright_mid DOUBLE PRECISION,
                value_date DATE,
                spot_reference DOUBLE PRECISION,
                PRIMARY KEY (base_ccy, quote_ccy, tenor, as_of)
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS reference_fomc_meetings (
                meeting_date DATE PRIMARY KEY,
                has_press_conference BOOLEAN DEFAULT true,
                announcement_time TIME DEFAULT '14:00:00',
                decision_rate DOUBLE PRECISION,
                implied_move DOUBLE PRECISION
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS reference_holidays (
                calendar VARCHAR NOT NULL,
                holiday_date DATE NOT NULL,
                holiday_name VARCHAR,
                PRIMARY KEY (calendar, holiday_date)
            )
        )");

        spdlog::debug("Input schema initialized");
    }

    void ensure_output_schema() {
        query(R"(
            CREATE TABLE IF NOT EXISTS runs (
                run_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                created_at TIMESTAMPTZ DEFAULT now(),
                name VARCHAR,
                description TEXT,
                git_commit VARCHAR(40),
                config JSONB,
                status VARCHAR DEFAULT 'running',
                parent_run_id UUID,
                tags JSONB
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS trades (
                run_id UUID NOT NULL,
                trade_id BIGINT NOT NULL,
                timestamp TIMESTAMPTZ NOT NULL,
                symbol VARCHAR NOT NULL,
                side VARCHAR NOT NULL,
                quantity DOUBLE PRECISION NOT NULL,
                price DOUBLE PRECISION NOT NULL,
                commission DOUBLE PRECISION DEFAULT 0,
                signal_name VARCHAR,
                strategy_id VARCHAR,
                metadata JSONB,
                PRIMARY KEY (run_id, trade_id)
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS equity (
                run_id UUID NOT NULL,
                timestamp TIMESTAMPTZ NOT NULL,
                nav DOUBLE PRECISION NOT NULL,
                cash DOUBLE PRECISION,
                positions_value DOUBLE PRECISION,
                drawdown DOUBLE PRECISION,
                high_water_mark DOUBLE PRECISION,
                PRIMARY KEY (run_id, timestamp)
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS orders (
                run_id UUID NOT NULL,
                order_id BIGINT NOT NULL,
                created_at TIMESTAMPTZ NOT NULL,
                symbol VARCHAR NOT NULL,
                side VARCHAR NOT NULL,
                order_type VARCHAR NOT NULL,
                quantity DOUBLE PRECISION NOT NULL,
                limit_price DOUBLE PRECISION,
                stop_price DOUBLE PRECISION,
                status VARCHAR NOT NULL,
                filled_qty DOUBLE PRECISION DEFAULT 0,
                avg_fill_price DOUBLE PRECISION,
                rejection_reason VARCHAR,
                strategy_id VARCHAR,
                time_in_force VARCHAR DEFAULT 'Day',
                metadata JSONB,
                PRIMARY KEY (run_id, order_id)
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS positions (
                run_id UUID NOT NULL,
                timestamp TIMESTAMPTZ NOT NULL,
                symbol VARCHAR NOT NULL,
                quantity DOUBLE PRECISION NOT NULL,
                avg_cost DOUBLE PRECISION,
                market_price DOUBLE PRECISION,
                unrealized_pnl DOUBLE PRECISION,
                realized_pnl DOUBLE PRECISION,
                currency VARCHAR(3) DEFAULT 'USD',
                PRIMARY KEY (run_id, timestamp, symbol)
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS risk_events (
                run_id UUID NOT NULL,
                timestamp TIMESTAMPTZ NOT NULL,
                event_type VARCHAR NOT NULL,
                limit_name VARCHAR NOT NULL,
                symbol VARCHAR,
                current_value DOUBLE PRECISION,
                limit_value DOUBLE PRECISION,
                breach_amount DOUBLE PRECISION,
                severity VARCHAR,
                action_taken VARCHAR,
                metadata JSONB,
                PRIMARY KEY (run_id, timestamp, limit_name, event_type)
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS signals (
                run_id UUID NOT NULL,
                timestamp TIMESTAMPTZ NOT NULL,
                signal_name VARCHAR NOT NULL,
                symbol VARCHAR NOT NULL,
                direction VARCHAR,
                strength DOUBLE PRECISION,
                confidence DOUBLE PRECISION,
                features JSONB,
                metadata JSONB,
                PRIMARY KEY (run_id, timestamp, signal_name, symbol)
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS calculated_curves (
                run_id UUID NOT NULL,
                as_of TIMESTAMPTZ NOT NULL,
                curve_type VARCHAR NOT NULL,
                currency VARCHAR(3) NOT NULL,
                pillar_dates JSONB,
                zero_rates JSONB,
                discount_factors JSONB,
                forward_rates JSONB,
                interpolation VARCHAR,
                success BOOLEAN DEFAULT true,
                error_message VARCHAR,
                PRIMARY KEY (run_id, as_of, curve_type, currency)
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS calculated_basis (
                run_id UUID NOT NULL,
                as_of TIMESTAMPTZ NOT NULL,
                basis_type VARCHAR NOT NULL,
                instrument_id VARCHAR NOT NULL,
                futures_code VARCHAR,
                gross_basis DOUBLE PRECISION,
                net_basis DOUBLE PRECISION,
                gross_basis_32nds DOUBLE PRECISION,
                net_basis_32nds DOUBLE PRECISION,
                implied_repo DOUBLE PRECISION,
                conversion_factor DOUBLE PRECISION,
                carry DOUBLE PRECISION,
                is_ctd BOOLEAN DEFAULT false,
                ctd_rank INTEGER,
                metadata JSONB,
                PRIMARY KEY (run_id, as_of, basis_type, instrument_id)
            )
        )");

        query(R"(
            CREATE TABLE IF NOT EXISTS analytics_summary (
                run_id UUID NOT NULL,
                metric_name VARCHAR NOT NULL,
                metric_value DOUBLE PRECISION NOT NULL,
                as_of TIMESTAMPTZ NOT NULL,
                category VARCHAR,
                metadata JSONB,
                PRIMARY KEY (run_id, metric_name, as_of)
            )
        )");

        spdlog::debug("Output schema initialized");
    }
};

// ============================================================================
// Run Management
// ============================================================================

struct RunInfo {
    string run_id;
    string name;
    string status;
    string git_commit;
};

auto start_run(DataStore& db, string_view name, string_view git_commit = "",
               string_view config_json = "{}") -> string {
    auto result = db.execute(
        "INSERT INTO runs (name, git_commit, config) "
        "VALUES ($1, $2, $3::jsonb) RETURNING run_id::text",
        string{name}, string{git_commit}, string{config_json});

    if (result.empty()) {
        spdlog::error("Failed to start run");
        return "";
    }
    return result[0][0].as<string>();
}

void complete_run(DataStore& db, string_view run_id) {
    db.execute("UPDATE runs SET status = 'completed' WHERE run_id = $1::uuid",
               string{run_id});
}

void fail_run(DataStore& db, string_view run_id, string_view error_msg = "") {
    db.execute(
        "UPDATE runs SET status = 'failed', description = $1 WHERE run_id = $2::uuid",
        string{error_msg}, string{run_id});
}

// ============================================================================
// Factory
// ============================================================================

auto create_data_store(const DatabaseConfig& config) -> unique_ptr<DataStore> {
    return std::make_unique<DataStore>(config);
}

auto create_data_store(const Config& config) -> unique_ptr<DataStore> {
    return std::make_unique<DataStore>(config.database);
}

auto create_data_store_from_env() -> unique_ptr<DataStore> {
    auto db_config = load_database_config_from_env();
    if (db_config) {
        return std::make_unique<DataStore>(*db_config);
    }
    auto config = load_config();
    return std::make_unique<DataStore>(config.database);
}

} // namespace finkit::data
```

- [ ] **Step 2: Build to verify compilation**

Run:
```bash
CC=/opt/homebrew/opt/llvm/bin/clang \
CXX=/opt/homebrew/opt/llvm/bin/clang++ \
cmake --preset conan-release && cmake --build build/build/Release
```

Expected: Compiles successfully. The placeholder test should still pass (it only imports the module and asserts true).

**If compilation fails:** Check the libpqxx CMake target name — it may be `libpqxx::libpqxx` instead of `libpqxx::pqxx`. Adjust `src/data/CMakeLists.txt` accordingly.

**If other modules fail:** Modules that `import finkit.data` and reference the old `InputDataStore`/`OutputDataStore`/`Database` types will fail. Fix them in Task 5 (Consumer Updates). For now, if this blocks the build, temporarily stub the old types as aliases:
```cpp
using InputDataStore = DataStore;  // temporary backward compat
using OutputDataStore = DataStore;
using Database = DataStore;
```

- [ ] **Step 3: Commit (includes build system swap from Task 1)**

```bash
git add conanfile.py CMakeLists.txt src/data/CMakeLists.txt src/data/data.cppm
git commit -m "feat: migrate data module from DuckDB to libpqxx/TimescaleDB

Replace DuckDB with libpqxx for PostgreSQL/TimescaleDB connectivity.
Merge InputDataStore/OutputDataStore into single DataStore class.
Config via TSDB_*/POSTGRES_* env vars with TOML fallback."
```

---

### Task 3: Write comprehensive tests

**Files:**
- Rewrite: `tests/data/data_test.cpp`

The tests are split into two groups:
1. **Unit tests** — config loading, connection string building (no DB required)
2. **Integration tests** — DataStore, schema, run management (require TSDB; skip if unavailable)

- [ ] **Step 1: Write `tests/data/data_test.cpp`**

```cpp
#include <cstdlib>
#include <gtest/gtest.h>

import finkit.data;

namespace {

using namespace finkit::data;

// ============================================================================
// Unit Tests — no database required
// ============================================================================

TEST(DatabaseConfigTest, BuildConnectionStringFromFields) {
    DatabaseConfig cfg{
        .host = "localhost",
        .port = 5432,
        .database = "testdb",
        .user = "testuser",
        .password = "testpass",
    };
    auto cs = build_connection_string(cfg);
    EXPECT_NE(cs.find("host=localhost"), std::string::npos);
    EXPECT_NE(cs.find("port=5432"), std::string::npos);
    EXPECT_NE(cs.find("dbname=testdb"), std::string::npos);
    EXPECT_NE(cs.find("user=testuser"), std::string::npos);
    EXPECT_NE(cs.find("password=testpass"), std::string::npos);
}

TEST(DatabaseConfigTest, ConnectionStringOverrideTakesPrecedence) {
    DatabaseConfig cfg{
        .host = "ignored",
        .connection_string = "postgresql://override:5433/mydb",
    };
    EXPECT_EQ(build_connection_string(cfg), "postgresql://override:5433/mydb");
}

TEST(DatabaseConfigTest, DefaultPort) {
    DatabaseConfig cfg;
    EXPECT_EQ(cfg.port, 5432);
}

// ============================================================================
// Integration Tests — require TSDB connection
// ============================================================================

class DataStoreTest : public ::testing::Test {
protected:
    void SetUp() override {
        auto config = load_database_config_from_env();
        if (!config) {
            GTEST_SKIP() << "TSDB not configured (set TSDB_HOST or POSTGRES_HOST)";
        }
        store_ = std::make_unique<DataStore>(*config);
    }

    void TearDown() override {
        if (store_ && !run_id_.empty()) {
            try {
                store_->execute(
                    "DELETE FROM runs WHERE run_id = $1::uuid", run_id_);
            } catch (...) {}
        }
    }

    std::unique_ptr<DataStore> store_;
    std::string run_id_;
};

TEST_F(DataStoreTest, ConnectsAndQueriesSuccessfully) {
    auto result = store_->query("SELECT 1 AS one");
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0][0].as<int>(), 1);
}

TEST_F(DataStoreTest, EnsureSchemaCreatesOutputTables) {
    store_->ensure_schema();

    // Verify a subset of tables exist by querying information_schema
    auto result = store_->query(
        "SELECT table_name FROM information_schema.tables "
        "WHERE table_schema = 'public' AND table_name IN "
        "('runs', 'trades', 'equity', 'calculated_basis') "
        "ORDER BY table_name");

    ASSERT_GE(result.size(), 4);
}

TEST_F(DataStoreTest, EnsureSchemaCreatesInputTables) {
    store_->ensure_schema();

    auto result = store_->query(
        "SELECT table_name FROM information_schema.tables "
        "WHERE table_schema = 'public' AND table_name IN "
        "('rates_sofr_fixings', 'bonds_reference', 'reference_fomc_meetings') "
        "ORDER BY table_name");

    ASSERT_GE(result.size(), 3);
}

TEST_F(DataStoreTest, EnsureSchemaIsIdempotent) {
    store_->ensure_schema();
    EXPECT_NO_THROW(store_->ensure_schema());
}

TEST_F(DataStoreTest, StartRunReturnsUUID) {
    store_->ensure_schema();
    run_id_ = start_run(*store_, "test_run", "abc123", "{}");

    EXPECT_FALSE(run_id_.empty());
    // UUID format: 8-4-4-4-12 hex chars
    EXPECT_EQ(run_id_.size(), 36);
    EXPECT_EQ(run_id_[8], '-');
}

TEST_F(DataStoreTest, CompleteRunUpdatesStatus) {
    store_->ensure_schema();
    run_id_ = start_run(*store_, "test_complete");

    complete_run(*store_, run_id_);

    auto result = store_->execute(
        "SELECT status FROM runs WHERE run_id = $1::uuid", run_id_);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0][0].as<std::string>(), "completed");
}

TEST_F(DataStoreTest, FailRunUpdatesStatusAndDescription) {
    store_->ensure_schema();
    run_id_ = start_run(*store_, "test_fail");

    fail_run(*store_, run_id_, "something broke");

    auto result = store_->execute(
        "SELECT status, description FROM runs WHERE run_id = $1::uuid",
        run_id_);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0]["status"].as<std::string>(), "failed");
    EXPECT_EQ(result[0]["description"].as<std::string>(), "something broke");
}

TEST_F(DataStoreTest, ParameterizedExecuteWorks) {
    store_->ensure_schema();

    store_->execute(
        "INSERT INTO rates_sofr_fixings (fixing_date, rate, source) "
        "VALUES ($1::date, $2, $3)",
        std::string{"2025-01-02"}, 4.55, std::string{"test"});

    auto result = store_->query(
        "SELECT rate FROM rates_sofr_fixings WHERE fixing_date = '2025-01-02'");
    ASSERT_EQ(result.size(), 1);
    EXPECT_NEAR(result[0][0].as<double>(), 4.55, 1e-6);

    // Cleanup
    store_->execute(
        "DELETE FROM rates_sofr_fixings WHERE fixing_date = $1::date",
        std::string{"2025-01-02"});
}

TEST_F(DataStoreTest, FactoryFromEnvConnects) {
    auto store = create_data_store_from_env();
    auto result = store->query("SELECT 1");
    EXPECT_EQ(result.size(), 1);
}

TEST_F(DataStoreTest, CanQueryPythonRepoOhlcvTable) {
    // Verify we can read from the Python repo's timeseries_ohlcv table.
    // This is the key table that proves shared data access works.
    auto result = store_->query(
        "SELECT COUNT(*) FROM timeseries_ohlcv");
    EXPECT_GE(result[0][0].as<int64_t>(), 0);
}

} // namespace
```

- [ ] **Step 2: Build and run tests**

Run:
```bash
cmake --build build/build/Release && \
ctest --test-dir build/build/Release -R DataStoreTest --output-on-failure
```

For integration tests, run under Infisical:
```bash
infisical run --env=dev --path="/kubernetes/infrastructure/timescaledb" -- \
  ctest --test-dir build/build/Release -R DataStoreTest --output-on-failure
```

Expected: Unit tests always pass. Integration tests pass when TSDB is available, skip when not.

- [ ] **Step 3: Fix any failures and re-run**

Common issues:
- `pqxx::result` API differences (check field access syntax)
- SQL type mismatches (`::uuid` cast, `::jsonb` cast)
- Connection string format issues

- [ ] **Step 4: Run ALL tests to catch regressions**

Run:
```bash
ctest --test-dir build/build/Release --output-on-failure
```

Expected: All existing tests pass (other modules should not be affected if they don't directly reference DuckDB types).

- [ ] **Step 5: Commit**

```bash
git add tests/data/data_test.cpp
git commit -m "test: add unit and integration tests for TimescaleDB data store"
```

---

## Chunk 2: Consumer Updates + Scratch Files

### Task 4: Fix consumer modules that reference old API

**Files:**
- Check/Modify: `src/analysis/analysis.cppm`
- Check/Modify: `src/curves/curves.cppm`
- Check/Modify: `apps/bond_basis/main.cpp`
- Check/Modify: `scratch/validate_bonds.cpp` (if tracked)
- Check/Modify: `scratch/CMakeLists.txt` (if tracked)

These modules `import finkit.data` but may reference removed types (`InputDataStore`, `OutputDataStore`, `Database`, `DataStores`, `create_data_stores`, `create_in_memory_stores`).

- [ ] **Step 1: Search for references to removed types**

Run:
```bash
rg 'InputDataStore|OutputDataStore|create_data_stores|create_in_memory_stores|DataStores' \
  --type cpp -l
```

Expected: Lists files that need updating.

- [ ] **Step 2: Update each file**

For each file found, replace:
- `InputDataStore` / `OutputDataStore` / `Database` → `DataStore`
- `DataStores` (struct with `.input` and `.output`) → `unique_ptr<DataStore>` (single store)
- `create_data_stores(config)` → `create_data_store(config)`
- `create_in_memory_stores()` → remove (tests use real TSDB or mock differently)
- `stores.input->query(...)` → `store->query(...)`
- `stores.output->execute(...)` → `store->execute(...)`
- `start_run(OutputDataStore& db, ...)` → `start_run(DataStore& db, ...)`

- [ ] **Step 3: Fix scratch files to prevent build breakage**

`scratch/` is gitignored, but the root `CMakeLists.txt` conditionally includes it (line 93-94). After removing `find_package(duckdb)`, `scratch/CMakeLists.txt` will cause a CMake configure error if present on disk.

If `scratch/CMakeLists.txt` exists locally, update its `duckdb::duckdb` link target to `libpqxx::pqxx`. If `scratch/validate_bonds.cpp` exists, update its DuckDB includes and API to use DataStore.

If `scratch/` doesn't exist locally, skip this step.

- [ ] **Step 4: Build and run ALL tests**

Run:
```bash
cmake --build build/build/Release && \
ctest --test-dir build/build/Release --output-on-failure
```

Expected: All tests pass.

- [ ] **Step 5: Commit**

```bash
git add -u
git commit -m "refactor: update consumer modules for DataStore API"
```

---

### Task 5: Update CLAUDE.md

**Files:**
- Modify: `CLAUDE.md`

- [ ] **Step 1: Update data storage reference**

Replace:
```
- `src/data/data.cppm` - Market data ingestion and storage (DuckDB)
```
With:
```
- `src/data/data.cppm` - Market data ingestion and storage (TimescaleDB via libpqxx)
```

- [ ] **Step 2: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: update CLAUDE.md for TimescaleDB migration"
```

---

## Chunk 3: Documentation

### Task 6: Update documentation files

All documentation files that reference DuckDB need updating. This task covers all of them.

**Files:**
- Modify: `docs/architecture.md`
- Modify: `docs/SETUP.md`
- Modify: `docs/DEPENDENCIES.md`
- Modify: `docs/STATUS.md`
- Modify: `docs/modules/data.md`
- Modify: `docs/getting-started.md`
- Modify: `docs/guides/loading-market-data.md`
- Modify: `docs/concepts/data-pipeline.md`
- Modify: `docs/diagrams/module-overview.md`
- Modify: `docs/roadmap.md`
- Modify: `apps/bond_basis/README.md`

- [ ] **Step 1: `docs/architecture.md`**

Lines 20-24 — in "Frameworks Compose Modules" section, replace:
```
- Load data from InputDataStore
```
With:
```
- Load data from DataStore
```
And replace:
```
- Write results to OutputDataStore
```
With:
```
- Write results to DataStore
```

Line 39 — replace:
```
| Data storage | **DuckDB** | SQL queries, aggregations, time series |
```
With:
```
| Data storage | **libpqxx/TimescaleDB** | SQL queries, time series, shared data store |
```

Lines 86-88 — replace:
```
- **finkit.data**: Database connection, config loading
  - InputDataStore: Read-only access to market/reference data
  - OutputDataStore: Write access to calculation results
```
With:
```
- **finkit.data**: Database connection, config loading
  - DataStore: Unified read/write access to TimescaleDB (shared with Python repo)
```

Lines 128-130 — replace:
```
- **InputDataStore**: Read-only access to market data, rates, bonds, FX
- **OutputDataStore**: Write access to runs, trades, equity curves, signals
```
With:
```
- **DataStore**: Unified access to TimescaleDB for market data (reads) and calculation results (writes)
```

Line 194 — replace:
```
- Memory-mapped files for large datasets (DuckDB)
```
With:
```
- PostgreSQL/TimescaleDB for persistent data storage
```

- [ ] **Step 2: `docs/SETUP.md`**

Line 89 — replace:
```
This builds duckdb, spdlog, and fmt from source (no pre-built binaries exist for this toolchain).
```
With:
```
This builds libpqxx, spdlog, and fmt from source (no pre-built binaries exist for this toolchain).
```

Line 129 — replace:
```
| DuckDB | Conan (built) | Data storage |
```
With:
```
| libpqxx | Conan (built) | PostgreSQL/TimescaleDB client |
```

Lines 163-165 — replace:
```
To rebuild a specific package:
```bash
uv run conan install . --build=duckdb -of=build ...
```
With:
```
To rebuild a specific package:
```bash
uv run conan install . --build=libpqxx -of=build ...
```

Add a new section after "Troubleshooting" (before end of file):
```markdown
### TimescaleDB connection

fin-kit connects to TimescaleDB for data storage. Set credentials via environment variables:

```bash
# Via Infisical (recommended)
infisical run --env=dev --path="/kubernetes/infrastructure/timescaledb" -- \
  cmake --build build/build/Release

# Or set manually
export TSDB_HOST=localhost
export TSDB_PORT=5432
export TSDB_DATABASE=finkit
export TSDB_USER=finkit
export TSDB_PASSWORD=secret
```

Alternatively, configure in `~/.config/finkit/config.toml`:
```toml
[database]
host = "localhost"
port = 5432
database = "finkit"
user = "finkit"
password = "secret"
```
```

- [ ] **Step 3: `docs/DEPENDENCIES.md`**

Line 20 — replace:
```
1. **Conan packages** (duckdb, spdlog, fmt, nlohmann_json, tomlplusplus)
```
With:
```
1. **Conan packages** (libpqxx, spdlog, fmt, nlohmann_json, tomlplusplus)
```

Lines 46-47 — replace:
```
self.requires("duckdb/1.5.0")  # Change version here
```
With:
```
self.requires("libpqxx/7.10.0")  # Change version here
```

Line 90 — replace:
```
| `self\.requires\("(?<depName>[^/]+)/(?<currentValue>[^"]+)"\)` | `self.requires("duckdb/1.4.3")` |
```
With:
```
| `self\.requires\("(?<depName>[^/]+)/(?<currentValue>[^"]+)"\)` | `self.requires("libpqxx/7.9.2")` |
```

- [ ] **Step 4: `docs/STATUS.md`**

Line 9 — replace:
```
- Conan for most deps (DuckDB, spdlog, fmt, tomlplusplus, nlohmann_json)
```
With:
```
- Conan for most deps (libpqxx, spdlog, fmt, tomlplusplus, nlohmann_json)
```

Line 19 — replace:
```
| `finkit.data` | Working | - | TOML config, DuckDB, InputDataStore/OutputDataStore |
```
With:
```
| `finkit.data` | Working | - | TOML config, TimescaleDB via libpqxx, DataStore |
```

Line 61 — replace:
```
1. **Data source integration** - How does market data flow into DuckDB?
```
With:
```
1. **Data source integration** - Market data flows from Python repo into shared TimescaleDB
```

- [ ] **Step 5: `docs/modules/data.md`**

Replace the entire file with updated content reflecting the new API:
- `DataStore` instead of `InputDataStore`/`OutputDataStore`
- `create_data_store` / `create_data_store_from_env` instead of `create_data_stores` / `create_in_memory_stores`
- `libpqxx` instead of `duckdb`
- Config via env vars + TOML
- Updated usage example

- [ ] **Step 6: `docs/getting-started.md`**

Line 106 — replace:
```
- Loading market data from DuckDB
```
With:
```
- Loading market data from TimescaleDB
```

Line 112 — replace:
```
1. **[Loading Market Data](guides/loading-market-data.md)** - How to populate DuckDB with your data
```
With:
```
1. **[Loading Market Data](guides/loading-market-data.md)** - How to query market data from TimescaleDB
```

Line 122 — replace:
```
| `finkit.data` | DuckDB data stores, config loading |
```
With:
```
| `finkit.data` | TimescaleDB data store, config loading |
```

- [ ] **Step 7: `docs/guides/loading-market-data.md`**

This file needs a significant rewrite:
- Replace all `DuckDB` references with `TimescaleDB`
- Remove file-path-based config examples
- Remove CSV loading via `read_csv_auto` (DuckDB-specific)
- Remove in-memory mode examples
- Update `query` result API from `result->HasError()` / `row.GetValue<T>(n)` to `pqxx::result` API
- Add note about shared data with Python repo

- [ ] **Step 8: `docs/concepts/data-pipeline.md`**

Replace `(DuckDB)` labels in the ASCII diagram (lines 25 and 52) with `(TimescaleDB)`.

Replace all occurrences of `InputDataStore` with `DataStore` and all occurrences of `OutputDataStore` with `DataStore` throughout the file (these appear in the overview, diagram labels, and prose sections).

Lines 108-116 — replace the "DuckDB Usage" section with a "TimescaleDB Usage" section describing connection-based access.

Remove "In-Memory Mode" section (lines 127-132).

- [ ] **Step 9: `docs/diagrams/module-overview.md`**

Line 24 — replace:
```
        DDB[DuckDB]
```
With:
```
        PQ[libpqxx/TimescaleDB]
```

Line 36 — replace:
```
    D --> DDB
```
With:
```
    D --> PQ
```

Lines 54-55 — replace:
```
        D1[DuckDB Wrapper]
        D2[Parquet Reader]
```
With:
```
        D1[TimescaleDB Client]
        D2[Schema Manager]
```

Lines 82-83 — replace:
```
        P[Parquet Files]
        DB[(DuckDB)]
```
With:
```
        DB[(TimescaleDB)]
```

Remove the `P --> D` edge (line 97).

- [ ] **Step 10: `docs/roadmap.md`**

Line 199 — the "Database backends (TimescaleDB, QuestDB)" item is now partially done. Replace:
```
- [ ] Database backends (TimescaleDB, QuestDB)
```
With:
```
- [x] TimescaleDB backend (via libpqxx)
- [ ] QuestDB backend
```

Line 416 — in "Thread Safety Audit Checklist", replace:
```
- [ ] DuckDB connections (use per-thread connections)
```
With:
```
- [ ] PostgreSQL connections (use per-thread connections or connection pool)
```

- [ ] **Step 11: `apps/bond_basis/README.md`**

Line 38 — replace:
```
- `finkit.data` - DuckDB data access for market data
```
With:
```
- `finkit.data` - TimescaleDB data access for market data
```

- [ ] **Step 12: Build to confirm nothing broke**

Run:
```bash
cmake --build build/build/Release && \
ctest --test-dir build/build/Release --output-on-failure
```

Expected: All tests pass.

- [ ] **Step 13: Commit all documentation**

```bash
git add docs/ apps/bond_basis/README.md
git commit -m "docs: update all references from DuckDB to TimescaleDB/libpqxx"
```
