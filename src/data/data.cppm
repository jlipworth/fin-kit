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
    if (auto v = std::getenv(primary))
        return string{v};
    if (auto v = std::getenv(fallback))
        return string{v};
    return std::nullopt;
}

auto load_database_config_from_env() -> optional<DatabaseConfig> {
    auto host = env_or("TSDB_HOST", "POSTGRES_HOST");
    if (!host)
        return std::nullopt;

    DatabaseConfig cfg;
    cfg.host = *host;
    if (auto p = env_or("TSDB_PORT", "POSTGRES_PORT")) {
        try {
            cfg.port = std::stoi(*p);
        } catch (...) {
            spdlog::warn("Invalid port '{}', using default 5432", *p);
        }
    }
    cfg.database = env_or("TSDB_DATABASE", "POSTGRES_DB").value_or("finkit");
    cfg.user = env_or("TSDB_USER", "POSTGRES_USER").value_or("");
    cfg.password = env_or("TSDB_PASSWORD", "POSTGRES_PASSWORD").value_or("");
    return cfg;
}

// Quote a value for a libpq connection string (single-quote + escape)
auto pq_quote(const string& val) -> string {
    if (val.find_first_of(" '\\") == string::npos)
        return val;
    string out = "'";
    for (char c : val) {
        if (c == '\'' || c == '\\')
            out += '\\';
        out += c;
    }
    out += '\'';
    return out;
}

auto build_connection_string(const DatabaseConfig& cfg) -> string {
    if (!cfg.connection_string.empty())
        return cfg.connection_string;
    return "host=" + pq_quote(cfg.host) + " port=" + std::to_string(cfg.port) +
           " dbname=" + pq_quote(cfg.database) + " user=" + pq_quote(cfg.user) +
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
    if (!fs::exists(path))
        return std::nullopt;

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
    explicit DataStore(const DatabaseConfig& config) : conn_{build_connection_string(config)} {
        spdlog::info("Connected to PostgreSQL: {}:{}/{}", config.host, config.port,
                     config.database);
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
    template <typename... Args> auto execute(string_view sql, Args&&... args) -> pqxx::result {
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
    auto result = db.execute("INSERT INTO runs (name, git_commit, config) "
                             "VALUES ($1, $2, $3::jsonb) RETURNING run_id::text",
                             string{name}, string{git_commit}, string{config_json});

    if (result.empty()) {
        spdlog::error("Failed to start run");
        return "";
    }
    return result[0][0].as<string>();
}

void complete_run(DataStore& db, string_view run_id) {
    db.execute("UPDATE runs SET status = 'completed' WHERE run_id = $1::uuid", string{run_id});
}

void fail_run(DataStore& db, string_view run_id, string_view error_msg = "") {
    db.execute("UPDATE runs SET status = 'failed', description = $1 WHERE run_id = $2::uuid",
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
