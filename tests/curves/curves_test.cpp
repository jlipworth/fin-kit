#include <gtest/gtest.h>

import finkit.curves;

namespace {

using namespace finkit::curves;

TEST(CurvesTest, G10PairsReturnsNinePairs) {
    auto pairs = g10_pairs();
    EXPECT_EQ(pairs.size(), 9);
}

TEST(CurvesTest, CurrencyToString) {
    EXPECT_EQ(currency_to_string(Currency::USD), "USD");
    EXPECT_EQ(currency_to_string(Currency::EUR), "EUR");
    EXPECT_EQ(currency_to_string(Currency::JPY), "JPY");
}

// TODO: Add tests once implementations are complete
// TEST(CurvesTest, BootstrapSOFRCurve) { }
// TEST(CurvesTest, CalculateCIPBasis) { }

} // namespace
