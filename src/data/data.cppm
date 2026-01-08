module;

#include <duckdb.hpp>
#include <filesystem>
#include <memory>
#include <mutex>
#include <optional>
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
    fs::path input_path{"~/.finkit/input.db"};   // Read-only input data
    fs::path output_path{"~/.finkit/output.db"}; // Calculation results
    bool wal_mode{true};
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

auto default_config_paths() -> vector<fs::path> {
    return {
        finkit::core::expand_path("~/.config/finkit/config.toml"),
        finkit::core::expand_path("~/.finkit/config.toml"),
        fs::path{"finkit.toml"},
    };
}

auto load_config(const fs::path& path) -> optional<Config> {
    if (!fs::exists(path)) {
        return std::nullopt;
    }

    try {
        auto tbl = toml::parse_file(path.string());
        Config cfg;

        if (auto db = tbl["database"].as_table()) {
            if (auto p = db->get("input_path")) {
                cfg.database.input_path = p->value_or(string{"~/.finkit/input.db"});
            }
            if (auto p = db->get("output_path")) {
                cfg.database.output_path = p->value_or(string{"~/.finkit/output.db"});
            }
            // Legacy support: old "path" field maps to output_path
            if (auto p = db->get("path")) {
                cfg.database.output_path = p->value_or(string{"~/.finkit/output.db"});
            }
            if (auto w = db->get("wal_mode")) {
                cfg.database.wal_mode = w->value_or(true);
            }
        }

        if (auto log = tbl["logging"].as_table()) {
            if (auto l = log->get("level")) {
                cfg.logging.level = l->value_or(string{"info"});
            }
        }

        if (auto bt = tbl["backtest"].as_table()) {
            if (auto tz = bt->get("default_timezone")) {
                cfg.backtest.default_timezone = tz->value_or(string{"America/New_York"});
            }
        }

        cfg.database.input_path = finkit::core::expand_path(cfg.database.input_path);
        cfg.database.output_path = finkit::core::expand_path(cfg.database.output_path);
        return cfg;
    } catch (const toml::parse_error& err) {
        spdlog::error("Failed to parse config {}: {}", path.string(), err.what());
        return std::nullopt;
    }
}

auto load_config() -> Config {
    for (const auto& path : default_config_paths()) {
        if (auto cfg = load_config(path)) {
            spdlog::info("Loaded config from {}", path.string());
            return *cfg;
        }
    }
    spdlog::info("No config file found, using defaults");
    Config cfg;
    cfg.database.input_path = finkit::core::expand_path(cfg.database.input_path);
    cfg.database.output_path = finkit::core::expand_path(cfg.database.output_path);
    return cfg;
}

// ============================================================================
// Base Database Connection
// ============================================================================

class Database {
public:
    explicit Database(const fs::path& path) : path_{path} {
        if (path_.has_parent_path() && path_.string() != ":memory:") {
            fs::create_directories(path_.parent_path());
        }

        db_ = std::make_unique<duckdb::DuckDB>(path_.string());
        conn_ = std::make_unique<duckdb::Connection>(*db_);

        spdlog::info("Database opened: {}", path_.string());
    }

    Database() : path_{":memory:"} {
        db_ = std::make_unique<duckdb::DuckDB>(nullptr);
        conn_ = std::make_unique<duckdb::Connection>(*db_);
        spdlog::debug("In-memory database created");
    }

    auto query(string_view sql) -> duckdb::unique_ptr<duckdb::MaterializedQueryResult> {
        std::lock_guard lock{mutex_};
        return conn_->Query(string{sql});
    }

    template <typename... Args>
    auto execute(string_view sql, Args&&... args) -> duckdb::unique_ptr<duckdb::QueryResult> {
        std::lock_guard lock{mutex_};
        auto prepared = conn_->Prepare(string{sql});
        if (prepared->HasError()) {
            spdlog::error("Prepare error: {}", prepared->GetError());
            return nullptr;
        }
        return prepared->Execute(std::forward<Args>(args)...);
    }

    auto connection() -> duckdb::Connection& { return *conn_; }
    auto path() const -> const fs::path& { return path_; }

protected:
    fs::path path_;
    unique_ptr<duckdb::DuckDB> db_;
    unique_ptr<duckdb::Connection> conn_;
    std::mutex mutex_;
};

// ============================================================================
// InputDataStore - Read-only access to input tables
// External feeds, never modified by fin-kit calculations
// ============================================================================

