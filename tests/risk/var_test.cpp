#include <cmath>
#include <gtest/gtest.h>
#include <optional>
#include <string>
#include <vector>

import finkit.risk;
import finkit.stats;
import finkit.trading;
import finkit.types;

namespace {

using namespace finkit::risk;
using namespace finkit::stats;
using namespace finkit::trading;
using namespace finkit::types;

// Shared covariance fixture: Sigma = [[4e-4, 1e-4], [1e-4, 9e-4]],
// symbols {"A","B"}, observations 250; exposures w = {100, 200}.
auto make_fixture_cov() -> CovarianceMatrix {
    return CovarianceMatrix{.matrix = {{4e-4, 1e-4}, {1e-4, 9e-4}},
                            .symbols = {"A", "B"},
                            .observations = 250,
                            .lookback_days = std::nullopt};
}

// ============================================================================
// Historical VaR Tests
// ============================================================================

TEST(RiskTest, HistoricalVarBasicQuantile) {
    std::vector<double> pnl = {-5, -4, -3, -2, -1, 0, 1, 2, 3, 4};
    auto r = historical_var(pnl, VaRConfig{.confidence = 0.80});
    EXPECT_TRUE(r.success);
    EXPECT_EQ(r.observations, 10u);
    EXPECT_EQ(r.method, VaRMethod::Historical);
    EXPECT_DOUBLE_EQ(r.var, 4.0);
    EXPECT_DOUBLE_EQ(r.expected_shortfall, 4.5);
}

TEST(RiskTest, HistoricalVarHighConfidence) {
    std::vector<double> pnl = {-5, -4, -3, -2, -1, 0, 1, 2, 3, 4};
    auto r = historical_var(pnl, VaRConfig{.confidence = 0.99});
    EXPECT_TRUE(r.success);
    EXPECT_DOUBLE_EQ(r.var, 5.0);
    EXPECT_DOUBLE_EQ(r.expected_shortfall, 5.0);
}

TEST(RiskTest, HistoricalVarHorizonScaling) {
    std::vector<double> pnl = {-5, -4, -3, -2, -1, 0, 1, 2, 3, 4};
    auto r = historical_var(pnl, VaRConfig{.confidence = 0.80, .horizon_days = 4.0});
    EXPECT_TRUE(r.success);
    EXPECT_DOUBLE_EQ(r.var, 8.0);
    EXPECT_DOUBLE_EQ(r.expected_shortfall, 9.0);
}

// (1-c)*n mathematically an integer: fp double-rounding (e.g. 1-0.99 ->
// 0.010000000000000009, times 100 -> just above 1) must not bump the tail
// index k up by one — that would give a systematically less conservative VaR.
TEST(RiskTest, HistoricalVarExactQuantileBoundary) {
    std::vector<double> pnl(100);
    for (int i = 0; i < 100; ++i) {
        pnl[static_cast<size_t>(i)] = -100.0 + i; // -100, -99, ..., -1
    }
    auto r99 = historical_var(pnl, VaRConfig{.confidence = 0.99});
    EXPECT_TRUE(r99.success);
    // k = ceil(0.01 * 100) = 1 -> VaR/ES are the single worst observation.
    EXPECT_DOUBLE_EQ(r99.var, 100.0);
    EXPECT_DOUBLE_EQ(r99.expected_shortfall, 100.0);

    auto r95 = historical_var(pnl, VaRConfig{.confidence = 0.95});
    EXPECT_TRUE(r95.success);
    // k = ceil(0.05 * 100) = 5 -> 5th worst = -96; ES = mean of 5 worst = 98.
    EXPECT_DOUBLE_EQ(r95.var, 96.0);
    EXPECT_DOUBLE_EQ(r95.expected_shortfall, 98.0);
}

TEST(RiskTest, HistoricalVarExactQuantileBoundarySmallSample) {
    std::vector<double> pnl(20);
    for (int i = 0; i < 20; ++i) {
        pnl[static_cast<size_t>(i)] = -20.0 + i; // -20, -19, ..., -1
    }
    auto r = historical_var(pnl, VaRConfig{.confidence = 0.95});
    EXPECT_TRUE(r.success);
    // k = ceil(0.05 * 20) = 1 -> worst observation.
    EXPECT_DOUBLE_EQ(r.var, 20.0);
    EXPECT_DOUBLE_EQ(r.expected_shortfall, 20.0);
}

TEST(RiskTest, HistoricalVarInsufficientData) {
    std::vector<double> pnl = {1.0};
    auto r = historical_var(pnl);
    EXPECT_FALSE(r.success);
    EXPECT_FALSE(r.error_message.empty());
}

TEST(RiskTest, HistoricalVarAllGainsIsNegative) {
    std::vector<double> pnl = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    auto r = historical_var(pnl, VaRConfig{.confidence = 0.90});
    EXPECT_TRUE(r.success);
    EXPECT_DOUBLE_EQ(r.var, -1.0);
}

// ============================================================================
// Normal Quantile Tests
// ============================================================================

TEST(RiskTest, NormalQuantileValues) {
    EXPECT_NEAR(normal_quantile(0.5), 0.0, 1e-9);
    EXPECT_NEAR(normal_quantile(0.95), 1.6448536270, 1e-6);
    EXPECT_NEAR(normal_quantile(0.99), 2.3263478740, 1e-6);
    EXPECT_NEAR(normal_quantile(0.05), -1.6448536270, 1e-6);
    EXPECT_TRUE(std::isnan(normal_quantile(0.0)));
}

// ============================================================================
// Parametric VaR Tests
// ============================================================================

TEST(RiskTest, ParametricVarTwoAsset95) {
    auto cov = make_fixture_cov();
    std::vector<double> w = {100.0, 200.0};
    auto r = parametric_var(w, cov, VaRConfig{.confidence = 0.95});
    EXPECT_TRUE(r.success);
    EXPECT_EQ(r.observations, 250u);
    EXPECT_NEAR(r.var, 10.9107, 1e-3);
    EXPECT_NEAR(r.expected_shortfall, 13.6825, 1e-3);
}

TEST(RiskTest, ParametricVarSingleAsset) {
    CovarianceMatrix cov{
        .matrix = {{4e-4}}, .symbols = {"A"}, .observations = 250, .lookback_days = std::nullopt};
    std::vector<double> w = {1000.0};
    auto r = parametric_var(w, cov, VaRConfig{.confidence = 0.95});
    EXPECT_TRUE(r.success);
    EXPECT_NEAR(r.var, 32.8971, 1e-3);
    EXPECT_NEAR(r.expected_shortfall, 41.2543, 2e-3);
}

TEST(RiskTest, ParametricVarHorizonScaling) {
    auto cov = make_fixture_cov();
    std::vector<double> w = {100.0, 200.0};
    auto r = parametric_var(w, cov, VaRConfig{.confidence = 0.95, .horizon_days = 10.0});
    EXPECT_TRUE(r.success);
    EXPECT_NEAR(r.var, 34.5027, 3e-3);
}

TEST(RiskTest, ParametricVarSizeMismatch) {
    auto cov = make_fixture_cov();
    std::vector<double> w = {100.0};
    auto r = parametric_var(w, cov);
    EXPECT_FALSE(r.success);
}

// ============================================================================
// Monte Carlo VaR Tests
// ============================================================================

TEST(RiskTest, MonteCarloVarConvergesToParametric) {
    auto cov = make_fixture_cov();
    std::vector<double> w = {100.0, 200.0};
    auto r = monte_carlo_var(
        w, cov, VaRConfig{.confidence = 0.95, .num_simulations = 200000, .seed = 42});
    EXPECT_TRUE(r.success);
    EXPECT_NEAR(r.var, 10.9107, 0.35);
    EXPECT_NEAR(r.expected_shortfall, 13.6825, 0.5);
}

TEST(RiskTest, MonteCarloVarDeterministic) {
    auto cov = make_fixture_cov();
    std::vector<double> w = {100.0, 200.0};
    VaRConfig cfg{.confidence = 0.95, .num_simulations = 10000, .seed = 7};
    auto r1 = monte_carlo_var(w, cov, cfg);
    auto r2 = monte_carlo_var(w, cov, cfg);
    EXPECT_TRUE(r1.success);
    EXPECT_DOUBLE_EQ(r1.var, r2.var);
    EXPECT_DOUBLE_EQ(r1.expected_shortfall, r2.expected_shortfall);
}

TEST(RiskTest, MonteCarloVarRejectsNonPositiveDefinite) {
    CovarianceMatrix cov{.matrix = {{1e-4, 2e-4}, {2e-4, 1e-4}},
                         .symbols = {"A", "B"},
                         .observations = 250,
                         .lookback_days = std::nullopt};
    std::vector<double> w = {100.0, 200.0};
    auto r = monte_carlo_var(w, cov, VaRConfig{.confidence = 0.95});
    EXPECT_FALSE(r.success);
    EXPECT_NE(r.error_message.find("positive definite"), std::string::npos);
}

} // namespace
