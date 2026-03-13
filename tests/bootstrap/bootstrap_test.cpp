#include <gtest/gtest.h>
#include <ql/quantlib.hpp>

import finkit.bootstrap;

namespace {

using namespace finkit::bootstrap;

TEST(FedProbabilityTest, NegativeMoveBrackets) {
    // meeting Jan 29, current rate 4.50%, futures price gives -62bp move
    // implied_avg = (100 - 95.54) / 100 = 0.0446
    // post_rate = (0.0446 * 31 - 29 * 0.045) / 2 = 0.0388
    // expected_move = (0.0388 - 0.045) * 10000 = -62bp
    ql::Date meeting(29, ql::January, 2025);
    ql::Date valuation(2, ql::January, 2025);
    double current_rate = 0.045;
    double futures_price = 95.54;

    auto result = calculate_fed_probability(futures_price, meeting, current_rate, valuation);

    EXPECT_NEAR(result.expected_move_bps, -62.0, 0.5);
    EXPECT_EQ(result.lower_move_bps, -75);
    EXPECT_EQ(result.upper_move_bps, -50);
    EXPECT_NEAR(result.prob_upper, 0.52, 0.02);
    EXPECT_NEAR(result.prob_lower, 0.48, 0.02);
}

TEST(FedProbabilityTest, PositiveMoveBrackets) {
    ql::Date meeting(15, ql::January, 2025);
    ql::Date valuation(2, ql::January, 2025);
    double current_rate = 0.045;
    double futures_price = 95.303871;

    auto result = calculate_fed_probability(futures_price, meeting, current_rate, valuation);

    EXPECT_NEAR(result.expected_move_bps, 38.0, 0.5);
    EXPECT_EQ(result.lower_move_bps, 25);
    EXPECT_EQ(result.upper_move_bps, 50);
    EXPECT_NEAR(result.prob_upper, 0.52, 0.02);
    EXPECT_NEAR(result.prob_lower, 0.48, 0.02);
}

TEST(FedProbabilityTest, ExactBoundaryMove) {
    ql::Date meeting(15, ql::January, 2025);
    ql::Date valuation(2, ql::January, 2025);
    double current_rate = 0.045;
    double futures_price = 95.629032;

    auto result = calculate_fed_probability(futures_price, meeting, current_rate, valuation);

    EXPECT_NEAR(result.expected_move_bps, -25.0, 0.5);
    EXPECT_EQ(result.lower_move_bps, -25);
    EXPECT_EQ(result.upper_move_bps, 0);
    EXPECT_NEAR(result.prob_lower, 1.0, 0.02);
    EXPECT_NEAR(result.prob_upper, 0.0, 0.02);
}

TEST(FedProbabilityTest, ZeroMoveNoChange) {
    ql::Date meeting(15, ql::January, 2025);
    ql::Date valuation(2, ql::January, 2025);
    double current_rate = 0.045;
    double futures_price = 95.50;

    auto result = calculate_fed_probability(futures_price, meeting, current_rate, valuation);

    EXPECT_NEAR(result.expected_move_bps, 0.0, 0.5);
    EXPECT_EQ(result.lower_move_bps, 0);
    EXPECT_EQ(result.upper_move_bps, 25);
    EXPECT_NEAR(result.prob_lower, 1.0, 0.02);
    EXPECT_NEAR(result.prob_upper, 0.0, 0.02);
}

} // namespace