class InputDataStore : public Database {
public:
    explicit InputDataStore(const fs::path& path) : Database(path) { init_input_schema(); }
    InputDataStore() : Database() { init_input_schema(); }

private:
    void init_input_schema() {
        // Market data tables
        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS market_ohlcv (
                symbol VARCHAR NOT NULL,
                timestamp TIMESTAMPTZ NOT NULL,
                open DOUBLE,
                high DOUBLE,
                low DOUBLE,
                close DOUBLE,
                volume DOUBLE,
                vwap DOUBLE,
                trade_count BIGINT,
                PRIMARY KEY (symbol, timestamp)
            )
        )");

        // Interest rate tables
        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS rates_sofr_fixings (
                fixing_date DATE PRIMARY KEY,
                rate DOUBLE NOT NULL,
                source VARCHAR
            )
        )");

        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS rates_sofr_futures (
                contract_code VARCHAR PRIMARY KEY,
                futures_type VARCHAR NOT NULL,
                price DOUBLE,
                implied_rate DOUBLE,
                reference_start DATE,
                reference_end DATE,
                last_trade_date DATE,
                convexity_adj DOUBLE,
                as_of TIMESTAMPTZ
            )
        )");

        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS rates_ois_quotes (
                currency VARCHAR NOT NULL,
                tenor VARCHAR NOT NULL,
                rate DOUBLE NOT NULL,
                effective_date DATE,
                maturity_date DATE,
                as_of TIMESTAMPTZ NOT NULL,
                PRIMARY KEY (currency, tenor, as_of)
            )
        )");

        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS rates_repo (
                as_of DATE NOT NULL,
                gc_rate DOUBLE NOT NULL,
                term_days INTEGER DEFAULT 1,
                collateral_type VARCHAR DEFAULT 'Treasury',
                source VARCHAR,
                PRIMARY KEY (as_of, term_days, collateral_type)
            )
        )");

        // Bond tables
        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS bonds_reference (
                cusip VARCHAR(9) PRIMARY KEY,
                isin VARCHAR(12),
                coupon DOUBLE NOT NULL,
                maturity DATE NOT NULL,
                issue_date DATE,
                first_coupon_date DATE,
                security_type VARCHAR DEFAULT 'Note'
            )
        )");

        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS bonds_prices (
                cusip VARCHAR(9) NOT NULL,
                as_of TIMESTAMPTZ NOT NULL,
                clean_price DOUBLE NOT NULL,
                accrued_interest DOUBLE,
                yield_to_maturity DOUBLE,
                modified_duration DOUBLE,
                source VARCHAR,
                PRIMARY KEY (cusip, as_of)
            )
        )");

        // Treasury futures tables
        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS futures_treasury (
                contract_code VARCHAR PRIMARY KEY,
                product VARCHAR NOT NULL,
                price DOUBLE,
                first_delivery DATE,
                last_delivery DATE,
                last_trade DATE,
                notional DOUBLE,
                as_of TIMESTAMPTZ
            )
        )");

        // FX tables
        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS fx_spot (
                base_ccy VARCHAR(3) NOT NULL,
                quote_ccy VARCHAR(3) NOT NULL,
                as_of TIMESTAMPTZ NOT NULL,
                mid DOUBLE NOT NULL,
                bid DOUBLE,
                ask DOUBLE,
                spot_date DATE,
                PRIMARY KEY (base_ccy, quote_ccy, as_of)
            )
        )");

        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS fx_forwards (
                base_ccy VARCHAR(3) NOT NULL,
                quote_ccy VARCHAR(3) NOT NULL,
                tenor VARCHAR NOT NULL,
                as_of TIMESTAMPTZ NOT NULL,
                forward_points_mid DOUBLE,
                forward_points_bid DOUBLE,
                forward_points_ask DOUBLE,
                outright_mid DOUBLE,
                value_date DATE,
                spot_reference DOUBLE,
                PRIMARY KEY (base_ccy, quote_ccy, tenor, as_of)
            )
        )");

        // Reference data tables
        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS reference_fomc_meetings (
                meeting_date DATE PRIMARY KEY,
                has_press_conference BOOLEAN DEFAULT true,
                announcement_time TIME DEFAULT '14:00:00',
                decision_rate DOUBLE,
                implied_move DOUBLE
            )
        )");

        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS reference_holidays (
                calendar VARCHAR NOT NULL,
                holiday_date DATE NOT NULL,
                holiday_name VARCHAR,
                PRIMARY KEY (calendar, holiday_date)
            )
        )");

        spdlog::debug("Input schema initialized");
    }
};

