# Fed Funds Futures Prediction System - Implementation Plan

## Summary

Build a Fed Funds futures-based prediction system to calculate probability and magnitude of FOMC rate changes, with full backtesting capability through historical time.

## Existing Infrastructure

**Good news**: Significant infrastructure already exists:
- `calculate_fed_probability()` and `calculate_cumulative_fed_probabilities()` in `src/bootstrap/bootstrap.cppm` (lines 306-425)
- `FedProbabilityResult` struct with probabilities (hike/cut/no_change), expected move, implied rates
- `reference_fomc_meetings` table with meeting dates, decision_rate, implied_move
- BacktestEngine with event-driven loop, portfolio, execution (70-80% complete)

**Key Gap**: No `DatabaseDataFeed` - only `InMemoryDataFeed` exists. Need this for historical backtesting.

---

## Architecture

### Decision: New Module
Create `finkit.fedfunds` in `src/fedfunds/` rather than extending bootstrap. This separates pure calculation (bootstrap) from orchestration/framework.

### Module Structure
```
src/fedfunds/
├── fedfunds.cppm           # Main module export
├── fedfunds-types.cppm     # Fed-specific types
├── fedfunds-engine.cppm    # FedPredictionEngine
└── fedfunds-datafeed.cppm  # DatabaseDataFeed implementation
```

---

## Data Model Changes

### New Input Tables (InputDataStore in `src/data/data.cppm`)

**1. Fed Funds Futures** (`rates_ff_futures`)
```sql
CREATE TABLE IF NOT EXISTS rates_ff_futures (
    contract_code VARCHAR NOT NULL,      -- e.g., "ZQF5" (Jan 2025)
    contract_month DATE NOT NULL,        -- First day of settlement month
    price DOUBLE,                        -- 100 - implied rate
    implied_rate DOUBLE,                 -- (100 - price) / 100
    last_trade_date DATE,
    as_of TIMESTAMPTZ NOT NULL,
    source VARCHAR,
    PRIMARY KEY (contract_code, as_of)
);
```

**2. Historical Target Rate** (`rates_ff_target`)
```sql
CREATE TABLE IF NOT EXISTS rates_ff_target (
    effective_date DATE PRIMARY KEY,
    lower_bound DOUBLE NOT NULL,
    upper_bound DOUBLE NOT NULL,
    mid_rate DOUBLE NOT NULL,
    source VARCHAR
);
```

**3. Daily Effective Fed Funds Rate** (`rates_ff_effr`)
```sql
CREATE TABLE IF NOT EXISTS rates_ff_effr (
    fixing_date DATE PRIMARY KEY,
    rate DOUBLE NOT NULL,
    volume DOUBLE,
    source VARCHAR
);
```

### New Output Tables (OutputDataStore)

**1. Predictions** (`calculated_fed_predictions`)
```sql
CREATE TABLE IF NOT EXISTS calculated_fed_predictions (
    run_id UUID NOT NULL,
    as_of_date DATE NOT NULL,
    meeting_date DATE NOT NULL,
    current_target_rate DOUBLE NOT NULL,
    ff_futures_code VARCHAR,
    ff_futures_price DOUBLE,
    implied_rate_post DOUBLE,
    expected_move_bps DOUBLE,
    lower_move_bps INT,
    prob_lower DOUBLE,
    upper_move_bps INT,
    prob_upper DOUBLE,
    days_to_meeting INT,
    PRIMARY KEY (run_id, as_of_date, meeting_date)
);
```

**2. Cumulative Path** (`calculated_fed_cumulative`)
```sql
CREATE TABLE IF NOT EXISTS calculated_fed_cumulative (
    run_id UUID NOT NULL,
    as_of_date DATE NOT NULL,
    horizon_date DATE NOT NULL,
    expected_total_cuts_25bp DOUBLE,
    expected_total_hikes_25bp DOUBLE,
    implied_terminal_rate DOUBLE,
    PRIMARY KEY (run_id, as_of_date, horizon_date)
);
```

**3. Accuracy Scoring** (`calculated_fed_accuracy`)
```sql
CREATE TABLE IF NOT EXISTS calculated_fed_accuracy (
    run_id UUID NOT NULL,
    meeting_date DATE PRIMARY KEY,
    prediction_date DATE,
    predicted_move_bps DOUBLE,
    actual_move_bps DOUBLE,
    direction_correct BOOLEAN,
    magnitude_error_bps DOUBLE,
    brier_score DOUBLE
);
```

---

## New Types

### File: `src/fedfunds/fedfunds-types.cppm`

```cpp
struct FFContract {
    string contract_code;      // "ZQF5"
    ql::Date contract_month;   // First day of month
    double price;              // 100 - rate
    double implied_rate;
    ql::Date as_of;
};

struct FFTargetRate {
    ql::Date effective_date;
    double lower_bound;
    double upper_bound;
    double mid_rate;
};

struct FedPredictionConfig {
    ql::Date start_date;
    ql::Date end_date;
    int meetings_ahead{8};
    bool persist_predictions{true};
};

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

struct FedDataSnapshot {
    ql::Date date;
    double current_target_rate;
    vector<FFContract> ff_futures;
    vector<types::FOMCMeeting> upcoming_meetings;
};

struct FedPredictionBacktestResult {
    string run_id;
    ql::Date start_date;
    ql::Date end_date;
    int total_days;
    int meetings_covered;
    int predictions_generated;
    double direction_accuracy_pct;
    double avg_magnitude_error_bps;
    vector<FedPrediction> all_predictions;
};
```

