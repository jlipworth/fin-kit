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
                store_->execute("DELETE FROM runs WHERE run_id = $1::uuid", run_id_);
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
    auto result = store_->query("SELECT table_name FROM information_schema.tables "
                                "WHERE table_schema = 'public' AND table_name IN "
                                "('runs', 'trades', 'equity', 'calculated_basis') "
                                "ORDER BY table_name");

    ASSERT_GE(result.size(), 4);
}

TEST_F(DataStoreTest, EnsureSchemaCreatesInputTables) {
    store_->ensure_schema();

    auto result =
        store_->query("SELECT table_name FROM information_schema.tables "
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

    auto result = store_->execute("SELECT status FROM runs WHERE run_id = $1::uuid", run_id_);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0][0].as<std::string>(), "completed");
}

TEST_F(DataStoreTest, FailRunUpdatesStatusAndDescription) {
    store_->ensure_schema();
    run_id_ = start_run(*store_, "test_fail");

    fail_run(*store_, run_id_, "something broke");

    auto result =
        store_->execute("SELECT status, description FROM runs WHERE run_id = $1::uuid", run_id_);
    ASSERT_EQ(result.size(), 1);
    EXPECT_EQ(result[0]["status"].as<std::string>(), "failed");
    EXPECT_EQ(result[0]["description"].as<std::string>(), "something broke");
}

TEST_F(DataStoreTest, ParameterizedExecuteWorks) {
    store_->ensure_schema();

    store_->execute("INSERT INTO rates_sofr_fixings (fixing_date, rate, source) "
                    "VALUES ($1::date, $2, $3)",
                    std::string{"2025-01-02"}, 4.55, std::string{"test"});

    auto result =
        store_->query("SELECT rate FROM rates_sofr_fixings WHERE fixing_date = '2025-01-02'");
    ASSERT_EQ(result.size(), 1);
    EXPECT_NEAR(result[0][0].as<double>(), 4.55, 1e-6);

    // Cleanup
    store_->execute("DELETE FROM rates_sofr_fixings WHERE fixing_date = $1::date",
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
    auto result = store_->query("SELECT COUNT(*) FROM timeseries_ohlcv");
    EXPECT_GE(result[0][0].as<int64_t>(), 0);
}

} // namespace
