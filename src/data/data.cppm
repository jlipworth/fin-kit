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
    fs::path path{"~/.finkit/data.db"};
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
            if (auto p = db->get("path")) {
                cfg.database.path = p->value_or(string{"~/.finkit/data.db"});
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

        cfg.database.path = finkit::core::expand_path(cfg.database.path);
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
    cfg.database.path = finkit::core::expand_path(cfg.database.path);
    return cfg;
}

// ============================================================================
// Database Connection
// ============================================================================

class Database {
public:
    explicit Database(const fs::path& path) : path_{path} {
        if (path_.has_parent_path()) {
            fs::create_directories(path_.parent_path());
        }

        db_ = std::make_unique<duckdb::DuckDB>(path_.string());
        conn_ = std::make_unique<duckdb::Connection>(*db_);

        init_schema();
        spdlog::info("Database opened: {}", path_.string());
    }

    Database() : path_{":memory:"} {
        db_ = std::make_unique<duckdb::DuckDB>(nullptr);
        conn_ = std::make_unique<duckdb::Connection>(*db_);
        init_schema();
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

private:
    void init_schema() {
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

        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS trades (
                run_id UUID,
                trade_id INTEGER,
                timestamp TIMESTAMPTZ,
                symbol VARCHAR,
                side VARCHAR,
                quantity DOUBLE,
                price DOUBLE,
                commission DOUBLE DEFAULT 0,
                signal_name VARCHAR,
                PRIMARY KEY (run_id, trade_id)
            )
        )");

        conn_->Query(R"(
            CREATE TABLE IF NOT EXISTS equity (
                run_id UUID,
                timestamp TIMESTAMPTZ,
                nav DOUBLE,
                cash DOUBLE,
                positions_value DOUBLE,
                drawdown DOUBLE,
                PRIMARY KEY (run_id, timestamp)
            )
        )");

        spdlog::debug("Schema initialized");
    }

    fs::path path_;
    unique_ptr<duckdb::DuckDB> db_;
    unique_ptr<duckdb::Connection> conn_;
    std::mutex mutex_;
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

auto start_run(Database& db, string_view name, string_view git_commit = "",
               string_view config_json = "{}") -> string {
    auto result = db.query(fmt::format(
        R"(
        INSERT INTO runs (name, git_commit, config)
        VALUES ('{}', '{}', '{}')
        RETURNING run_id::VARCHAR
        )",
        name, git_commit, config_json));

    if (result->HasError()) {
        spdlog::error("Failed to start run: {}", result->GetError());
        return "";
    }

    auto chunk = result->Fetch();
    if (chunk && chunk->size() > 0) {
        return chunk->GetValue(0, 0).ToString();
    }
    return "";
}

void complete_run(Database& db, string_view run_id) {
    db.query(fmt::format("UPDATE runs SET status = 'completed' WHERE run_id = '{}'", run_id));
}

void fail_run(Database& db, string_view run_id, string_view error_msg = "") {
    db.query(
        fmt::format("UPDATE runs SET status = 'failed', description = '{}' WHERE run_id = '{}'",
                    error_msg, run_id));
}

} // namespace finkit::data
