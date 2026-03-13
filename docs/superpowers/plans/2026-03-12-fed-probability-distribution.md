# Fed Probability Distribution Redesign — Implementation Plan

> **For agentic workers:** REQUIRED: Use superpowers:subagent-driven-development (if subagents available) or superpowers:executing-plans to implement this plan. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the clamped three-outcome probability model in `FedProbabilityResult` with a two-outcome bracket interpolation model that handles any move size.

**Architecture:** Update the struct and calculation in `src/bootstrap/bootstrap.cppm`, add tests in a new `tests/bootstrap/` directory, then update downstream documentation.

**Tech Stack:** C++20 modules, GoogleTest, QuantLib, CMake

**Spec:** `docs/superpowers/specs/2026-03-12-fed-probability-distribution-design.md`

---

## File Structure

| File | Action | Responsibility |
|------|--------|----------------|
| `src/bootstrap/bootstrap.cppm` | Modify | Update `FedProbabilityResult` struct and `calculate_fed_probability()` |
| `tests/bootstrap/bootstrap_test.cpp` | Create | Unit tests for probability bracket math |
| `tests/bootstrap/CMakeLists.txt` | Create | Build config for bootstrap tests |
| `tests/CMakeLists.txt` | Modify | Add `bootstrap` subdirectory |
| `docs/modules/bootstrap.md` | Modify | Update field documentation |
| `docs/design/fed-prediction-system.md` | Modify | Update struct, SQL, and algorithm section |
| `docs/design/fed-prediction-system.html` | Delete | Redundant untracked file |

---

## Task 1: Create bootstrap test scaffold

**Files:**
- Create: `tests/bootstrap/CMakeLists.txt`
- Create: `tests/bootstrap/bootstrap_test.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Create `tests/bootstrap/CMakeLists.txt`**

```cmake
add_executable(bootstrap_test bootstrap_test.cpp)