// ============================================================================
// OutputDataStore - Write access to output tables
// Written by fin-kit frameworks and calculations
// ============================================================================

class OutputDataStore : public Database {
public:
    explicit OutputDataStore(const fs::path& path) : Database(path) { init_output_schema(); }
    OutputDataStore() : Database() { init_output_schema(); }

private:
    void init_output_schema() {
        // Run management
        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS runs (
                run_id UUID PRIMARY KEY DEFAULT gen_random_uuid(),
                created_at TIMESTAMPTZ DEFAULT now(),
                name VARCHAR,
                description TEXT,
                git_commit VARCHAR(40),
                config JSON,
                status VARCHAR DEFAULT 'running',
                parent_run_id UUID,
                tags JSON
            )
        )");

        // Trade records
        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS trades (
                run_id UUID NOT NULL,
                trade_id BIGINT NOT NULL,
                timestamp TIMESTAMPTZ NOT NULL,
                symbol VARCHAR NOT NULL,
                side VARCHAR NOT NULL,
                quantity DOUBLE NOT NULL,
                price DOUBLE NOT NULL,
                commission DOUBLE DEFAULT 0,
                signal_name VARCHAR,
                strategy_id VARCHAR,
                metadata JSON,
                PRIMARY KEY (run_id, trade_id)
            )
        )");

        // Portfolio equity curve
        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS equity (
                run_id UUID NOT NULL,
                timestamp TIMESTAMPTZ NOT NULL,
                nav DOUBLE NOT NULL,
                cash DOUBLE,
                positions_value DOUBLE,
                drawdown DOUBLE,
                high_water_mark DOUBLE,
                PRIMARY KEY (run_id, timestamp)
            )
        )");

        // Orders (all submitted orders)
        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS orders (
                run_id UUID NOT NULL,
                order_id BIGINT NOT NULL,
                created_at TIMESTAMPTZ NOT NULL,
                symbol VARCHAR NOT NULL,
                side VARCHAR NOT NULL,
                order_type VARCHAR NOT NULL,
                quantity DOUBLE NOT NULL,
                limit_price DOUBLE,
                stop_price DOUBLE,
                status VARCHAR NOT NULL,
                filled_qty DOUBLE DEFAULT 0,
                avg_fill_price DOUBLE,
                rejection_reason VARCHAR,
                strategy_id VARCHAR,
                time_in_force VARCHAR DEFAULT 'Day',
                metadata JSON,
                PRIMARY KEY (run_id, order_id)
            )
        )");

        // Position snapshots
        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS positions (
                run_id UUID NOT NULL,
                timestamp TIMESTAMPTZ NOT NULL,
                symbol VARCHAR NOT NULL,
                quantity DOUBLE NOT NULL,
                avg_cost DOUBLE,
                market_price DOUBLE,
                unrealized_pnl DOUBLE,
                realized_pnl DOUBLE,
                currency VARCHAR(3) DEFAULT 'USD',
                PRIMARY KEY (run_id, timestamp, symbol)
            )
        )");

        // Risk events
        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS risk_events (
                run_id UUID NOT NULL,
                timestamp TIMESTAMPTZ NOT NULL,
                event_type VARCHAR NOT NULL,
                limit_name VARCHAR NOT NULL,
                symbol VARCHAR,
                current_value DOUBLE,
                limit_value DOUBLE,
                breach_amount DOUBLE,
                severity VARCHAR,
                action_taken VARCHAR,
                metadata JSON,
                PRIMARY KEY (run_id, timestamp, limit_name, event_type)
            )
        )");

        // Strategy signals
        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS signals (
                run_id UUID NOT NULL,
                timestamp TIMESTAMPTZ NOT NULL,
                signal_name VARCHAR NOT NULL,
                symbol VARCHAR NOT NULL,
                direction VARCHAR,
                strength DOUBLE,
                confidence DOUBLE,
                features JSON,
                metadata JSON,
                PRIMARY KEY (run_id, timestamp, signal_name, symbol)
            )
        )");

        // Calculated curves (point-in-time)
        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS calculated_curves (
                run_id UUID NOT NULL,
                as_of TIMESTAMPTZ NOT NULL,
                curve_type VARCHAR NOT NULL,
                currency VARCHAR(3) NOT NULL,
                pillar_dates JSON,
                zero_rates JSON,
                discount_factors JSON,
                forward_rates JSON,
                interpolation VARCHAR,
                success BOOLEAN DEFAULT true,
                error_message VARCHAR,
                PRIMARY KEY (run_id, as_of, curve_type, currency)
            )
        )");

        // Calculated basis
        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS calculated_basis (
                run_id UUID NOT NULL,
                as_of TIMESTAMPTZ NOT NULL,
                basis_type VARCHAR NOT NULL,
                instrument_id VARCHAR NOT NULL,
                futures_code VARCHAR,
                gross_basis DOUBLE,
                net_basis DOUBLE,
                gross_basis_32nds DOUBLE,
                net_basis_32nds DOUBLE,
                implied_repo DOUBLE,
                conversion_factor DOUBLE,
                carry DOUBLE,
                is_ctd BOOLEAN DEFAULT false,
                ctd_rank INTEGER,
                metadata JSON,
                PRIMARY KEY (run_id, as_of, basis_type, instrument_id)
            )
        )");

        // Analytics summaries (for viz)
        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS analytics_summary (
                run_id UUID NOT NULL,
                metric_name VARCHAR NOT NULL,
                metric_value DOUBLE NOT NULL,
                as_of TIMESTAMPTZ NOT NULL,
                category VARCHAR,
                metadata JSON,
                PRIMARY KEY (run_id, metric_name, as_of)
            )
        )");

        spdlog::debug("Output schema initialized");
    }
};

