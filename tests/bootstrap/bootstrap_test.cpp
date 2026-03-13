#include <gtest/gtest.h>
#include <optional>
#include <ql/quantlib.hpp>

import finkit.bootstrap;
import finkit.types;

namespace {

using namespace finkit::bootstrap;
using finkit::types::FOMCMeeting;

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

TEST(FedProbabilityTest, CumulativeChainingPreservesTerminalRate) {
    // Two meetings: Jan 15 and Mar 19
    // The key invariant: running_rate chains through implied_rate_post
    ql::Date meeting1(15, ql::January, 2025);
    ql::Date meeting2(19, ql::March, 2025);
    ql::Date valuation(2, ql::January, 2025);
    double current_rate = 0.045;

    // Futures prices implying ~-12bp for each meeting
    double futures_price1 = 95.5609;
    double futures_price2 = 95.60;

    FOMCMeeting m1{meeting1, false, std::nullopt};
    FOMCMeeting m2{meeting2, false, std::nullopt};

    auto result = calculate_cumulative_fed_probabilities({futures_price1, futures_price2}, {m1, m2},
                                                         current_rate, valuation);

    EXPECT_EQ(result.by_meeting.size(), 2);
    // Second meeting should use first meeting's implied_rate_post as its current rate
    EXPECT_NEAR(result.by_meeting[1].current_target_rate, result.by_meeting[0].implied_rate_post,
                1e-10);
    // Terminal rate should equal second meeting's implied_rate_post
    EXPECT_NEAR(result.implied_terminal_rate, result.by_meeting[1].implied_rate_post, 1e-10);
}

} // namespace
