# FedWatch CLI App — Design Spec

> **Design note:** This document assumes the repo is already set up. Use
> [../../SETUP.md](../../SETUP.md) for the LLVM/QuantLib/Conan build flow and
> [../../LOCAL_SERVICES.md](../../LOCAL_SERVICES.md) for TimescaleDB-backed
> local workflows.

## Goal

Build a CLI app (`apps/fedwatch/`) that connects to TimescaleDB, pulls the current FF futures strip and FOMC meeting schedule, calculates rate change probabilities using existing bootstrap math, and prints a CME FedWatch-style table. This is a validation tool — results should be manually compared against CME's FedWatch website.

This app is intentionally disposable. Once the full `fedfunds` module is built, this app will be replaced.

## Prerequisites

- TSDB populated with:
  - 12-rank FF continuous contracts (`FF_CONTINUOUS` through `FF_CONTINUOUS_12`) in `timeseries_ohlcv`
  - FOMC meeting dates/decisions in `fomc_meetings` (Python repo's table — source of truth)
- Infisical env vars injected (`POSTGRES_HOST`, `POSTGRES_PORT`, `POSTGRES_DB`, `POSTGRES_USER`, `POSTGRES_PASSWORD`)

---

## Component 1: CME Contract Code Parser (reusable, `finkit.core`)

Add to `src/core/core.cppm` — a utility for decoding/encoding CME futures contract codes. This will be used throughout the repo for any CME instrument.

### CME Month Letter Convention

```
F=Jan  G=Feb  H=Mar  J=Apr  K=May  M=Jun
N=Jul  Q=Aug  U=Sep  V=Oct  X=Nov  Z=Dec
```

### Types and Functions

```cpp
struct CmeContractMonth {
    int year;       // Full year, e.g. 2026
    int month;      // 1-12
};

/// Parse a CME contract code like "FFJ26" or "SFRZ4" into product root and contract month.
/// Returns nullopt if the code is too short or has an invalid month letter.
auto parse_cme_contract_code(string_view code) -> optional<pair<string, CmeContractMonth>>;

/// Get just the month from a contract code (convenience wrapper).
auto cme_contract_month(string_view code) -> optional<CmeContractMonth>;

/// Convert month number (1-12) to CME month letter.
auto cme_month_letter(int month) -> optional<char>;

/// Convert CME month letter to month number (1-12).
auto cme_letter_month(char letter) -> optional<int>;
```

### Parsing Rules

- The last character before the year digits is the month letter
- Year digits: 1 digit (e.g., `J5` = 2025) or 2 digits (e.g., `J26` = 2026)
- For 1-digit years, assume 2020s decade (add 2020). For 2-digit years, assume 2000s century (add 2000)
- Product root is everything before the month letter (e.g., `FF`, `SFR`, `TY`)

**Note:** The 1-digit year heuristic (assume 2020s) will need updating in 2030. Add a comment in the code.

### Tests

Unit tests in `tests/core/core_test.cpp` (new file) covering:
- Standard codes: `FFJ26` → (`FF`, Apr 2026), `SFRZ4` → (`SFR`, Dec 2024)
- 1-digit vs 2-digit years
- Invalid month letter → nullopt
- Too-short codes → nullopt
- Round-trip: `cme_month_letter(4)` → `J`, `cme_letter_month('J')` → 4

---

## Component 2: FedWatch App (`apps/fedwatch/`)

### Files

| File | Action |
|------|--------|
| `apps/fedwatch/main.cpp` | New — CLI app |
| `apps/fedwatch/CMakeLists.txt` | New — build config |
| `apps/CMakeLists.txt` | Modify — add `add_subdirectory(fedwatch)` |
| `src/core/core.cppm` | Modify — add CME contract code parser |
| `tests/core/core_test.cpp` | New — unit tests for CME parser |

### Dependencies

- `finkit::core` — CME contract code parsing
- `finkit::data` — TSDB connection, `DataStore`, `load_database_config_from_env()`
- `finkit::bootstrap` — `calculate_fed_probability()`, `calculate_cumulative_fed_probabilities()`
- `libpqxx` — via `finkit::data`

### Data Flow

```
1. Connect to TSDB
   └─ load_database_config_from_env() → DataStore

2. Get current target rate
   └─ Query the Python repo's fomc_meetings table (not fin-kit's reference_fomc_meetings):
      SELECT rate_upper, rate_lower FROM fomc_meetings
      WHERE rate_upper IS NOT NULL
      ORDER BY meeting_date DESC LIMIT 1
   └─ mid_rate = (rate_upper + rate_lower) / 2
   └─ Convert to decimal: mid_rate / 100 (e.g., 4.375 → 0.04375)
      (calculate_fed_probability expects decimal rates)

3. Get next 8 FOMC meetings
   └─ SELECT meeting_date, has_presser FROM fomc_meetings
      WHERE meeting_date > CURRENT_DATE
      ORDER BY meeting_date LIMIT 8
   └─ Construct FOMCMeeting structs (implied_move left as nullopt)

4. Get latest FF strip (12 ranks)
   └─ Look up instrument IDs dynamically:
      SELECT id FROM instruments WHERE symbol LIKE 'FF_CONTINUOUS%' ORDER BY id
   └─ For each instrument_id:
      SELECT ts, implied_rate, source_contract FROM timeseries_ohlcv
      WHERE instrument_id = $1 AND granularity = 'daily'
      ORDER BY ts DESC LIMIT 1
   └─ Parse source_contract with cme_contract_month() to get calendar month
   └─ Use the ts from the strip query as valuation_date (the actual trade date,
      not today — avoids issues on weekends/holidays)

5. Map meetings → contracts
   └─ For each meeting, find the rank whose source_contract month/year
      matches the meeting's month/year
   └─ If no match (meeting >12 months out), skip it
   └─ Note: scheduled FOMC meetings never share a calendar month,
      so the mapping is always 1:1. Emergency meetings are not handled.

6. Calculate probabilities
   └─ Build vectors of prices and FOMCMeeting structs
   └─ implied_rate is stored as percentage in TSDB (e.g., 3.6375 for 3.6375%)
   └─ Convert to futures price: price = 100.0 - implied_rate
      (calculate_fed_probability internally does: (100 - price) / 100 to get decimal rate)
   └─ Pass mid_rate as decimal (step 2) as current_target_rate
   └─ Pass strip ts (step 4) as valuation_date
   └─ Call calculate_cumulative_fed_probabilities()

7. Print table
   └─ Compute cumulative cuts as running sum:
      for each meeting, accumulate abs(expected_move_bps) / 25.0 if negative
   └─ "Implied Post" column shows FedProbabilityResult::implied_rate_post (as percentage)
```

### Output Format

```
FedWatch Probability Table (as of 2026-03-13)
Current target rate: 4.25-4.50% (mid: 4.375%)

Meeting      Contract  Implied Post  Move(bps)  Lower       P(Lower)  Upper       P(Upper)  Cumul Cuts
──────────── ──────── ──────────── ─────────── ─────────── ──────── ─────────── ──────── ──────────
2026-05-07   FFK26     4.375%        0.0        no change   100.0%   +25bp        0.0%     0.0
2026-06-18   FFM26     4.312%       -6.3        -25bp        25.0%   no change    75.0%    0.25
2026-07-30   FFN26     4.198%      -11.4        -25bp        45.6%   no change    54.4%    0.71
...
```

The "Lower" and "Upper" columns show human-readable labels:
- `0` bps → "no change"
- `+25` bps → "+25bp"
- `-25` bps → "-25bp"
- etc.

### Error Handling

- No TSDB connection → exit with error message
- No FOMC meetings found → exit with "no upcoming meetings"
- Missing FF rank for a meeting month → skip that meeting, warn to stderr
- Empty strip data → exit with "no FF futures data available"

---

## Validation

Manual comparison against CME FedWatch website:
1. Run the app with Infisical env vars: `cd ~/proxmox-project && infisical run --env=dev --path="/kubernetes/infrastructure/timescaledb" -- /path/to/fedwatch`
2. Open https://www.cmegroup.com/markets/interest-rates/cme-fedwatch-tool.html
3. Compare probabilities — differences >2% indicate a bug in either mapping or calculation

---

## Not In Scope

- Automated CME FedWatch API comparison (manual for now)
- Persisting results to TSDB
- Backtesting / historical runs
- Full `fedfunds` module extraction (future work)