// ============================================================================
// Legacy Database class for backward compatibility
// Maps to OutputDataStore
// ============================================================================

// Note: The original Database class behavior is preserved through inheritance.
// For new code, use InputDataStore for reading market data and OutputDataStore
// for writing calculation results.

// ============================================================================
// Run Management (operates on OutputDataStore)
// ============================================================================

struct RunInfo {
    string run_id;
    string name;
    string status;
    string git_commit;
};

auto start_run(OutputDataStore& db, string_view name, string_view git_commit = "",
               string_view config_json = "{}") -> string {
    auto result = db.execute(
        R"(
        INSERT INTO runs (name, git_commit, config)
        VALUES ($1, $2, $3)
        RETURNING run_id::VARCHAR
        )",
        string{name}, string{git_commit}, string{config_json});

    if (!result || result->HasError()) {
        spdlog::error("Failed to start run: {}", result ? result->GetError() : "null result");
        return "";
    }

    auto chunk = result->Fetch();
    if (chunk && chunk->size() > 0) {
        return chunk->GetValue(0, 0).ToString();
    }
    return "";
}

// Overload for backward compatibility with Database class
auto start_run(Database& db, string_view name, string_view git_commit = "",
               string_view config_json = "{}") -> string {
    auto result = db.execute(
        R"(
        INSERT INTO runs (name, git_commit, config)
        VALUES ($1, $2, $3)
        RETURNING run_id::VARCHAR
        )",
        string{name}, string{git_commit}, string{config_json});

    if (!result || result->HasError()) {
        spdlog::error("Failed to start run: {}", result ? result->GetError() : "null result");
        return "";
    }

    auto chunk = result->Fetch();
    if (chunk && chunk->size() > 0) {
        return chunk->GetValue(0, 0).ToString();
    }
    return "";
}

void complete_run(OutputDataStore& db, string_view run_id) {
    db.execute("UPDATE runs SET status = 'completed' WHERE run_id = $1", string{run_id});
}

void complete_run(Database& db, string_view run_id) {
    db.execute("UPDATE runs SET status = 'completed' WHERE run_id = $1", string{run_id});
}

void fail_run(OutputDataStore& db, string_view run_id, string_view error_msg = "") {
    db.execute("UPDATE runs SET status = 'failed', description = $1 WHERE run_id = $2",
               string{error_msg}, string{run_id});
}

void fail_run(Database& db, string_view run_id, string_view error_msg = "") {
    db.execute("UPDATE runs SET status = 'failed', description = $1 WHERE run_id = $2",
               string{error_msg}, string{run_id});
}

// ============================================================================
// Data Store Factory
// ============================================================================

struct DataStores {
    unique_ptr<InputDataStore> input;
    unique_ptr<OutputDataStore> output;
};

auto create_data_stores(const Config& config) -> DataStores {
    return {
        .input = std::make_unique<InputDataStore>(config.database.input_path),
        .output = std::make_unique<OutputDataStore>(config.database.output_path),
    };
}

auto create_in_memory_stores() -> DataStores {
    return {
        .input = std::make_unique<InputDataStore>(),
        .output = std::make_unique<OutputDataStore>(),
    };
}

} // namespace finkit::data