target_link_libraries(bootstrap_test PRIVATE finkit::bootstrap GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(bootstrap_test)
```

- [ ] **Step 2: Create `tests/bootstrap/bootstrap_test.cpp` with a placeholder test**

```cpp
#include <gtest/gtest.h>

import finkit.bootstrap;

namespace {

using namespace finkit::bootstrap;

TEST(BootstrapTest, Placeholder) {
    EXPECT_TRUE(true);
}

} // namespace
```

- [ ] **Step 3: Add bootstrap subdirectory to `tests/CMakeLists.txt`**

Add `add_subdirectory(bootstrap)` after the existing `add_subdirectory(backtest)` line.

- [ ] **Step 4: Build and run to verify scaffold works**

Run: `cmake --build build/build/Release && ctest --test-dir build/build/Release -R BootstrapTest --output-on-failure`
Expected: 1 test passes.

- [ ] **Step 5: Commit**

```bash
git add tests/bootstrap/CMakeLists.txt tests/bootstrap/bootstrap_test.cpp tests/CMakeLists.txt
git commit -m "test: add bootstrap test scaffold"
```

---

## Task 2: Write failing tests for bracket probability math

**Files:**
- Modify: `tests/bootstrap/bootstrap_test.cpp`

- [ ] **Step 1: Write test for negative move (-62bp implied)**

Uses a futures price that produces an expected move of approximately -62bp.

Setup: meeting on Jan 29 (31 days in month, `days_before = 29`, `days_after = 2`). Current rate = 4.50%. We need `implied_avg` such that `post_rate` yields ~-62bp move.

Working backward: `post_rate = 0.045 - 0.0062 = 0.0388`. `implied_avg = (29 * 0.045 + 2 * 0.0388) / 31 = (1.305 + 0.0776) / 31 = 0.0446`. `futures_price = 100 - 4.46 = 95.54`.

```cpp
TEST(FedProbabilityTest, NegativeMoveBrackets) {
    // Setup: meeting Jan 29, current rate 4.50%, futures price chosen to give -62bp move
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
```

- [ ] **Step 2: Write test for positive move (+38bp implied)**

Setup: meeting on Jan 15 (31 days in month, `days_before = 15`, `days_after = 16`). Current rate = 4.50%.

Working backward: `post_rate = 0.045 + 0.0038 = 0.0488`. `implied_avg = (15 * 0.045 + 16 * 0.0488) / 31 = (0.675 + 0.7808) / 31 = 0.04696129...`. `futures_price = 100 - 4.696129 = 95.303871`.

```cpp
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
```

- [ ] **Step 3: Write test for exact 25bp boundary**

Setup: meeting Jan 15, current rate 4.50%, post_rate = 4.25% (exactly -25bp).

`implied_avg = (15 * 0.045 + 16 * 0.0425) / 31 = (0.675 + 0.68) / 31 = 0.04370967...`. `futures_price = 95.629032...`.

```cpp
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
```

- [ ] **Step 4: Write test for zero move (no change)**

Setup: futures price implies the same rate as current.

`implied_avg = (15 * 0.045 + 16 * 0.045) / 31 = 0.045`. `futures_price = 95.50`.

```cpp
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
```

- [ ] **Step 5: Build and run to verify tests fail**

Run: `cmake --build build/build/Release && ctest --test-dir build/build/Release -R FedProbability --output-on-failure`
Expected: FAIL — `lower_move_bps` and `upper_move_bps` are not members of `FedProbabilityResult`.

- [ ] **Step 6: Commit**

```bash
git add tests/bootstrap/bootstrap_test.cpp
git commit -m "test: add failing tests for bracket probability model"
```

---

## Task 3: Update struct and calculation

**Files:**
- Modify: `src/bootstrap/bootstrap.cppm:307-379`

- [ ] **Step 1: Update `FedProbabilityResult` struct (lines 307-317)**

Replace:
```cpp
struct FedProbabilityResult {
    ql::Date meeting_date;
    double current_target_rate; // Current Fed target rate
    double implied_rate_pre;    // Implied rate before meeting
    double implied_rate_post;   // Implied rate after meeting
    double prob_hike_25bp;      // Probability of 25bp hike
    double prob_cut_25bp;       // Probability of 25bp cut
    double prob_no_change;      // Probability of no change
    double expected_move_bps;   // Expected move in bps
    int days_to_meeting;
};
```

With:
```cpp
struct FedProbabilityResult {
    ql::Date meeting_date;
    double current_target_rate; // Current Fed target rate
    double implied_rate_pre;    // Implied rate before meeting
    double implied_rate_post;   // Implied rate after meeting
    double expected_move_bps;   // Expected move in bps
    int days_to_meeting;
    int lower_move_bps;         // Lower 25bp bracket (floor)
    double prob_lower;          // Probability of lower bracket outcome
    int upper_move_bps;         // Upper 25bp bracket (lower + 25)
    double prob_upper;          // Probability of upper bracket outcome
};
```

- [ ] **Step 2: Replace the clamp block (lines 364-379)**

Replace:
```cpp
    // Calculate probabilities assuming 25bp increments
    // If expected move is positive -> leaning hike
    // If expected move is negative -> leaning cut
    double move_in_25bp_units = result.expected_move_bps / 25.0;

    if (move_in_25bp_units >= 0) {
        // Probability of hike = move / 25bp (capped at [0, 1])
        result.prob_hike_25bp = std::clamp(move_in_25bp_units, 0.0, 1.0);
        result.prob_cut_25bp = 0.0;
        result.prob_no_change = 1.0 - result.prob_hike_25bp;
    } else {
        // Probability of cut = abs(move) / 25bp (capped at [0, 1])
        result.prob_cut_25bp = std::clamp(-move_in_25bp_units, 0.0, 1.0);
        result.prob_hike_25bp = 0.0;
        result.prob_no_change = 1.0 - result.prob_cut_25bp;
    }
```

With:
```cpp
    // Bracket the expected move between two adjacent 25bp outcomes
    int lower = static_cast<int>(std::floor(result.expected_move_bps / 25.0)) * 25;
    int upper = lower + 25;
    double p_upper = (result.expected_move_bps - lower) / 25.0;

    result.lower_move_bps = lower;
    result.upper_move_bps = upper;
    result.prob_upper = p_upper;
    result.prob_lower = 1.0 - p_upper;
```

- [ ] **Step 3: Build and run tests**

Run: `cmake --build build/build/Release && ctest --test-dir build/build/Release -R FedProbability --output-on-failure`
Expected: All 4 tests pass.

- [ ] **Step 4: Commit**

```bash
git add src/bootstrap/bootstrap.cppm
git commit -m "feat: replace clamped probability with bracket interpolation model"
```

---

## Task 4: Verify cumulative function still works

**Files:**
- Modify: `tests/bootstrap/bootstrap_test.cpp`

- [ ] **Step 1: Write regression test for cumulative chaining**

```cpp
TEST(FedProbabilityTest, CumulativeChainingPreservesTerminalRate) {
    // Two meetings: Jan 15 and Mar 19, both with same futures price
    // The key invariant: running_rate chains through implied_rate_post
    ql::Date meeting1(15, ql::January, 2025);
    ql::Date meeting2(19, ql::March, 2025);
    ql::Date valuation(2, ql::January, 2025);
    double current_rate = 0.045;

    // Futures prices implying ~-12bp for each meeting
    // meeting1: implied_avg = (15*0.045 + 16*0.0438) / 31 = 0.04439...
    // futures_price1 = 100 - 4.439... = 95.5609...
    double futures_price1 = 95.5609;
    double futures_price2 = 95.60;

    FOMCMeeting m1{meeting1, false, std::nullopt};
    FOMCMeeting m2{meeting2, false, std::nullopt};

    auto result = calculate_cumulative_fed_probabilities(
        {futures_price1, futures_price2}, {m1, m2}, current_rate, valuation);

    EXPECT_EQ(result.by_meeting.size(), 2);
    // Second meeting should use first meeting's implied_rate_post as its current rate
    EXPECT_NEAR(result.by_meeting[1].current_target_rate,
                result.by_meeting[0].implied_rate_post, 1e-10);
    // Terminal rate should equal second meeting's implied_rate_post
    EXPECT_NEAR(result.implied_terminal_rate,
                result.by_meeting[1].implied_rate_post, 1e-10);
}
```

- [ ] **Step 2: Build and run**

Run: `cmake --build build/build/Release && ctest --test-dir build/build/Release -R FedProbability --output-on-failure`
Expected: All 5 tests pass.

- [ ] **Step 3: Commit**

```bash
git add tests/bootstrap/bootstrap_test.cpp
git commit -m "test: add cumulative chaining regression test"
```

---

## Task 5: Update documentation

**Files:**
- Modify: `docs/modules/bootstrap.md:75-78`
- Modify: `docs/design/fed-prediction-system.md` (struct, SQL, algorithm)
- Delete: `docs/design/fed-prediction-system.html`

- [ ] **Step 1: Update `docs/modules/bootstrap.md` lines 75-78**

Replace:
```markdown
Returns `FedProbabilityResult` with:
- `implied_rate_pre`, `implied_rate_post`
- `prob_hike_25bp`, `prob_cut_25bp`, `prob_no_change`
- `expected_move_bps`
```

With:
```markdown
Returns `FedProbabilityResult` with:
- `implied_rate_pre`, `implied_rate_post`
- `expected_move_bps`
- `lower_move_bps`, `prob_lower` — lower 25bp bracket and its probability
- `upper_move_bps`, `prob_upper` — upper 25bp bracket and its probability
```

- [ ] **Step 2: Update `FedPrediction` struct in `docs/design/fed-prediction-system.md` (lines 151-163)**

Replace:
```cpp
struct FedPrediction {
    ql::Date as_of_date;
    ql::Date meeting_date;
    double current_target_rate;
    string ff_futures_code;
    double ff_futures_price;
    double implied_rate_post;
    double expected_move_bps;
    double prob_hike_25bp;
    double prob_cut_25bp;
    double prob_no_change;
    int days_to_meeting;
};
```

With:
```cpp
struct FedPrediction {
    ql::Date as_of_date;
    ql::Date meeting_date;
    double current_target_rate;
    string ff_futures_code;
    double ff_futures_price;
    double implied_rate_post;
    double expected_move_bps;
    int lower_move_bps;
    double prob_lower;
    int upper_move_bps;
    double prob_upper;
    int days_to_meeting;
};
```

- [ ] **Step 3: Update `calculated_fed_predictions` SQL in `docs/design/fed-prediction-system.md` (lines 78-92)**

Replace:
```sql
    prob_hike_25bp DOUBLE,
    prob_cut_25bp DOUBLE,
    prob_no_change DOUBLE,
```

With:
```sql
    lower_move_bps INT,
    prob_lower DOUBLE,
    upper_move_bps INT,
    prob_upper DOUBLE,
```

- [ ] **Step 4: Update Algorithm section in `docs/design/fed-prediction-system.md` (line 238)**

Replace:
```
5. Probability = `|expected_move| / 25` (capped at [0,1])
```

With:
```
5. Bracket the expected move between adjacent 25bp outcomes:
   `lower = floor(expected_move / 25) * 25`, `upper = lower + 25`
   `P(upper) = (expected_move - lower) / 25`, `P(lower) = 1 - P(upper)`
```

- [ ] **Step 5: Delete `docs/design/fed-prediction-system.html`**

```bash
rm docs/design/fed-prediction-system.html
```

- [ ] **Step 6: Build to confirm nothing broke**

Run: `cmake --build build/build/Release && ctest --test-dir build/build/Release -R BootstrapTest --output-on-failure`
Expected: All tests still pass (docs don't affect build, but confirm no accidental edits).

- [ ] **Step 7: Commit**

```bash
git add docs/modules/bootstrap.md docs/design/fed-prediction-system.md
git commit -m "docs: update probability model to bracket interpolation"
```
