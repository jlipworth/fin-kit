# Fed Probability Distribution Redesign

## Summary

Replace the three-field probability model (`prob_hike_25bp`, `prob_cut_25bp`, `prob_no_change`) in `FedProbabilityResult` with a two-outcome bracketing model that correctly handles implied moves beyond +/-25bp. This is required for accurate backtesting and Brier score calculation.

## Problem

The current `calculate_fed_probability()` in `src/bootstrap/bootstrap.cppm` (lines 364-379) clamps the probability to [0, 1] assuming only three outcomes: +25bp hike, -25bp cut, or no change. When the implied move exceeds 25bp (e.g., -62bp), the clamp discards magnitude information and reports "100% cut" — losing the distinction between a -30bp and -80bp implied move.

For backtesting accuracy scoring (Brier scores, direction correctness, magnitude error), this is incorrect.

## Design

### Approach

For any expected move derived from the futures day-weighted formula, find the two adjacent 25bp outcomes that bracket it and assign probabilities by linear interpolation.

Given `expected_move_bps`:
```
lower_move_bps = floor(expected_move_bps / 25) * 25
upper_move_bps = lower_move_bps + 25
prob_upper = (expected_move_bps - lower_move_bps) / 25
prob_lower = 1 - prob_upper
```

This is the standard CME FedWatch simplification: with a single futures price as input, we can solve for at most two unknowns.

### Example

Expected move = -62bp:
- `lower_move_bps = floor(-62/25) * 25 = -75`
- `upper_move_bps = -50`
- `prob_upper = (-62 - (-75)) / 25 = 0.52` (52% chance of -50bp)
- `prob_lower = 0.48` (48% chance of -75bp)

Expected move = +38bp:
- `lower_move_bps = 25`, `upper_move_bps = 50`
- `prob_upper = (38 - 25) / 25 = 0.52` (52% chance of +50bp)
- `prob_lower = 0.48` (48% chance of +25bp)

Edge case — expected move = -50bp exactly:
- `lower_move_bps = -50`, `upper_move_bps = -25`
- `prob_upper = 0.0`, `prob_lower = 1.0` (100% at -50bp)

Edge case — expected move = 0bp:
- `lower_move_bps = 0`, `upper_move_bps = 25`
- `prob_upper = 0.0`, `prob_lower = 1.0` (100% no change)
- Note: `upper_move_bps = 25` is a structural artifact; `prob_upper = 0.0` means it carries no weight

### Meeting day convention

The FOMC meeting day is inclusive in `days_before` (the new rate takes effect the day after announcement). The current code is correct: `days_before = day_of_meeting`.

### Updated struct

```cpp
struct FedProbabilityResult {
    ql::Date meeting_date;
    double current_target_rate;
    double implied_rate_pre;
    double implied_rate_post;
    double expected_move_bps;
    int days_to_meeting;
    int lower_move_bps;
    double prob_lower;
    int upper_move_bps;
    double prob_upper;
};
```

Replaces: `prob_hike_25bp`, `prob_cut_25bp`, `prob_no_change`.

### Updated calculation

Replaces the clamp block (lines 364-379 of `src/bootstrap/bootstrap.cppm`):

```cpp
int lower = static_cast<int>(std::floor(result.expected_move_bps / 25.0)) * 25;
int upper = lower + 25;
double p_upper = (result.expected_move_bps - lower) / 25.0;

result.lower_move_bps = lower;
result.upper_move_bps = upper;
result.prob_upper = p_upper;
result.prob_lower = 1.0 - p_upper;
```

### Impact on cumulative function

`calculate_cumulative_fed_probabilities()` chains via `implied_rate_post` and accumulates `expected_move_bps`. These fields are unchanged, so the cumulative function requires no logic changes — only the struct field references need updating (remove `prob_hike_25bp` / `prob_cut_25bp` references). The `CumulativeFedProbabilities` struct inherits the change automatically via its `vector<FedProbabilityResult> by_meeting` member.

### Impact on fedfunds design doc

The `FedPrediction` struct in `docs/design/fed-prediction-system.md` gets the same field swap. `FedPredictionBacktestResult` inherits the change transitively via its `vector<FedPrediction> all_predictions` member. The SQL table `calculated_fed_predictions` also needs columns updated: replace `prob_hike_25bp`, `prob_cut_25bp`, `prob_no_change` with `lower_move_bps INT`, `prob_lower DOUBLE`, `upper_move_bps INT`, `prob_upper DOUBLE`. The Algorithm section (line 238) must be updated to replace `Probability = |expected_move| / 25 (capped at [0,1])` with the bracket interpolation formula.

### Brier score with two-outcome model

The accuracy scoring uses the standard Brier score formula across the two bracket outcomes: `BS = (p_lower - o_lower)^2 + (p_upper - o_upper)^2`, where `o_lower` and `o_upper` are 0 or 1 depending on which bracket the actual move fell into. If the actual move matches neither bracket exactly (e.g., predicted [-75, -50] but actual was -25bp), map the actual to the nearest bracket boundary — the prediction was simply wrong and the Brier score reflects that.

## Files to modify

| File | Changes |
|------|---------|
| `src/bootstrap/bootstrap.cppm` | Update `FedProbabilityResult` struct, replace clamp block |
| `docs/design/fed-prediction-system.md` | Update `FedPrediction` struct fields and `calculated_fed_predictions` SQL columns |
| `docs/modules/bootstrap.md` | Update `FedProbabilityResult` field documentation |
| `docs/design/fed-prediction-system.html` | Delete (redundant rendered copy of the .md, already untracked) |

## Verification

- Unit test (negative move): known futures price + meeting date → verify brackets and probabilities match hand calculation
- Unit test (positive move): e.g., +38bp implied → brackets [25, 50], verify probabilities
- Edge case test: move exactly on 25bp boundary → single outcome at 100%
- Edge case test: zero move → 100% no change
- Regression: cumulative function produces same `implied_terminal_rate` as before (the chaining math is unchanged)