---

## Key Functions

### Data Access
```cpp
auto get_target_rate_as_of(InputDataStore& db, ql::Date date) -> optional<FFTargetRate>;
auto get_ff_futures_as_of(InputDataStore& db, ql::Date date) -> vector<FFContract>;
auto get_upcoming_meetings(InputDataStore& db, ql::Date date, int count) -> vector<FOMCMeeting>;
auto get_data_snapshot(InputDataStore& db, ql::Date date, int meetings_ahead) -> optional<FedDataSnapshot>;
```

### Prediction Engine
```cpp
class FedPredictionEngine {
public:
    FedPredictionEngine(InputDataStore& input, OutputDataStore& output, FedPredictionConfig config);

    auto run() -> FedPredictionBacktestResult;           // Full backtest
    auto process_date(ql::Date date) -> vector<FedPrediction>;  // Single date
    auto score_predictions() -> void;                    // Compare to actuals
};
```

### DatabaseDataFeed (fills backtest gap)
```cpp
class DatabaseDataFeed : public backtest::IDataFeed {
public:
    DatabaseDataFeed(InputDataStore& db, ql::Date start, ql::Date end);

    auto next() -> optional<BarEvent> override;
    auto has_more() const -> bool override;
    void reset() override;

    // Fed-specific
    auto current_date() const -> ql::Date;
    auto get_data_snapshot(int meetings_ahead) -> optional<FedDataSnapshot>;
};
```

---

## Algorithm: Futures-to-Meeting Mapping

For each FOMC meeting:
1. Find FF futures contract for meeting month
2. Calculate implied average rate: `(100 - price) / 100`
3. Solve for post-meeting rate using day-weighted formula:
   ```
   implied_avg = (days_before * current_rate + days_after * post_rate) / days_in_month
   post_rate = (implied_avg * days_in_month - days_before * current_rate) / days_after
   ```
4. Expected move = `(post_rate - current_rate) * 10000` bps
5. Bracket the expected move between adjacent 25bp outcomes:
   `lower = floor(expected_move / 25) * 25`, `upper = lower + 25`
   `P(upper) = (expected_move - lower) / 25`, `P(lower) = 1 - P(upper)`

For cumulative predictions: chain calculations, updating running rate after each meeting.

---

## Implementation Phases

### Phase 1: Data Model (~1 day)
- Add tables to `InputDataStore::init_input_schema()` and `OutputDataStore::init_output_schema()`
- Files: `src/data/data.cppm`

### Phase 2: Types and Data Access (~1 day)
- Create `src/fedfunds/fedfunds-types.cppm`
- Implement data query functions
- Files: `src/fedfunds/fedfunds-types.cppm`

### Phase 3: DatabaseDataFeed (~1 day)
- Implement `IDataFeed` for database iteration
- This also benefits BacktestEngine for other use cases
- Files: `src/fedfunds/fedfunds-datafeed.cppm`

### Phase 4: Prediction Engine (~2 days)
- Core prediction logic (reuse `bootstrap::calculate_fed_probability`)
- Cumulative path calculation
- Backtest orchestration loop
- Files: `src/fedfunds/fedfunds-engine.cppm`

### Phase 5: Output and Scoring (~1 day)
- Persist predictions to database
- Score against actual outcomes
- Files: `src/fedfunds/fedfunds-engine.cppm`

### Phase 6: Testing and Integration (~1 day)
- Unit tests for probability math
- Integration tests with mock data
- Files: `tests/fedfunds/fedfunds_test.cpp`

---

## Verification Plan

1. **Unit Tests**: Probability calculation matches manual calculation
2. **Known Data Test**: Use historical FF futures prices from known dates, verify predictions match documented CME FedWatch-style probabilities
3. **Backtest Run**: Run prediction engine over 2023-2024 period, verify reasonable accuracy on known meeting outcomes
4. **Build**: `cmake --build build/build/Release && ctest --test-dir build/build/Release -R fedfunds`

---

## Files to Modify

| File | Changes |
|------|---------|
| `src/data/data.cppm` | Add 3 input tables, 3 output tables |
| `src/fedfunds/fedfunds.cppm` | New - main module export |
| `src/fedfunds/fedfunds-types.cppm` | New - type definitions |
| `src/fedfunds/fedfunds-datafeed.cppm` | New - DatabaseDataFeed |
| `src/fedfunds/fedfunds-engine.cppm` | New - FedPredictionEngine |
| `tests/fedfunds/fedfunds_test.cpp` | New - unit tests |
| `CMakeLists.txt` | Add fedfunds module |
