# Roadmap

This document tracks the development roadmap for fin-kit, including completed work, current priorities, and long-term vision.

> **Tracking note (2026-07-24):** all outstanding items below have been
> migrated to [GitHub issues](https://github.com/jlipworth/fin-kit/issues) —
> individual issues for active pipeline work and code TODOs (#1–#24), and
> per-theme umbrella issues with task lists for the long-range phases
> (#25–#36). GitHub issues are the source of truth for open work; this
> document remains as strategic context and phase history. Do not add new
> work items here — file an issue instead.

---

## Completed Phases

### Phase 1: Foundation

- [x] Project structure and C++20 modules build system
- [x] Core module (paths, logging)
- [x] Data module with DataStore (TimescaleDB via libpqxx)
- [x] Types module with shared financial types
- [x] Unit test infrastructure (GoogleTest)

### Phase 2: Calculation Libraries

- [x] Bootstrap module (SOFR curve, OIS, CB cut probabilities)
- [x] Basis module (bond basis, CIP basis, index futures)
- [x] Stats module (rolling statistics, covariance, signal analysis)
- [x] Valuation module (bond valuation, swap valuation)
- [x] Curves module (rate accessors, CIP utilities)
- [x] Analysis module (bond basket analysis)

### Phase 3: Frameworks

- [x] Trading module (Order/Fill types, IExecutionEngine, fill models)
- [x] Risk module (limits, pre-trade checks, active monitoring)
- [x] Backtest module (engine, IStrategy, Portfolio)

---

## Current Phase

### Phase 4: Visualization

- [ ] Viz module implementation
- [ ] Terminal-based charts
- [ ] Performance metrics display
- [ ] Equity curve plotting

### Phase 5: Testing & Validation

- [ ] Mock data with expected results
- [x] End-to-end backtest integration tests
- [x] Risk engine test coverage
- [ ] Calculation module unit tests with known results
- [ ] Performance benchmarks

---

## Planned Phases

### Phase 5.5: Profiling Infrastructure

**Required BEFORE any concurrency work** - must identify actual bottlenecks first.

- [ ] CPU profiling setup (perf, Instruments, or Valgrind/Callgrind)
- [ ] Memory profiling (heap allocation patterns)
- [ ] Benchmark suite for critical paths:
  - Curve bootstrapping (single currency)
  - Bond basket valuation (CTD identification)
  - Covariance matrix calculation
  - Full backtest run (end-to-end)
- [ ] Baseline performance metrics documentation
- [ ] Hot path identification report

**Profiling Tools to Evaluate:**
- `perf` (Linux) - CPU sampling, call graphs
- `Instruments` (macOS) - Time Profiler, Allocations
- `Valgrind/Callgrind` - Detailed call analysis
- `Tracy` - Frame profiler with timeline visualization
- `Google Benchmark` - Microbenchmarks for isolated functions

### Phase 6: Concurrency & Performance

**Concurrency Support**
- [ ] Thread pool for parallel calculations
- [ ] Parallel indicator calculation for independent computations
- [ ] Concurrent curve bootstrapping across currencies
- [ ] Parallel strategy signal generation in backtests
- [ ] Execution policy support for STL algorithms

**Performance Optimization**
- [ ] SIMD optimization for calculations
- [ ] Memory-mapped large dataset support
- [ ] Incremental calculation updates

### Phase 7: Precision & Numerics

- [ ] Decimal type for transaction amounts (avoid floating-point rounding)
- [ ] Proper rounding rules for different asset classes (penny stocks, futures tick sizes)
- [ ] Currency-specific decimal places (JPY=0, USD/EUR=2, crypto=8+)
- [ ] Tick size validation for orders
- [ ] Lot size constraints

### Phase 8: Risk Framework Enhancements

- [ ] Factor risk model integration (Barra, Axioma style)
- [x] VaR suite (Historical, Parametric, Monte Carlo)
- [x] Stress testing framework with predefined scenarios
- [ ] Correlation-based net exposure calculation
- [x] Liquidity risk (ADV-based position limits)
- [x] Sector/geography concentration limits
- [ ] Intraday VaR updates
- [ ] Risk attribution (factor contribution)

### Phase 9: Trading Framework Enhancements

- [ ] Market impact models (square-root, linear)
- [ ] Order queue position modeling for limit orders
- [ ] Exchange rejection simulation
- [ ] Partial fill detailed handling
- [ ] TWAP/VWAP/POV execution algorithms
- [ ] Multi-leg orders (spreads, pairs)

### Phase 10: Backtest Framework Enhancements

- [x] Look-ahead bias detection/prevention
- [x] Survivorship bias handling (delisted securities)
- [x] Dynamic universe management (filter by criteria as-of date)
- [x] Short selling realism (locate, borrow costs, availability)
- [ ] Margin/collateral modeling
- [ ] Settlement cycle modeling (T+1, T+2)
- [ ] Corporate actions (splits, dividends, spin-offs)
- [ ] Futures roll handling
- [ ] Options expiration/exercise

### Phase 11: Calculation Module Extensions

**Bootstrap Module**
- [ ] Dual-curve bootstrapping
- [ ] Spread curves
- [ ] Basis swap curves
- [ ] Cap/floor vol bootstrapping

**Basis Module**
- [ ] Roll basis calculations
- [ ] Calendar spreads
- [ ] Swap spreads
- [ ] TRS basis
- [ ] Cross-currency basis term structure

**Valuation Module**
- [ ] Option pricing (Black-Scholes, binomial)
- [ ] Callable/putable bonds (OAS)
- [ ] Swaption pricing
- [ ] CDS valuation

**Stats Module**
- [ ] EWMA with configurable decay
- [ ] Robust statistics (median, MAD)
- [ ] Regime detection
- [ ] Factor analysis
- [ ] Cointegration tests

### Phase 12: Data Quality & Infrastructure

**Data Quality**
- [ ] Bad tick detection and filtering
- [ ] Data staleness alerts
- [ ] Missing data interpolation
- [ ] Data revision tracking
- [ ] Adjusted vs unadjusted price handling

**Market Microstructure**
- [ ] Trading halt handling
- [ ] Short sale restriction (SSR/uptick rule)
- [ ] Limit up/limit down bands
- [ ] Odd lot handling
- [ ] Pre/post market trading
- [ ] Auction period modeling

**Instrument Lifecycle**
- [ ] IPO handling (securities with no history)
- [ ] Delisting handling
- [ ] Ticker changes
- [ ] Mergers and acquisitions
- [ ] Bankruptcies
- [ ] Rights issues
- [ ] Bond maturity

---

## Long-Term Vision

### Architecture
- [ ] Walk-forward optimization framework
- [ ] Monte Carlo scenario simulation
- [ ] Multi-strategy framework with shared capital
- [ ] Cross-strategy risk management
- [ ] Real-time risk monitoring

### Integration
- [ ] Python bindings (pybind11)
- [ ] Live trading adapter interface
- [ ] WebSocket data feeds
- [x] TimescaleDB backend (via libpqxx)
- [ ] QuestDB backend
- [ ] Cloud deployment support

### Performance
- [ ] GPU acceleration for large matrix operations

---

## Known Critical Gaps

These gaps were identified during architecture review and should be prioritized:

| Gap | Impact | Module | Status |
|-----|--------|--------|--------|
| Look-ahead bias prevention | Critical | backtest | Addressed (Phase 10) |
| Survivorship bias | Critical | backtest | Addressed (Phase 10) |
| Liquidity risk | Critical | risk | Addressed (Phase 8) |
| Factor risk models | High | risk | Open |
| Market impact model | High | trading | Open |
| Settlement cycles | Moderate | trading | Open |
| Corporate actions | Moderate | backtest | Open |
| Bad tick detection | Moderate | data | Open |

---

## Implementation Notes

### Concurrency Strategy

**PREREQUISITE: QuantLib Thread Safety**

QuantLib uses a global singleton `Settings::instance()` to store the evaluation date. This is NOT thread-safe by default. Any parallel QuantLib calls will race on this global state.

Why QuantLib chose this design:
- Convenience: Avoids passing evaluation date to every function
- Historical: Designed in early 2000s when multi-core was rare
- Mental model: Assumes pricing a portfolio at ONE point in time

To enable thread-safe QuantLib, rebuild with:
```bash
# Add to scripts/build-quantlib.sh cmake command:
-DQL_ENABLE_THREAD_SAFE_SETTINGS=ON
```

Trade-offs:
| Aspect | Without Flag | With Flag |
|--------|-------------|-----------|
| Settings storage | Single global | Thread-local (per thread) |
| Performance | Baseline | ~5-10% overhead on Settings access |
| Parallel pricing | Unsafe (race condition) | Safe |

**What can be parallelized WITHOUT rebuilding QuantLib:**
- Rolling statistics (pure C++ math)
- Covariance matrix computation (pure C++ math)
- Signal analysis (pure C++ math)

**What REQUIRES thread-safe QuantLib:**
- Curve bootstrapping (uses QuantLib)
- Bond valuation (uses QuantLib)
- Swap valuation (uses QuantLib)

**Implementation order:**
1. **Profile first** - identify actual bottlenecks before any parallelization
2. Start with embarrassingly parallel operations (multi-symbol calculations)
3. Use Intel TBB or OpenMP (see libc++ PSTL note below)
4. Only build custom thread pool if specific scheduling is needed
5. Only rebuild QuantLib if parallel pricing shows significant benefit

**Why NOT to build a custom ThreadPool:**
- Intel TBB provides battle-tested work-stealing scheduler
- Custom pools are hard to get right (exceptions, shutdown, deadlocks)
- OpenMP is mature and widely supported
- Maintenance burden for marginal benefit

**Parallel Curve Bootstrapping (G10): HIGH PRIORITY for backtesting**

Proposed: Bootstrap 10 G10 currency curves in parallel instead of sequentially.

Analysis for DAILY use:
- Each currency is independent (embarrassingly parallel)
- Current time: ~100ms per curve = ~1 second for 10 curves
- Parallel time: ~100ms total (with 10+ cores)
- Savings: ~0.9 seconds per daily run (negligible)

Analysis for BACKTESTING (the real use case):
```
10 years × 252 days × 13 bars/day (30-min) = 32,760 builds per currency
× 10 G10 currencies = 327,600 total curve builds

Sequential: 327,600 × 100ms = 9.1 hours
Parallel:    32,760 × 100ms = 55 minutes (10x speedup)
```

Conclusion:
- For daily production: low priority (sub-second savings)
- For backtesting: HIGH PRIORITY (hours of savings)
- Requires QuantLib thread-safe rebuild (`-DQL_ENABLE_THREAD_SAFE_SETTINGS=ON`)
- The 5-10% Settings overhead is negligible compared to 10x parallelism gain

**Parallel Covariance Matrix: USE EIGEN**

Instead of hand-rolling parallel covariance, use Eigen (now added as dependency):

```cpp
#include <Eigen/Dense>

auto calculate_covariance_matrix(const Eigen::MatrixXd& returns) -> Eigen::MatrixXd {
    Eigen::MatrixXd centered = returns.rowwise() - returns.colwise().mean();
    return (centered.adjoint() * centered) / (returns.rows() - 1);
}
```

Why Eigen:
- Battle-tested numerical code
- Automatic parallelization with OpenMP
- Handles edge cases (numerical stability)
- ~3 lines vs ~30 lines of hand-rolled code
- Will be needed anyway for factor models, Monte Carlo (Cholesky), VaR

Refactor: `stats-covariance.cppm` should be updated to use Eigen internally.

**Eigen Migration Tasks: COMPLETED**

All identified Eigen migration tasks have been completed:

- [x] `stats-covariance.cppm` - Full rewrite with Eigen (covariance, correlation, EWMA, shrinkage)
- [x] `valuation-bond.cppm` - Fixed unstable variance with centered Eigen approach
- [x] `backtest-engine.cppm` - Sharpe ratio migrated to Eigen
- [x] `stats-signals.cppm` - IC, autocorrelation, ADF test migrated to Eigen
- [x] `stats-rolling.cppm` - Rolling variance recalculation migrated to Eigen

**NEW: stats-returns.cppm utility module added** with reusable return metrics:
- `sharpe_ratio()` - Annualized Sharpe ratio
- `sortino_ratio()` - Downside risk-adjusted return
- `calmar_ratio()` - Return / max drawdown
- `max_drawdown()` - Maximum peak-to-trough decline
- `annualized_volatility()` - Annualized standard deviation
- `calculate_return_metrics()` - Comprehensive metrics including skewness/kurtosis

**GPU Parallelization (Future Investigation):**

Candidates for GPU acceleration (when portfolio/universe size justifies it):
- Large covariance matrices (1000+ assets)
- Monte Carlo VaR simulations (10,000+ paths)
- Stress testing with many scenarios
- Historical backtests with parameter sweeps

Libraries to evaluate:
- **CUDA** (NVIDIA): cuBLAS, cuSOLVER for linear algebra
- **Eigen + CUDA**: Eigen can offload to GPU with plugins
- **ArrayFire**: Cross-platform GPU computing
- **SYCL/oneAPI**: Portable heterogeneous computing

Note: GPU overhead only worthwhile for large matrices. Profile before implementing.

**Parallelization Priority Matrix:**

| Operation | Data Size | Frequency | Speedup | Priority |
|-----------|-----------|-----------|---------|----------|
| G10 curve bootstrap (backtest) | 10 curves | 32K times | 10x | **HIGH** |
| Bond basket valuation | 50-200 bonds | Per bar | 8-16x | **HIGH** |
| Covariance matrix | 100-1000 assets | Daily | Via Eigen | MEDIUM |
| Rolling stats | 1 series | Per bar | Negligible | LOW |
| Signal generation | Per symbol | Per bar | Linear | MEDIUM |

**Clang/libc++ std::execution Support: DO NOT USE**

**CRITICAL FINDING (January 2026):** libc++'s PSTL (Parallel STL) on Linux is a **test stub** with NO actual parallelism.

Investigation results:
- Compiling with `-D_LIBCPP_ENABLE_EXPERIMENTAL` enables `std::execution::par` syntax
- However, the `std_thread` backend contains this comment in the source:
  > "This partial backend implementation is for testing purposes only"
- Benchmarking confirmed: 1.04x "speedup" on 32 threads (i.e., no parallelism)
- The `libdispatch` backend works on macOS but is unavailable on Linux
- Serial backend is the actual default on Linux

**Alternatives that actually work:**
1. **Intel TBB** - Provides `tbb::parallel_for`, production-ready
2. **OpenMP** - Add `-fopenmp` flag, use `#pragma omp parallel for`
3. **std::jthread + manual partitioning** - C++20 threads with stop tokens

Recommendation: Use Intel TBB for explicit parallelism:
```cpp
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>

tbb::parallel_for(tbb::blocked_range<size_t>(0, bonds.size()),
    [&](const tbb::blocked_range<size_t>& r) {
        for (size_t i = r.begin(); i < r.end(); ++i) {
            valuations[i] = value_bond(bonds[i], curve, date);
        }
    });
```

**Backtest-Specific Parallelism:**

For backtesting, the most impactful parallelization targets:

1. **Multi-run parallelism** (parameter sweeps):
   - Run 100 parameter combinations in parallel
   - Each run is independent
   - Near-linear scaling with cores

2. **Walk-forward optimization**:
   - Each fold is independent after training window
   - Parallelize across folds

3. **Multi-symbol signal generation**:
   - Generate signals for 500 symbols in parallel
   - Requires no shared state between symbols

**Thread Safety Audit Checklist:**

Before enabling parallelism, verify thread safety of:
- [ ] QuantLib Settings (rebuild required)
- [ ] spdlog (thread-safe by default)
- [ ] PostgreSQL connections (use per-thread connections or connection pool)
- [ ] Random number generators (use per-thread RNG)
- [ ] Caches (use thread-local or lock-free)

### Decimal Implementation Strategy
When implementing decimal precision:
1. Evaluate boost::multiprecision vs custom fixed-point
2. Define precision requirements per asset class
3. Implement rounding rules at trade execution
4. Consider performance impact on hot paths
5. Maintain backward compatibility with double API

---

*Last updated: 2026-07-23*
